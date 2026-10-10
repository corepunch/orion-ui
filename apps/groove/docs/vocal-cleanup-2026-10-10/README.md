# Vocal cleanup — 10 October 2026

Replaced all 55 delivered vocal MP3s with cleaned exports from the existing
lossless masters. The original MP3s and all intermediate audio are preserved in
the ignored `tools/groove_audio/work/vocal-cleanup-2026-10-10/` directory.

The earlier alignment edits faded individual intervals to zero and replaced
their internal attack-to-stretch joins with straight-line interpolation. These
created short notches and unnatural waveform gaps inside otherwise continuous
vocals. The cleanup restores authentic isolated-source waveform handles around
356 documented joins in 50 samples, with smooth 8 ms crossfades. The five
earlier samples receive de-clicking and release smoothing only.

FFmpeg's autoregressive de-click filter runs with threshold 4, order 4%, and
burst fusion 1.5%, with 100 ms of temporary padding on each side to avoid filter
startup damage. Padding is removed sample-exactly. The first 30 ms of each
documented phoneme remains protected, with a 4 ms transition into processing.
Releases fade over 25 ms at the actual sound endpoint, including releases before
trailing slot silence. Existing quiet attacks retain their original fades.
The -1 dBFS ceiling required no gain reduction for this batch.

No resampling, pitch shifting, time stretching, regeneration, or new backing
was used. Each MP3 is mono 44.1 kHz, 320 kbps, with Xing gapless information and
its original metadata, including `TBPM=140`. Library names and IDs are unchanged.

## Verification

[verification.json](verification.json) records the original and cleaned hashes,
source hashes, every repaired join, processing settings, and per-file checks.
All 55 exports pass Groove's actual minimp3 decoder: exact original bar frames,
finite audible signal, no clipping, signal within 5 ms at -55 dBFS, and quiet
endpoints. All 216 checked attack templates retain zero-frame displacement;
minimum correlation is 0.99989. This establishes preservation of the supplied
attack placement, not independent correctness of the earlier musical annotations.

All ten before/after contact sheets and the [seam detail plot](seam-details.png)
were visually reviewed. These are plots of real PCM with shared axes. Listening
was unavailable; remaining generation or separation defects are not ruled out.
Groove builds successfully, all 20 Groove tests pass, and the 55 installed build
resource hashes match the verified exports.

## Reproduction

The [cleanup plan](cleanup-plan.json) identifies the ignored lossless masters,
isolated sources, hashes, gains, and previously reviewed landmark maps. The five
early masters were recovered from the matching `/private/tmp/groove-grid-fix/exports/`
exports and archived with the other working PCM. Source/head correlations were
checked before creating the plan; the 50 source-backed masters match their
original isolated attack heads to correlation greater than 0.999.

With the original app assets restored and the local ignored sources available,
run from the repository root using the audio environment described in
`tools/groove_audio/README.md`:

```sh
python tools/groove_audio/clean_vocals.py \
  --plan apps/groove/docs/vocal-cleanup-2026-10-10/cleanup-plan.json \
  --out-dir tools/groove_audio/work/vocal-cleanup-reproduction
```

The command requires a new output directory, checks input hashes, stages its
outputs, and never overwrites app assets. The historical generation manifests
describe the earlier exports; this cleanup's verification supersedes their
delivery hashes. A running Groove session caches sample PCM and needs relaunching
to load the updated resources.
