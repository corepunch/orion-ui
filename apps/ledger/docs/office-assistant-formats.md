# Office Assistant character formats

Findings behind Ledger's assistant balloon (see
[the assistant README](../share/addons/assistant/README.md#the-office-97-balloon-model)).
The main question was whether Word 97's speech balloon is stored in the
character file or built by Office.

**Short answer:** an Office 97 character (`.ACT`) contains no balloon data. It
holds artwork, sounds and animation sequences. Office drew the balloon from its
own Balloon record. Office 2000 and later characters (`.ACS`, Microsoft Agent)
declare a balloon style, but only for spoken text.

No Office Assistant source code is publicly available. Everything below comes
from published object-model documentation and open-source readers of the
character files.

## Sources

| Source | What it covers |
|---|---|
| [agentviewer](https://github.com/andrewfader/agentviewer) `crates/acs/src/actor.rs`, read at commit `bdd4568` | A Rust reader for Microsoft Actor `.ACT` (Microsoft Bob and Office 97) |
| agentviewer `crates/acs/src/types.rs` | The Agent 2.0 `.ACS` character header, including `BALLOONINFO` |
| [MSAgentUtils](https://github.com/tkfoss/MSAgentUtils) | A Swift `.ACS` reader |
| [clippy.js](https://github.com/clippyjs/clippy.js) | A web reimplementation with a text-only balloon; source of the tail proportions |
| [Double Agent](https://doubleagent.sourceforge.net/) | An open-source Microsoft Agent replacement (GPL/LGPL) |
| Lebeau Software's MSAgent Decompiler | Agent 2.0 `.ACS` only; does not support Office 97 `.ACT` |

The `.ACT` notes are read from agentviewer's source and its unit tests. No
`.ACT` file was available to check them against, and agentviewer's code was
read, not run. The `.ACS` balloon values were read from the bundled
`CLIPPIT.ACS` with a small independent reader.

## Microsoft Actor (`.ACT`): Office 97

Actor descends from Microsoft Bob. Version 1 is Bob, version 2 is Office 97.
Office 2000 and later cannot load these files, and Office 97 cannot load
`.ACS` files.

### Header

All integers are little-endian.

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | Signature `LP` |
| 2 | 2 | Version: 1 (Bob) or 2 (Office 97) |
| 8 | 2 | Name length `n`, including the terminating zero |
| 18 | `n` | Character name, 8-bit text |

Let `h = 18 + n`. The rest of the header is relative to `h`:

| Offset | Size | Field |
|---|---|---|
| `h + 4` | 2 | `first_frame`: index of the first frame composition in the offset table |
| `h + 12` | 2 | Canvas width (192 for the characters agentviewer was tested with) |
| `h + 14` | 2 | Canvas height (144) |
| `h + 38` | 8 × 4 | Section pointers, each relative to `h` |

### Sections

| Pointer | Contents |
|---|---|
| 0 | Metafile and frame-composition blobs |
| 1 | Offset table into section 0 (`u32` each); entry pairs bound each blob |
| 2 | Sound blobs (WAVE) |
| 3 | Offset table into section 2 |
| 4 | Animation sequences |
| 5 | Action table |
| 6 | Character biography strings (not decoded) |
| 7 | A secondary sequence table (not decoded) |

None of these sections holds balloon geometry, colours, fonts or text. The
reader fills its `balloon` field with nothing.

### Artwork

The artwork is vector. Offset-table entries below `first_frame` are placeable
Windows Metafiles (WMF). The remaining entries are frame compositions:

```
u16  (not read)
u16  layer count
per layer, 10 bytes:
  u16  metafile index (0xFFFF = no artwork)
  i16  left, top, right, bottom   on a 2880 × 2160 logical canvas
```

A frame is a stack of metafiles placed and scaled into rectangles, so the same
part (an eye, a hand) is reused across frames. The logical canvas is scaled to
the declared canvas size. There is no palette, unlike `.ACS` bitmaps.

### Animations

The action table is a `u32` count followed by 6-byte records:

```
u16  action id
u16  variant count
u16  first sequence, 1-based
```

An action with several variants has several sequences, and Office picks one.
The file names actions only by number. The mapping from these numbers to
Office's `msoAnimation…` constants was not checked.

The sequence section starts with two bytes. Each sequence follows as a `u16`
slot count, a `u16` flags word, then 6-byte slots `{u16 id, u16 value, u16 flags}`:

| Slot flag | Meaning |
|---|---|
| `0x0001` | A branch record follows |
| `0x0002` | A sound record follows (after any branch records) |
| `0x0100` | Last frame of the sequence |

- In a frame slot, `id` is a frame composition (`id − first_frame`), or
  `0xFFFF` for a blank frame. `value` is its duration in milliseconds.
- In a sound record, `id` is the sound index.
- In a branch record, `id` is the target slot and `value` is a probability out
  of `0x8000`. Probabilities cascade: each branch takes its share of what the
  earlier branches left, and the last branch takes the rest. Branch records
  chain while their own `0x0001` flag is set.

Ledger's prototype plays `.ACS`-derived atlases with one average interval per
clip and no branches (see the README). Actor's per-frame durations and branch
cascades are what a faithful player would add.

## Microsoft Agent (`.ACS`): Office 2000 and later

The `.ACS` character header has an optional `BALLOONINFO` block, present when
the character flags enable the balloon (`0x0300` in agentviewer's reader):

```
u8      lines
u8      characters per line
4 bytes text colour
4 bytes fill colour
4 bytes border colour
string  font name (u32 length, UTF-16LE, zero-terminated)
i32     font height
i32     font weight
u8      italic
u8      (unknown, observed as zero)
```

The bundled `third-party/clippitMS/CLIPPIT.ACS` declares:

| Field | Value |
|---|---|
| Lines × characters | 2 × 28 |
| Text / border | Black / black |
| Fill | `#FFFFE1` |
| Font | MS Sans Serif, height −13 (13 px), weight 400, not italic |

The fill bytes are `FF FF E1 00`. Read as a Windows `COLORREF` (red first),
they are `#FFFFE1`, the standard tooltip yellow. agentviewer reads these
colours as `RGBQUAD` (blue first), which gives `#E1FFFF`, a pale cyan; the
`COLORREF` reading matches the yellow balloon seen in screenshots.

This block styles Agent's own word balloon, which shows the text passed to
`Speak` ([IAgentBalloon](https://learn.microsoft.com/en-us/windows/win32/lwef/iagentballoon)).
It has no labels, check boxes, buttons or edit field.

## What this means for Ledger

- The interactive balloon was never character data. Office built it from a
  runtime record (Heading, Text, up to five Labels, Checkboxes, a button set)
  and laid it out on every `Show`. Ledger's `SpeechBubble` does the same: the
  `.orion` form declares the fixed slots, and Lua's `self.balloon` record fills
  them on each `data()`.
- Only the look is character-specific, and only in `.ACS`: a pale yellow fill,
  a black border and a small sans-serif font. Orion takes these from the theme
  (`brInfoBackground`, `brInfoText`, `theme_t.speechbubble`) rather than from
  the character file.
- Supporting an Office 97 character directly would need a WMF rasterizer and
  the Actor sequence player above; neither is needed for the balloon.
