# Groove EuroDance demos

Choose a song from **File → Demo**, then press **Play**. Each is a normal,
editable `.groove` project. **Save Song** asks for a name and keeps the copy in
Groove's own song library, so the bundled project is preserved. **File → My Songs**
shows every saved song with a preview drawn from the file. The songs play once by
default; enable **Loop** to repeat.

| Song | Arrangement inspiration | Tempo | Length | Project |
|---|---|---:|---:|---|
| Neon Skyline | Jazz1's buoyant question-and-answer interplay | 140 BPM | 88 bars, 2:31 | [neon-skyline.groove](neon-skyline.groove) |
| Afterglow Avenue | Jazz2's syncopated instrumental pocket and contrasting textures | 140 BPM | 88 bars, 2:31 | [afterglow-avenue.groove](afterglow-avenue.groove) |
| Victory Lights | The heroic low movement, rising arrivals and spacious responses of RAPTOR's seventh mission | 140 BPM | 96 bars, 2:45 | [victory-lights.groove](victory-lights.groove) |

These are new EuroDance arrangements, built around a continuous 909-style floor,
offbeat sampled bass guitar, grand-piano hooks, sampled guitars and sustained
synth harmony. The supplied games inform section development and instrumental
handoffs. No game melody, tracker instrument, game recording or commercial eJay/
MTV audio is used. The rejected game-themed scores and demos remain removed.

The sampled instrument bank, synthesizer and drum sound generator are unchanged.
All 500 existing recipes retain their notes and rhythms. The new demo material is
appended as 83 recipes in `demo_phrases.h`; old source IDs remain stable.
The complete projects fit in the editor's 128-bar timeline.

## Mix balance

Each placement stores its own gain, applied after library source levelling.
The library now has role-based default levels too; the demo placements compensate
for those source trims at 140 BPM to retain the approved mix. The total
reductions below are relative to the earlier, uniformly levelled sources.
Piano accompaniment is reduced by 6–7 dB, chorus hooks by 5 dB, octave hooks by
6 dB, and exposed breakdown piano by 3 dB. Intro and closing piano use a 5 dB
reduction. Guitar backing sits 2 dB lower (answers 2–3 dB), pads 4 dB lower,
and broad synth chords 5 dB lower. Risers/crashes are down 3 dB and short calls
2 dB. Drum loops and bass retain their previous levels, including the soft hats.

Instrument synthesis, sample files, effects and note patterns stay unchanged.
Library audition now uses the balanced default levels. Version 4 project files
save the additional clip gain in dB after its length; version 2/3 projects use
unity clip gain over the current library levels. Save/Open preserves every
placement's mix level. The rebuild script records both total `mix_gain_db` and
saved `gain_db`, and accounts for the measured source outliers used by the demos.

## Musical development

Each song has its own original eight-bar piano hook in two four-bar halves. The
first chorus establishes the hook, the breakdown changes the register and hands
its quieter idea to guitar, and the final chorus introduces octave voicings and
a different combination of guitars, pad and pumped synth chords. The eight-bar
melodic/harmonic arrangements are different within each song, while recognizable
motifs return as they should in a dance track.

At 140 BPM the existing short male calls retain their native pitch and timing.
They act as pickup accents around builds; the instrumental hook stays foreground.
No new vocals were generated. Hats use soft single hits, without simultaneous
open/closed strikes or an additional hat-only layer.

The four-bar drum turnarounds include their own final-bar fill, rather than
cutting a long loop to stack a fill over it. The eight-bar builds move from the
full floor to increasing snare density and a two-bar riser; the final beat of the
snare build is left open before the chorus. Bass and piano also leave space at
the end of that build. The final cadence gives piano and guitar time to release.

### Neon Skyline and Afterglow Avenue

Both use this radio-length form, with different melodies, chord paths and guitar
articulation. Skyline uses clean guitar and an A-minor/F/C/G chorus; Afterglow
uses a wah pocket and a C/G/A-minor/F chorus.

