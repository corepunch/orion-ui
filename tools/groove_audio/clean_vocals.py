"""Repair documented vocal edit seams, de-click, and verify staged gapless MP3s.

Inputs are lossless masters and optional original isolated WAVs with reviewed
landmark maps. Output is staged for review; this tool never replaces app assets.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

import imageio_ffmpeg
import numpy as np
import soundfile as sf
from scipy.signal import butter, correlate, sosfiltfilt

from verify_sample import draw_comparison

SR, BEAT = 44100, 18900
SETTINGS = {'seam_context_ms': 8, 'seam_original_core_ms': 2,
            'attack_fade_ms': 2, 'release_fade_ms': 25,
            'protected_phoneme_ms': 30, 'protection_crossfade_ms': 4,
            'declick': 'adeclick=t=4:a=4:b=1.5', 'declick_padding_frames': 4410,
            'peak_ceiling_dbfs': -1, 'mp3_kbps': 320}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def blend_source(audio, source, start, source_start, weights):
    end = start + len(weights)
    if start < 0 or source_start < 0 or end > len(audio) or source_start + len(weights) > len(source):
        raise ValueError('Source seam context is out of bounds')
    audio[start:end] = audio[start:end] * (1 - weights) + source[source_start:source_start+len(weights)] * weights


def restore_seams(audio, source, row):
    """Use authentic waveform handles instead of zero notches/linear bridges."""
    context, core = round(.008 * SR), round(.002 * SR)
    restored = []
    source_frames = [round(x * SR) for x in row['source_seconds']]
    target_frames = [round(x * BEAT) for x in row['target_beats']]
    for i, (a, b, start, end) in enumerate(zip(source_frames, source_frames[1:], target_frames, target_frames[1:])):
        if i:
            frames = np.arange(-context, context+1)
            weights = np.ones(len(frames))
            edge = abs(frames) > core
            weights[edge] = .5 + .5 * np.cos(np.pi * (abs(frames[edge])-core)/(context-core))
            blend_source(audio, source, start-context, a-context, weights)
            restored.append({'kind': 'zero_notch', 'frame': start})
        fitted = end-start
        if i == len(source_frames)-2 and (b-a)/fitted < .70:
            fitted = round((b-a)/.80)
        head = min(round(.035 * SR), (b-a)//3, fitted//3)
        if b-a-head < 256 or fitted-head < 256:
            head = 0
        if head > 44:
            # The original first 35 ms remain intact, apart from the old 2 ms
            # linear bridge. Continue the real source through that bridge,
            # then crossfade into the stretched vowel over 8 ms.
            left, right = head-66, head+context
            weights = np.ones(right-left)
            ramp_start = 110
            weights[ramp_start:] = .5 + .5 * np.cos(np.linspace(0, np.pi, len(weights)-ramp_start))
            blend_source(audio, source, start+left, a+left, weights)
            restored.append({'kind': 'linear_bridge', 'frame': start+head})
    return restored


def match_attack(before, after, target):
    skip, length, radius = round(.004*SR), round(.025*SR), round(.005*SR)
    template = before[target+skip:target+skip+length].copy()
    template -= template.mean()
    lo = max(0, target+skip-radius)
    segment = after[lo:min(len(after), target+skip+radius+length)]
    dots = correlate(segment, template, mode='valid', method='fft')
    sums, power = np.r_[0, np.cumsum(segment)], np.r_[0, np.cumsum(segment**2)]
    k = np.arange(len(dots))
    variance = power[k+length]-power[k]-(sums[k+length]-sums[k])**2/length
    scores = dots/np.sqrt(np.maximum(variance, 1e-20)*max(float(template@template), 1e-20))
    best = int(np.argmax(scores))
    offset = lo+best-skip-target
    return {'frame': target, 'offset_frames': offset, 'correlation': float(scores[best]),
            'passed': abs(offset) <= round(.002*SR) and float(scores[best]) >= .95}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', required=True, type=Path)
    parser.add_argument('--out-dir', required=True, type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    rows = json.loads(args.plan.read_text())['samples']
    output = args.out_dir.resolve()
    if output.exists():
        parser.error('Output directory already exists; use a fresh staging directory')
    names = [r['file'] for r in rows]
    if not names or len(names) != len(set(names)) or any(Path(n).name != n or not n.endswith('.mp3') for n in names):
        parser.error('Plan has missing, duplicate, or unsafe filenames')
    output.mkdir(parents=True)
    decoder = output/'decode_sample'
    subprocess.run(['cc', '-std=c99', '-O2', '-I', str(repo), str(Path(__file__).with_name('decode_sample.c')), '-o', str(decoder)], check=True)
    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    reports = []
    for row in rows:
        name = Path(row['file']).stem
        original = repo/'apps/groove/share/vocals'/row['file']
        if sha(original) != row['original_sha256']:
            raise ValueError(f'{name}: app asset changed since plan creation')
        master = repo/row['master']
        if row.get('master_sha256') and sha(master) != row['master_sha256']:
            raise ValueError(f'{name}: master hash mismatch')
        before, hz = sf.read(master, dtype='float64')
        if hz != SR or before.ndim != 1 or len(before) != row['frames'] or not np.isfinite(before).all():
            raise ValueError(f'{name}: invalid master')
        audio = before.copy()
        seams = []
        source_hash = None
        if row.get('source'):
            source_path = repo/row['source']
            if row.get('source_sha256') and sha(source_path) != row['source_sha256']:
                raise ValueError(f'{name}: isolated source hash mismatch')
            source, hz = sf.read(source_path, always_2d=True)
            if hz != SR or not np.isfinite(source).all():
                raise ValueError(f'{name}: invalid isolated source')
            source = sosfiltfilt(butter(2, 55, fs=SR, btype='highpass', output='sos'), source.mean(axis=1)) * row['source_gain']
            seams = restore_seams(audio, source, row)
            source_hash = sha(source_path)
        intermediate = output/(name+'-seams.wav')
        sf.write(intermediate, audio, SR, subtype='FLOAT')
        cleaned = output/(name+'-declick.wav')
        pad = SETTINGS['declick_padding_frames']
        filters = f'adelay={pad}S:all=1,apad=pad_len={pad},{SETTINGS["declick"]},atrim=start_sample={pad}:end_sample={pad+len(audio)}'
        subprocess.run([ffmpeg, '-v', 'error', '-y', '-i', str(intermediate), '-af', filters, '-c:a', 'pcm_f32le', str(cleaned)], check=True)
        declicked, hz = sf.read(cleaned)
        if hz != SR or len(declicked) != len(before) or not np.isfinite(declicked).all():
            raise ValueError(f'{name}: de-click changed format or duration')
        beats = row.get('target_beats', [0, row['frames']/BEAT])[:-1]
        # Rap consonants and glottal attacks can resemble impulsive noise.
        # Keep their authored heads, blending de-clicking in after each head.
        protection = np.ones(len(audio))
        head, ramp = round(.030*SR), round(.004*SR)
        for beat in beats:
            at = round(beat*BEAT)
            protection[at:at+head] = 0
            protection[at+head:at+head+ramp] = .5-.5*np.cos(np.linspace(0, np.pi, ramp))
            if at >= ramp:
                protection[at-ramp:at] = .5+.5*np.cos(np.linspace(0, np.pi, ramp))
        declicked = audio+(declicked-audio)*protection
        declick_delta = audio-declicked
        audio = declicked
        active = np.flatnonzero(abs(audio) > 1e-6)
        if not len(active):
            raise ValueError(f'{name}: empty audio')
        end = int(active[-1])+1
        fade = min(round(.025*SR), end)
        audio[end-fade:end] *= .5+.5*np.cos(np.linspace(0, np.pi, fade))
        audio[end:] = 0
        attack = round(.002*SR)
        if abs(audio[0]) > 1e-6:
            audio[:attack] *= .5-.5*np.cos(np.linspace(0, np.pi, attack))
        gain = min(1.0, 10**(SETTINGS['peak_ceiling_dbfs']/20)/float(max(abs(audio))))
        audio *= gain
        wav, mp3 = output/(name+'.wav'), output/row['file']
        sf.write(wav, audio, SR, subtype='PCM_24')
        subprocess.run([ffmpeg, '-v', 'error', '-y', '-i', str(wav), '-i', str(original), '-map', '0:a:0', '-map_metadata', '1', '-c:a', 'libmp3lame', '-b:a', '320k', '-write_xing', '1', '-id3v2_version', '3', '-metadata', 'TBPM=140', str(mp3)], check=True)
        raw = output/'decoded.pcm'
        count, channels, hz, delay = map(int, subprocess.check_output([str(decoder), str(mp3), str(raw)], text=True).split())
        decoded = np.fromfile(raw, np.int16).astype(float)/32768
        matches = [match_attack(before, decoded, round(b*BEAT)) for b in beats]
        signal = np.flatnonzero(abs(decoded) > 10**(-55/20))
        leading = float(signal[0]/SR*1000) if len(signal) else None
        checks = {'exact_frames': count == len(before), 'mono_44100': channels == 1 and hz == SR,
                  'finite_nonzero': bool(np.isfinite(decoded).all() and len(signal)),
                  'no_clipping': bool(max(abs(decoded)) < 1),
                  'signal_within_5ms': leading is not None and leading <= 5,
                  'quiet_endpoints': bool(max(abs(decoded[[0,-1]])) < 10**(-55/20)),
                  'preserved_attack_placement': all(m['passed'] for m in matches)}
        report = {'file': row['file'], 'original_mp3_sha256': row['original_sha256'],
                  'master_sha256': sha(master), 'isolated_source_sha256': source_hash,
                  'cleaned_mp3_sha256': sha(mp3), 'cleaned_wav_sha256': sha(wav),
                  'frames': count, 'bars': count//75600, 'source_gain': row.get('source_gain'),
                  'restored_seams': seams, 'release_end_frame': end, 'peak_gain': gain,
                  'declick_changed_frames_above_minus80db': int(np.sum(abs(declick_delta) > .0001)),
                  'declick_peak_change': float(max(abs(declick_delta))),
                  'leading_signal_ms_at_minus55db': leading,
                  'attack_matches': matches, 'checks': checks, 'passed': all(checks.values())}
        reports.append(report)
        draw_comparison([('Original lossless master', before, SR), ('Cleaned final / Groove MP3 decoder', decoded, SR)], 140, BEAT, beats, [], output/(name+'-comparison.png'))
        (output/'verification.json').write_text(json.dumps({'settings': SETTINGS, 'decoder': 'Groove minimp3_ex / MP3D_SEEK_TO_SAMPLE', 'limits': 'Computational and visual QA only; no listening performed. Preserved attack templates verify unchanged placement, not the musical accuracy of original annotations or absence of all generation/separation artifacts.', 'samples': reports}, indent=2)+'\n')
        print(f'{name}: {"PASS" if report["passed"] else "FAIL"}; {len(seams)} seams', flush=True)
        if not report['passed']:
            raise ValueError(f'{name}: {checks}')


if __name__ == '__main__':
    main()
