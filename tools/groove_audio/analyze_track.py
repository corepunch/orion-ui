from pathlib import Path
import json
import numpy as np
import librosa
import soundfile as sf

from common import prepare_work, work_arguments
parser = work_arguments('Estimate tempo, beat times and tonal center.')
args = parser.parse_args()
out = prepare_work(args)
x, sr = sf.read(out / 'input.wav', dtype='float32', always_2d=True)
y = librosa.resample(x.mean(axis=1), orig_sr=sr, target_sr=22050)
sr = 22050
hop = 256
onset = librosa.onset.onset_strength(y=y, sr=sr, hop_length=hop)
tempo, beats = librosa.beat.beat_track(onset_envelope=onset, sr=sr, hop_length=hop, start_bpm=140, tightness=100)
beat_times = librosa.frames_to_time(beats, sr=sr, hop_length=hop)
if len(beat_times) < 2:
    raise ValueError('Need at least two detected beats to estimate tempo')
step = float(np.median(np.diff(beat_times)))
tempo_refined = 60 / step
grid_candidates = np.arange(125, 155, .01)
times = np.arange(len(onset)) * hop / sr
weight = np.maximum(onset - np.median(onset), 0)
scores = np.array([abs(np.sum(weight * np.exp(2j * np.pi * times * bpm / 60))) for bpm in grid_candidates])
best = float(grid_candidates[np.argmax(scores)])
phase = np.angle(np.sum(weight * np.exp(2j * np.pi * times * best / 60)))
offset = (phase / (2 * np.pi) * 60 / best) % (60 / best)
sample = y[int(12 * sr):int(100 * sr)] if len(y) > 13 * sr else y
chroma = librosa.feature.chroma_stft(y=sample, sr=sr, n_fft=4096, hop_length=1024).mean(axis=1)
major = np.array([6.35,2.23,3.48,2.33,4.38,4.09,2.52,5.19,2.39,3.66,2.29,2.88])
minor = np.array([6.33,2.68,3.52,5.38,2.60,3.53,2.54,4.75,3.98,2.69,3.34,3.17])
names = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
keys = sorted([(float(np.corrcoef(chroma, np.roll(profile, root))[0,1]), names[root]+' '+mode) for mode,profile in [('major',major),('minor',minor)] for root in range(12)], reverse=True)
result = {'duration_seconds':len(y)/sr,'beat_tracker_bpm':float(np.ravel(tempo)[0]),'median_beat_bpm':tempo_refined,'periodic_onset_bpm':best,'beat_offset_seconds':float(offset),'key_candidates':keys[:5],'chroma':dict(zip(names,map(float,chroma))),'beat_times':beat_times.tolist()}
(out/'track-analysis.json').write_text(json.dumps(result,indent=2))
print(json.dumps({k:v for k,v in result.items() if k!='beat_times'},indent=2),flush=True)
