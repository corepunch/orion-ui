# Dance eJay sample library

Reference for the Groove block bin. The disc is a late-1990s Dance eJay CD at `/Users/igor/Downloads/Dance eJay` (about 4456 files). Groove synthesizes its own loops in the same roles. It does not import the original audio.

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

A few files sit a handful of samples off those sizes (75630, 75649, …). The command stream never produces more samples than this capacity. 671 of 1352 files fill it within 2 percent. The rest are shorter than the slot they were allocated. None run past it.

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

`gs` is a runtime allocation (`GlobalAlloc` of `0x7D0` bytes, then `GlobalLock`). The selector is stored at data offset `0x3166`. No int16 codebook is stored in the DLL.

The index histogram of all 1352 files is centered on `0x80`: that byte is the most common, then `0x7F` and `0x81`, and the mean of `(index - 128)` is about `+0.6`. So index 128 is the zero delta.

`delta = (int16)(index - 128)` (range −128..127) is the table that decodes. A leaky integrator (`y = delta + 0.995 * y`) then measures like the title:

| clip | zero-crossing rate |
|---|---:|
| Eurobass | 250 Hz |
| Kick B | 800 Hz |
| Come on! | 1300 Hz |
| Hihat / electric | 1500 Hz |
| Clap | 1800 Hz |
| Scratch | 2400 Hz |
| Tambourine / Fast | 2900 Hz |

A raw wrapping int16 sum also stays in range on tonal clips (Come on! peaks near 13000). Loud noisy clips walk into the rails because a small DC bias in the deltas integrates over a whole bar. The player's saturate-on-store path is the one that matches the mix buffer.

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

Groove already had Drums, Bass, Piano, Guitar, and Electronic. The bin now also has a tab for each explicit family the titles name, with bass lines staying in Bass and chord or piano loops staying in Piano and Electronic:

| tab | role taken from the reference |
|---|---|
| Kicks | dry, room, half-time, and end-of-bar kicks |
| Snares | backbeat, rim, ghosts, end roll |
| Hats | closed, open, sixteenths, shuffle |
| Claps | backbeat, stacked, doubles, rush |
| Cymbals | ride, crash, bell, splash |
| Perc | shaker, tambourine, conga, wood |
| Fills | tom run, snare build, kick tumble, hat lift |
| Scratch | forward zip, reverse, chopped loop, brake |
| Organ | stab, offbeat chop, held chord, fifths |
| Vocals | short synthetic chops, not recorded phrases |
| FX | rising noise, falling noise, impact, air bed |

Block names in those tabs are original. They describe the same job as the reference clips.
