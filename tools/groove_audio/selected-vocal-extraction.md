# Selected Take Me Higher vocal extraction

On 2026-10-06, the user chose **`Take me higher let me rise - 01-existing-mel.wav`**
after listening to the extraction comparisons. The restored ensemble sounded
nearly the same to them, so the original Mel-Band RoFormer extraction is the
selected baseline. This records the process that actually produced that file.

## Source and master

Source recording:

```text
/Users/ICHERNA/Library/CloudStorage/OneDrive-Mercedes-Benz(corpdir.onmicrosoft.com)/Desktop/Take Me Higher.mp3
```

The MP3 is stereo, 48 kHz, approximately 207.96 seconds. FFmpeg 7.1 decoded the
whole song to **44.1 kHz stereo float32 WAV**, without changing its tempo or
pitch. The result is `work/song2/input.wav` (9,171,036 frames).

Mel-Band RoFormer separated the entire decoded song into a vocal master and an
instrumental master. Separating the full song before selecting phrases gives
the model surrounding musical context. The selected source is:

```text
tools/groove_audio/work/song2/stems/vocals_full.wav
```

This master is 44.1 kHz stereo float32 WAV, with the same frame count as the
decoded input. Retain it for future cuts.

## Separation settings

| Setting | Actual run |
|---|---|
| Model | `vocals_mel_band_roformer.ckpt` |
| Model configuration | `vocals_mel_band_roformer.yaml` |
| Separator | `audio-separator 0.47.0` |
| Runtime | Python 3.12.13, PyTorch 2.14.1, macOS ARM64 |
| Acceleration | Apple Silicon MPS |
| Precision | Native float16 (`--use_native_fp16`) |
| Segment size | 512, model segment-size override enabled |
| Overlap | 4 |
| Normalization setting | 0.98 |
| Output | WAV through SoundFile; float input subtype retained |
| Output names | `vocals_full.wav`, `instrumental.wav` |

The log at `work/song2/separation.log` confirms MPS and native float16. It records
4 minutes 9 seconds for separation, excluding the initial model download.
The machine used for this run was an M1 Pro MacBook Pro with 32 GB RAM.

The model's YAML declares a stereo 44.1 kHz Mel-Band RoFormer, dimension 384,
depth 6 and 60 mel bands. Keep that configuration with the checkpoint; the CLI
overrides above are also needed to reproduce the run.

From the repository root, using the environment described in [README.md](README.md):

```sh
work_dir="$PWD/tools/groove_audio/work/song2-reproduction"
source_mp3="$HOME/Library/CloudStorage/OneDrive-Mercedes-Benz(corpdir.onmicrosoft.com)/Desktop/Take Me Higher.mp3"
tools/groove_audio/.venv/bin/python tools/groove_audio/separate_vocals.py \
  "$source_mp3" --work-dir "$work_dir" \
  --model-dir "$PWD/tools/groove_audio/work/song2/models" \
  --segment-size 512 --overlap 4
```

The script supplies native FP16, normalization, output names, SoundFile output,
and the FFmpeg path wrapper. Confirm the log selects MPS when reproducing the
Mac run. Model inference on another runtime or device may not be byte-identical.

## Exact approved phrase export

The approved audition uses **0.30–3.90 seconds** of the vocal master:
158,760 frames, or 3.60 seconds. Its export steps were:

1. Read the separated master as float32 stereo.
2. Select frames `round(0.30 * 44100)` through `round(3.90 * 44100)`, end exclusive.
3. Average the two channels to mono, then convert the result to float64 for export processing.
4. Apply an 8 ms linear fade-in and fade-out (353 frames each), reaching zero at both endpoints.
5. Write mono 44.1 kHz PCM24 WAV.

The comparison exporter level-matched alternatives to this baseline and used a
shared peak guard. For this selected phrase, its level adjustment and shared gain
were both **1.0**, so the selected file received no gain change.

The following reproduces the approved WAV exactly from the retained master:

```sh
tools/groove_audio/.venv/bin/python - <<'PY'
from pathlib import Path
import numpy as np
import soundfile as sf

root = Path('tools/groove_audio/work/song2')
audio, sample_rate = sf.read(root / 'stems/vocals_full.wav',
                             dtype='float32', always_2d=True)
assert sample_rate == 44100 and audio.shape[1] == 2
clip = audio[round(.30 * sample_rate):round(3.90 * sample_rate)]
clip = clip.mean(axis=1).astype('float64')
fade_frames = round(.008 * sample_rate)
clip[:fade_frames] *= np.linspace(0, 1, fade_frames)
clip[-fade_frames:] *= np.linspace(1, 0, fade_frames)
sf.write(root / 'Take me higher let me rise - 01-existing-mel.wav',
         clip, sample_rate, subtype='PCM_24')
PY
```

This export was verified byte-for-byte against the selected comparison file.
Its SHA-256 is:

```text
63a8a16aca7d53d828ec602be33e60983ba5573f3b47d24e74ec01c588f44901
```

## Processing scope and future sample selection

The selected audio uses **no dereverb, gate, high-pass filter, denoiser, ensemble,
Apollo restoration, ACE-Step generation/extraction, pitch correction, time
stretching or added silence**. The separator itself is the only model stage.
The fades prevent boundary clicks; they do not remove music leakage within a phrase.

Future samples should use this raw vocal master and the same minimal export
processing. Select complete lyrics that make sense on their own, preserving
consonants, breaths and note releases. Use estimated transcription only to locate
candidates, then confirm words and boundaries by listening. Use the lyrical phrase
as the title and `Cut 01`, `Cut 02`, etc. for variations, following the
[Dance eJay naming reference](../../apps/groove/docs/dance-ejay-pxd.md).

The desired library slots remain 4 or 8 beats. At 140 BPM these are 75,600 and
151,200 frames at 44.1 kHz. This approved **3.60-second audition is longer than an
8-beat slot** (3.4286 seconds); it is a sound-quality reference, not a verified
beat-conforming export. Do not truncate a word or automatically compress this
approved performance to claim it fits. Choose a naturally fitting complete phrase,
or review a boundary/timing adjustment by ear before final packaging.

Existing `song2/vocals` and `song2/vocal-phrases` were exported by an earlier
dereverb/expander/beat-fitting workflow. They have not been replaced by this
documentation update. The selected comparison and `stems/vocals_full.wav` identify
the new baseline unambiguously.

## Provenance checks

| Asset | SHA-256 |
|---|---|
| Original MP3 | `bc5dabc95fff24cf662209252dfd77e327cfee98b94dcb3c39f7d6cb1bf19309` |
| Decoded `input.wav` | `62955607d4e59aa71b13c5241d177d534e39e0fd5f872dcfc797f7a2ef38a70b` |
| Model checkpoint | `87201f4d31afb5bc79993230fc49446918425574db48c01c405e44f365c7559e` |
| Model YAML | `b958b29c8f7195f0d86bee6759a33980db675c4ecaf2fcaa80fa125828e6cd38` |
| Stereo vocal master | `7f6901aa0ea8c963197e31450226cd7bdae8bb6b5dfa0765fe7ab400ccbe77d0` |

Local `work/song2/selected-extraction-provenance.json` records paths, formats,
frame counts, file sizes and hashes. Audio, logs and model weights stay local
under the ignored work directory; this document is the tracked recipe.
