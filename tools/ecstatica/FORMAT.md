# Ecstatica 1 disc data: geometry, rig, animation and rendering

Decoded against the supplied `ecstatica.iso` on 2026-09-29. This supersedes the earlier statement that the model dimensions had not been decoded.

**The stored dimensions are now recovered:** all 323 indexed actor definitions, containing 6,678 parts, their parent relationships, ellipsoid halfaxes, centres, attachment offsets, positions, rotations, colours, flags, points and triangles. Both stores contain byte-identical actor definitions. The [complete catalogue](CATALOG.md) lists every actor and its exact numbers, plus all 669 indexed action headers and key headers. Actors include scenery and props as well as characters; 323 does not mean 323 humanoids.

The [decoder](ecstatica_decode.py) also reads every FANT section, including action and scene events, repertoires, script tokens and source lines, sound headers/payloads, sectors, cameras and map areas. Optional visual decoding handles all supplied backgrounds, graphics and the ellipsoid shade/depth map. The JSON export retains the original event operands and offsets; it does not silently replace unknown operands with guessed meanings.

**Coverage is structural, not a complete recreation of the executable.** Exact stored geometry is established. Runtime interpolation, the complete joint solver, physical unit conversion and several flag meanings are not established by this work. The unresolved items and the evidence needed to settle them are listed explicitly below. This is a reproducible data-format reference, not a claim that the entire DOS engine has been decompiled.

## Reproduce

Run from the repository root. Python 3 standard library and 7-Zip suffice. The decoder never runs game executables or imports the external research library.

```sh
7z x -y -o/tmp/ecstatica-assets /path/to/ecstatica.iso \
  OFFSETS OFF2 'FILES/*' 'CODE/*' 'VIEWS/*' 'GRAPHICS/*' SHADEMAP.DAT
python3 tools/ecstatica/ecstatica_decode.py /tmp/ecstatica-assets \
  --markdown /tmp/ecstatica-catalog.md \
  --json /tmp/ecstatica-decoded.json
python3 -m unittest discover -s tools/ecstatica -p 'test_*.py'
```

The Markdown is deterministic and can be compared with `CATALOG.md`. JSON includes decoded image planes, scripts, sounds and opaque payloads and is large; keep it outside the repository. Omit `--json` for a coverage check. Graphics directories and SHADEMAP are optional; their absence is reported as zero/absent coverage, not success at decoding them. Both containers and the main FANT file are required. The parser intentionally rejects versions other than the supplied version 30.

For one original record, the existing C extractor remains useful:

```sh
cc -std=c99 -Wall -Wextra -Werror tools/ecstatica/ecstatica_index.c -o /tmp/ecstatica_index
/tmp/ecstatica_index /tmp/ecstatica-assets/OFFSETS \
  /tmp/ecstatica-assets/FILES/ECSTATIC 1000 /tmp/fdinhe.fant
```

### Source fingerprints

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `CODE/ECSTATIC.FAN` | 478342 | `f49bb307ac54b9cdbcbcd91ecbb47a738c72558b6acaaa32664a5442b624db5d` |
| `OFFSETS` | 14100 | `a3dd22b36175e41b498ba5f5b7740ba213f8863d4fb45f399043345f8640cefa` |
| `OFF2` | 14100 | `6997be13ac9cd787cbf3b610bdc8634af4169ff5177a1ecbe5101409aa089a10` |
| `FILES/ECSTATIC` | 36368706 | `e6a04470ab8a81e5bc21f15763107ac9fdcb128768ab057bb66db0ded4d110dd` |
| `FILES/ECST2` | 5965414 | `78474e17e4c0edb38e866e35be72cb0ee21b77903faab4205d21f4be45c7ab57` |
| `SHADEMAP.DAT` | 49152 | `8f1e322f3024d9a879b67da51ac06b63bcace58967335a31dc75c5b58f865f57` |

### Verified coverage

| Data | Full store | Compact store |
| --- | ---: | ---: |
| Present / missing index slots | 2531 / 994 | 2283 / 1242 |
| Fully consumed FANT records | 2168 | 1920 |
| Scene records | 732 | 732 |
| Actor records / part definitions | 323 / 6678 | 323 / 6678 |
| Action records | 669 | 669 |
| Repertoire records | 105 | 105 |
| Sound records | 339 | 91 |
| Length-prefixed opaque records | 363 | 363 |

All 4,088 indexed FANT records and the main FANT file parse exactly to their ends. No unknown event opcode is encountered. Every actor parent chain resolves without missing parents or cycles. There are no negative stored halfaxes; 593 parts contain at least one zero halfaxis. A zero axis therefore cannot be rejected globally as malformed geometry.

Actor, action, repertoire and opaque records are byte-identical between stores. Of 732 scenes, 731 are identical. Of the 91 sound records shared by the stores, 24 are identical; the others differ. The compact store is not a different character-detail level.

