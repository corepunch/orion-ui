---
name: groove-vocal-samples
description: Generate original isolated singing and scat samples for Groove, using eJay/MTV role references, prepared lyrics and prompts, pitch-preserving 140 BPM alignment, and decoded-audio waveform verification. Use for new vocal packs or correcting existing vocal timing.
---

# Groove vocal samples

Produce original vocal assets that begin immediately, occupy exact musical slots, and place their main sung attacks on the intended grid. A BPM tag and an exact file duration do not establish internal timing.

Use the current checkout as the repository root. The original workspace is `/Users/igor/Developer/mapview/ui`. Respect its AGENTS.md and the user's destination, format, voice, count and rhythm. For direct Groove delivery, use `apps/groove/share/vocals/` and register the assets. For correction-only or skill/documentation requests, do not start new paid generations.

## Study references and voice

- Read `apps/groove/mtv-samples/index.tsv` and `apps/groove/ejay-samples/index.tsv`, then inspect representative clips. Commercial audio stays ignored and untracked. Use its musical roles, durations and articulation; deliver new titles, lyrics and recordings.
- MTV folders are instrument pools; genre is a catalogue column. eJay folders are musical families. Study rhythmic lyric hooks, longer melodic calls, scat, vowel phrases and humming. Scat is sung nonlexical syllables, not speech or beatboxing.
- Inspect approved originals in `apps/groove/share/vocals/` or the user's reference. The test2/2026-10-09 character was one adult female warm slightly husky alto/low mezzo, clear chest voice, open vowels, restrained vibrato, roughly A3–C5. Use that description when continuing this pack, not for every future singer.
- Distinguish voice character from singer identity. Runway's music tool used previously had no voice ID or audio-reference input. The user accepted character consistency for that batch. Check current capabilities before promising an exact match; clarify only if the difference remains material and unresolved.

## Prepare the complete batch first

Read [planning-and-prompts.md](references/planning-and-prompts.md) for plan fields and complete lyrical/scat prompts.

Save a JSON/CSV list of every intended sample before generation: original title/filename, role, exact lyrics or literal scat syllables, separate syllable breakdown, melodic contour/articulation, voice description, optional key, bar count, zero-based attack beats and seconds, and full per-sample prompt. Give the plugin complete instructions for each asset, not only a generic pack description.

Choose complementary roles; include several wordless samples when requested, without imposing a fixed ratio. Keep the same voice description throughout a continuing pack. Request solo dry singing, no backing, harmony, doubling, drums, audible click, lead-in, count-in or repeats. First phoneme starts at time zero; final note releases cleanly before the slot ends.

Default here: **140 BPM, 4/4, two or four bars**, unless the user specifies otherwise. At 44.1 kHz:

| Unit | Frames | Seconds |
|---|---:|---:|
| Beat | 18,900 | 0.428571429 |
| Bar | 75,600 | 1.714285714 |
| Two bars | 151,200 | 3.428571429 |
| Four bars | 302,400 | 6.857142857 |

Use `frame = round(beat * 18900)`. Beat zero is the start; endpoint `bars * 4` is not an attack. Intentional half-beats are valid. Do not invent triplets or swing to hide bad timing. Sustained vowels/melismas need not attack on every beat. The visual box is a musical slot: clean release and tail silence can occupy its remaining length correctly.

## Generate and isolate

Discover the connected singing tool and read its current schema. The previous successful route was Runway `generate_music`, model `lyria-3-clip`, full prompt in `promptText`; speech generation is not a substitute. Confirm authentication/availability. Do not invent duration or voice-reference parameters. Fixed-length output may contain lead-ins, backing and repetitions despite instructions.

Save task IDs and exact prompts immediately. Reuse pending tasks instead of submitting duplicates; follow the plugin's viewer/polling instructions. Bound transient retries, for example two; stop on account/policy/credit restrictions. Download returned audio for requested processing, preserving raw originals and hashes. Keep signed URLs and credentials out of durable manifests.

