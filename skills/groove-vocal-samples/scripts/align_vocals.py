#!/usr/bin/env python3
"""Align reviewed vocal landmarks to Groove's 140 BPM grid and verify decoded MP3s.

Requires NumPy, SciPy, SoundFile, Pillow, imageio-ffmpeg and the Groove checkout.
This helper neither generates nor separates audio. See references/alignment-and-qa.md.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

import numpy as np
import soundfile as sf
from scipy.signal import butter, correlate, sosfiltfilt

SR = 44100
BEAT = 18900
SETTINGS = dict(bpm=140, sample_rate=SR, channels=1, highpass_hz=55,
                preserved_attack_ms=35, boundary_ramp_ms=1.5,
                final_fade_ms=20, peak_dbfs=-2, short_release_rate=.8)
LIMITS = ('Template matching verifies placement of supplied source annotations, '
          'not perceptual annotation correctness. Review every image and listen '
          'when available. Singer identity, key, lyrics and zero backing leakage '
          'are not established by these measurements. No listening is performed.')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def increasing(values):
    return (len(values) >= 2 and all(math.isfinite(x) and x >= 0 for x in values)
            and all(a < b for a, b in zip(values, values[1:])))


def prepare(plan_path, output, overwrite):
    plan = json.loads(plan_path.read_text())
    rows = plan.get('samples', [])
    if not rows:
        raise ValueError('Plan must contain a nonempty samples list')
    names, prepared = set(), []
    planned_outputs = [output / 'manifest.json']
    for row in rows:
        name = row['name']
        if not isinstance(name, str) or not re.fullmatch(r'[a-z0-9][a-z0-9_-]{0,119}', name) or name in names:
            raise ValueError(f'Unsafe or duplicate output name: {name!r}')
        names.add(name)
        if type(row['bars']) is not int or row['bars'] not in (1, 2, 4):
            raise ValueError(f'{name}: bars must be 1, 2 or 4')
        if not isinstance(row.get('annotation'), str) or not row['annotation'].strip():
            raise ValueError(f'{name}: supply a reviewed source annotation description')
        source = Path(row['source']).expanduser()
        source = (plan_path.parent / source).resolve()
        if source.suffix.lower() != '.wav':
            raise ValueError(f'{name}: use an isolated WAV source')
        info = sf.info(source)
        times = [float(x) for x in row['source_seconds']]
        beats = [float(x) for x in row['target_beats']]
        if (not increasing(times) or not increasing(beats) or len(times) != len(beats)
                or beats[0] != 0 or beats[-1] != row['bars'] * 4):
            raise ValueError(f'{name}: invalid monotonic source/target map or endpoint')
        source_frames = [round(t * SR) for t in times]
        target_frames = [round(b * BEAT) for b in beats]
        if info.samplerate != SR or source_frames[-1] > info.frames:
            raise ValueError(f'{name}: expected 44100 Hz and in-bounds source anchors')
        if any(b-a < 256 for a, b in zip(source_frames, source_frames[1:])):
            raise ValueError(f'{name}: source intervals must be at least 256 frames')
        if any(b-a < 256 for a, b in zip(target_frames, target_frames[1:])):
            raise ValueError(f'{name}: target intervals must be at least 256 frames')
        files = [output / (name + suffix) for suffix in
                 ('.wav', '.mp3', '-before.wav', '-grid.png', '-final.png')]
        planned_outputs.extend(files)
        prepared.append({**row, 'source': str(source), 'source_seconds': times,
                         'target_beats': beats, 'source_frames': source_frames,
                         'target_frames': target_frames})
    sources = {Path(row['source']) for row in prepared}
    if sources.intersection(planned_outputs) or plan_path in planned_outputs:
        raise ValueError('An output would overwrite a source or the plan')
    if not overwrite and any(path.exists() for path in planned_outputs):
        raise FileExistsError('Planned outputs exist; use a fresh directory or explicit --overwrite')
    return prepared


def layout(source_count, target_count, final):
    fitted_count = target_count
    if final and source_count / target_count < .70:
        fitted_count = round(source_count / .80)
    head = min(round(.035 * SR), source_count // 3, fitted_count // 3)
    if source_count - head < 256 or fitted_count - head < 256:
        head = 0
    rate = (source_count - head) / (fitted_count - head)
    return fitted_count, head, rate


def stretch(source, count, head, rate, ffmpeg, temporary):
    src, dst = temporary / 'source.wav', temporary / 'fitted.wav'
    sf.write(src, source[head:], SR, subtype='FLOAT')
    filters, remainder = [], rate
    while remainder < .5:
        filters.append('atempo=0.5')
        remainder /= .5
    while remainder > 2:
        filters.append('atempo=2')
        remainder /= 2
    filters += [f'atempo={remainder:.10f}', 'apad', f'atrim=end_sample={count-head}']
    subprocess.run([ffmpeg, '-v', 'error', '-y', '-i', str(src), '-af', ','.join(filters),
                    '-c:a', 'pcm_f32le', str(dst)], check=True)
    fitted, rate_hz = sf.read(dst)
    if rate_hz != SR or len(fitted) != count - head or not np.isfinite(fitted).all():
        raise ValueError('Time stretch returned invalid audio')
    result = np.r_[source[:head], fitted]
    if head > 44:
        result[head-44:head+44] = np.linspace(result[head-44], result[head+44], 88)
    edge = min(round(.0015 * SR), len(result) // 4)
    result[:edge] *= np.linspace(0, 1, edge)
    result[-edge:] *= np.linspace(1, 0, edge)
    return result


def match_attacks(source, decoded, row, intervals):
    skip, length, radius = round(.004 * SR), round(.025 * SR), round(.060 * SR)
    matches, observed = [], []
    for i, (a, target) in enumerate(zip(row['source_frames'][:-1], row['target_frames'][:-1])):
        match = dict(target_beat=row['target_beats'][i], source_seconds=row['source_seconds'][i])
        # The inner join edits the last 44 frames of the preserved head.
        if intervals[i]['preserved_head_frames'] - 44 < skip + length:
            matches.append({**match, 'verified': False, 'reason': 'Preserved head too short for template'})
            continue
        template = source[a+skip:a+skip+length].copy()
        template -= template.mean()
        energy = float(np.sum(template ** 2))
        if energy < 1e-12:
            matches.append({**match, 'verified': False, 'reason': 'Source template has insufficient signal'})
            continue
        lo = max(0, target + skip - radius)
        hi = min(len(decoded), target + skip + radius + length)
        segment = decoded[lo:hi]
        dots = correlate(segment, template, mode='valid', method='fft')
        sums = np.r_[0, np.cumsum(segment)]
        power = np.r_[0, np.cumsum(segment ** 2)]
        k = np.arange(len(dots))
        variance = power[k+length] - power[k] - (sums[k+length] - sums[k]) ** 2 / length
        scores = dots / np.sqrt(np.maximum(variance, 1e-20) * energy)
        best = int(np.argmax(scores))
        measured = lo + best - skip
        offset_ms = (measured - target) / SR * 1000
        score = float(scores[best])
        matches.append({**match, 'offset_ms': offset_ms, 'correlation': score,
                        'verified': abs(offset_ms) <= 2 and score >= .75})
        observed.append(max(0, measured / SR))
    return matches, observed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', required=True, type=Path, help='Current Groove checkout')
    parser.add_argument('--plan', required=True, type=Path)
    parser.add_argument('--out-dir', required=True, type=Path)
    parser.add_argument('--ffmpeg', help='Optional FFmpeg executable path')
    parser.add_argument('--cc', default='cc', help='C compiler command')
    parser.add_argument('--overwrite', action='store_true')
    parser.add_argument('--allow-extreme-rate', action='store_true')
    args = parser.parse_args()
    repo, plan_path, output = args.repo.resolve(), args.plan.resolve(), args.out_dir.resolve()
    verifier = repo / 'tools/groove_audio/verify_sample.py'
    if not verifier.is_file():
        parser.error('Repo must contain tools/groove_audio/verify_sample.py and decode_sample.c')
    sys.path.insert(0, str(verifier.parent))
    from verify_sample import draw_comparison
    try:
        rows = prepare(plan_path, output, args.overwrite)
        for row in rows:
            intervals = []
            for i, (a, b, c, d) in enumerate(zip(row['source_frames'], row['source_frames'][1:],
                                                row['target_frames'], row['target_frames'][1:])):
                count, head, rate = layout(b-a, d-c, i == len(row['source_frames'])-2)
                if not args.allow_extreme_rate and not .5 <= rate <= 2:
                    raise ValueError(f"{row['name']}: interval {i} atempo={rate:.3f}; review map before --allow-extreme-rate")
                intervals.append(dict(fitted_frames=count, preserved_head_frames=head, atempo_rate=rate))
            row['intervals'] = intervals
        if args.ffmpeg:
            ffmpeg = args.ffmpeg
        else:
            import imageio_ffmpeg
            ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
        output.mkdir(parents=True, exist_ok=True)
        reports = []
        with tempfile.TemporaryDirectory(prefix='vocal-align-', dir=output) as tmp:
            temporary = Path(tmp)
            decoder = temporary / 'decode_sample'
            subprocess.run(shlex.split(args.cc) + ['-std=c99', '-O2', '-I', str(repo),
                            str(verifier.with_name('decode_sample.c')), '-o', str(decoder)], check=True)
            for row in rows:
                source_path = Path(row['source'])
                source, hz = sf.read(source_path, always_2d=True, dtype='float64')
                if hz != SR or not np.isfinite(source).all():
                    raise ValueError(f'{source_path}: invalid sample rate or non-finite audio')
                source = sosfiltfilt(butter(2, 55, fs=SR, btype='highpass', output='sos'), source.mean(axis=1))
                result = np.zeros(row['bars'] * 4 * BEAT)
                for i, (a, b, start) in enumerate(zip(row['source_frames'], row['source_frames'][1:], row['target_frames'])):
                    interval = row['intervals'][i]
                    fitted = stretch(source[a:b], interval['fitted_frames'], interval['preserved_head_frames'],
                                     interval['atempo_rate'], ffmpeg, temporary)
                    result[start:start+len(fitted)] = fitted
                tail = round(.020 * SR)
                result[-tail:] *= np.linspace(1, 0, tail)
                peak = float(np.max(np.abs(result)))
                if not math.isfinite(peak) or peak <= .0005:
                    raise ValueError(f"{row['name']}: silent or invalid processed vocal")
                result *= 10 ** (-2/20) / peak
                name = row['name']
                wav, mp3 = output / (name+'.wav'), output / (name+'.mp3')
                before = source[row['source_frames'][0]:row['source_frames'][-1]]
                sf.write(output / (name+'-before.wav'), before, SR, subtype='FLOAT')
                sf.write(wav, result, SR, subtype='PCM_24')
                subprocess.run([ffmpeg, '-v', 'error', '-y', '-i', str(wav), '-c:a', 'libmp3lame',
                                '-b:a', '320k', '-write_xing', '1', '-id3v2_version', '3',
                                '-metadata', 'TBPM=140', '-metadata', 'title='+row.get('title', name), str(mp3)], check=True)
                raw = temporary / 'decoded.pcm'
                count, channels, hz, delay = map(int, subprocess.check_output(
                    [str(decoder), str(mp3), str(raw)], text=True).split())
                pcm = np.fromfile(raw, dtype=np.dtype('=i2'))
                if count != len(pcm) or channels != 1 or hz != SR or not len(pcm):
                    raise ValueError(f'{mp3}: invalid actual Groove decode')
                decoded = pcm.astype('float64') / 32768
                matches, observed = match_attacks(source, decoded, row, row['intervals'])
                signal = np.flatnonzero(np.abs(decoded) > 10 ** (-55/20))
                leading = float(signal[0]/SR*1000) if len(signal) else None
                checks = dict(exact_bar_frames=count == len(result), mono_44100=channels == 1 and hz == SR,
                              finite=bool(np.isfinite(decoded).all()), non_silent=bool(len(signal)),
                              below_full_scale=bool(np.max(np.abs(decoded)) < 1),
                              leading_signal_within_5ms=leading is not None and leading <= 5,
                              supplied_attack_placement=all(m['verified'] for m in matches))
                report = {**row, 'file': mp3.name, 'frames': count, 'settings': SETTINGS,
                          'source_sha256': sha(source_path), 'mp3_sha256': sha(mp3), 'wav_sha256': sha(wav),
                          'decoder': 'Groove minimp3_ex / MP3D_SEEK_TO_SAMPLE',
                          'removed_encoder_delay_samples': delay, 'leading_signal_ms_at_minus55db': leading,
                          'attack_matches': matches, 'checks': checks, 'checks_pass': all(checks.values()),
                          'visual_review_required': True, 'limits': LIMITS}
                reports.append(report)
                (output / 'manifest.json').write_text(json.dumps(reports, indent=2)+'\n')
                series = [('Isolated phrase before timing edits', before, SR), (row.get('title', name)+' / final MP3', decoded, SR)]
                draw_comparison(series, 140, BEAT, row['target_beats'][:-1], observed, output/(name+'-grid.png'))
                draw_comparison(series[-1:], 140, BEAT, row['target_beats'][:-1], observed, output/(name+'-final.png'))
                print(f"{name}: {'PASS' if report['checks_pass'] else 'REVIEW'}; visual review still required", flush=True)
        return 0 if all(r['checks_pass'] for r in reports) else 1
    except (ValueError, KeyError, TypeError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(2, f'Error: {error}\n')


if __name__ == '__main__':
    raise SystemExit(main())