The main file contains 1,405 code definitions, a 128×128 sector map, 24,034 sector records, 250 cameras and 74 map areas; its actor/action/scene/repertoire event streams are empty apart from terminators. All 220 supplied background files decode into 64,000 colour samples and 64,000 depth samples each, with their tails fully consumed. All 11 graphics decode at their declared dimensions. The shade-map sphere equation matches all 16,384 stored depth samples exactly.

## Container index

Each offset table contains 3,525 **big-endian unsigned 32-bit** absolute offsets into its paired container. `0xffffffff` means absent. A record extends to the next greater present offset, or EOF for the final record. The index order is not a replacement for sorting offsets; aliases should share the same end. Global ID ranges:

| Inclusive slots | Meaning | Local/name ID |
| --- | --- | --- |
| 0–999 | scenes | slot |
| 1000–1499 | actors | slot − 1000 |
| 1500–2499 | actions | slot − 1500 |
| 2500–2649 | repertoires | slot − 2500 |
| 2650–3149 | sounds | slot − 2650 |
| 3150–3524 | other, opaque payloads | slot − 3150 |

The final category has no `FANT` signature. All 363 present records start with a BE `u32` equal to `record_size − 4`. The rest is exported verbatim. Its internal format/purpose has not been verified; do not label it animation or model data merely from its position in the index.

## FANT version 30 layout

`BE`/`LE` describe byte order; `s16` is signed two's-complement. Mixed endianness is intentional.

| Offset/order | Encoding | Meaning |
| --- | --- | --- |
| `0x00` | 4 bytes | ASCII `FANT` |
| `0x04` | BE s16 | version: 30 |
| `0x06` | BE s16 | omit name tables: 0 in main file, 1 in indexed records |
| `0x08..0x21` | 26 bytes | reserved/opaque header, retained |
| `0x22`, if names present | ten string lists | names in the order below |
| next | event stream | actions |
| next | event stream | actors |
| next | event stream | scenes/scripts |
| next | BE s16 count + entries | compiled code and source lines |
| next | event stream | repertoires |
| next | byte-marked list | sounds |
| next | BE s16 | sector-data-present flag |
| if present | map, sectors, cameras, map areas | described below |

Each name is NUL-terminated, and an extra NUL ends each list. Names are decoded losslessly as Latin-1; display labels trim trailing spaces. Name existence does not imply that the corresponding indexed record is present.

| Order | List | Entries | Next byte offset in main file |
| ---: | --- | ---: | --- |
| 0 | parts | 203 | `0x06eb` |
| 1 | actors | 467 | `0x1407` |
| 2 | actions | 869 | `0x3c32` |
| 3 | scenes | 932 | `0x5644` |
| 4 | points | 45 | `0x56fa` |
| 5 | triangles | 27 | `0x5746` |
| 6 | codes | 1444 | `0x8a57` |
| 7 | repertoires | 137 | `0x8e67` |
| 8 | sounds | 455 | `0x9cd4` |
| 9 | map areas | 74 | `0x9eec` |

### Events

An ordinary event is exactly ten bytes: five BE s16 words `(opcode, index, value1, value2, value3)`. Opcode 0 terminates a stream, but **all ten bytes must be consumed**. Its other words can be nonzero and must not be treated as another event.

Opcode 27, `NEXT_SCENE`, with nonzero index has a following NUL-terminated attachment. Consume its actual length; neither a fixed 24-byte skip nor alignment to an even address is correct. Events after this attachment can start at odd byte offsets.

`index` changes namespace according to opcode: part, actor slot, triangle, action or repertoire. In an actor record, `ADD_THING.value1` identifies its local actor slot; it is not necessarily the external actor-name ID. E.g. record 1001 has external actor ID 1 but can construct slot 0. Never use that slot to name the record.

The table below gives conventional opcode names from the format research. A name is not proof that every runtime behavior is known. Counts include all four event streams of the main file and full store, once each, including terminators. Unused opcodes are documented but have no verification from occurrence in this disc.

