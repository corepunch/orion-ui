# Planning and prompts

## Reference decisions

Read local catalogues, then representative clips. Compare musical role, syllable count, active phrase duration versus slot duration, hard/soft attacks, rhythmic density, legato, register, dryness, single/doubled voices and release. A filename saying “140” or an attractive waveform is not timing evidence.

Populate complementary originals: concise calls, ascending hooks, descending responses, longer two-part phrases, bouncy doo/ba scat, softer sha/ya scat, open oo/ah/ee vowels and humming transitions. Choose according to the references and current library gaps. Avoid filling a pack with near-duplicate lyrics or transpositions of one take.

## Batch plan

One row per asset, with equivalent fields:

```json
{
  "title": "Carry the Spark",
  "slug": "carry_the_spark",
  "role": "ascending melodic hook",
  "lyrics": "Carry the spark",
  "syllables": ["Car", "ry", "the", "spark"],
  "sound": "Four sung notes rising gently; sustain spark, then release cleanly.",
  "voice": "One adult female warm slightly husky alto, clear chest voice, open vowels, restrained vibrato, intimate dry microphone.",
  "bpm": 140,
  "meter": "4/4",
  "bars": 2,
  "attack_beats": [0, 1, 2, 4],
  "attack_seconds": [0, 0.428571429, 0.857142857, 1.714285714],
  "slot_seconds": 3.428571429,
  "release_before_seconds": 3.35,
  "full_prompt": "Expand the complete instruction below before submitting."
}
```

Keep written words in `lyrics` and rhythmic syllables separate: “Carry” is one word with two scheduled syllables. For wordless assets, use `lyrics: null`, literal `vocalization` and syllable sequence. Attack lists exclude the slot endpoint. The edit map created after source inspection includes the endpoint.

## Complete lyrical prompt

> DRY SOLO FEMALE A CAPPELLA SAMPLER PHRASE. One adult female singer: warm clear resonant alto/low mezzo, soulful chest voice, slight natural husky edge, bright open vowels, restrained vibrato, comfortable A3–C5. Intimate close microphone; one consistent singer, no harmony, doubling or choir. Exact 140 BPM, 4/4, straight timing. Sing only the original lyric “Carry the spark”. Four sung notes rising gently; sustain “spark”, then release cleanly. One complete two-bar take, 3.428571 seconds total. Syllable attacks from the start: “Car” at 0.000000 s; “ry” at 0.428571 s; “the” at 0.857143 s; “spark” at 1.714286 s. Release before 3.35 s. First phoneme starts immediately at time zero, no initial pause, breath, intro or count-in. One phrase only, no repeats or extra lyrics. Human singing voice only: no instruments, drums, bass, piano, synth, pads, chords, percussion, beatboxing, claps, clicks or audible metronome. No reverb, delay or room ambience. Tempo is a silent internal reference. This is an isolated short sample for a grid sampler.

## Complete wordless prompt

> DRY SOLO FEMALE WORDLESS SCAT SAMPLE. One adult female singer: warm clear resonant alto/low mezzo, soulful chest voice, slight natural husky edge, bright open vowels, restrained vibrato, comfortable A3–C5. Intimate dry close microphone, one consistent singer, no harmony, doubling or choir. Exact 140 BPM, 4/4, straight timing. Sing ONLY these nonlexical syllables: “doo ba dee dah”. No actual words, narration, speech or beatboxing. Four melodic attacks; rounded “doo”, light crisp “ba”, brighter “dee”, open sustained descending “dah”. Two bars, 3.428571 seconds total. Attacks: “doo” at 0.000000 s; “ba” at 0.428571 s; “dee” at 0.857143 s; “dah” at 1.714286 s. Clean release before 3.35 s. Start the first phoneme immediately, no lead-in, breath, count-in, intro, repeats or additional syllables. Isolated human singing only: no music, drums, instruments, bass, piano, synth, pads, percussion, claps, click or audible metronome. No reverb, echo or delay. Silent internal tempo reference. One short sampler phrase.

Adjust contour, rhythm, register and release per sample. Specify key only when useful; a prompt alone does not verify the resulting key. Do not impose this example voice on a user requesting another singer.

## Plugin handoff

Inspect the live schema. The 2026-10-09 pack used Runway `generate_music`, `model: "lyria-3-clip"`, full text in `promptText`. It returned MP3 with fixed output length and no singer-identity input. Recheck capabilities instead of freezing pricing, availability or parameters into future calls.

Save the plan before submission and task IDs as responses arrive. A generation caption is not a transcript or proof of prompt compliance. Preserve raw audio and hashes; inspect the actual phrase. On uncertain submission outcome, inspect task state before spending credits again. Do not generate additional audio just to test this skill.
