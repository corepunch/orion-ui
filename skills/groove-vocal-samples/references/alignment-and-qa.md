# Isolation, alignment and verification

## Environment and isolation

Use Python 3.12, a C99 compiler and FFmpeg with `atempo`/`libmp3lame`. The helper needs NumPy, SciPy, SoundFile, Pillow and imageio-ffmpeg. Reuse the repository environment or install `tools/groove_audio/requirements.txt` as documented in its README. Do not assume a previous `/tmp` environment exists. Separation models are additional downloads.

From the repo root, with `work_dir` set to an ignored batch directory:

```sh
python tools/groove_audio/separate_vocals.py "$work_dir/raw/take.mp3" \
  --work-dir "$work_dir/separation" --model-dir "$work_dir/models" \
  --segment-size 256 --overlap 2
```

Outputs: `stems/vocals_full.wav` and `stems/instrumental.wav`. Mel-Band RoFormer native FP16 on MPS with 256/2 worked for the large batch on an 8 GB M1. Default 512/4 may suit shorter sources but caused substantial swapping on that batch. Inspect device/memory logs; do not describe CPU execution as GPU acceleration. Run memory-heavy separation/transcription sequentially. If concatenating selected regions with 0.5-second gaps, record exact offsets for splitting.

Separation does not prove zero backing leakage. Inspect and, when possible, listen for backing and damaged consonants. Preserve quiet F/S/H starts. Select a complete phrase/release, removing repeats and instrumental intros. Unprompted ASR is advisory; singing word timestamps can be very wrong.

## Reviewed edit map

Create this JSON schema after observing source landmarks:

```json
{
  "samples": [{
    "name": "carry_the_spark_140bpm_2bars",
    "title": "Carry the Spark",
    "source": "isolated/carry.wav",
    "bars": 2,
    "source_seconds": [0.08, 0.54, 0.99, 1.86, 3.55],
    "target_beats": [0, 1, 2, 4, 8],
    "annotation": "Illustrative only. Replace with observed phoneme/note landmarks and final release endpoint."
  }]
}
```

Paths are relative to the plan or absolute. Times above illustrate the format, not reusable measurements. First anchor: first retained phoneme. Intermediate anchors: reviewed attacks. Last anchor: retained release endpoint. Equal-length lists must increase strictly; target starts at zero and ends at `bars * 4`. Intentional subdivisions are valid. Do not invent a rhythm to hide bad source annotation. Sources must already be isolated 44.1 kHz WAVs. Keep the generation plan separately.

Run the bundled helper:

```sh
skill_dir="${CODEX_HOME:-$HOME/.codex}/skills/groove-vocal-samples"
python "$skill_dir/scripts/align_vocals.py" \
  --repo "$PWD" --plan "$work_dir/edit-plan.json" \
  --out-dir "$work_dir/aligned"
```

It writes PCM24 masters, gapless MP3s, cropped before WAVs, actual-decoder comparison/final PNGs and `manifest.json`. Outputs are protected unless `--overwrite` is explicit; prefer a fresh directory for revisions. It does not generate, separate, listen to or import audio.

Processing defaults reproduce the accepted batch:

- Mono fold-down, second-order 55 Hz zero-phase high-pass.
- Preserve up to 35 ms of each attack, bounded by one-third of source/target interval. Stretch the rest with pitch-preserving `atempo`, not sample-rate resampling.
- Small interpolated join inside the vowel, 1.5 ms boundary ramps, final 20 ms fade, −2 dBFS peak normalization. Inspect audio before assuming these suit every source.
- If final release source/target ratio is below 0.70, stretch it at 0.80 and pad the remaining slot instead of unnaturally prolonging a short note.
- Rates outside 0.5–2 require review and `--allow-extreme-rate`. The override does not prove acceptable quality. Prefer a better map, a longer allowed slot or a new take.

## Numerical evidence

The helper compiles `tools/groove_audio/decode_sample.c` against the current repo and uses Groove minimp3_ex `MP3D_SEEK_TO_SAMPLE`. Check actual encoder-delay/padding removal: two bars must decode to 151,200 mono frames, four to 302,400. FFprobe duration alone is insufficient.

For sufficiently long preserved heads, match a 25 ms source template starting 4 ms after the landmark, searching both sides within ±60 ms. Pass: offset ≤2 ms and normalized correlation ≥0.75. Short heads are explicitly unverified. This proves placement of the supplied source landmark through encoding, not annotation correctness or perceived rhythmic feel. Never label an intended time as an observed one.

For separate manual final observations, the repository verifier supports:

```sh
python tools/groove_audio/verify_sample.py "$work_dir/aligned/example.mp3" \
  --before "$work_dir/aligned/example-before.wav" \
  --work-dir "$work_dir/manual-qa/example" --bars 2 --bpm 140 \
  --attacks 0,1,2,4 --observed-attacks 0.001,0.429,0.858,1.715 \
  --max-leading-ms 5
```

Replace names/times with real observations. These numbers demonstrate syntax only. Forward RMS-envelope rise is a diagnostic proxy; it misses early attacks and can confuse a preceding sustained vowel. It does not independently prove timing.

## Image review

NumPy/Pillow draws images from PCM; no generated artwork. The repository renderer bins peak magnitudes per pixel on shared time/amplitude axes with beat/bar lines and intended/observed markers. Existing picture beat labels are one-based; data are zero-based. Template markers show measured placement of annotated templates.

Open **every** comparison and final PNG. Inspect the waveform around both sides of each gridline, not only the numerical offset. An attack can be early, late, missing or annotated inside a previous vowel. Check first consonant, pauses/repeats, sustained-note transitions and release. Use zoomed waveforms and SciPy STFT spectrograms for soft consonants/rapid scat. A prior scat's 68 ms incorrect source anchor passed correlation until visual review corrected it.

For contact sheets use final-only rows with labeled full slots. Do not stretch images to look aligned or normalize before/after independently. A long before phrase can extend a comparison's shared axis; it must not distort a final-only sheet. Waveforms cannot prove identity, key, exact lyrics or absence of quiet backing. Listen when hearing is available; otherwise report that listening-based checks were not performed.

## Integration and evidence

Resolve numerical failures and complete image review before import. Keep WAV/raw/stems/models ignored. For direct app delivery, copy MP3s into `apps/groove/share/vocals/`, append `M(...)` rows at the end of `kDefs`, check current capacity/unique names, and match manifest bars. Existing order affects IDs and potentially synth noise seeds. Build/test using SKILL.md commands and verify resource synchronization.

Combine the helper manifest with the generation plan, task IDs, requested/final rhythm and transcript; save durable verification and plots in a batch docs directory without temporary URLs. Report limits. Template correlation alone cannot establish exact perceived timing, and every phoneme need not land on a beat.