| ID | Name | Count | Operand interpretation / status |
| ---: | --- | ---: | --- |
| 0 | `NO_EVENT` | 8676 | End this stream; preserve all operands |
| 1 | `ROTATE` | 166569 | Part rotation vector |
| 2 | `OFFSET` | 32744 | Part attachment offset |
| 3 | `COLOUR` | 8223 | Part shade-ramp index in value1; retain other words |
| 4 | `VECTOR1` | 11348 | Ellipsoid halfaxes x,y,z |
| 5 | `VECTOR2` | 9443 | Ellipsoid centre vector |
| 6 | `VECTOR3` | 6678 | Opaque vector; all actor-definition instances are (0,0,0) |
| 7 | `ADD_PART` | 6556 | Attach child value1 to parent index; retain value2/3 |
| 8 | `ADD_THING` | 323 | Create actor slot value1 |
| 9 | `TYPE` | 6678 | Part type metadata; retain all three words |
| 10 | `ADD_PART_TO_THING` | 323 | Attach root part value1 to actor slot index |
| 11 | `PSEUDO_ACTION` | 669 | Action header; flags/timing metadata, retain all words |
| 12 | `PSEUDO_KEY` | 23601 | Key header; value1 contains the unsigned phase/time word |
| 13 | `DISP_PNT` | 6686 | Auxiliary display-point vector; runtime role unverified |
| 14 | `FLAGS` | 7017 | Part flag masks; research maps value1=set, value2=clear |
| 15 | `MOVE_ACT` | 0 | Move-action control; not observed |
| 16 | `RAND_ACT` | 0 | Random-action control; not observed |
| 17 | `RAND_INFO` | 0 | Randomization metadata; not observed |
| 18 | `ROTATE_THING` | 1354 | Actor rotation |
| 19 | `MOVE_THING` | 2055 | Actor movement vector |
| 20 | `START_POSITION` | 323 | Actor initial position |
| 21 | `THING_FLAGS` | 323 | Actor flags/metadata; retain all words, not the same contract as 14 |
| 22 | `SCRIPT_MOVE` | 12065 | Scene-script actor position |
| 23 | `SCRIPT_TURN` | 11416 | Scene-script actor orientation |
| 24 | `SPAWN_ACTION` | 0 | Action spawning; not observed |
| 25 | `PSEUDO_SCENE` | 732 | Scene header |
| 26 | `PSEUDO_SCRIPT` | 1508 | Scene script header |
| 27 | `NEXT_SCENE` | 3660 | Next-scene header with conditional string attachment |
| 28 | `ANCHOR_PART` | 0 | Anchor-part control; not observed |
| 29 | `LOOSEN_JOINT` | 0 | Loosen-joint control; not observed |
| 30 | `UNLOOSEN_JOINT` | 0 | Undo loosen-joint control; not observed |
| 31 | `POSITION` | 62433 | Part position/origin or constraint target, according to runtime mode |
| 32 | `2_PART_LIMB` | 4 | Two-part-limb control; four scene occurrences |
| 33 | `FIX_PART` | 0 | Fix-part control; not observed |
| 34 | `UNFIX_PART` | 0 | Unfix-part control; not observed |
| 35 | `UNMAKE_LIMB` | 0 | Undo two-part limb; not observed |
| 36 | `REORIENT_THING` | 0 | Reorient actor; not observed |
| 37 | `PSEUDO_ADJUNCT` | 0 | Adjunct scene metadata; not observed |
| 38 | `PSEUDO_ADJUNCT_2` | 0 | Additional adjunct metadata; not observed |
| 39 | `ABSOLUTE_POS` | 0 | Absolute part position; not observed |
| 40 | `ABSOLUTE_ROT` | 0 | Absolute part rotation; not observed |
| 41 | `ADD_POINT` | 1371 | Attach point value1 to part index |
| 42 | `OFFSET_POINT` | 1393 | Point index position x,y,z |
| 43 | `ADD_TRIANGLE` | 432 | Triangle index with three point IDs |
| 44 | `COLOUR_TRIANGLE` | 448 | Triangle front/back colour IDs in value1/2 |
| 45 | `TRIANGLE_FLAGS` | 432 | Triangle flag operands; runtime mask semantics unverified |
| 46 | `INTERACT` | 4641 | Interaction command; index selects subtype |
| 47 | `PSEUDO_ACTION_2` | 669 | Additional action header; value1 can reference a following action |
| 48 | `POINT_TO_POINT` | 0 | Point-target constraint; not observed |
| 49 | `HELD_OFFSET` | 323 | Actor right-hand held-object offset |
| 50 | `HELD_ROTATE` | 323 | Actor right-hand held-object rotation |
| 51 | `BACKGROUND` | 24 | Background actor control |
| 52 | `PSEUDO_SCENE_2` | 732 | Additional scene metadata |
| 53 | `PSEUDO_REP` | 105 | Repertoire header |
| 54 | `REP_ENTRY` | 2546 | Repertoire mapping: value1 entry, value2 action |
| 55 | `ACTOR_REP` | 658 | Actor repertoire assignment |
| 56 | `DEF_ROTATE` | 1230 | Default rotation |
| 57 | `DEF_OFFSET` | 1230 | Default offset |
| 58 | `DEF_VECTOR1` | 1230 | Default halfaxes |
| 59 | `DEF_VECTOR2` | 1230 | Default centre |
| 60 | `DEF_COLOUR` | 1230 | Default colour |
| 61 | `DEF_FLAGS` | 1230 | Default flags |
| 62 | `DEF_POSITION` | 1230 | Default position |
| 63 | `CUT_PART` | 142 | Detach/cut part command |
| 64 | `HELD_OFF_LEFT` | 323 | Actor left-hand held-object offset |
| 65 | `HELD_ROT_LEFT` | 323 | Actor left-hand held-object rotation |
| 66 | `THING_CODE` | 323 | Actor code hooks; research interprets operands as code ID + 1 |

## Exact reference biped: actor 0, `fdinhe`

