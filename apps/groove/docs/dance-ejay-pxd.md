# Dance eJay sample library

Reference for the Groove block bin. The disc is a late-1990s Dance eJay CD at `/Users/igor/Downloads/Dance eJay` (about 4456 files). It holds the full Dance eJay library and demos of Dance eJay 2, HipHop eJay, Rave eJay and Techno eJay. Groove synthesizes its own loops in the same roles. It does not import the original audio.

The player is the 16-bit `DANCE/DMACHINE/DANCE.EXE` plus `DANCE02.DLL` ("DanceMachine Audio-DLL", Bernhard Throll / THROLL GmbH, 1997). Both are NE binaries, not PE.

## Where the clips live

The musical library is `DANCE/{AA..AL,BA..BW}/*.PXD`: 1352 files, 484 distinct button titles. Every bank holds 39 files except `AL`, which holds 26. `DANCE/DMACHINE/` is the application, not a sample bank.

Bank letters are storage buckets. The musical family is the button title inside the file, not the directory.

`MIN.TXT` and `MAX.TXT` are install lists (`ba\aaaf.pxd`). `PXD.TXT` is a string dump of button labels (`Grp. 1` / `Vers1`), not a category index. `DM.TXT` is dialog text.

## File names and button labels

The filename is a 4-letter index, not the title. The first letter is `A` or `B`, the last letter is one of `F I O P Q R S U Y`, and the middle two are `A`–`Z`. Banks `AA`–`AL` use names starting with `B`; banks `BA`–`BW` use names starting with `A`.

The button label is Latin-1. A CR/LF inside it is the second line (the variation: `Fig`, `Line`, `Vers`, `Cut`, `Version`). Trailing NULs are padding inside the length byte and are not part of the title.

Stars on a title (`*`, `**`, `***`, `****`) are thicker or wetter versions of the same clip. `L` and `R` are the two mono halves of one stereo loop, because a slot in the player is mono. `Center` / `Start` / `Stop` are the in, body, and out of one atmosphere. `Grp.1`–`Grp.7` are unnamed loop groups.

## Container

```
offset  size  field
0       4     magic "tPxD" (74 50 78 44)
4       1     name length N
5       N     button label, Latin-1, may contain CR LF and trailing NULs
5+N     …     audio payload
```

The payload is not WAV and not raw PCM. Every payload starts with `0x54` (`T`).

## Audio payload

```
'T'                         one byte
uint32 little-endian        output capacity, in samples
command stream              the rest of the file
```

Capacity is a sample count at 44100 Hz on a 140 BPM grid. One beat is 18900 samples, one bar is 75600.

| capacity | bars at 140 BPM | files |
|---:|---:|---:|
| 37800 | 0.5 | 4 |
| 75600 | 1 | 274 |
| 151200 | 2 | 581 |
| 226800 | 3 | 11 |
| 302400 | 4 | 308 |
| 378000 | 5 | 41 |
| 604800 | 8 | 73 |
| 1209600 | 16 | 2 |

A few files sit a handful of samples off those sizes (75630, 75649, …). The player checks the capacity when a slot ends, so one slot can write a sample past the dword. The export clamps to the capacity. 671 of 1352 files fill the slot within 2 percent. The rest are shorter than the slot they were allocated.

The product's mix export is 44.1 kHz 16-bit stereo. A library clip is mono.

## Command stream

Decoded from the inner loop in `DANCE02.DLL` code segment, file offset `0x350`, loop at segment offset `0x3ED5`. Registers on entry: `fs:edi` compressed input, `ds:esi` output, `dx` running int16 predictor, `es` dictionary, `gs` delta table.

The dictionary has 256 slots. Slot `i` is 16 bytes at `es:[i * 16]`, a list of int16 words, ended by a 0 word. Each stored word is a delta-table index shifted left by 1 (`index * 2`). Playback walks the slot two words at a time. A 0 low word or a 0 high word ends the slot. Each non-zero word emits one sample.

