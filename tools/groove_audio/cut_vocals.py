from pathlib import Path
import csv, json, re
import numpy as np
import soundfile as sf
from scipy.ndimage import binary_closing, binary_opening
from scipy.signal import butter, sosfiltfilt

from common import prepare_work, work_arguments
parser = work_arguments('Cut isolated vocals into timestamped PCM24 WAV samples.')
args = parser.parse_args()
root = prepare_work(args)
samples = root / 'vocal-samples'
samples.mkdir(exist_ok=True)
x, sr = sf.read(root / 'stems/vocals_full.wav', dtype='float32', always_2d=True)
hop = round(sr * .01)
count = len(x) // hop
rms = np.sqrt(np.mean(x[:count * hop].reshape(count, hop, -1) ** 2, axis=(1,2)))
reference = float(np.percentile(rms, 95))
active = rms > max(reference * .045, .0013)
active = binary_closing(active, structure=np.ones(23, bool))
active = binary_opening(active, structure=np.ones(9, bool))
edges = np.diff(np.r_[False, active, False].astype(int))
regions = list(zip(np.flatnonzero(edges == 1) * .01, np.flatnonzero(edges == -1) * .01))
alignment_path = root / 'lyrics-alignment.json'
alignment = json.loads(alignment_path.read_text()) if alignment_path.exists() else {'segments': []}
words = [w for s in alignment['segments'] for w in s.get('words', [])]
subregions = []
for start, end in regions:
    if end - start < .32: continue
    parts = [start]
    while end - parts[-1] > 5.8:
        left, right = parts[-1] + 1.8, min(parts[-1] + 5.2, end - .7)
        gaps = [(w['end'], words[i+1]['start']) for i,w in enumerate(words[:-1])
                if left <= w['end'] < words[i+1]['start'] <= right
                and .07 < words[i+1]['start'] - w['end'] < .7]
        if gaps:
            cut = min(gaps, key=lambda g: abs((g[0]+g[1])/2-(parts[-1]+3.3)))
            point = sum(cut)/2
        else:
            i0, i1 = round(left/.01), round(right/.01)
            smooth = np.convolve(rms, np.ones(8)/8, 'same')
            point = (i0 + int(np.argmin(smooth[i0:i1]))) * .01
        assert left <= point <= right and parts[-1] < point < end
        parts.append(point)
    parts.append(end)
    subregions += list(zip(parts[:-1], parts[1:]))

rows = []
for index, (start,end) in enumerate(subregions,1):
    previous_end = subregions[index-2][1] if index>1 else 0
    next_start = subregions[index][0] if index<len(subregions) else len(x)/sr
    a = max(previous_end, start-.045)
    b = min(next_start, end+.22)
    clip = x[round(a*sr):round(b*sr)].astype('float64')
    if len(clip)<sr*.3: continue
    clip = sosfiltfilt(butter(2, 55, btype='highpass', fs=sr, output='sos'), clip, axis=0)
    peak = np.max(np.abs(clip))
    if peak < .012: continue
    gain = min(10**(6/20), 10**(-1/20)/peak)
    clip *= gain
    nfade = round(.005 * sr)
    clip[:nfade] *= np.linspace(0,1,nfade)[:,None]
    clip[-nfade:] *= np.linspace(1,0,nfade)[:,None]
    spoken = [w['word'].strip() for w in words if start-.05 <= w['start'] and w['end'] <= end+.05]
    text = ' '.join(spoken)
    tokens = re.findall(r'[a-z]+', text.lower())
    if len(tokens) > 8 and len(set(tokens)) / len(tokens) < .35:
        text = ''; tokens = []
    filename = f'{index:03d}_vocal_{a:07.3f}-{b:07.3f}.wav'
    sf.write(samples / filename, clip, sr, subtype='PCM_24')
    rows.append({'file':filename,'source_start_seconds':round(a,3),'source_end_seconds':round(b,3),
                 'duration_seconds':round(len(clip)/sr,3),'estimated_lyrics':text,'gain_db':round(20*np.log10(gain),2)})
if not rows:
    raise ValueError('No audible vocal regions found; existing samples were left intact')
with (samples / 'sample-index.csv').open('w',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=rows[0].keys());writer.writeheader();writer.writerows(rows)
keep = {r['file'] for r in rows}
for p in samples.glob('*.wav'):
    if p.name not in keep: p.unlink()
(samples / 'sample-info.txt').write_text(
    f'{root.name} vocal samples\n{sr} Hz, {x.shape[1]} channels, PCM 24-bit WAV.\n'
    'Original pitch and timing preserved; no time stretching.\n'
    'Mel-Band RoFormer vocal isolation; 55 Hz high-pass, 5 ms boundary fades.\n'
    'Peak ceiling -1 dBFS, boosts capped at +6 dB.\n'
    'Filenames use source timestamps; estimated lyrics in the index may contain errors.\n'
    'Cuts are automatic and may split held notes or phrases.\n'
    'Source timestamps and gain adjustments are in sample-index.csv.\n'
    'Original full-length separation is preserved separately as vocals_full.wav.\n')
print('Exported', len(rows), 'vocal WAV samples.')
