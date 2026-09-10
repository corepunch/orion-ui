# Layout and validation reference

## Coordinate model

Author lengths in centimetres and new scenes with `<scene up="z">`. Primitive
local axes do not change: a wall/window still uses local X for width, local Y
for height and local Z for thickness. Rotate `rot="90 0 0"` to map that height
to world Z; the window's front then faces world −Y. Boxes remain centred on all
axes, so a Z-up box rests on Z=0 with `pos.z = size.z / 2`.

Transforms are hierarchical. A shared wall frame avoids repeating world-space
rotations on every insert:

```xml
<scene up="z" ambient="0.3 0.3 0.35" background="dusk">
  <group pos="-400 0 0" rot="90 0 25">
    <wall length="600" height="280" thickness="32" material="wall" />
    <window preset="round-arch" pos="-30 170 0" width="140" height="160"
            frameWidth="10" depth="20" frameMaterial="wood" />
  </group>
</scene>
```

Both children inherit the same orientation. The window's local opening bottom
is `170 - 160/2 = 90` cm; the wall starts at local Y=0. Its automatic cutter
uses the window's outer profile. Do not add a separate cutter or `<opening>`.

## Wall opening calculations

A wall's origin is at its base, centred along its length. A window's origin is
at the centre of its outer opening rectangle. For a legacy rectangular child
`<opening>`, X is measured from the wall's left edge. With wall length L,
opening start x, width w, bottom elevation s and height h:

```text
window centre in wall coordinates = (x + w/2 - L/2, s + h/2, 0)
window outer dimensions = (w, h)
```

