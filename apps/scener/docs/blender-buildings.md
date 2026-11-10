# Blender building scripts in Scener

Review of the building pipeline in `nile-archive/tools` (`building_kit.py`, `buildings.py`,
`render_buildings_blender.py`, the per-game `build_scene.py`, about 4,500 lines of builders for three
games), and how much of it Scener already covers.

## What the Blender scripts do

1. **Kit** (`building_kit.py`): each building is a function drawing into a `Kit` in whole-tile
   coordinates (X = u, Y = -v, Z up). The kit collects triangles per material with a per-vertex colour,
   then `assemble()` builds meshes, a bevel modifier per material, lights and a shared isometric camera.
2. **Render** (`render_buildings_blender.py`, `buildings.py`): a Cycles sprite and a ground shadow per
   building, registered into the game. This stage is the game's sprite pipeline and has no Scener
   counterpart; Scener renders scenes and exports OBJ/glB.

Only stage 1 is modelling, and that is what ports.

## The port

| Files | |
|---|---|
| `tools/building_kit.py`, `tools/building_kit.lua` | the kit: box, beam, tube, lathe, ball, poly, frustum, pyramid, puff, palm, crate, basket, lights, frames |
| `tools/gen_nile_buildings.py`, `tools/gen_nile_buildings.lua` | palette, helpers and six builders: small pyramid, mud-brick house, pottery, bakery, watchtower, pylon gate |
| `scenes/nile_buildings.blks` | the generated scene, cameras `Overview` and one per building |
| `tests/test_nile_building_generators.py` | the Python and Lua output are byte-identical and match the committed scene |

```sh
python3 apps/scener/tools/gen_nile_buildings.py     # or: lua apps/scener/tools/gen_nile_buildings.lua
scener --render apps/scener/scenes/nile_buildings.blks --camera Overview --output-dir /tmp/nile
```

The kit draws with modifiers, not hard-coded primitives: a box is a `rect` or `rounded-rect` extruded and
bevelled, a battered block adds `taper`, a tube is a `circle` extruded and tapered along its axis, a sphere,
vase or pyramid is one profile revolved with `<lathe>` (a sphere is a half circle, shared by every ball and
scaled), a flag is a line extruded. Only `<group>` frames remain besides these.

The builders keep their Blender bodies: same coordinates, same calls. Blender's (x, y, z) tiles become
Scener's (x, z, -y) cm at 160 cm per tile, which keeps handedness (Blender's front, -Y, is Scener's +Z).
Frames (`k.at(...)`, wall faces for doors and reliefs) become nested `<group>`s.
Both generators share one small LCG so Python and Lua jitter colours identically. The Lua kit is plain
Lua 5.4 with no dependencies; Scener itself does not embed Lua, so for now it generates `.blks` files.

## Where Scener differs

| Blender kit | Scener | Effect |
|---|---|---|
| per-vertex colour (`col` attribute) | `gradientFrom`/`gradientTo` on a shape (bounding-box gizmo) | leaves and clay darken towards the ground; other gradients are authored per shape |
| free surfaces (`surface`, palm fronds, tiled roofs `pane`) | none | fronds are flattened revolved spheres; `pane`/`hip_roof` are not ported |
| triangle soup per material, one bevel modifier | `<rounded-rect>` + `<extrude>` + `<bevel>` per box | bevels follow the piece, edges parallel to depth only |
| `frustum` with different x and y insets | `<taper>` is uniform in both axes | the pylon's depth inset is 0.16 tile, not 0.10 |
| arbitrary 3D `poly` | `<shape>` + `<extrude>` in a vertical plane | enough for flags and cloth; arched window fills need a vertical plane too |
| footprint clamp after the fact | none | builders must stay inside their footprint |
| emissive materials, point lights | `unlit="1"` and `<light>` | same idea; light units differ and need tuning |

## How far from the rest

Done: kit core, six of 96 catalogue builders (`nile-archive`), both languages.

Not yet:
- `wall()`, `ring()`, `arch()`, `window()`: running-bond masonry is hundreds of boxes per wall; works as
  `<box>` but wants `<array>` or the wall element. `arch` maps to `<arch>`.
- `pane()` and `hip_roof()` (tiled roofs): a roof shell via the shape modifier stack (as in
  `gen_mini_forest_buildings.py`), shingles as studs.
- `surface()` consumers (fronds, banners, sails): `<loft>` along a bent path or `<bend>`.
- Remaining ~90 builders: mostly the same calls; each needs a visual pass. Relic Rush and Christmas
  Market use their own palettes and more kit.
- The sprite stage (Cycles render, shadows, registration) stays in Blender, or becomes `scener --render`
  with an orthographic camera if Scener gets one.

## Kenney palettes

Kenney's kits are textured from one `colormap.png`: columns of vertical gradients that faces are mapped
across by height. `tools/kenney_palette.py COLORMAP.png --name fantasy_town --blks … --lua … --py …`
reads them as swatches (bottom and top colour) and writes them three ways, ready in `palettes/` for the
fantasy-town, holiday and mini-forest kits:

- `kenney_<kit>.xml`: `<material id="fantasy_town_c01_b1" color="1 1 1" gradientFrom=… gradientTo=…/>`
  elements for a `.blks`, used as `material="…"` (a material's gradient applies unless the shape has its own);
- `kenney_<kit>.py` and `kenney_<kit>.lua`: tables for the kits. `building_kit.palette("fantasy_town")`
  (Python) or `K.palette("fantasy_town")` (Lua) returns them, and a swatch is accepted anywhere a kit
  takes a colour: `k.box("stone", …, palette["fantasy_town_c01_b1"])`.

Each swatch is taken over the middle 40 % of its height (`--span 0.3 0.7`), the part Kenney's own wall
faces map to. Scener applies one gradient over a shape's bounding box, where Kenney maps each face
separately, so tops of boxes take the top colour instead of their own.
