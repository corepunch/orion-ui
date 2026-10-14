# Demo scenes

Four scenes written entirely through the agent-facing CLI: each was authored
blind, validated with `scener --check`, measured with `scener --query` and
`scener --prefab-info`, and only then rendered. Run every command from the
Orion UI root after `make scener`.

| Scene | Shows |
|---|---|
| `round_table.blks` | `<let>` values and `{expr}` trigonometry placing chairs on a ring; a lathed pedestal table and vase; a procedural tiled `<floor>`; a cottage `<window>`; Y-up library prefabs (`items/mug`, `fixtures/hanging_lamp`) in a Z-up room |
| `chess_study.blks` | A board built from four `<array>` boxes; Staunton pieces as `<lathe>` profiles addressed by file and rank through `{S*(file - 3.5)}`; grouped assemblies (king cross, knight) that may interlock |
| `lighthouse_dusk.blks` | An exterior: tapered rock prisms, a lathed tower, gallery posts on a trigonometric ring, a lantern that is a real point light, a low sun with a shadowless fill |
| `evening_walk.blks` | A 7 s timeline: the biped preset walks with `<gait>` and waves with a `<layer>`; the kitten sits on a chair through `attach="chair:seat"`; a `follow` camera tracks the walk |

```sh
./build/bin/scener --check apps/scener/scenes/demos/chess_study.blks --json
./build/bin/scener --query apps/scener/scenes/demos/round_table.blks
./build/bin/scener --query apps/scener/scenes/demos/evening_walk.blks --time 2.6
./build/bin/scener --list-joints apps/scener/scenes/demos/evening_walk.blks --time 5.5
./build/bin/scener --render apps/scener/scenes/demos/lighthouse_dusk.blks --size 1536x1024 --format jpg --output-dir render/lighthouse
./build/bin/scener --render apps/scener/scenes/demos/evening_walk.blks --camera track --frames 0:7:24 --size 1280x720 --output render/walk.mp4
```

Lessons the checker taught while authoring these, worth knowing before writing
a scene from scratch:

- Cylinders, cones, prisms and lathes stand along local Y. In a Z-up scene give
  them `rot="90 0 0"`, or they lie down.
- `<taper amount="a">` scales the base by `1 - a` and the top by `1 + a`: a
  rock that stands on its wide end takes a negative amount, and either way the
  wide end reaches past the nominal radius.
- A camera inside a room must stay inside its walls: a position past a wall
  renders a flat, unlit frame with no diagnostic.
- Furniture on a rug stands on the rug's top, not through it; the checker
  reports the shared floor plane as z-fighting.
- Walls with `bottomTrimHeight` project a 1 cm skirting; keep shelves 2 cm off
  the wall face.
