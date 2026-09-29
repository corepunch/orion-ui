# Ecstatica ISO → Scener

`ecstatica_import.py` reads ISO 9660 directly with Python's standard library. It never mounts a disc, executes game code, or requires 7-Zip. It supports the supplied Ecstatica I FANT v30 and Ecstatica II FANT v55 images. Use `ecstatica_decode.py` for the older, exhaustive v30 scene/audio/background report; this importer deliberately reads the character/action prefix and repertoire data only.

```sh
python3 tools/ecstatica/ecstatica_import.py \
  '/path/to/ecstatica.iso' '/path/to/Ecstatica 2.iso' \
  --output apps/scener/imports/ecstatica
```

The output must be new or empty for each game; the importer refuses to overwrite existing work. `--actor 0` restricts conversion to source actor slot 0 (all indexed variants with that slot). Omit it for the complete actor library, including props. `--no-animations` keeps all original action JSON but omits playable animation scenes. `--duration 1.5` chooses an approximate duration for every imported clip; `--scale .207` chooses centimetres per raw source unit. Neither value is a recovered game-unit convention.

`--feature gloves=0`, `--feature boots=0`, `--feature hair=0`, `--feature fingers=1` and `--feature effects=1` override feature defaults. Repeated switches are supported. Fingers and effect/held-item volumes default off; the original records remain intact in JSON. The CAT adaptation also retains the original face/hair volumes under its head control.

## Generated layout

```
output/
  ecstatica1/                         # FANT v30
  ecstatica2/                         # FANT v55
    README.md                        # actor inventory and links
    manifest.json                    # provenance, coverage, features, clips
    disc-index.json                  # names, record hashes, repertoire links
    actions/0008-stherorun.json       # original commands and phase words
    actors/02500-0000-joe/            # record ID, actor slot, safe name
      source.json                    # exact part fields and original events
      conversion.json                # associations, mapping and clip limitations
      prefabs/source.blk             # original volumes, inferred source pose
      prefabs/cat.blk                # when the actor fits the biped adapter
      scenes/preview.blks            # three source-model camera views
      scenes/bare-hands.blks         # when source gloves are present
      scenes/cat-study.blks          # editable CAT adaptation
      scenes/cat-bare-hands.blks
      scenes/animations/*.blks       # linked experimental animation scenes
```

Keep every actor folder together: its scenes use the standard `scenes/` + `prefabs/` convention, so the folder is portable. Record IDs are part of the directory name because distinct records can share an actor slot or label. Filenames are sanitized; original labels stay in JSON.

```sh
scener apps/scener/imports/ecstatica/ecstatica2/actors/02500-0000-joe/scenes/preview.blks
scener apps/scener/imports/ecstatica/ecstatica2/actors/02500-0000-joe/scenes/bare-hands.blks
scener --render apps/scener/imports/ecstatica/ecstatica2/actors/02500-0000-joe/scenes/animations/0008-stherorun.blks \
  --camera Standing --frames 0:1:24 --size 1000x1000 --format png --output-dir /tmp/joe-run
```

The large generated library is a local artifact (about 1 GiB for both supplied discs), excluded from Git. The tool, tests, format notes and small review examples belong in Git. No ISO or extracted executable is copied into the repository.

## Cosmetic options in any CAT character

Options are per prefab, with per-instance overrides. Conditions select decorative volumes, keeping the underlying joints and IK endpoints intact. The standard biped preset now declares `gloves` off by default.

```xml
<!-- In a character prefab -->
<option name="gloves" enabled="1"/>
<ellipsoid on="0 0" at="0.5" radii="4 5 7" if-feature="gloves" color=".07 .055 .04"/>
<ellipsoid on="0 0" at="0.5" radii="4 5 7" unless-feature="gloves" color=".92 .63 .35"/>

<!-- In a scene: another instance can keep the default -->
<prefab source="cat" name="BareHands">
  <option name="gloves" enabled="0"/>
</prefab>
```

`enabled` is `0` or `1`; undeclared options and invalid values are diagnosed on stderr. Conditions apply to decorative ellipsoid, sphere, capsule, box, cylinder, cone, prism and torus volumes, not joints, groups or architectural cutters. Declarations and overrides survive scene/prefab saving. Boot switches affect imported boot covers; they do not redesign a foot into anatomically bare toes. The CAT rig's existing `fingers="1"` setting remains the switch for articulated finger joints.

## What is exact, and what is adapted

