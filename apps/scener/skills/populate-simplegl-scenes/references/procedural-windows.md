# Procedural window recipes

Read the [window schema](scene-format.md#window) for the complete supported
presets, styles, parameter types/defaults, dimension constraints and limitations.
Use this guide to choose and place them. All spatial numbers below are
centimetres; rotations are degrees. Every complete scene declares Z up.

## Choose an example

| Need | Recipe |
|------|--------|
| Compare all three types and both styles | [Preset and style gallery](#preset-and-style-gallery) |
| Control every frame/pane/sill dimension and materials | [Recessed cottage window](#recessed-cottage-window) |
| Stack windows or leave an aperture open | [Stacked open windows](#stacked-open-windows) |
| Reuse windows on a rotated wall | [Prefab on a shared wall frame](#prefab-on-a-shared-wall-frame) |
| Keep an existing manually declared hole | [Insert into an existing opening](#insert-into-an-existing-opening) |
| Diagnose missing cuts, hidden sills or rejected parameters | [Troubleshooting](#troubleshooting) |

Save a complete `<scene>` block as a `.blks` file. The prefab recipe specifies
both files and their relative locations. Do not combine all recipes into one
scene; each demonstrates a separate authoring choice.

## Preset and style gallery

Save as `window-gallery.blks`. From left to right: round arch, rectangular
cottage, pointed Gothic. The bottom row is plain; the top row is storybook.
Both rows use the same dimensions, so the difference in frame thickness is
visible. Only the cottage defaults to a sill; its projection is enlarged here
so it reaches beyond the 32 cm wall. All six cuts come from the windows.

```xml
<scene up="z" ambient="0.32 0.35 0.4" background="dusk">
  <camera name="gallery" pos="0 -1150 340" look="0 0 290" fov="42"
          comment="Compare all three presets, plain below and storybook above." />
  <light pos="-260 -400 620" color="1 0.9 0.76" intensity="1.6" radius="1600" castShadows="1" />
  <wall length="560" height="580" thickness="32" rot="90 0 0" material="wall" />
  <window preset="round-arch" style="plain" pos="-180 0 150" rot="90 0 0" frameMaterial="wood" />
  <window preset="cottage" style="plain" pos="0 0 150" rot="90 0 0" sillProjection="24" frameMaterial="wood" />
  <window preset="gothic" style="plain" pos="180 0 150" rot="90 0 0" frameMaterial="stone" />
  <window preset="round-arch" style="storybook" pos="-180 0 440" rot="90 0 0" frameMaterial="wood" />
  <window preset="cottage" style="storybook" pos="0 0 440" rot="90 0 0" sillProjection="24" frameMaterial="wood" />
  <window preset="gothic" style="storybook" pos="180 0 440" rot="90 0 0" frameMaterial="stone" />
</scene>
```

Choose explicit width/height when fitting a story location. Presets do not infer
sizes from a wall. `style="storybook"` changes the default thickness only; it
works with Gothic and rectangular windows as well as round arches.

## Recessed cottage window

Save as `window-recess.blks`. This gives a 140 × 160 cm outer opening in a
40 cm wall. The window is centred through the wall and recessed 10 cm from
its front face: half the wall is 20 cm, half the frame depth is 10 cm.
The sill extends 24 cm beyond the frame front, hence 14 cm beyond the wall:
`20/2 + 24 - 40/2 = 14`. Its total width is `140 + 2*24 = 188` cm.

The pane is 2 cm thick at local Z=−6, within the valid offset range −9…+9.
With this rotation, local −Z points away from the front camera. The opening
bottom is at world Z=`150 - 160/2 = 70` cm; the sill extends nominally to Z=62.
Custom materials are defined at scene level. Setting frame/glass material IDs
avoids the default wood/glass colors overriding a bare `color` attribute.

```xml
<scene up="z" ambient="0.3 0.34 0.4" background="dusk">
  <material id="painted-teal" color="0.08 0.34 0.33" shininess="16" />
  <material id="blue-pane" color="0.3 0.53 0.7" shininess="70" />
  <camera name="recess" pos="260 -560 270" look="0 0 150" fov="42"
          comment="Inspect pane recess, frame depth and sill projection beyond the wall." />
  <light pos="-180 -300 400" color="1 0.9 0.76" intensity="1.5" radius="1100" castShadows="1" />
  <wall length="340" height="300" thickness="40" rot="90 0 0" material="wall" />
  <window name="shop-window" preset="cottage" style="storybook"
          pos="0 0 150" rot="90 0 0" scale="1 1 1"
          width="140" height="160" frameWidth="10" depth="20"
          frameMaterial="painted-teal" glassMaterial="blue-pane"
          pane="1" paneDepth="2" paneOffset="-6"
          sill="1" sillHeight="8" sillProjection="24"
          cutWalls="1" cutDepth="20" segments="32"
          castShadow="1" renderable="1" unlit="0" />
</scene>
```

`segments` is shown for completeness; it does not change a rectangular cottage
profile. `cutDepth="20"` is enough here because that volume intersects the wall;
it does not need to span all 40 cm. A surface-mounted frame entirely outside the
wall would require a larger matching depth or moving its centre inward.

## Stacked open windows

Save as `window-stack.blks`. Explicit instances can share a horizontal position
at different heights. `pane="0"` leaves the outside visible through each hole;
it does not remove the frame or stop the wall cut. No window primitive creates
a light, so the example supplies one. There is no `open="1"` sash parameter.

```xml
<scene up="z" ambient="0.32 0.35 0.4" background="noon">
  <camera name="stack" pos="180 -650 250" look="0 0 220" fov="44"
          comment="Verify both vertically stacked holes and the open apertures." />
  <light pos="-120 -260 460" color="1 0.94 0.82" intensity="1.5" radius="1200" castShadows="1" />
  <wall length="280" height="440" thickness="28" rot="90 0 0" material="wall" />
  <window name="lower" preset="round-arch" pos="0 0 110" rot="90 0 0"
          width="110" height="140" frameWidth="9" depth="18"
          pane="0" sill="0" frameMaterial="wood" segments="48" />
  <window name="upper" preset="gothic" pos="0 0 320" rot="90 0 0"
          width="100" height="180" frameWidth="9" depth="18"
          pane="0" sill="0" frameMaterial="stone" segments="48" />
</scene>
```

The lower opening spans Z=40…180; the upper spans Z=230…410, leaving 50 cm of
wall between them. Prefer architectural separation even though the cutter can
compute unions of overlapping holes.

## Prefab on a shared wall frame

Save the following as `prefabs/fixtures/round-window.blk` beside the scene's
directory. The prefab origin is the opening centre, local Y is height and
local +Z is front. Its window owns the cutter; do not add a negative shape.
The sill is off, so its outer bounds are exactly 120 × 180 cm in XY, depth
16 cm. Materials resolve from the containing scene or built-in presets.

```xml
<prefab>
  <window preset="round-arch" width="120" height="180"
          frameWidth="10" depth="16" sill="0" frameMaterial="wood" />
</prefab>
```

Save as `window-prefabs.blks`. Rotating the containing group applies equally
to its wall and both inserts. Children are authored in the wall's local
coordinates: `pos.y=160` puts their opening bottoms at 70 cm. The group maps
local Y up into world Z and turns the whole assembly 25 degrees about world Z.
Do not repeat that rotation on the children.

```xml
<scene up="z" ambient="0.32 0.35 0.4" background="dusk">
  <camera name="prefabs" pos="400 -780 320" look="0 0 150" fov="42"
          comment="Inspect two reusable windows on a rotated wall assembly." />
  <light pos="-100 -350 420" color="1 0.9 0.76" intensity="1.5" radius="1400" castShadows="1" />
  <group rot="90 0 25">
    <wall length="480" height="300" thickness="30" material="wall" />
    <prefab source="fixtures/round-window" name="left-window" pos="-130 160 0" />
    <prefab source="fixtures/round-window" name="right-window" pos="130 160 0" />
  </group>
</scene>
```

Use explicit instances as shown. An `<array>` on a window-containing prefab
currently repeats its visible geometry without repeating its cutters.
Do not use `attach` to position the window assembly. Translate and rotate the
whole prefab/group instead. To change the prefab's window dimensions or
materials, edit its inner `<window>`; attributes such as `width` on the outer
`<prefab>` instance do not forward as window parameters. Instance `scale`
transforms both the frame and cutter, including their thicknesses.

## Insert into an existing opening

Normally let the window cut its wall. If a rectangular opening must remain
explicitly authored, turn automatic cutting off for its insert.
Save as `window-existing-hole.blks`.

```xml
<scene up="z" ambient="0.32 0.35 0.4" background="dusk">
  <camera name="existing-hole" pos="100 -600 240" look="0 0 150" fov="40"
          comment="Check a procedural insert against one existing rectangular opening." />
  <light pos="-140 -300 380" color="1 0.92 0.8" intensity="1.5" radius="1100" castShadows="1" />
  <group rot="90 0 0">
    <wall length="400" height="300" thickness="32" material="wall">
      <opening type="window" x="140" width="120" height="140" sill="80" />
    </wall>
    <window preset="cottage" pos="0 150 0" width="120" height="140"
            depth="20" sillProjection="20" cutWalls="0" />
  </group>
</scene>
```

The insert centre is `(140 + 120/2 - 400/2, 80 + 140/2, 0) = (0,150,0)`.
`<opening sill="80">` is an elevation; `<window sill="1">` is a boolean for
whether a physical sill is generated. Do not confuse these attributes.
A round or pointed window inside that rectangular hole would leave corner gaps;
use automatic cutting for shaped openings.

## Troubleshooting

| Symptom | Check / action |
|---------|----------------|
| Wall covers the window | Use `<wall>`, not `<box>`; check `cutWalls`, matching-depth overlap, and parallel axes |
| Only one of several prefab copies cuts | Replace a prefab `<array>` with explicit instances |
| Corners gap around an arch | Remove the independently sized cutter; let `<window>` supply its outline |
| Sill disappears in the wall | Calculate its front extent and increase `sillProjection` or adjust recess |
| Geometry is absent | Read stderr; check preset spelling, arc height, inset size, pane containment and forbidden children/`attach` |
| Color changes have no effect | Set `frameMaterial` / `glassMaterial` to defined material IDs |
| No outside scene is visible | The pane is opaque; use `pane="0"` if an open aperture is intended |
| Frame is luminous when only glass should be | `unlit` affects the whole window; separate pane-only lighting is not a window option |
| Hiding the insert leaves a hole | `renderable` and `cutWalls` are independent |
| Agent proposes `bars`, `grille`, `open`, `archRise`, `style="cartoon"` | These are not implemented window parameters; use the supported schema |

## Validate and inspect

From the directory where a recipe was saved:

```sh
xmllint --noout window-gallery.blks
xmllint --xpath 'count(/scene/ambient | /scene/background)' window-gallery.blks
scener --list-cameras window-gallery.blks
scener --render window-gallery.blks --camera gallery --size 1200x1000 --format jpg --output-dir /tmp/window-gallery-review
```

The XPath count must be 0. Also run `xmllint --noout` on any prefab files.
Use the current build/deployment commands in [../../../CLI.md](../../../CLI.md)
when changing Scener code. The camera listing validates loading, not composition
or geometric fit: inspect the raster output and stderr. GPU-backed shadow
rendering is required for final review. Check the frame/pane junctions, pointed
tip, wall reveal, projecting sill and distinct neighbouring windows. For a new
wall placement inspect both its front and back to confirm the cut goes through.
The checked-in [three-camera fixture](../../../tests/procedural_windows.blks)
provides front, oblique and rear views for comparison.