Use the environment described in `tools/groove_audio/README.md`. Keep raw takes, stems, models, masters and intermediates under ignored `tools/groove_audio/work/<batch>/`. If backing is present, use `tools/groove_audio/separate_vocals.py`; retain both stems. Read [alignment-and-qa.md](references/alignment-and-qa.md) for settings, annotation and helper usage.

Select one complete phrase; remove instrumental intros, repetitions and unwanted words. Retain quiet F/S/H consonants. Unprompted ASR helps flag lyric substitutions and suggest regions; its singing timestamps are unreliable. Prompted ASR can echo the supplied lyric. Inspect waveforms/spectrograms and audio when hearing is available. Regenerate an unsuitable phrase instead of forcing extreme stretching.

## Align and verify

1. Annotate independently observed source landmarks in seconds. Record initial phoneme, main word/note attacks, and final release endpoint. Preserve consonant-to-vowel relationships and legato.
2. Map source landmarks to planned beats. Keep requested and final schedules separately when a musical correction changes them. A two-to-four-bar change can preserve natural phrasing within a user's two-to-four-bar request; document it.
3. Stretch intervals while preserving pitch and attack transients, apply short joins/fades, and pad the tail to the exact slot. Whole-file trimming/resampling cannot fix internal rhythm. Bundled [align_vocals.py](scripts/align_vocals.py) implements the proven FFmpeg `atempo` workflow and verifies output through the current Groove decoder. Read its schema/defaults in [alignment-and-qa.md](references/alignment-and-qa.md).
4. Verify actual decoded delivery: mono 44.1 kHz, exact frames, finite/nonzero signal, no clipping, first signal within 5 ms above −55 dBFS. Container duration alone misses encoder padding. Use independently observed final attacks or preserved source-template matching; never supply planned timestamps as observed evidence.
5. Inspect **every** before/after and final waveform. NumPy/Pillow draws real PCM with shared time/amplitude axes and beat/bar lines; SciPy STFT can expose consonants/overlap. Do not use ImageGen or fabricate waveforms. Correlation proves placement of a supplied landmark, not annotation correctness: a prior scat passed correlation with a source anchor 68 ms late, caught by visual review.
6. Check early/late attacks, leading pauses, repeats, chopped syllables, abrupt tails and separation artifacts. Listen against a temporary click and loop in Groove if hearing is available; never export the click. State limits when only computational/visual inspection was possible; do not claim to have listened.

## Import and preserve evidence

For the existing app, use gapless 320 kbps MP3 with Xing information and `TBPM=140`; retain PCM24 WAV masters ignored. Honor explicit WAV delivery and check current app playback support before promising direct import.

Append matching `M("Original title", genres, bars, "filename.mp3")` rows at the **end** of `kDefs` in `apps/groove/library.c`. Preserve existing order/IDs; check current `GR_MAX_BLOCKS` capacity. Existing library lengths are one, two and four bars; this workflow normally uses two/four. Check unique filenames and consistency with assets/manifest.

From the repo root:

```sh
make build/bin/groove build/bin/test_groove_test
./build/bin/test_groove_test
```

Verify build resources updated. A running app may cache old audio; report relaunching is needed without closing an unsaved session automatically. Verification at 140 BPM does not establish pitch-preserving playback at other tempos; inspect the current engine before claiming it.

Save a durable batch manifest, verification JSON and waveform images under `apps/groove/docs/<batch>/` for an app pack. Include requested lyrics versus transcript, full prompts/task IDs, source/delivery hashes, requested/final rhythms, observed source anchors, bar lengths, processing settings and verification limits. Exclude commercial recordings, models, raw takes and signed URLs. Report count, location, timing results and remaining limitations accurately.

Historical evidence: `apps/groove/docs/vocals-2026-10-09/manifest.json`, `verification.json`, comparisons and contact sheets. Broader workflow: `tools/groove_audio/sample-production.md`. Avoid rerunning hardcoded scripts from ignored historical work folders.