Prefer a procedural `<window>` for fixed rectangular, round-arch and Gothic
windows. It owns the visible frame and exact cutter, and can cut the full wall
thickness when its matching depth volume merely overlaps the wall slab. Use
`cutWalls="0"` only when inserting it into an independently authored hole.
For full parameter bounds, sill projection calculations and examples, read the
[window schema](scene-format.md#window) and
[recipes](procedural-windows.md). Keep frame/sill/pane configuration on the
window, and share transforms through a group or prefab.

Custom doors or other inserts may still require a separate negative shape in
the same prefab. A legacy negative box must span both wall faces; this differs
from the procedural window's depth-overlap matching. For example:

```xml
<prefab>
  <bool-negative-box size="100 210 34" />
  <box size="100 210 8" material="wood" />
</prefab>
```

This is a rectangular custom insert centred on its opening, with local +Z as
front. Its cutter fits a 32 cm wall, with 1 cm excess on each side. Keep its
cutter within the wall's X/Y extents and do not duplicate it as a child
`<opening>`. A round or pointed insert needs a matching outline; do not hide
corner gaps with bars. Window profile cuts support overlapping/stacked openings
and clipping at wall edges. Wall cutters are not arbitrary mesh CSG.

## Primitive placement

- Box: centered on all axes. Rest on a surface using half its Y size.
- Sphere: centered at `pos`; rest on a surface using `pos.y = radius`.
- Cylinder/prism/cone/pyramid: centered along Y; rest using half `height`.
- Torus: centered at `pos`, lying in the XZ plane.
- Wall: base is at local `y=0`; length extends symmetrically around local X after the wall transform.
- Window: centred on its outer opening rectangle; optional sill extends below and beyond it. In Z-up scenes rotate its local height into world Z. See the window schema for frame/pane/sill bounds.

For assemblies, calculate positions from declared dimensions. Avoid visually tuned constants until the structural dimensions are correct.

Use 0.1 cm (`0.001` internal scene units) as the default contact tolerance. For a nominally grounded or connected part, the absolute difference between its lower/upper surface and the target surface must not exceed that tolerance. Small gaps such as 0.5 cm (`0.005` internal units) are errors, not harmless rounding.

Do not overlap visible faces on the same plane. A depth buffer cannot consistently decide which coplanar fragment owns a pixel, so the result flickers or forms striped patches as the camera moves. Build assemblies from non-overlapping exterior regions: for example, fit a sofa base and backrest between its arms instead of extending all three boxes across the same front or side planes. Adjacent parts may share an edge, and hidden structural intersections are acceptable only when none of their exterior faces overlap. Do not use tiny offsets or polygon offset to conceal unintended duplicate geometry.

## Visual relationships and termination

Classify nearby geometry before spacing it:

- **Assembly contact:** Parts that construct or operate as one object, such as a table and its chairs, a window frame and mullions, or a lamp and cord, may touch or overlap where the relationship is intentional.
- **Independent objects:** Unrelated fixtures, furniture, and decorations must retain readable negative space. Do not let their bounds, silhouettes, or cast shadows touch accidentally; tangency makes separate objects read as one malformed assembly.

Make every exposed linear member terminate against an intended support. Bars,
mullions, rails, legs, and cords must not stop visibly inside open space. Keep a
nominal contact within 0.1 cm (`0.001` internal units). For a rectangular member meeting a
curved frame, calculate the boundary at the member's outermost edge so both
corners reach or enter the support without leaving a visible gap. For a circular
boundary centered at `(cx, cy)` with radius `r`, the upper intersection at local
X coordinate `x` is:

```text
y = cy + sqrt(r*r - (x - cx)*(x - cx))
```

Use the member edge nearest the tighter part of the curve, not only its center
line. Permit a small hidden penetration into the support when necessary, but do
not overshoot through its visible exterior face.

Check independent-object spacing in the rendered view as well as world space.
Perspective can close a valid three-dimensional gap, and cast shadows can merge
otherwise separate silhouettes. Start with a projected gap at least as wide as
the smaller object's nearby trim or structural member, then enlarge it until the
separation remains obvious in every affected story camera. Treat deliberate
occlusion as a composition choice and verify that it does not imply a false
physical connection.

## Attach points and pivot offset

Use semantic subfolders to organize authored prefab families and composite
objects. Keep generic furniture and items independent, then reference them from
room-specific composites such as `workshop/desks/main.blk` or
`workshop/commode/stocked.blk`. Do not encode hierarchy with underscore
prefixes in a flat filename.

Use `attach="instanceName:slotName"` on supported shapes or prefabs (not procedural windows or window-containing assemblies) to place it at a prefab's named reference point without manual surface-height calculations. The target instance must carry a `name` attribute.

```xml
<prefab source="furniture/dining_table" name="dining_table" pos="0 0 -150"/>
<sphere attach="dining_table:center" pos="0 14 0" radius="14" material="wood"/>
```

The object inherits the named attach frame; its own `pos`, `rot` and `scale` remain local offsets within that frame. Account for the object's origin: a centred sphere needs an upward offset equal to its radius to rest on the surface.

Place a general-purpose surface attach at the usable surface center. Name it
`top_surface` for the primary work or table surface. Name additional named
slots by location: `under_center` for the clearance volume beneath a bench,
`shelf_lower` and `shelf_upper` for tiered storage, or `edge_n`/`edge_s` for
perimeter positions. Do not put the default shelf/table attach on its front
edge: a child prefab is positioned by its origin, so doing so centers half of
the child's footprint outside the support. If a composition needs multiple
offsets, place the support and props inside one local `<group>`, use the
surface height as each child's baseline, and author local X/Z offsets from the
surface center.

Before accepting a supported prop, transform all footprint corners by its
scale and yaw and verify they remain within the support rectangle with a small
visible margin. Checking only the origin is insufficient, especially after
rotation.

For shelves, desks, and active work surfaces, avoid mechanically uniform rows.
Use small deterministic differences in yaw, spacing, depth, and scale while
maintaining exact vertical contact and non-intersection. Rotate asymmetric
props enough to read but not so far that they overhang. A cylinder does not
visibly change under Y rotation; vary its position or neighboring silhouette
instead. Use X/Z tilt only when the prefab origin is a plausible contact pivot
and the resulting footprint does not penetrate the support.

Treat visible storage volume as part of the authored object. A commode with an
open lower bay, a cubby wall, or a dressed desk should usually be a composite
prefab that owns smaller item-prefab instances. Stock the volume at multiple
depths and heights while keeping item footprints inside the support.

Use `pivotOffset` to rotate a shape around an edge instead of its center. The offset is in local space, applied before rotation:

```xml
<box size="30 2 20" rot="0 0 45" pivotOffset="-15 0 0"/>
```

## Rotation and facing

`rot="rx ry rz"` applies X, then Y, then Z rotations. Positive Y rotation maps local +Z toward world +X and local +X toward world -Z.

Current directional prefabs:

| Prefab | Default front | Footprint |
|---|---|---|
| `chair` | +Z | approximately 45 × 45 cm |
| `sofa` | +Z | approximately 220 × 90 cm |
| `coffee_table` | none | 120 × 70 cm |
| `dining_table` | none | 160 × 100 cm |

For an object facing +Z by default:

| Y rotation | Resulting front |
|---:|---|
| `0` | +Z |
| `90` | +X |
| `-90` | -X |
| `180` | -Z |

For a prefab whose default front is local +Z and a target direction `(dx, dz)`, use `rot.y = atan2(dx, dz)` converted to degrees. Cardinal rotations are preferable when the intended direction is cardinal; otherwise calculate the angle rather than eyeballing it.

## Materials and renderer constraints

Built-in preset materials are always available: `wall`, `floor`, `wood`, `metal`, `glass`. Reference them without defining `<material>` tags. Define custom materials explicitly; a scene-defined `<material>` with the same `id` overrides a preset.

Built-in background presets: `midnight`, `twilight`, `dusk`, `dawn`, `overcast`, `noon`, `neutral`, `black`. Use `<scene background="dusk">` for presets or `<scene background="r g b">` for a custom color.

Author material, shape, ambient, background, light, sun, unlit-emitter, and
dummy colors as sRGB `0..1` values. Do not manually linearize them. Light
`intensity` is a separate linear scalar and must not be gamma-corrected or
folded into `color`; keep the color within `0..1` and raise `intensity` when a
source must be brighter than white. The renderer converts colors once before
linear lighting and automatically encodes the result when writing the sRGB
framebuffer. See
[scene-format.md](scene-format.md#color-space-and-numeric-units) for the full
attribute table.

Materials provide diffuse color and shininess only. There is no transparency or texture support. A material named `glass` renders as an opaque shiny surface; make it thin, and do not promise transparent glass.

Use the default shadow casting for floors, panes, and decorative surfaces. Reserve `castShadow="0"` for self-luminous emitters or explicitly documented non-physical helper geometry. Use `castShadows="0"` on a light for an unshadowed additive light.

Use `renderable="0" castShadow="1"` for scene boundaries that must remain invisible to the camera while still blocking light and contributing to stencil shadow volumes.

Use the `<array>` modifier to create repeating geometry (books, shelves, stairs). It duplicates the mesh `count` times with per-step `translation` and `rotation`, producing compact scene files for repetitive structures.

## Interior enclosure and lighting

Treat a room as a complete shell: floor, walls, and ceiling or roof. In the common box-room case, place the ceiling so its lower face meets the wall tops. Omit it only for an explicitly open or roofless design. Keep plan cameras below a visible ceiling so they continue to show the interior.

When cameras need to view a closed room from outside, mark the camera-facing wall
`renderable="0" castShadow="1"`. It stays invisible to the viewer but continues
blocking directional light and contributing to stencil shadow volumes so the
interior lighting remains correct:

```xml
<wall pos="0 0 0" length="1000" height="420" thickness="20"
      material="plaster" renderable="0" castShadow="1"/>
```

Follow the enclosed-room pattern in `scenes/sample_room.blks`: combine a low ambient base with at least one motivated, shadow-casting key light. A room must contain light sources that shape the space, not merely enough ambient illumination to avoid black pixels.

- Put a reusable practical point light inside the same prefab as its fixture geometry. Place it inside the bulb or flame, below the ceiling and on the emitting side of any opaque shade.
- Mark the visible emitter `unlit="1" castShadow="0"`: unlit keeps its authored bright color, while disabling shadow casting prevents it from occluding its own point light. Keep the shade and fixture body shadow-casting.
- Use a window, doorway, or second practical to motivate a weaker fill or rim. Keep it subordinate to the key so shadows remain dramatic.
- Aim a directional sun or moon light through the corresponding opening rather than through a solid wall or ceiling. Point`dir` **45–60 degrees below horizontal** and offset it **15–45 degrees horizontally from the wall axis**: `dir="-0.6 -1 1"` for 45° down with 30° offset, `dir="0 -1.7 1"` for ~60° down. Never let either horizontal component be zero — shadows parallel to walls read as flat and uninteresting.
- Confirm important characters, props, and interactions receive both readable illumination and grounding cast shadows.
- Avoid lifting ambient light until shadows disappear. Correct the key position, intensity, and motivated fill first.
- Compare and tune light intensities as linear multipliers. Do not apply gamma
  compensation to an intensity because the displayed result already receives
  sRGB encoding after lighting.

## Camera declarations

Aim cameras at useful targets, not arbitrary Euler directions. Keep the near plane away from geometry.

Give each `<camera>` a `comment` describing its purpose. The active
`--list-cameras` command prints names only; read comments from the XML to
choose the intended composition:

```sh
scener --list-cameras scenes/scene.blks
```

Example output:

```text
Main
Top
Close
```

Use descriptive comments: a person or agent reading the list should understand what each camera shows and when to select it.

## Review checklist

Before completion, verify:

1. Every material reference resolves in the containing scene.
2. Every opening lies within its wall and does not overlap another opening unexpectedly.
3. Every insert uses the wall's local frame and fits inside the opening.
4. Every grounded object touches the intended surface without penetrating it.
5. Every directional prefab faces its target.
6. Repeated objects use prefabs or groups rather than divergent copies.
7. Exterior face extents do not overlap on the same plane.
8. Every interior has its intended ceiling or roof, and overhead cameras remain inside the shell.
9. Every room has a motivated shadow-casting key; reusable practicals own prefab-local point lights, emitters are unlit and shadow-free, and hero subjects remain readable.
10. Every supported prop's transformed footprint stays inside its surface; default surface attach points are centered rather than placed on an edge.
11. Lived-in prop clusters use deliberate variation without floating, penetration, overlap, or accidental overhang.
12. Edited XML files pass `xmllint --noout`.
13. Every practical point light remains inside its emitter and below the shade lip after instance transforms and scale.
14. The scene loads with `scener --list-cameras`; code changes also build and pass relevant tests.
15. Each procedural window matches a parallel wall slab and owns its exact outer-profile cut. Legacy custom-insert cutters must cross their wall completely. No duplicate cuts or accidental frame/reveal gaps remain.
16. Every visible RGB value is authored directly as sRGB, while light
    intensity remains a separate, unmodified linear scalar.
17. Every exposed bar, mullion, rail, leg, cord, or similar member terminates cleanly against its intended support without a visible floating endpoint or exterior overshoot.
18. Unrelated neighboring objects retain readable negative space in every affected story camera; no accidental overlap, silhouette tangency, or shadow merger makes them appear grouped.