Full-store record **1000**, container byte **2233776** (`0x2215b0`), length **5269** bytes; SHA-256 `a3de84f75c83081bc81b0fe6f52275573bf094e4fca2061a460242727b5a39c4`. It has **43 part definitions, 11 attachment points and 5 triangles**. Its anatomical layout makes it a useful biped reference; the exact character/costume shown by any particular screenshot must not be identified solely by this internal name.

All vectors below are exact signed words. Halfaxes are radii; multiply each by two for full diameters. `Parent` is the actual part ID, even when a stored left/right label disagrees. The complete face/hair/accessory table and point/triangle commands are in [CATALOG.md](CATALOG.md).

| ID / stored name | Parent | Halfaxes | Attachment offset | Centre | Rotation | VECTOR1 offset |
| --- | ---: | --- | --- | --- | --- | --- |
| 2: Body | root | 64,64,60 | -8,-384,40 | 0,0,0 | 0,0,0 | `0xb8` |
| 4: Left thigh | 2 | 32,44,108 | -43,3,-5 | 0,0,96 | -14336,0,0 | `0x126` |
| 15: Left shin | 4 | 20,24,116 | 0,6,94 | 0,0,88 | -7680,0,0 | `0x194` |
| 5: Left foot | 15 | 28,12,68 | 0,0,98 | 0,12,41 | 0,-4096,512 | `0x202` |
| 29: Left knee | 15 | 24,28,40 | 0,-3,-76 | 0,0,0 | 0,0,0 | `0x270` |
| 34: Right molet | 15 | 28,32,56 | 0,7,-12 | 0,0,0 | 0,0,0 | `0x2de` |
| 12: Right thigh | 2 | 32,44,108 | 43,3,-5 | 0,0,96 | -14336,0,0 | `0x34c` |
| 13: Right shin | 12 | 20,24,116 | 0,6,94 | 0,0,88 | -7680,0,0 | `0x3ba` |
| 14: Right foot | 13 | 28,12,68 | 0,0,98 | 0,12,41 | 0,4096,-512 | `0x428` |
| 30: Right knee | 13 | 24,28,40 | 0,-3,-76 | 0,0,0 | 0,0,0 | `0x496` |
| 35: Left molet | 13 | 28,32,56 | 0,7,-12 | 0,0,0 | 0,0,0 | `0x504` |
| 17: Chest | 2 | 68,112,56 | 0,-38,22 | 0,-90,0 | 2048,0,0 | `0x572` |
| 3: Head | 17 | 44,80,48 | 0,-108,25 | 0,-60,30 | 0,0,0 | `0x5e0` |
| 6: Left upper arm | 17 | 24,28,80 | -63,-70,16 | 0,0,60 | -10240,-16384,-8704 | `0xd9c` |
| 7: Left forearm | 6 | 20,16,72 | 0,0,62 | 0,0,63 | -16640,0,0 | `0xe0a` |
| 1: Left hand | 7 | 20,20,32 | 0,20,62 | 0,0,16 | -7424,-1536,-13824 | `0xe78` |
| 23: Right shoulder | 6 | 44,40,44 | -8,0,-48 | 0,0,0 | 0,0,0 | `0xee6` |
| 8: Right upper arm | 17 | 24,28,80 | 63,-70,16 | 0,0,60 | -10240,16384,8704 | `0xf54` |
| 9: Right forearm | 8 | 20,16,72 | 0,0,62 | 0,0,63 | -15872,0,0 | `0xfc2` |
| 0: Right hand | 9 | 20,20,32 | 0,20,61 | 0,0,16 | -7424,7424,14080 | `0x1030` |
| 26: Left shoulder | 8 | 44,40,44 | 8,0,-48 | 0,0,0 | 0,0,0 | `0x109e` |
| 24: Right pectorals | 17 | 64,68,48 | 16,-32,8 | 0,0,0 | 0,0,0 | `0x110c` |
| 25: Left pectorals | 17 | 64,68,48 | -16,-32,8 | 0,0,0 | 0,0,0 | `0x117a` |
| 27: Left butt | 2 | 52,60,48 | 17,12,-14 | 0,0,0 | 0,0,0 | `0x11e8` |
| 28: Right butt | 2 | 52,60,48 | -17,12,-14 | 0,0,0 | 0,0,0 | `0x1256` |
| 33: belt | 2 | 64,16,64 | 0,-48,0 | 0,0,0 | -2048,0,0 | `0x12c4` |

### A complete part record, byte for byte

Right pectorals (part 24) consists of these eleven events. Offsets are relative to record 1000, not the whole container; add 2,233,776 to obtain absolute file offsets.

