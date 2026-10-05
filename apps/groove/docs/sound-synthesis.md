# How Groove makes its sounds

Groove ships no audio files. Every block in the library is computed by a small synthesizer written in C, from a one-line recipe. This document explains how a recipe becomes sound, how blocks are tagged, and what the approach cannot do.

The code is in three files:

| file | what it holds |
|---|---|
| `synth.c` | the engine: drum kits, pitched voices, effects |
| `synth.h` | the engine's interface and the names used in recipes |
| `library.c` | the block table (one row per block) and the block cache |

## From a table row to a waveform

A block is one row in `kDefs` in `library.c`:

```c
D("Euro Floor",   CAT_DRUMS, DAN, 1, KIT_909,  0, "k=x...x...x...x... h=oo.ooo.ooo.ooo.o o=..x...x...x...x. c=....x.......x..."),
N("Euro Offbeat", CAT_BASS,  DAN, 1, I_DONK,   0, 2, 0.8f, "- A1 - A1 - A1 - A1"),
```

The row names the block, its family tab, its genre tags, its length in bars, a kit or instrument, effect flags, and a pattern. `block_render()` turns that into audio in six steps:

1. Allocate a silent mono buffer: the block's bars at the project tempo, plus two seconds of tail.
2. Run the recipe. Each hit or note is a **voice**: a short loop that computes samples and adds them into the buffer at the right position.
3. Run the row's effects over the whole buffer.
4. If the row asks for it, fold the tail back onto the start, so an echo or reverb that rings past the last bar continues at the first.
5. Level the block (see [Levelling](#levelling)) and fade the last 128 samples so a cut tail does not click.
6. Reduce the result to 128 peak values per bar. The cards draw their waveform from those.

The output is 44.1 kHz mono `float`, exactly `bars * bar_frames` long, so blocks sit on the grid without stretching. The mixer in `mixer.c` only copies and adds these buffers.

Rendering is deterministic. The noise generator is seeded from the block's index, so a block sounds the same on every run.

## The building blocks

There are six ways the engine produces a signal. Every voice combines a few of them.

| technique | what it is | used for |
|---|---|---|
| Sine with a moving pitch | `sin` of a phase that advances faster or slower over time | kicks, toms, sub bass, zaps, sirens |
| Saw and pulse waves | bright waves with many harmonics; edges are rounded (PolyBLEP) so high notes do not alias | basses, leads, stabs, strings, hoover |
| Filtered noise | random samples through a low-, band- or high-pass filter | snares, claps, shakers, risers, breath |
| Additive | a handful of sine partials, each with its own decay | piano, organ, vibes |
| FM | one sine bends the phase of another; a falling amount gives a bright attack and a dull tail | electric piano, bells, FM bass |
| Plucked string | a short burst of noise circulating in a delay line that loses a little each pass (Karplus-Strong) | guitars, pizzicato |

Two more pieces shape those signals:

- An **envelope** is a gain curve over the life of a note: how fast it starts, whether it decays while held, how fast it dies after the gate ends.
- A **filter** removes part of the spectrum. Moving its cutoff during a note is what makes an acid line squelch and a stab bite. The engine uses one state-variable filter that stays stable at any cutoff.

### What a drum hit is made of

| hit | recipe |
|---|---|
| kick | a sine that drops from a few hundred Hz to about 50 Hz in 30 ms, an exponential decay, a noise click at the start, optional soft clipping |
| snare | two short sine tones plus high-passed noise with its own decay |
| hat, ride, crash | six square waves at unrelated pitches (the classic drum-machine cymbal source), mixed with noise, high-passed; the decay time separates closed, open and crash |
| clap | band-passed noise in three quick bursts, then a tail |
| rim, clave, conga | two decaying sines and a noise snap |
| cowbell | two pulse waves through a band-pass |

A kit is a table of numbers for these voices (`kKits` in `synth.c`). The same pattern played through another kit is a different drum sound.

| kit | character |
|---|---|
| `KIT_CLASSIC` | the original starter voices |
| `KIT_909` | punchy kick, snappy snare, bright hats |
| `KIT_808` | long sub kick, tight snare |
| `KIT_HARD` | overdriven kick, loud noisy snare |
| `KIT_TECHNO` | deep driven kick, tin snare, dry hats |
| `KIT_BREAK` | short clicky kick, full snare, noisier hats |
| `KIT_LOFI` | rounded kick, dull hats |

### Vocals

There is no recorded voice. `I_VOX` is a saw wave (standing in for the vocal cords) through three band-pass filters placed at the formants of a vowel. Gliding the three filters from one vowel to the next produces a word-like sound: `"iea"` reads as "yeah", `"hei"` adds a breath and reads as "hey", `"ou"` as "oh". `I_ROBOT` uses a pulse wave at a fixed pitch and jumps between vowels. `I_CHOIR` is three detuned saws through one fixed vowel.

### Scratches

`sy_scratch()` first renders half a second of a held vowel, then plays that buffer back at a varying position, forwards and backwards, the way a hand moves a record. Output level follows the speed, so a still record is silent.

## Recipe notation

### Drum rows: `D(name, family, genres, bars, kit, fx, lanes)`

Lanes are separated by spaces. Each is `key=pattern`, one character per sixteenth note. A lane shorter than the block repeats, so `k=x...` is four-on-the-floor for any length.

| step | meaning |
|---|---|
| `x` | hit |
| `o` | soft hit |
| `X` | accent |
| `r` | two-stroke roll inside the step |
| `R` | three-stroke roll inside the step |
| `.` | rest |

| lane | sound | lane | sound |
|---|---|---|---|
| `k` | kick | `T` `t` `l` | high, mid, low tom |
| `s` | snare | `g` `G` | high, low conga |
| `h` | closed hat | `b` | cowbell |
| `o` | open hat | `m` | shaker |
| `c` | clap | `a` | tambourine |
| `r` | rim | `w` | clave / wood |
| `y` | crash | `z` | zap |
| `d` | ride | | |

### Note rows: `N(name, family, genres, bars, instrument, fx, steps_per_beat, hold, steps)`

Steps are separated by spaces. `hold` is the gate length in steps.

| step | meaning |
|---|---|
| `A1`, `C#4`, `Bb2` | a note |
| `A3+C4+E4` | a chord |
| `-` | rest |
| `_` | the previous note lasts one more step |
| `!` after a note | accent: louder, and brighter on filtered voices |
| `~` after a note | glide from the previous note (mono voices) |

`V(...)` is a note row with one more argument: the vowel words for the formant voices, cycled note by note.

### Hand-written rows: `F(name, family, genres, bars, fx, function)`

Sweeps, drops, sirens, scratches and the rumble kick are short C functions (`GEN(...)` in `library.c`) built from engine primitives such as `sy_noise_sweep()`, `sy_sweep_tone()` and `sy_scratch()`.

A pattern must fill its block or divide it evenly. `block_render()` rejects a row that does not and logs which one.

### Instruments

| group | names |
|---|---|
| starter voices | `I_PIANO` `I_BASS` `I_SUB` `I_PLUCK` `I_MUTE` `I_ACID` `I_PAD` `I_ARP` |
| keys | `I_HPIANO` (bright house piano) `I_EPIANO` `I_ORGAN` `I_VIBES` `I_BELL` |
| basses | `I_FMBASS` `I_DONK` `I_REESE` `I_MOOG` `I_SQUARE` `I_UPRIGHT` |
| leads, stabs, layers | `I_SAW` `I_SUPERSAW` `I_HOOVER` `I_STAB` `I_BRASS` `I_STRINGS` `I_CHOIR` `I_WHISTLE` `I_PIZZ` `I_BLEEP` `I_FMSEQ` |
| guitars | `I_WAH` `I_DIST` `I_CLEAN` |
| formant voices | `I_VOX` `I_ROBOT` |

### Effects

Effects run over the finished block, in this order.

| flag | effect |
|---|---|
| `X_DRIVE` | soft clipping |
| `X_CRUSH` | half sample rate and about six bits, then a dull low-pass |
| `X_GATE` | sixteenth-note trance gate |
| `X_CHORUS` | two modulated delay taps |
| `X_ECHO` | dotted-eighth feedback delay; each repeat is duller |
| `X_ROOM`, `X_HALL` | short and long reverb (four comb filters into two all-pass filters) |
| `X_UP`, `X_DOWN` | a low-pass that opens or closes across the block |
| `X_PUMP` | ducks every quarter note, as if side-chained to a kick |
| `X_FOLD` | wrap the tail onto the start (`LOOP` = echo + fold, `WIDE` = hall + fold) |
| `X_SWING` | every second sixteenth is 28 % of a step late; applied while the recipe runs |
| `X_REVERSE` | play the finished block backwards |

## Genres are tags

The reference disc is Dance eJay with demos of HipHop, Rave and Techno eJay, so the library covers those four genres (see [dance-ejay-pxd.md](dance-ejay-pxd.md)).

A block has one **family** and any number of **genres**:

- The family (`category_t`: Drums, Kicks, ... FX) is exclusive. It picks the sidebar tab and the card colour.
- The genres are bit flags in `block_t.genres`, because many sounds belong to several styles. A dry four-on-the-floor kick is tagged `GENRE_DANCE | GENRE_RAVE | GENRE_TECHNO`. Risers and a reverse crash are tagged `GENRE_ANY`.

```c
enum { GENRE_DANCE = 1 << 0, GENRE_HIPHOP = 1 << 1, GENRE_RAVE = 1 << 2, GENRE_TECHNO = 1 << 3 };
```

Splitting the library into one folder per genre would have meant either copying shared blocks or hiding them from three of the four genres.

The genre control in the library toolbar holds one genre or All: `visible = (filter == 0 || (block.genres & filter)) && matches(search)`. The search field also matches genre names.

Of the 451 blocks, 211 carry more than one tag and 46 carry all four.

| | Dance | Hip Hop | Rave | Techno |
|---|---:|---:|---:|---:|
| blocks tagged | 204 | 181 | 186 | 190 |

Every tonal block is written in A minor (chords Am, F, C, G, Dm, Em), so any two blocks can be stacked without clashing.

Blocks are rendered at the project tempo, whatever their genre. The bar lengths stored on the disc give the genres' home tempos: 140 BPM (Dance, Techno), 96 BPM (HipHop) and 180 BPM (Rave). The tempo range was raised to 180 to reach the last one.

## Levelling

Each block is scaled so its peak is 0.85. A sustained sound at that peak is far louder than a drum loop at the same peak, so if the scaled block's RMS would exceed 0.2, the RMS is brought to 0.2 instead, but never so far that the peak falls under 0.4. A 15 Hz high-pass first removes any offset left by pulse-width modulation or clipping.

## When blocks are rendered

Rendering all 451 blocks takes about 5 seconds in the default unoptimized build (1.5 seconds with `OPT=-O2`) and would hold 200 MB at 120 BPM, 344 MB at 70 BPM. So blocks are rendered on demand, on the main thread, and only what is in use stays loaded:

| need | what happens | what is kept |
|---|---|---|
| a card is painted | the block is rendered, at most 4 per 33 ms tick | the 128-per-bar peaks only |
| a card is clicked | the block is rendered and auditioned | PCM, until the next audition |
| a block is dropped on the sheet | the block is rendered before the audio lock is taken | PCM, while the song lasts |
| the tempo changes | blocks in the song are re-rendered and swapped in under the lock; all others are dropped | as above |

A typical block renders in about 10 ms unoptimized. The slowest, a four-bar piano progression, takes about 150 ms. The audio callback never renders; it reads buffers that the main thread installed under the audio lock.

## Limitations

- **No recordings.** There is no sung or spoken phrase, no rap, and no real instrument. The vocal blocks are vowels and vowel glides. More than half of the reference library is vocal hooks and atmospheres, and HipHop eJay adds a rap group; synthesis does not replace that material.
- **No consonants.** The formant voice can say "yeah", "hey", "oh" and "wow" after a fashion. It cannot say a word with s, t, k or m in it.
- **Electronic sounds are close; acoustic ones are sketches.** Drum machines, acid, FM bass, saw stacks and noise sweeps are what the original hardware did, so they hold up. Piano, guitar, strings, brass and choir are simple models and sound like it.
- **Mono.** A block is one channel. The reference also stored mono clips and split wide sounds into L and R halves; Groove has no stereo pairs, and chorus here only thickens the sound.
- **One fixed sound per block.** There is no per-clip pitch, filter or volume. A block in another key needs another row.
- **One key.** Everything tonal is in A minor. That keeps blocks compatible and limits harmony to the chords of that key.
- **Fixed effect settings.** Reverb, echo and drive have one setting each; a row can only switch them on.
- **Effects are per block, not per voice.** A drum row with `X_ROOM` puts the kick in the room too.
- **Open hats do not choke.** A closed hat does not cut a ringing open hat, as it would on a drum machine.
- **Decay times do not follow tempo.** Envelopes are in seconds, so at 180 BPM long kicks overlap and at 70 BPM short sounds leave gaps.
- **Folded tails.** A `LOOP` or `WIDE` block starts with the tail of its own ending, which is right when it repeats and slightly wrong the first time it plays.
- **A small pause on first use.** A block that was never rendered is rendered on click or drop. On a slow machine a long pad can take a noticeable moment.
- **Fewer blocks than the reference.** Dance eJay has 1352 clip files under 484 titles; many are L/R halves and numbered variations of one idea. Groove has 451 blocks. Drums, the seven drum-part families, bass, keys, synth, pads, stabs, vocals and FX have at least three blocks in every genre (usually many more). Guitar, organ and scratch are tagged only for the genres whose reference product has them. Numbered variations of one idea are not reproduced.
- **Not checked by ear in CI.** The tests verify that every block renders, is finite, levelled and the right length. They cannot tell whether it sounds good.

## Adding a block

1. Add a row to `kDefs` in `library.c`, under the genre it mainly belongs to. Pick the family, the genre tags, the bars, a kit or instrument, effect flags and the pattern.
2. If no existing voice fits, add one to `synth.c`, give it a name in `sy_inst_t` and a case in `sy_voice()`.
3. Run the Groove tests. `test_blocks` renders every row and fails on a pattern that does not fit, a duplicate name, silence or a bad level.
4. Listen to it in the app next to a kick and a bass from the same genre.