| Bars | Development |
|---|---|
| 1–8 | Filtered piano signal over a sparse floor and pad; sampled bass and guitar enter, then a vocal pickup. |
| 9–16 | Rhythmic piano and bass establish the verse; clean guitar answers in the second four bars. |
| 17–24 | The backing changes from pad-supported piano to guitar-supported bass, ending in a drum turnaround. |
| 25–32 | Pre-chorus harmony rises; snare density increases, a riser leads to a one-beat drum breath, and a short call announces the drop. |
| 33–40 | The two-part piano hook enters over a full four-on-the-floor groove and offbeat bass; guitar hands over to pad. |
| 41–48 | The hook returns with both guitar and pad, then a more open answering half and a final-bar fill. |
| 49–56 | Beat and bass drop out. Normal grand piano introduces the quiet theme; sampled clean guitar answers it in a lower register. |
| 57–64 | The kick, rising chords and a new guitar layer rebuild the track; snare lift, riser and call lead to the return. |
| 65–72 | The hook returns in octave piano voicings, with fuller guitar/pad backing and pumped synth harmony in its answer. |
| 73–80 | The original hook contour contrasts with the wide final answer; changing harmony layers and a drum turnaround prepare the outro. |
| 81–88 | Guitar, bass and the floor remain for a short DJ outro. A song-specific piano cadence closes as the drums withdraw and notes release. |

### Victory Lights

Victory uses broader held piano gestures and a persistent low foundation while
keeping the same club pulse. Its pre-chorus moves through D minor/F/E minor/E
major before the A-minor arrival. An extra middle passage makes room for the
heroic idea to grow without turning the track into a game-soundtrack imitation.

| Bars | Development |
|---|---|
| 1–8 | A broad piano signal and sparse floor establish the horizon; bass and guitar enter. |
| 9–24 | Offbeat bass and rhythmic piano support a clean-guitar answer, then changing pad/guitar backing leads into the lift. |
| 25–32 | Rising harmony, a snare build, riser and call prepare the first chorus. |
| 33–48 | The heroic two-part piano hook appears, then gains guitar and pad before a turnaround into the breakdown. |
| 49–56 | Beat and bass disappear; the quiet piano phrase is answered by low clean guitar. |
| 57–64 | Broad synth chords support the quiet theme, then the low pulse gathers momentum beneath guitar. |
| 65–72 | Kick, rising piano/guitar harmony and pad build toward the second snare lift and drop. |
| 73–88 | Octave piano, changing guitar/pad backing and pumped chords give the final chorus greater weight; a different answering texture leads out. |
| 89–96 | The floor and guitar provide a DJ outro, followed by a distinct piano cadence and a final held fifth over A minor. |

## Validation and source

- The three native projects load, play to their complete endings, stop, and
  round-trip through Save/Open with every clip and its mix level still editable.
- Timeline regression checks select the final hook after scrolling beyond the
  old 32-bar limit. Loading another demo resets the viewport and playhead.
- There are no hidden overlapping clips or duplicated eight-bar tonal layouts.
- Every block renders finite, non-silent audio at 70, 140 and 180 BPM in the
  standard library suite. The three complete mix renders have no hard-clipped
  PCM samples and end at zero.
- The original melodic scores have no matching eight-attack interval/rhythm
  sequence against the stored Jazz1/Jazz2/RAPTOR study streams. That comparison
  permits transposition and tempo scaling; it does not reconstruct tracker
  effects or establish perceptual originality.
- Listening quality remains unverified by ear in this session. The full mix
  renders in `build/groove-eurodance/` are provided for listening review.

`tools/groove_audio/compose_eurodance_demos.py --check` checks authored recipes
and the distributed projects without replacing them. `--write` deliberately
regenerates those files; do not use it on hand-edited bundled projects without
preserving the edits. `build/groove-eurodance/scores.json` records bar patterns,
section plans and every placement.

Launch from the build's `bin` directory with `groove --demo` (Neon Skyline),
`groove --demo=skyline`, `groove --demo=afterglow`, or `groove --demo=victory`.
The normal share/install process packages all three projects and the existing
instrument bank. Preview WAV files are build artifacts and are not required for
playback in Groove.