| Offset | Opcode | Index | value1 | value2 | value3 |
| --- | --- | ---: | ---: | ---: | ---: |
| `0x10da` | ADD_PART | 17 | 24 | 25 | 26 |
| `0x10e4` | OFFSET | 24 | 16 | -32 | 8 |
| `0x10ee` | POSITION | 24 | 0 | 0 | 0 |
| `0x10f8` | ROTATE | 24 | 0 | 0 | 0 |
| `0x1102` | DISP_PNT | 24 | 0 | 0 | 0 |
| `0x110c` | VECTOR1 | 24 | 64 | 68 | 48 |
| `0x1116` | VECTOR2 | 24 | 0 | 0 | 0 |
| `0x1120` | VECTOR3 | 24 | 0 | 0 | 0 |
| `0x112a` | COLOUR | 24 | 13 | 0 | 25152 |
| `0x1134` | TYPE | 24 | 4 | 13 | 0 |
| `0x113e` | FLAGS | 24 | 0 | -1 | 0 |

### What the anatomy numbers actually say

- **Pecs:** both halfaxis triples are `(64,68,48)`, so full diameters are `(128,136,96)`. The chest halfaxes are `(68,112,56)`. The pec attachment vectors differ by only 32 units in x: `(16,-32,8)` and `(-16,-32,8)`. Each pec is 94.1% of the chest's x radius, but their lateral separation is only 25% of a pec's full width. This is broad overlapping geometry, not two isolated round balls. The forward offset plus pec z radius is `8 + 48 = 56`, matching the chest z radius. That is a local-parameter comparison, not a solved world-space silhouette; do not flatten the attachment/centre distinction when importing it.
- **Shoulders:** the decorative deltoids have halfaxes `(44,40,44)`, versus upper-arm `(24,28,80)`. They attach to the upper arms with z offset `-48`, rather than being extra joints atop the neck. The upper-arm attachment x values are ±63 and y is −70. These values provide a reproducible reference for placement without prescribing an unverified centimetre conversion.
- **Knees:** halfaxes `(24,28,40)`; attachment `(0,-3,-76)` on the shin. They are distinct from both thigh and shin. Relative to shin `(20,24,116)`, the kneecap is 1.2× as wide, 1.167× in the other cross axis, and 0.345× in the long axis. It moves with the lower segment.
- **Calves (`molet`):** halfaxes `(28,32,56)`; attachment `(0,7,-12)` on the shin. Their cross radii are 1.4× and 1.333× the shin radii, while their long radius is only 0.483× the shin's. A separate shorter bulge over a long narrower shin is explicit in the data. “Gastrocnemius” is our anatomical interpretation of the part and silhouette; the stored name is `molet`.
- **Hands:** `(20,20,32)` halfaxes, centre `(0,0,16)`, one part per hand. Actor 0 has no digit parts. There are no names containing “finger” or “thumb” in the complete 203-entry part-name table. This is supporting evidence for simple hands, not proof against geometry with a different label in some other actor. Our biped's no-fingers requirement remains explicit.
- **Elbows:** names exist in the global table, but actor 0 does **not** instantiate the named elbow parts. Do not mistake a name-table entry for visible model geometry. Our requested explicit elbow volumes are an authored choice.
- **Faces are not ellipsoids only:** actor 0 has 11 points attached to its head and five triangles with front/back colours. Ears have one zero halfaxis. A faithful importer must support these cases instead of forcing every part into a solid nondegenerate ellipsoid.

### Hierarchy and side-label traps

```text
Body (2)
├─ Left thigh (4) → Left shin (15)
│  ├─ Left foot (5)
│  ├─ Left knee (29)
│  └─ Right molet (34)       [stored name; belongs to left shin]
├─ Right thigh (12) → Right shin (13)
│  ├─ Right foot (14)
│  ├─ Right knee (30)
│  └─ Left molet (35)        [stored name; belongs to right shin]
├─ Chest (17)
│  ├─ Head (3) → face/hair/points
│  ├─ Left upper arm (6)
│  │  ├─ Left forearm (7) → Left hand (1)
│  │  └─ Right shoulder (23) [stored name; belongs to left upper arm]
│  ├─ Right upper arm (8)
│  │  ├─ Right forearm (9) → Right hand (0)
│  │  └─ Left shoulder (26)  [stored name; belongs to right upper arm]
│  ├─ Right pectorals (24)
│  └─ Left pectorals (25)
├─ Left butt (27)
├─ Right butt (28)
├─ belt (33) → patch (42)
└─ secret piece (38)
```

The catalogue follows numeric parent relationships, not English names. Prop actors can reuse anatomical name IDs for completely different geometry.

### Coordinates, dimensions and rotation words

`VECTOR1` is the three ellipsoid halfaxes. In its own ellipsoid frame, a nondegenerate surface satisfies `(x/a)^2 + (y/b)^2 + (z/c)^2 = 1`. `VECTOR2` is the centre relative to the part frame. `OFFSET` specifies attachment relative to the parent geometry. `POSITION` is separate and is used for part origins/targets depending on constraint state. The format research stores all three separately and rescales attachment offsets by parent halfaxes internally. Therefore `OFFSET` and `POSITION` must not be collapsed into a single translation, and halfaxis changes can affect attachments.