- **Exact decoded data:** part IDs, parent IDs, names, raw halfaxes/centres/offsets/rotations, palette indices, flags, triangle/point commands, all source action events, unsigned key phases and repertoire associations. Unknown v55 commands keep their numeric opcode and operands. Full byte consumption is claimed only for the parsed prefix, not for the unparsed scene/script/audio tail.
- **Source geometry preview:** uses each original ellipsoid's local halfaxes and centre, at one uniform scale. Zero axes receive a 0.35-source-unit thickness to avoid degenerate meshes. Triangle meshes, textures, and the original shade-map renderer are not reproduced. Palette indices are preserved, but displayed RGB values are an authored 16-colour approximation.
- **Source pose:** an inferred forward-kinematics model attaches each child at its parent's centre plus OFFSET. Angle words use a 65,536-unit turn with XYZ Euler order. Flag `0x2000` is treated as actor-relative orientation; flagged two-link limbs use end POSITION targets and an analytic solve. These interpretations produce useful previews but are not a verified implementation of either game's runtime. The source basis maps `(x,y,z)` to `(-x,-z,-y)`, followed by uniform scale, centering and a floor offset.
- **CAT adaptation:** a neutral, symmetric Base Human preset with source-derived core girths/limb lengths, original face/hair details, and optional costume pieces. Spine, attachments, some joint lengths and auxiliary masses remain authored preset choices. It supplies Scener's CAT role tags, gait, pose library and limb IK; it does not claim the source rest pose or exact full-body dimensions. Non-bipeds still receive source prefabs and data, but no misleading biped conversion.
- **Animations:** repertoire entries associate actors and action IDs. ROTATE, OFFSET and POSITION update the inferred source rig; keys are emitted as Scener joint transforms with linear quaternion interpolation. Each action's unsigned phase is normalized to the requested duration. Original frame rate, easing, transition rules, scripts, dynamic constraints, root travel, changes of shape/colour, and runtime visibility commands are not emulated. Every unsupported command and missing target part is counted in `conversion.json`; the exact command remains in the shared action JSON. Imported clips animate `source.blk`; `--cat-animations` additionally exports equivalent inferred motion to `controlled.blk`. The older neutral `cat.blk` is not a target. They are experimental previews, not faithful game playback.

The `effects` switch hides only the explicitly named `Left bb`, `Right bb` and `h_hold` accessory volumes. Flag `0x400` is unclassified and never used to hide geometry: villagers also set it on thighs, shins, calves and the pelvis. The earlier flag-based filter incorrectly hid valid anatomy and has been removed. All original flags remain in source.json.

## Ecstatica II findings

The supplied image has a 14,060-entry big-endian offset table, 8,082 occupied entries, and 5,254 indexed FANT records. FANT v55 adds a texture-name table after the ten v30 name tables. Its action and actor sections retain 10-byte, big-endian five-word events. Actor parts additionally carry opcodes 70 and 72; their values are preserved without inventing semantics. The parser accepts only the observed version/opcode range and rejects malformed/truncated input.

Protagonist `Joe`, actor slot 0 in record 2500, has 70 parts. His glove sleeves are parts 67 and 68 (`Left Glove`, `Right Glove`), each with halfaxes `(20,24,42)`; cuff parts 69 and 70 have `(28,28,22)`. Boot covers are parts 62 and 63. Their parents are forearms and shins respectively, so costume follows the articulated segment. Original finger/thumb pieces remain in the source record and can be enabled in the source preview; the default view uses simple hands.

The v30 baseline was informed by [Fabian Hachenberg's format research](https://github.com/fHachenberg/pyecstaticalib). No third-party implementation is imported or executed. The v55 extension and ISO reader were tested directly against the supplied discs.

## Validation

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools/ecstatica -p 'test_*.py'
make build/bin/scener build/bin/test_scener_input_test
DYLD_LIBRARY_PATH="$PWD/build/lib" ./build/bin/test_scener_input_test
```

Importer tests use a synthetic ISO, so CI does not need either game. Tests cover extent bounds, filename/version handling, malformed streams, unsigned phase ordering, matrix conversion, portable output references, feature defaults, unsupported-animation reporting and overwrite refusal. Scener tests cover default/overridden cosmetics, simultaneous gloved/bare instances, timeline rebuilding, unchanged joint transforms and save/reload.

Verified on the supplied discs: **2,246 actor records, 57,248 source parts, 2,002 actions, 9,333 experimental clips and 612 CAT adaptations**. Every generated XML file parsed successfully (2,858 prefabs and 12,504 scenes); source references, clip joint targets, finite transform values and bare-hand variants were checked across the full library. Action IDs were unique within each game.

Validation passed: 24 Python decoder/importer tests, 45 Scener tests, deployed CLI regressions and nine GPU shadow-regression views. Both protagonists were rendered on Apple M1, including five Ecstatica I walk samples and 25 Ecstatica II motion samples. This verifies usable previews, not equivalence with the original runtime. See [portable examples and library index](../../apps/scener/imports/README.md).

## Calibrated CAT controls

Complete biped chains also produce `prefabs/controlled.blk`, `scenes/controlled.blks`, and Bend/PresentLeft/PresentRight pose studies. These preserve the original ellipsoid volumes while adding semantic CAT controls; choose this export when the source appearance matters. The older `cat.blk` remains the neutral preset adaptation. Use `--cat-animations` to also write `scenes/cat-animations/*.blks` and `controlled_file` references in each clip report. `--no-animations` suppresses both animation exports. Degenerate/incomplete chains are not forced into this control rig.

See [volume-characters.md](../../apps/scener/docs/volume-characters.md) for the mapping, validation, authoring recipes and current limitations. Both local reference libraries were retained and extended with calibrated controls (91 Ecstatica I and 451 Ecstatica II actors).

[Multi-character rendered animations](../../apps/scener/imports/animation-review/README.md) include Joe, fuller/slender villagers and a goblin. `render_review.py` prepares these portable samples and native frame sequences; the GIF and MP4 encoders are documented alongside them.

### Missing shoulder connections

The slender villager has no decoded shoulder parts. Its upper-arm roots lie outside the narrow chest ellipsoid, leaving visible gaps during motion. Preview conversion now adds two explicitly reconstructed connecting ellipsoids, sized from arm girth and the chest surface, attached to the chest. They follow torso motion while covering the arm pivots. Original source parts and raw JSON remain unchanged; `preview_repairs` records the additions. This is an authored conversion repair, not recovered game geometry. Calibrated CAT rigs and the studio use the same repair.
