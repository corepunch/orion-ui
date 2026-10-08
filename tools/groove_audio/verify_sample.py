"""Plot decoded samples against a beat grid and report bounded timing checks."""
from pathlib import Path
import argparse
import hashlib
import json
import math
import os
import shlex
import subprocess
import tempfile

import numpy as np
import soundfile as sf
from PIL import Image, ImageDraw, ImageFont


def numbers(value):
    try:
        result = [float(x) for x in value.split(',')]
    except ValueError as error:
        raise argparse.ArgumentTypeError('Use comma-separated numbers') from error
    if not result or not all(math.isfinite(x) and x >= 0 for x in result):
        raise argparse.ArgumentTypeError('Values must be finite and nonnegative')
    if any(a >= b for a, b in zip(result, result[1:])):
        raise argparse.ArgumentTypeError('Values must be strictly increasing')
    return result


def decode(path, work, cc):
    if path.suffix.lower() != '.mp3':
        audio, rate = sf.read(path, always_2d=True, dtype='float64')
        return audio, rate, {'decoder': 'libsndfile'}
    repository = Path(__file__).resolve().parents[2]
    executable = work / 'decode_sample'
    # Rebuild against the current headers, not a stale cached decoder.
    subprocess.run(shlex.split(cc) + ['-std=c99', '-O2', '-I', str(repository),
                   str(Path(__file__).with_name('decode_sample.c')), '-o', str(executable)], check=True)
    with tempfile.TemporaryDirectory(dir=work) as directory:
        raw = Path(directory) / 'decoded.pcm'
        info = subprocess.check_output([str(executable), str(path), str(raw)], text=True)
        count, channels, rate, delay = map(int, info.split())
        audio = np.fromfile(raw, dtype=np.dtype('=i2'))
    if channels < 1 or len(audio) != count or count % channels:
        raise ValueError(f'Invalid decoded sample count: {path}')
    return audio.reshape(-1, channels).astype('float64') / 32768, rate, {
        'decoder': 'Groove minimp3_ex / MP3D_SEEK_TO_SAMPLE',
        'removed_encoder_delay_samples_per_channel': delay // channels,
    }


def envelope_rise(audio, rate, start, end):
    """Forward-only diagnostic; cannot prove an early attack is absent."""
    segment = audio[start:min(end, start + round(.210 * rate))]
    hop = max(1, int(rate / 1000))
    count = len(segment) // hop
    if not count:
        return None
    envelope = np.sqrt(np.mean(segment[:count * hop].reshape(count, hop, -1) ** 2, axis=(1, 2)))
    if envelope.max() < 10 ** (-55 / 20):
        return None
    return float(np.flatnonzero(envelope > envelope.max() * .18)[0] * hop / rate * 1000)