The dimensional triples in this report are **raw model units**. No absolute conversion to metres or centimetres is encoded in the actor definitions. The external library's generic `x/16384` fixed-point helper is not evidence that every dimension should be divided by 16384. Ratios between stored dimensions need no unit assumption.

For an angle word, a conventional 16-bit turn conversion is `degrees = signed_word * 360 / 65536` (e.g. 16384 → 90°, 2048 → 11.25°). Keep the raw value authoritative: the older research helper divides by 65535, and this work has not verified the executable's trigonometric table, Euler order or handedness. Importing into Scener's z-up space also needs a deliberate basis conversion. A direct copy of these three angles into Scener Euler fields is not a verified reconstruction.

### Constraint evidence

Actor 0's upper arms store `FLAGS.value1 = 8224 = 0x2020`; thighs store `8352 = 0x20a0`. The external runtime research identifies bit 5 (`0x20`) as a two-part-limb flag. Both flags include that bit; hands/feet store `0x2000`, shins/forearms store zero, and many decorative volumes store `0x1000`. The meanings of all remaining bits are not verified here.

The full store also contains four actual opcode-32 `2_PART_LIMB` commands: scene 395 (`din_herm`) at record offsets `0x74c`, `0x756`, and scene 746 (`x_d_herm`) at `0x7ec`, `0x7f6`. They address parts 18 and 39 in the scenes' active actor context. Do not interpret those IDs as protagonist limbs without resolving that context.

This is stronger evidence for a constraint-capable rig than the earlier inspection of names alone. It still does not establish the executable's exact IK equations, joint limits, pole convention, update order or blending behavior. The static parser neither solves constraints nor claims a reproduced runtime pose.

## Animation and repertoire data

The 669 actions contain 23,601 `PSEUDO_KEY` headers. Each key owns the following event commands until the next key/action header or stream terminator. Those commands reuse the same part IDs and field encodings as the actor definitions, allowing rotations, positions, attachment offsets, halfaxes, colours and other properties to vary.

All observed key headers have index 0 and value2/value3 equal to zero. **Interpret value1 as an unsigned 16-bit time/phase word when ordering keys**, even though the generic event decoder preserves signed words. Every action's key sequence is nondecreasing under unsigned interpretation. 448 actions end at 65535 (`0xffff`), and 221 end at 65280 (`0xff00`). This supports a normalized phase representation; it does not establish a frame rate.

Concrete example: record **1525**, action **25, `walk 7`**:

| Field | Stored values |
| --- | --- |
| `PSEUDO_ACTION` | index 25; operands `(60,29,-5)` |
| `PSEUDO_ACTION_2` | index 0; operands `(-1,0,0)` |
| Key phase words, unsigned | `8448,14336,20224,30208,32512,41216,47104,52992,62976,65280` |
| Same words / 256, for inspection | `33,56,79,118,127,161,184,207,246,255` |
| First-key body OFFSET | `(-1,-368,0)` |
| First-key actor MOVE_THING | `(0,0,99)` |
| First-key left-hand POSITION | `(-106,-346,-69)` |
| First-key right-hand POSITION | `(93,-355,110)` |
| First-key right-foot POSITION | `(20,-98,-120)` |
| First-key left-foot POSITION | `(-37,-16,46)` |

Division by 256 in this table is only a useful display convention; many other keys have nonzero low bytes. Do not quantize them away. Do not read the negative signed exports as keys moving backward in time. The action-header operands, playback speed, loop flags, transition flags and interpolation curves require runtime confirmation before playback can be faithful.

Repertoire records group action choices. `PSEUDO_REP` begins the definition; `REP_ENTRY` uses value1 as an entry selector and value2 as an action ID. Repertoire 0 includes mappings `0→70`, `1→24`, `2→558`, `3→21`, `4→0`, `5→22`, `7→28`, `9→652`, `10→788`. Actor 0 assigns repertoire 0 with opcode 55. These are action dispatch mappings, not extra geometry.

The actor stores right and left held-object offsets `(0,224,-32)` and held rotations `(18432,0,0)`, separately from hand shape. This is useful evidence for keeping grip/attachment transforms separate from the mitten mesh in our rig.

## Remaining FANT sections

### Script code

A BE s16 count is followed by code entries. Each entry starts with a BE s16 code ID, followed by BE 16-bit tokens until token zero. A token with high nibble `0xe` is followed by `((token & 0x0fff) + 1) & ~1` string bytes, an even-sized payload. Read that payload before the next token. The main file has 423 such tokens, all consumed successfully. After the zero token comes a BE s16 source-line count and that many NUL-terminated source strings.

The source lines are retained in the JSON and give a readable script representation without needing a speculative disassembler. High-nibble `0xf` tokens are signed 12-bit numeric literals in the external research; high-nibble zero encodes script keywords. Other token namespaces are retained numerically. The parser is not a script interpreter and does not claim the entire script VM's behavior.

### Sounds

A one-byte nonzero marker introduces each entry; zero ends the list. Header after the marker:

| Relative offset | Type | Meaning |
| --- | --- | --- |
| 0 | LE s16 | sound ID |
| 2 | LE u16 | flags |
| 4 | LE s16 | unknown1 |
| 6 | LE s32 | payload length in bytes |
| 10 | LE s16 | unknown2 |
| 12 | payload bytes | retained exactly |

The length is validated against the enclosing record. Do not call these bytes WAV, PCM, ADPCM or a particular sample rate without decoding the playback routine. No audio conversion was used to support the geometry findings.

### Sectors and cameras

When the sector-present flag is nonzero, read:

1. `128×128` LE s16 sector-map entries.
2. BE s32 sector count, followed by ten-byte records: `u8 priority`, `s8 section`, `u8 camera`, `u8 ymax`, `s8 unknown`, one padding byte, BE u16 packed code/flags, two padding bytes. The packed field's low 14 bits are a code index and top two bits are flags.
3. LE s16 camera count, then seven LE s16 words per camera: position x/y/z, rotation x/y/z and focal/projection parameter. Preserve raw units; no unverified focal-length-to-FOV formula is applied.
4. Map-area list with a byte marker, LE s16 area ID, LE s16 value count and that many LE s16 values; zero marker ends it. This disc uses counts supported by the reader (0–10). Reject larger counts instead of copying the older library's silent clamp and losing byte alignment.

The parser exports padding and unknown fields. The semantic meanings of the full sector bitfields, map-area slots and the camera projection equation need additional runtime work.

## Visual data and original ellipsoid rendering evidence

### SHADEMAP.DAT: exact depth equation recovered

This file is **interleaved**, not separate colour and depth planes. At pixel `(x,y)`, `offset = 3*(128*y+x)`:

| Bytes | Type | Meaning |
| --- | --- | --- |
| offset | u8 | shade-table value |
| offset+1..2 | BE s16 | sphere depth or outside sentinel |

For x,y in 0..127:

```text
nx = (x - 63.5) / 63.5
ny = (y - 63.5) / 63.5
q = 1 - nx*nx - ny*ny
stored_depth = trunc_toward_zero(-32768 * sqrt(q))   if q >= 0
stored_depth = 32767                               otherwise
```

**All 16,384 depth words match this equation exactly.** Centre samples `(63,63)` and `(64,64)` have depth −32765; outside samples such as `(0,0)` are 32767. The shade values are a separate stored lookup, not the depth bytes. Their lighting equation has not been reconstructed; the original values are exported so no approximation is necessary to reuse the lookup in an experimental renderer.

The external renderer research describes mapping the shade lookup through material-specific palette ramps. Actor `COLOUR` IDs select these ramps; they are not RGB triples. `GRAPHICS/TITLE2.RAW` contains a 256×3 palette with full 8-bit RGB components. Do not assume a VGA 6-bit palette and multiply all channels by four. This lookup-based renderer is materially different from smooth modern PBR ellipsoids; copying only dimensions does not recreate Ecstatica's shading.

### Background views

All 220 supplied `VIEWS/*.RAW` have:

| Order | Encoding |
| --- | --- |
| header | LE u16 zero, LE s32 packed-colour byte count, LE s32 packed-depth byte count |
| colour | packed stream expanding to 320×200 u8 samples |
| depth | packed stream expanding to 320×200 u16 samples |
| tail | LE s16 count, then that many BE s16 words |

Every tail has 1,503 words, and every file is consumed exactly. The tail's internal meaning is not established; exported words must not be discarded.

For both packed streams, each control byte has `count = byte >> 2`, `mode = byte & 3`. Count zero terminates. The previous value starts at zero and continues across runs. An odd nibble count leaves the final high nibble unused.

| Mode | Colour plane | Depth plane |
| ---: | --- | --- |
| 0 | packed signed four-bit deltas, low nibble first, wrap modulo 256 | same deltas ×4, wrap modulo 65536 |
| 1 | repeat following byte `count` times | `count` signed byte deltas ×4 |
| 2 | `count` literal bytes | `count` LE u16 literals, each shifted left 2 and masked to 16 bits |
| 3 | repeat following byte `count` times | repeat following LE u16 shifted left 2 and masked to 16 bits |

These formulas were exercised against every supplied view, not only a handpicked file. Depth values are preserved unsigned after decoding; the world-unit interpretation and camera comparison rule are runtime concerns.

### Menu/title graphics

All 11 supplied `GRAPHICS/*.RAW` start with the eight bytes `mhwanh\0\4`. A BE u16 width and height follow at offsets 8 and 10. The rest of the 32-byte header is retained, followed by 768 RGB palette bytes and exactly `width*height` indexed pixels. No graphics file has leftover bytes. Example dimensions are 93×18 (`END2`) and 107×100 (`ENDEND`); the full headers and pixels are available in JSON.

## Applying the findings to our biped

Keep the requested division: CAT-style controls and Scener's technical rig, with Ecstatica's visual organization. Scener supplies its own layered animation, procedural gait and BVH retargeting. Decoding Ecstatica’s data does not establish equivalence with CAT or reproduce the original game’s runtime animation solver.