Output is 16-bit. Each sample does `predictor += delta`, then `sample = saturate(predictor + buffer[esi])`, then `esi += 4`. The stride of 4 is the player's mix buffer (one int16 written, two bytes left alone). A zeroed buffer stores the predictor. The add into the buffer saturates to int16 (`0x7FFF` / `0x8000`). The predictor add itself wraps. For a solo decode, start the predictor at 0 and the buffer at 0.

Commands:

| bytes | meaning |
|---|---|
| `00`–`F3` | Replay dictionary slot `byte`. An empty slot emits nothing. |
| `FF ii` | One sample. Delta index is `ii`. |
| `F4`–`FE` | Define a slot, then replay it. `count = opcode - 0xF3` (1..11). Layout is `opcode`, `index`, then `count` raw index bytes. Those index bytes may be `>= 0xF0` and are not commands. The slot is `count` words `(byte << 1)` and a terminating 0. `FE` (11 words plus the terminator) spills into the next slot. |

Bytes before the first `F4`–`FF` are replays. On an empty dictionary they emit nothing, so the `T` and the capacity dword are harmless if the decoder starts at the payload. All 1352 files parse to the end under that rule.

## Delta table

`gs` is a 256-entry int16 table built at runtime. `DANCE02.DLL` `GlobalAlloc`s `0x7D0` bytes, `GlobalLock`s it, and stores the selector at data offset `0x3166`. The builder is the function at segment offset `0x3346`. The same curve is in the 32-bit codecs `PXD32D4.DLL`, `PXD32HA.DLL`, `PXD32RA.DLL` and `PXD32R4.DLL`. No codebook is stored on the disc.

`table[128] = 0`. The doubles are 2.2, 1.0565, 0.52 and 0.00022. For `i` from 1 to 127 the step is 1 when `i < 6`, otherwise `trunc(a - 0.52)` toward zero, and `table[128 + i]` is the running sum. After every step, including the first five, `a = a * b` and `b = b - 0.00022`, starting from `a = 2.2` and `b = 1.0565`. Entries 127 down to 0 are the negations of entries 129 through 256. Entry 256 is never written, so `table[0] = 0`. Each entry is then shifted left by 1.

Around silence the shifted steps are 0, ±2, ±4, ±6, ±8, ±10, and then the curve steepens. `table[255] = 25266` and `table[1] = -25266`.

Playback does `predictor += table[index]` with a wrapping int16 (`add dx, gs:[bx]`, `bx = index << 1`). A solo clip starts at 0 and stores that predictor. The mix-buffer add saturates; with a zeroed buffer it never fires.

`delta = (int16)(index - 128)` is not this table. On these files that linear map averages about `+0.6` per sample, so the sum climbs, wraps, and the tail sits on a flat DC. That is the silent-with-clicks export. The library WAVs use the player curve.

Index 255 is a real step of +25266, not a sentinel. A clip that emits it (claps, and loops that begin with the single-sample command `FF FF`) keeps that offset for the rest of the slot. The sound is the variation around the offset. The export does not high-pass it.

## Families

The button title's first line is the family. A keyword pass over all 1352 files, first match wins:

| family | files | how the title matched |
|---|---:|---|
| kicks | 125 | "kick" |
| groups | 88 | "grp" |
| snares | 55 | "snare" |
| hats | 52 | "hat" |
| organs | 46 | "organ" |
| lines | 30 | "line" |
| tambourine | 25 | "tamb" |
| percussion | 24 | shaker, conga, bongo, perc, rim, tom |
| cymbals | 23 | cymbal, crash, ride |
| claps | 21 | "clap" |
| fills | 19 | "fill" |
| keys | 13 | "piano" |
| scratches | 10 | "scratch" |
| chords | 9 | "chord" |
| hits | 3 | "hit" |
| bass | 2 | the word "bass" (bass material is mostly named "line") |
| other | 807 | phrase hooks, atmospheres, and titles with none of the words above |

The large "other" bucket is mostly vocal hooks and atmospheres (`Come on!`, `Spinning Wheel`, `Chilly`), plus one-word bits such as the Robot set. Those are roles, not a folder the player ships.

## The other products on the disc

