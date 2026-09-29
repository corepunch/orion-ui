# Decoded Ecstatica II reference actors

These JSON files are copied from the ISO importer's original `source.json` records. They retain source part IDs, fields, transforms, dimensions and provenance; the builder feeds them directly to `calibrated_cat`. No authored replacement face is inserted.

| File | Actor ID | Imported folder |
|---|---:|---|
| joe.json | 0 | 02500-0000-joe |
| villager-full.json | 65 | 02565-0065-w-vil1 |
| villager-belly.json | 66 | 02566-0066-w-vil2 |
| villager-slim.json | 67 | 02567-0067-w-vil3 |
| freegirl.json | 2656 | 05156-2656-freegirl |

Selected raw halfaxes (source order, before basis conversion):

| Part | Joe | Fuller villager w_vil1 | Slender villager w_vil3 |
|---|---|---|---|
| Body | 52,60,43 | 76,66,59 | 60,60,51 |
| Chest | 57,111,53 | 79,99,57 | 51,111,32 |
| Shoulder | 35,39,45 | 33,37,57 | See JSON |
| Upper arm | 21,26,72 | 28,34,66 | 15,14,72 |
| Thigh | 31,41,109 | 37,55,101 | 31,41,109 |
| Back | 95,54,32 | 109,54,32 | Absent |
| Pectoral | 62,64,55 | 66,80,69 | Absent |
| Head | 41,36,50 | 41,36,50 | 41,36,50 |
| Cheeks | 34,33,33 | 42,31,33 | 30,33,33 |
| Neck | 24,44,22 | 36,50,28 | 24,44,22 |

The fuller/round-bellied villagers include a `frame` primitive with halfaxes 84,74,78. Their dimensions are similar but their transforms differ: dimensions alone do not describe the silhouette. The builder borrows w_vil2's actual abdominal primitive and relative placement, not an invented sphere. Chest width/depth directions use the above measured ratios; other authored coefficients are explicit in `build_character.py`.

The E2 names table contains weeboy (4064), boy (4065), girl (4066) and homboy1 (4094). Corresponding indexed records 6564,6565,6566,6594 are absent (`0xffffffff`); the main FANT actor stream did not provide alternatives. Therefore no recovered child model is claimed. `youth` is an authored proportion change that preserves the chosen source face.

Raw converted models and animation evidence remain in `apps/scener/imports/` and the ignored full import library. See `tools/ecstatica/IMPORT.md` for decoding and conversion limits.

## Frame versus height

Measured from the calibrated source controls, in imported centimetres (not canonical game-world human heights):

| Source | Height | Head units | Shoulder joint span | Hip joint span |
|---|---:|---:|---:|---:|
| Joe | 152.7 | 5.68 | 24.8 | 17.4 |
| Fuller villager | 132.0 | 4.61 | 30.2 | 25.0 |
| Round-bellied villager | 130.2 | 4.86 | 30.2 | 25.0 |
| Slender villager | 155.4 | 6.32 | 21.7 | 19.5 |
| freegirl | 147.2 | 6.87 | 26.1 | 12.4 |

These are stylized actor differences, not evidence of a biological height-to-width law. The taller slender villager has narrower shoulders than the shorter fuller villager. This supports separate height and frame parameters. A broad frame remains broad with muscle at its minimum. The .7–1.6 frame range is an authored control, not a decoded Ecstatica morph parameter.

### Missing shoulder connections

The slender villager has no decoded shoulder parts. Its upper-arm roots lie outside the narrow chest ellipsoid, leaving visible gaps during motion. Preview conversion now adds two explicitly reconstructed connecting ellipsoids, sized from arm girth and the chest surface, attached to the chest. They follow torso motion while covering the arm pivots. Original source parts and raw JSON remain unchanged; `preview_repairs` records the additions. This is an authored conversion repair, not recovered game geometry. Calibrated CAT rigs and the studio use the same repair.
