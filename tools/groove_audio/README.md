# Groove audio study tools

These scripts preserve the workflow used to isolate vocals from **Take Me Higher**,
cut WAV samples, and test whether its backing could be approximated with Groove's
existing synthesizer. Source recordings, generated samples, model weights and
rendered studies are deliberately excluded from this directory's Git history.

The selected vocal extraction for the `song2` library is documented in
[Selected Take Me Higher extraction](selected-vocal-extraction.md). That recipe
reproduces the approved `01-existing-mel.wav` audition from the untouched
Mel-Band RoFormer master. Use it as the baseline for future vocal samples;
the automatic cutter and later cleanup experiments described elsewhere are
different processing paths.

The pipeline runs locally. Model weights are downloaded on first use. The synth
study compiles the repository's current `apps/groove/synth.c`; it does not alter the
application or maintain a second copy of the engine.

## Setup

Use Python 3.12 and a C99 compiler (`cc`/Clang). On macOS the compiler comes with
Xcode Command Line Tools. FFmpeg may come from Homebrew or the `imageio-ffmpeg`
Python package; the scripts support both.

From the repository root:

```sh
python3.12 -m venv tools/groove_audio/.venv
source tools/groove_audio/.venv/bin/activate
python -m pip install -r tools/groove_audio/requirements.txt
# Optional: English lyric alignment on Apple Silicon only.
python -m pip install mlx-whisper==0.4.3
```

`librosa==0.11.0` is intentional: the newer 1.0 API broke the separator used for
this study. The successful run used `audio-separator 0.47.0`, PyTorch 2.14.1,
Torchaudio 2.11.0, NumPy 2.5.3 and SciPy 1.18.1 on an 8 GB M1 Mac. The requirements
pin the separator, Librosa and Torchaudio, with ranges for general dependencies;
this is not a complete dependency lockfile.

Keep every generated file and downloaded model under the ignored `work/` folder:

```sh
work_dir="$PWD/tools/groove_audio/work/take-me-higher"
mkdir -p "$work_dir"
export HF_HOME="$work_dir/cache/huggingface"
export TORCH_HOME="$work_dir/cache/torch"
export NUMBA_CACHE_DIR="$work_dir/cache/numba"
```

Process models sequentially on an 8 GB machine. Concurrent separation and Whisper
runs caused severe memory pressure. Native MLX Demucs was tried but hit a GPU
command timeout; the instrument study below uses the successful CPU method.

## Separate and cut vocals

```sh
python tools/groove_audio/separate_vocals.py \
  "$HOME/Downloads/Take Me Higher.mp3" --work-dir "$work_dir"
python tools/groove_audio/analyze_track.py --work-dir "$work_dir"
# Optional; cuts also work without lyrics-alignment.json.
python tools/groove_audio/transcribe_vocals.py --work-dir "$work_dir"
python tools/groove_audio/cut_vocals.py --work-dir "$work_dir"
```

The source is decoded to stereo 44.1 kHz float WAV at `input.wav`. Mel-Band RoFormer
(`vocals_mel_band_roformer.ckpt`) writes full-length `stems/vocals_full.wav` and
`stems/instrumental.wav`. The successful M1 settings were native FP16, segment size
512, overlap 4 and the model segment-size override; these are the defaults. Use
`--full-precision` if the selected device does not support native FP16, or adjust
`--segment-size` to fit memory. The full song took about 12 minutes to separate.
`--model-dir` can reuse an existing model cache.

Whisper accepts a local model path via `--model` and short lyric hints via
`--prompt`. Singing transcription was unreliable, especially after the middle of
this song. Estimated words only guide some cuts and provide optional labels; they
are not authoritative lyrics.

The cutter uses vocal energy, word gaps when available, and quiet valleys to make
short cuts. It preserves pitch and timing, applies a gentle 55 Hz high-pass, 5 ms
boundary fades and up to +6 dB gain with a -1 dBFS peak ceiling. It exports PCM24
WAVs under `vocal-samples/`, named by source timestamps, plus `sample-index.csv`.
The source separation remains untouched. These are automatic chops, not guaranteed
complete lyrical phrases; sustained vowels can be split. Reverb or faint backing
may remain. For precise recuts, use the full vocal master. Rerunning the cutter
replaces WAVs in this dedicated output directory; do not store hand edits there.

## Separate a backing excerpt and render a synth study

```sh
python tools/groove_audio/separate_instruments.py \
  --work-dir "$work_dir" --start 32 --duration 16
python tools/groove_audio/build_renderer.py --work-dir "$work_dir"
python tools/groove_audio/reconstruct.py --work-dir "$work_dir"
python tools/groove_audio/package.py --work-dir "$work_dir"
```

Demucs (`htdemucs.yaml`) runs on CPU with four threads, one shift and 0.25 overlap.
It writes bass, drums, melodic backing and vocal leakage under `instrument-study/`.
`excerpt.json` records the original source offset; the reconstruction reads it.
This is a short excerpt study, not full-song four-stem separation. Adjust the start
and duration to examine another section; provide enough audio for eight bars plus
any offset to the first detected beat.

The renderer generates a standalone header from the current synth interface,
substituting only framework includes and constants, then compiles the actual
engine with `render_study.c`. Build files and raw float intermediates remain under
`build/` in the local workspace. `--cc` overrides the compiler.

The reconstruction fits one local tempo to detected beats and quantizes harmonic
pitch estimates to eighth notes. It currently assumes **A minor** and selects
chords from that key; adapting it to another key requires changing its chord table.
Polyphonic estimates can confuse octaves and chord tones with the melody. The 909
pattern is generic, not a transcription of the recording's drums.

| Part | Existing Groove voice / effect |
|---|---|
| Bass | `I_MOOG`, dry |
| Lead alternatives | `I_SAW`, `I_HPIANO`, `I_PLUCK`, `X_ECHO` |
| Pad | `I_PAD`, `X_ROOM` |
| Rhythm | `KIT_909`, four-on-the-floor kick, clap and hats |

Outputs in `synth-study/` include individual PCM24 WAVs, the combined sketch,
reference backing, and `reference-then-groove.wav` (reference, one second of silence,
then synth). CSV events use `start_seconds,duration_seconds,MIDI_note,gain`, without
a header. Packaging adds editable MIDI, Groove recipe notes and ZIP archives for
both output packs. MIDI GM programs are placeholders; only the rendered WAVs use
Groove's voices. Packaging validates levels, formats, sample ordering and ZIP
integrity, and does not apply further fades when repeated.

This demonstrates which sound families the existing mono synth can approximate.
It is not an exact melody or timbre recreation, and it does not implement vocal
sample playback in Groove. More accurate matching needs note corrections, patch
tuning and possibly stereo voices. See
[the engine guide](../../apps/groove/docs/sound-synthesis.md) for recipe syntax.

## First study observations

Analysis suggested A minor and tempo estimates around 140–146 BPM across sections.
The eight-bar study starts at approximately 32.380 s and uses 140.305 BPM. It yielded
57 vocal chops and a tentative `Am, Am, Am, Dm, G, C, F, Dm` chord sequence. These are
approximate observations from the automated analysis, not manually verified scores.
All audio and model assets remain local until explicitly approved for inclusion.