`DEMO/` holds playable demos of four more eJay titles. Together with the full Dance library they define the four genres Groove tags its blocks with: Dance, Hip Hop, Rave and Techno.

| folder | product | clips | bar length | tempo |
|---|---|---:|---:|---:|
| `DANCE/` | Dance eJay (full) | 1352 | 75600 | 140 BPM |
| `DEMO/DANCE/` | Dance eJay demo | 152 | 75600 | 140 BPM |
| `DEMO/DANCE2/` | Dance eJay 2 demo | 279 | 75600 | 140 BPM |
| `DEMO/HIPHOP/` | HipHop eJay demo | 181 | 110250 | 96 BPM |
| `DEMO/RAVE/` | Rave eJay demo | 216 | 58800 | 180 BPM |
| `DEMO/TECHNO/` | Techno eJay demo | 157 | 75600 | 140 BPM |

### HipHop and Rave: one file per clip, group in the label

These use the same `tPxD` container, but the label has six lines instead of two:

```
thick \r\n goon \r\n HipHop eJay \r\n \r\n bass \r\n 110250
title    variation  product        (empty)  group    samples per bar
```

The fifth line is the sound group the product files the clip under, which Dance eJay never stored:

| product | groups (clips in the demo) |
|---|---|
| HipHop eJay | loop 16, drum 21, bass 21, guitar 20, key 20, rap 29, voice 26, effect 28 |
| Rave eJay | loop 19, drum 23, bass 18, sequence 17, sphere 18, voice 20, effect 18, special 19, hyper 64 |

"sphere" is pads and atmospheres, "sequence" is synth riffs and arpeggios, and "hyper" is one-shot hits for the built-in pattern generator.

### Techno and Dance 2: one archive plus a catalogue

`EJAY/PXD/R_DEMO20` (Techno) and `D_EJAY2/PXD/DDEMO20` (Dance 2) are single files holding every clip back to back, each still a `tPxD` record. The matching `.INF` is a text catalogue. After a `[SAMPLES]` line it has twelve lines per clip:

```
14539        id
6            flags
"T1FX537"    code: product digit, two-letter group, number, optional L/R
0            byte offset in the archive
38056        byte length
"jam-"       button label, line 1
"mer1"       button label, line 2
1
7            group number
""  0  ""
```

The two letters in the code are the group:

| code | group | code | group |
|---|---|---|---|
| `LA` | loop | `SQ` | sequence |
| `DA` `DB` `DC` `DD` `DF` | kick, snare, hihat, cymbal, percussion | `SR` | sphere (Techno) |
| `BS` | bass | `LY` | layer (Dance 2) |
| `GT` | guitar (Dance 2) | `RP` `VC` `VF` `VX` | rap, voice |
| `FX` | effect | `EX` | xtra |
| `MA`–`MG` | Dance 2 groove-generator one-shots: kick, snare, hat, cymbal, percussion, tom, hit | | |

The groove-generator one-shots are the only clips shorter than half a bar: 4725, 9450 and 18900 samples (a sixteenth, an eighth and a quarter note at 140 BPM).

## What Groove takes from this

Groove takes the roles, not the audio and not the names. Each block has one family, which is its tab, and one or more genre tags, which the toolbar's genre control filters on. How the sounds are made is in [sound-synthesis.md](sound-synthesis.md).

| tab | reference group it stands for |
|---|---|
| Drums | loop: complete beats |
| Kicks, Snares, Hats, Claps, Cymbals, Perc, Fills | drum: one part per clip |
| Bass | bass (the Dance titles call these "line") |
| Keys, Organ | key: piano, electric piano, organ |
| Guitar | guitar (HipHop, Dance 2) |
| Synth | sequence: riffs, arpeggios, leads |
| Pads | sphere, layer |
| Stabs | hyper, xtra: short chord hits |
| Vocals | voice and rap, as synthetic vowel chops, not recorded phrases |
| Scratch | the scratch clips in the HipHop effect group |
| FX | effect: sweeps, drops, sirens, impacts |

Block names are original. They describe the same job as the reference clips.