def draw_comparison(series, bpm, frames_per_beat, attacks, observed, output):
    width, left, right, row_height = 1440, 54, 26, 210
    image = Image.new('RGB', (width, 72 + row_height * len(series)), '#14202b')
    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default(size=18)
    draw.text((left, 16), f'Sample timing / {bpm:g} BPM / 4/4', font=font, fill='white')
    draw.text((left, 42), 'Gold: beats   Blue: intended attacks   White: supplied observed attacks', font=font, fill='#c6d3db')
    seconds_per_beat = frames_per_beat / 44100
    duration = max(len(audio) / rate for _, audio, rate in series)
    extent = max(1, math.ceil(duration / seconds_per_beat - 1e-9)) * seconds_per_beat
    plot_width = width - left - right
    xpos = lambda seconds: left + round(seconds / extent * plot_width)
    for row, (label, audio, rate) in enumerate(series):
        top = 90 + row * row_height
        draw.text((left, top), f'{label} / {len(audio) / rate:.6f} s', font=font, fill='white')
        middle = top + 94
        # Shared time and amplitude axes; no independent normalization or stretching.
        for pixel in range(plot_width):
            a = int(pixel / plot_width * extent * rate)
            b = min(len(audio), max(a + 1, int((pixel + 1) / plot_width * extent * rate)))
            if a >= len(audio):
                break
            peak = min(1, float(np.max(np.abs(audio[a:b]))))
            draw.line((left + pixel, middle - peak * 66, left + pixel, middle + peak * 66),
                      fill='#d0888d' if row < len(series) - 1 else '#67d8bb')
        for beat in range(round(extent / seconds_per_beat) + 1):
            x = xpos(beat * seconds_per_beat)
            draw.line((x, top + 29, x, top + 160), fill='#e1bb5c', width=2 if beat % 4 == 0 else 1)
            if beat < 33:
                draw.text((x + 3, top + 164), str(beat + 1), font=font, fill='#e1bb5c')
        for beat in attacks:
            x = xpos(beat * seconds_per_beat)
            draw.polygon([(x - 5, top + 23), (x + 5, top + 23), (x, top + 30)], fill='#65baff')
        if row == len(series) - 1:
            for seconds in observed:
                x = xpos(seconds)
                draw.line((x, top + 30, x, top + 160), fill='white')
    image.save(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('--before', type=Path, help='Optional uncorrected sample on the same time axis')
    parser.add_argument('--work-dir', required=True, type=Path)
    parser.add_argument('--bpm', type=float, default=140)
    parser.add_argument('--bars', type=int, required=True)
    parser.add_argument('--attacks', type=numbers, default=[], help='Intended zero-based beat positions, excluding the endpoint')
    parser.add_argument('--observed-attacks', type=numbers, default=[], help='Independently annotated attack times in seconds, one per intended attack')
    parser.add_argument('--tolerance-ms', type=float, default=5)
    parser.add_argument('--max-leading-ms', type=float, help='Optional first-signal check, not a voiced-onset check')
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    args = parser.parse_args()
    if not math.isfinite(args.bpm) or not 70 <= args.bpm <= 180 or not 1 <= args.bars <= 4:
        parser.error('Use Groove tempo 70–180 and 1–4 bars')
    if not math.isfinite(args.tolerance_ms) or args.tolerance_ms < 0:
        parser.error('Tolerance must be finite and nonnegative')
    if args.max_leading_ms is not None and (not math.isfinite(args.max_leading_ms) or args.max_leading_ms < 0):
        parser.error('Leading-signal limit must be finite and nonnegative')
    if any(t >= args.bars * 4 for t in args.attacks):
        parser.error('Attack beats must precede the clip endpoint')
    if args.observed_attacks and len(args.observed_attacks) != len(args.attacks):
        parser.error('Supply exactly one observed time per intended attack')
    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    audio, rate, decoder = decode(args.input.resolve(), work, args.cc)
    if not len(audio) or not np.isfinite(audio).all():
        parser.error('Audio is empty or contains non-finite values')
    # Mirror library.c: round the bar length, then clear its bottom two bits.
    frames_per_bar = int(44100 * 60 * 4 / args.bpm + .5) & ~3
    frames_per_beat = frames_per_bar / 4
    signal = np.flatnonzero(np.max(np.abs(audio), axis=1) > 10 ** (-55 / 20))
    leading_ms = float(signal[0] / rate * 1000) if len(signal) else None
    intended = [b * frames_per_beat / 44100 for b in args.attacks]
    bounds = [round(t * rate) for t in intended] + [len(audio)]
    offsets = [(observed - target) * 1000 for observed, target in zip(args.observed_attacks, intended)]
    observed_valid = all(t < len(audio) / rate for t in args.observed_attacks)
    checks = {'sample_rate_44100': rate == 44100, 'mono_for_groove': audio.shape[1] == 1,
              'exact_bar_frames': len(audio) == frames_per_bar * args.bars,
              'audible_signal': bool(len(signal)), 'below_full_scale': float(np.max(np.abs(audio))) < 1}
    if args.max_leading_ms is not None:
        checks['leading_signal'] = leading_ms is not None and leading_ms <= args.max_leading_ms
    if args.observed_attacks:
        checks['annotated_alignment'] = observed_valid and all(abs(x) <= args.tolerance_ms for x in offsets)
    report = {'input': str(args.input.resolve()), 'sha256': hashlib.sha256(args.input.read_bytes()).hexdigest(),
              **decoder, 'bpm': args.bpm, 'bars': args.bars, 'frames': len(audio), 'sample_rate': rate,
              'channels': audio.shape[1], 'expected_frames': frames_per_bar * args.bars,
              'leading_signal_ms_at_minus55db': leading_ms, 'intended_attack_beats': args.attacks,
              'observed_attack_seconds': args.observed_attacks, 'annotated_offsets_ms': offsets,
              'post_boundary_envelope_rise_ms': [envelope_rise(audio, rate, a, b) for a, b in zip(bounds, bounds[1:])],
              'checks': checks, 'checks_pass': all(checks.values()),
              'limits': 'Envelope rises are forward-only proxies, not proof of perceptual timing or absent accompaniment. '
                        'Annotated offsets are only as reliable as supplied annotations. Listen against a click and in Groove.'}
    (work / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
    series = []
    if args.before:
        before, before_rate, _ = decode(args.before.resolve(), work, args.cc)
        if not len(before) or not np.isfinite(before).all():
            parser.error('Comparison audio is empty or non-finite')
        series.append(('Before', before, before_rate))
    series.append((args.input.stem, audio, rate))
    draw_comparison(series, args.bpm, frames_per_beat, args.attacks, args.observed_attacks, work / 'waveform-grid.png')
    print(json.dumps(report, indent=2))
    return 0 if report['checks_pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