1. Keep pelvis/chest hubs and limb control chains separate from decorative muscle volumes.
2. Use broad overlapping pec volumes, with their front extent close to the chest extent. The source values explain why two separate spherical lobes are the wrong reading of the reference.
3. Attach shoulder masses to the upper-arm segments; knee and calf masses to the lower-leg segments. Preserve explicit knees during flexion.
4. Give the calf a shorter, wider mass over a narrower long shin, using the decoded ratios as the reference.
5. Use one hand volume per side and keep held-object transforms separate. Do not reintroduce digits.
6. Preserve part IDs and actual parent relationships when studying imported data. Correcting English side labels is a presentation choice, not permission to swap branches.
7. Normalize the chosen reference actor to the desired authored height only after a verified pose/basis conversion. Avoid arbitrary per-part unit conversions.

The current Scener prefab remains authored geometry. This decoding task documents the source data; it does not silently replace the prefab with extracted game assets or claim a pixel-identical game rendering.

## What is still unresolved, precisely

| Item | What is recovered | What would establish the remaining behavior |
| --- | --- | --- |
| Joint transforms / IK | full parent graph, offsets/positions/centres, rotations, limb flags, target-like positions | trace original part-update and two-part-limb routines; verify Euler basis, attachment rescaling, constraints and solver order against captured poses |
| Animation playback | every command and key time word, monotonic sequences, all action metadata | inspect action update/interpolation and timer routines; establish flags, tick conversion, wrap and blend rules |
| Physical scale | exact raw halfaxes and ratios | identify an explicit engine/export scale or a calibrated external reference; no cm claim follows from the file alone |
| Opaque operands | bytes, offsets and observed distributions; all actor VECTOR3 values zero | executable dispatch cases for TYPE, DISP_PNT, flags, interactions and other metadata |
| Script execution | compiled tokens and readable source strings | full token dispatch and VM semantics |
| Audio / final index category | exact payloads, sizes, IDs and headers | playback/loader dispatch and codec identification |
| View tails / sectors / projection | all stored values and file boundaries | renderer/world-loader routines explaining the 1,503 tail words and projection/sector rules |
| Shade brightness | exact lookup values and exact sphere-depth generation | original brightness-table generation and ellipsoid projection/rasterization routines |

Recovering all stored fields is different from proving the executable's interpretation of every field. Unknown meanings are preserved and named here so a later implementation cannot quietly substitute guesses. The `E2Recomp` material in the separately inspected EcstaticaRecompiled repository concerns Ecstatica 2 and is not evidence for these Ecstatica 1 runtime details.

## Research provenance and validation

Format leads and conventional event names came from Fabian Hachenberg's [pyecstaticalib](https://github.com/fHachenberg/pyecstaticalib/tree/17be427f02166a4077eae3e129e93cd6d10cea0f), notably [FANTLoad.py](https://github.com/fHachenberg/pyecstaticalib/blob/17be427f02166a4077eae3e129e93cd6d10cea0f/Ecstatica/FANTLoad.py), [LoadActor.py](https://github.com/fHachenberg/pyecstaticalib/blob/17be427f02166a4077eae3e129e93cd6d10cea0f/Ecstatica/LoadActor.py), [EventTypes.py](https://github.com/fHachenberg/pyecstaticalib/blob/17be427f02166a4077eae3e129e93cd6d10cea0f/Ecstatica/EventTypes.py), [PixelEncoding.py](https://github.com/fHachenberg/pyecstaticalib/blob/17be427f02166a4077eae3e129e93cd6d10cea0f/Ecstatica/PixelEncoding.py) and [Colour.py](https://github.com/fHachenberg/pyecstaticalib/blob/17be427f02166a4077eae3e129e93cd6d10cea0f/Ecstatica/Colour.py). That project is GPL-3.0. It was inspected as a format reference, not installed, executed or vendored into this repository. The new bounded parser is independently written around the observed wire format.

The older library contains incomplete runtime functions, Python-2-era constructs and ambiguous helpers. Its labels are therefore distinguished here from direct observations in the supplied files. Exact dimensions, parent edges, inventory counts, end offsets, the unsigned key-order check, store comparisons and the shade-depth equation were independently checked against this ISO's bytes.

Synthetic regression tests cover all truncated prefixes of a minimal FANT record, nonzero terminator operands, variable NEXT_SCENE attachments, unsupported versions/opcodes, script string padding, mixed-endian sound headers and negative lengths, parent cycles/missing parents, exact dimension offsets, nibble order/wrapping, depth scaling and pixel count limits. Real-disc validation additionally exercises every indexed record and every extracted visual file. These tests establish parser behavior and byte coverage; they do not validate a game-runtime emulator.

## Ecstatica II and direct ISO conversion

See [IMPORT.md](IMPORT.md) for the v55 character/action extension, direct ISO reader, CAT options, generated libraries and explicit conversion limits.
