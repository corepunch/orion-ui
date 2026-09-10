# SimpleGL XML scene format reference

SimpleGL reads scenes from XML files. The root node is `<scene>`, which accepts scene-wide attributes and contains a mix of config tags and shape objects.

This is the canonical schema reference for Scener XML. Build and inspect the
current command syntax from the Orion repository root:

```sh
make scener
./build/bin/scener --help
./build/bin/scener --version
```

## File types and element placement

| File | Root | Purpose | Allowed direct children |
|------|------|---------|-------------------------|
| `.blks` | `<scene>` | Complete scene/room | Config tags, reusable profiles, renderable content, groups, prefab instances, cutters, overlays |
| `.blk` | `<prefab>` | Reusable object or assembly | `<attach>`, `<shape>`, renderable content, nested groups/prefabs, cutters, overlays |

A `.blks` scene is the authority for cameras, ambient/background color,
materials, character definitions, and directional lights. A `.blk` prefab is
authored in a stable local coordinate frame and obtains materials from its
containing scene. It can be opened directly in Scener for editing; Scener then
provides a default camera and preview lights.

The complete supported element inventory is:

| Placement | Elements |
|-----------|----------|
| `<scene>` attributes | `ambient`, `background`, `up`, `convention` |
| Scene configuration | `<camera>`, `<material>`, `<sun>`, `<chardef>`, `<shape>` |
| Transformable content | `<box>`, `<sphere>`, `<cylinder>`, `<capsule>`, `<arch>`, `<prism>`, `<cone>`, `<pyramid>`, `<torus>`, `<lathe>`, `<loft>`, `<wall>`, `<window>`, `<door>`, `<group>`, `<prefab>`, `<light>`, `<line>`, `<dummy>` |
| Wall cutters | `<bool-negative-box>`, `<bool-negative-arch>`, `<bool-negative-cylinder>` |
| Mesh modifiers | `<taper>`, `<twist>`, `<bend>`, `<stretch>`, `<skew>`, `<array>`, `<extrude>`, `<mirror>`, `<noise>`, `<shell>` |
| Context-only children | `<camera><transform>`, `<shape><v>`, `<prefab><attach>` |

Unknown elements produce an `unsupported XML element` warning. Element names
and attribute names are case-sensitive.

## Build, edit, validate, and render

```sh
xmllint --noout apps/scener/scenes/sample_room.blks
make scener
./build/bin/scener --list-cameras apps/scener/scenes/sample_room.blks
./build/bin/scener --render apps/scener/scenes/sample_room.blks
./build/bin/scener --render apps/scener/scenes/sample_room.blks \
  --size 1600x900 --camera Main --output-dir render/sample-room
```

After changing a referenced `.blk`, re-run the same `.blks` render command;
prefabs are loaded from disk for each Scener process. Batch rendering defaults
to `1280x800`, all cameras, JPEG output, and the current directory. It
uses a hidden OpenGL context and returns nonzero on load, camera, context, or
write failure.

For scenes authored against the 3ds Max conversion on upstream main, use
`<scene convention="3dsmax">`: position/rotation `(x,y,z)` maps to `(x,z,-y)`
and box size maps to `(x,z,y)` in the Y-up renderer. This takes precedence
over `up`. Omit it to keep native axes and the explicit `up="z"` view mode.

## Quick example

```xml
<scene ambient="0.10 0.10 0.13" background="0.04 0.05 0.07">
  <camera name="Main" comment="Default forward view" pos="0 200 600" look="0 100 0" fov="60"/>

  <light pos="0 400 0" color="1.0 0.95 0.85" intensity="1.3" castShadows="1"/>

  <material id="red" color="0.8 0.2 0.2" shininess="20"/>

  <box pos="0 0 0" size="200 200 200" material="red"/>
  <sphere pos="300 100 0" radius="80" color="0.2 0.5 1.0"/>
</scene>
```

## Color space and numeric units

Scene files use author-facing sRGB values for every visible RGB color. Enter
the values shown by an ordinary sRGB color picker; do not convert them before
writing XML. The renderer stores those authored values unchanged while loading
the scene, converts them to linear values at the rendering boundary, performs
ambient and per-light calculations in linear space, then lets the sRGB
framebuffer encode the result for display.

| XML data | Authored meaning | Renderer treatment |
|----------|------------------|--------------------|
| `ambient`, `background`, `up`, `convention` | sRGB color | Converted once to linear |
| Material, shape, prefab, unlit, and dummy `color` | sRGB color | Converted once to linear |
| Light and sun `color` | sRGB color/chromaticity | Converted once to linear |
| Light and sun `intensity` | Linear scalar | Never color-converted |
| Position, direction, radius, shininess, transforms | Linear/non-color data | Never color-converted |

Keep color channels normally within `0..1` and use light `intensity` for values
brighter than white. Do not pre-linearize colors or gamma-correct intensity to
compensate for display appearance; either operation causes a double conversion
or mixes two different units. This is the same authoring model commonly used
by linear-space engines: colors are entered as sRGB while shaders receive
linear colors.

Colors remain normalized RGB triples rather than `#RRGGBB` strings. The triple
format is explicit for sRGB authoring, works naturally for lights and ambient
values, and keeps the existing parser/schema simple. Hex input could be added
later as optional syntax, but it should not replace the current representation.

An `unlit="1"` object still follows the color pipeline: its authored sRGB color
is converted to linear before drawing and encoded back to sRGB by the
framebuffer. “Unlit” skips illumination; it does not mean “already linear.”

All spatial values are authored in centimeters (cm). This includes positions,
look-at targets, sizes, radii, dimensions, wall measurements, pivot offsets,
attachment positions, array translations, shape vertices, and spatial geometry
modifier amounts. Rotations remain degrees, scales remain unitless, normalized
deformation amounts remain dimensionless, and colors remain normalized sRGB
triples. The renderer converts centimeter spatial values to its
internal scene units while loading. For example, `pos="0 150 400"` means
`(0 cm, 150 cm, 400 cm)`, and a 30 cm character uses `height="30"`.

## Scene root attributes

The `<scene>` element accepts `ambient` and `background` attributes. These are
not child elements. Never write `<ambient color="..."/>` or
`<background id="..."/>`; the parser ignores those nodes without reporting an
error and uses its defaults instead.

```xml
<!-- correct -->
<scene ambient="0.10 0.10 0.13" background="midnight">
```

```xml
<!-- wrong: both nodes are silently ignored -->
<scene>
  <ambient color="0.10 0.10 0.13"/>
  <background id="midnight"/>
</scene>
```

### `ambient` (attribute of `<scene>`)

Global ambient light color applied to all objects before per-light additive passes. A vec3 value.

| Attribute | Type | Default           | Description |
|-----------|------|-------------------|-------------|
| `ambient` | vec3 | 0.12 0.12 0.14    | Global ambient light, authored as sRGB color |

### `background` (attribute of `<scene>`)

Background clear color. Accepts either a preset ID (one-word string, no spaces) or a raw vec3 color.

| Attribute    | Type   | Default         | Description |
|--------------|--------|-----------------|-------------|
| `background` | string/vec3 | 0.08 0.10 0.14  | Preset ID or custom sRGB clear color |

Preset background IDs:

| Preset     | Color           | Notes |
|------------|-----------------|-------|
| `midnight` | 0.02 0.03 0.07 | Deep night blue |
| `twilight` | 0.06 0.05 0.10 | Purple-blue, post-sunset |
| `dusk`     | 0.08 0.10 0.14 | Dark blue-grey |
| `dawn`     | 0.16 0.10 0.14 | Warm dusky pink |
| `overcast` | 0.25 0.27 0.30 | Flat cloudy grey |
| `noon`     | 0.40 0.48 0.64 | Bright sky blue |
| `neutral`  | 0.18 0.20 0.24 | Mid-grey |
| `black`    | 0.00 0.00 0.00 | Pure black for compositing |

```xml
<scene ambient="0.06 0.07 0.10" background="midnight">
```
```xml
<scene ambient="0.10 0.10 0.13" background="0.04 0.05 0.07">
```

## Config tags (top-level children of `<scene>`)

### World up axis

`<scene up="y">` (the default) uses Y up; `<scene up="z">` uses Z up for
camera views, interactive navigation, ground placement and diagnostic layouts.
This never rearranges geometry coordinates, rotations or primitive local axes.
For Z-up scenes, cylinders and walls still have local Y height; rotate them
`rot="90 0 0"` when that local height must point along world Z. Box size always
remains local X/Y/Z. The scene's horizontal compass mapping is authored separately.

### `<camera>`

Defines a viewpoint. Multiple cameras are allowed; select one with
`--camera Name` or inspect them with `--list-cameras`.

| Attribute | Type   | Default  | Description |
|-----------|--------|----------|-------------|
| `name`    | string | Camera1  | Camera identifier |
| `comment` | string | (empty)  | Human-readable description shown by `--list-cameras` |
| `pos`     | vec3   | 0 160 500  | Eye position in cm |
| `look`    | vec3   | 0 120 0  | Look-at target in cm |
| `fov`     | float  | 60       | Vertical FOV in degrees |

When no `<camera>` tag is present, a default "Camera1" is created with the defaults above.

A camera may contain additive `<transform>` overrides for named shapes, groups,
or prefab instances. The target keeps its authored transform in every other
camera. Override translation is local to the target, rotation uses the target's
authored `pivotOffset`, and scale multiplies the authored scale. Camera changes
restore the scene from XML before applying the selected shot's overrides, so a
pose cannot leak between shots.

| Attribute | Type | Default | Description |
|-----------|------|---------|-------------|
| `target` | string | (required) | `name` of the shape, group, or prefab to transform |
| `pos` | vec3 | 0 0 0 | Additive local translation |
| `rot` | vec3 | 0 0 0 | Additive local Euler rotation |
| `scale` | vec3 | 1 1 1 | Multiplicative local scale |

```xml
<camera name="ClockReveal" pos="0 100 400" look="0 200 0" fov="48">
  <transform target="secret_clock" rot="0 118 0"/>
</camera>
<prefab source="workshop/clock" name="secret_clock"
        pos="0 200 0" pivotOffset="70 0 0"/>
```

### `<material>`

Named material referenced by shapes via the `material` attribute. Scene-defined materials override built-in presets of the same name.

| Attribute    | Type   | Default        | Description |
|--------------|--------|----------------|-------------|
| `id`         | string | (required)     | Unique material name |
| `color`      | vec3   | 0.8 0.8 0.8    | Diffuse sRGB colour |
| `shininess`  | float  | 8.0            | Phong specular exponent |

Built-in presets (always available, no `<material>` tag needed):

| Preset    | Color           | Shininess | Notes |
|-----------|-----------------|-----------|-------|
| `wall`    | 0.80 0.78 0.72 | 6         | Warm off-white interior |
| `floor`   | 0.35 0.28 0.22 | 12        | Dark wood floor |
| `wood`    | 0.50 0.32 0.18 | 20        | Mid-brown furniture wood |
| `metal`   | 0.70 0.70 0.75 | 60        | Grey metallic |
| `glass`   | 0.65 0.80 0.85 | 90        | Blue-tinted shiny |
| `stone`   | 0.38 0.36 0.33 | 8         | Grey stone block |
| `concrete`| 0.52 0.50 0.46 | 4         | Matte grey |
| `plaster` | 0.90 0.88 0.80 | 3         | Warm flat white |
| `bronze`  | 0.48 0.30 0.14 | 40        | Dark brown metallic |
| `iron`    | 0.28 0.28 0.30 | 55        | Dark grey metallic |

To override a preset, define a `<material>` with the same `id` in the scene. Unlisted materials (e.g. `brass`, `slate`, `fabric`) must be defined explicitly.

### `<chardef>`

Defines reusable character-gizmo proportions for `<dummy type="character"
ref="...">`. Declare it as a top-level scene child.

| Attribute | Type | Default | Description |
|-----------|------|---------|-------------|
| `name` | string | (required) | Definition name referenced by a character dummy |
| `height` | float | 100 | Baseline-to-head height in cm |
| `radius` | float | 10 | Head and landmark radius basis in cm |
| `top` | float | 1.0 | Head-center height as a fraction of `height` |
| `neck` | float | 0.75 | Shoulder/neck height fraction |
| `pelvis` | float | 0.25 | Pelvis height fraction |
| `feet` | float | 0.0 | Foot landmark height fraction |

### `<shape>`

Defines a reusable 2D profile for `<lathe>` and `<loft>`. The profile is
constructed in the XY plane as a sequence of `(x, y)` vertices. Shapes may
appear at the top level of a scene or prefab file; use them to model
cross-sections and sweep paths that can be shared by many objects.

| Attribute | Type | Default | Description |
|-----------|------|---------|-------------|
| `id`      | string | (required) | Unique name, referenced by lathe/loft |
| `closed`  | int | 0 | 1 if the profile closes back to its first point (for loft cross-sections) |

Child `<v>` elements define vertices in order. Each vertex is a single point on
the profile curve. The lathe sweeper reads them top-to-bottom; the loft sweeper
projects the profile into world space along the chosen axis.

| Attribute | Type | Default | Description |
|-----------|------|---------|-------------|
| `x`       | float | 0 | Horizontal distance from the axis in the profile plane |
| `y`       | float | 0 | Vertical position along the profile |

**Lathe profiles must be open** (`closed="0"`); the first vertex defines the
bottom cap radius and the last vertex defines the top cap radius. The profile
starts at the minimum Y and ends at the maximum Y of the shape; intermediate
vertices define the contour in between. Vertices at `x="0"` produce a closed
tip (the lathe creates a single center point for the cap at that Y).

**Loft cross-section profiles should be closed** (`closed="1"`) when they
represent a closed tube cross-section (e.g. a circle for a pipe or torus). Open
cross-sections (ribbons) are supported but less common.

**Loft path profiles** map the shape's XY to the world XZ plane (Y=0) to form
a 3D sweep path. The path can be open or closed.

Example — a bottle profile for lathe (open, from bottom at y=0 to neck at y=3):

```xml
<shape id="bottle" closed="0">
  <v x="0" y="0"/>
  <v x="40" y="0"/>
  <v x="40" y="30"/>
  <v x="25" y="160"/>
  <v x="12" y="270"/>
  <v x="8" y="300"/>
  <v x="0" y="300"/>
</shape>
```

Example — a circle for loft cross-section (closed, radius 0.15, 8 segments):

```xml
<shape id="circle_small" closed="1">
  <v x="15" y="0"/>
  <v x="10.6" y="10.6"/>
  <v x="0" y="15"/>
  <v x="-10.6" y="10.6"/>
  <v x="-15" y="0"/>
  <v x="-10.6" y="-10.6"/>
  <v x="0" y="-15"/>
  <v x="10.6" y="-10.6"/>
</shape>
```

Example — a rectangular loft path in XZ (the shape's Y becomes world Z):

```xml
<shape id="rect_path" closed="1">
  <v x="-100" y="-50"/>
  <v x="100" y="-50"/>
  <v x="100" y="50"/>
  <v x="-100" y="50"/>
</shape>
```

### `<light>`

Point light source with distance attenuation.

| Attribute    | Type | Default | Description |
|--------------|------|---------|-------------|
| `pos`        | vec3 | 0 300 0   | Light position in cm |
| `color`      | vec3 | 1 1 1   | Light colour authored as sRGB |
| `intensity`  | float| 1.0     | Linear brightness multiplier; never sRGB-converted |
| `radius`     | float| 0       | Attenuation radius in cm (0 = no falloff). Light reaches 25% at this distance |
| `castShadows`| int  | 1       | Whether this light casts stencil shadows |

When `radius` is 0 (default), the light has no distance attenuation — it illuminates at full intensity regardless of distance. When `radius` > 0, smooth quadratic attenuation is applied: the light is 25% of nominal strength at the given radius and reaches roughly 10% around `2.16 × radius`.

A good rule of thumb: set `radius` to about double the distance from the light to the farthest object it should noticeably illuminate.

`<light>` is transformable scene content as well as a valid top-level node. A
top-level position is in world space. Inside a `<group>` or prefab, its position
is local and inherits the complete parent translation, rotation, and scale.
Instance scale changes the light's positional offset but not its intensity.

Keep reusable practical lights inside their fixture prefab:

```xml
<!-- origin is the ceiling suspension point -->
<prefab>
  <cone pos="0 -53 0" radius="28" radiusTop="8" height="20" material="brass"/>
  <sphere pos="0 -61 0" radius="7" color="1 0.96 0.82"
          unlit="1" castShadow="0"/>
  <light pos="0 -64 0" color="1 0.70 0.36"
         intensity="0.85" castShadows="1"/>
</prefab>
```

The light above is inside the bulb and slightly below the opaque shade lip.
The shade may cast shadows; the emitter must not.

### `<sun>`

Directional light (infinite distance). Light rays travel parallel in the given `dir`. Useful for sunlight, moonlight, or any distant light source.

Aim sun or moon light **45–60 degrees below horizontal** so cast shadows have
readable depth and shape. Offset the horizontal angle **15–45 degrees from the
wall axis** so shadows fall diagonally across the room rather than parallel to
walls. Never set one horizontal component to zero — axial shadows read as flat
and uninteresting. The Y component magnitude should be roughly 0.7–1.7 times
the horizontal component magnitude.

| Attribute    | Type | Default | Description |
|--------------|------|---------|-------------|
| `dir`        | vec3 | 1 -1 0  | Direction light travels (normalized automatically) |
| `color`      | vec3 | 1 1 1   | Light colour authored as sRGB |
| `intensity`  | float| 1.0     | Linear brightness multiplier; never sRGB-converted |
| `castShadows`| int  | 1       | Whether this light casts stencil shadows |

Example: moon through a back-wall window at 45° down and 30° horizontal offset:

```xml
<sun dir="-0.6 -1 1" color="0.34 0.48 0.82" intensity="0.82" castShadows="1"/>
```

## Shape objects

All shapes share common attributes plus shape-specific ones. Shapes may contain modifier child elements. The supported shapes are: `box`, `sphere`, `cylinder`, `arch`, `prism`, `cone`/`pyramid`, `torus`, `lathe`, `loft`, `wall`.

### Common attributes

| Attribute    | Type   | Default          | Description |
|--------------|--------|------------------|-------------|
| `pos`        | vec3   | 0 0 0            | World position |
| `rot`        | vec3   | 0 0 0            | Euler rotation `rx ry rz` in degrees, applied X→Y→Z |
| `scale`      | vec3   | 1 1 1            | Non-uniform scale |
| `material`   | string | (none)           | Reference to a `<material id="...">` |
| `color`      | vec3   | 0.8 0.8 0.8      | Diffuse sRGB colour (used if no material ref) |
| `shininess`  | float  | 8.0              | Specular exponent (used if no material ref) |
| `castShadow` | int    | 1                | Whether this object casts shadows |
| `renderable` | int    | 1                | Whether this object is drawn; non-renderable objects may still cast shadows |
| `unlit`      | int    | 0                | Render the authored diffuse color without ambient or additive light contribution |
| `pivotOffset`| vec3   | 0 0 0            | Offsets the rotation center. Translate by offset, rotate, translate back |
| `attach`     | string | (none)           | Snap to a named instance's attach point: `instanceName:slotName` |
| `tint`       | int    | 0                | On prefab children, allow instance `color` to replace this part's diffuse color |

If a `material` attribute is given, `color` and `shininess` are taken from the referenced material definition.

An unlit object still renders into the depth buffer. Its `castShadow` setting is
independent, although visible emitter geometry normally uses
`unlit="1" castShadow="0"` so it stays bright without blocking the light it
represents.

**Rotation convention:** `rot="rx ry rz"` applies rotations around X, then Y, then Z. Positive Y rotation maps the **+X** axis toward **-Z** and **+Z** toward **+X**.

### `pivotOffset` attribute

Shifts the rotation center away from the object's origin. The object is translated by `+pivotOffset`, rotated, then translated by `-pivotOffset`. Useful for hinged objects (book covers, open drawers) or any shape that should rotate around an edge instead of its center.

```xml
<!-- box rotating 45° around its left edge (half-width = -0.15) -->
<box pos="0 50 0" size="30 2 20" rot="0 0 45" pivotOffset="-15 0 0"/>
```

The `pivotOffset` is in local space, applied before the object's own `rot`. Rotation-only matrices (for normals) are unaffected.

### `<box>`

Axis-aligned box centered at origin.

| Attribute | Type | Default | Description |
|-----------|------|---------|-------------|
| `size`    | vec3 | 1 1 1   | Width, height, depth |
| `inset`   | float/vec2 | 0 | Hollow the box into a rectangular tube. One value uses the same inset on X and Y; two values are `insetX insetY`. The opening passes fully through local Z |

### `<sphere>`

UV sphere centered at origin.

| Attribute | Type  | Default | Description |
|-----------|-------|---------|-------------|
| `radius`  | float | 50      | Sphere radius in cm |
| `rings`   | int   | 16      | Latitude subdivisions |
| `slices`  | int   | 24      | Longitude subdivisions |

### `<cylinder>`

Capped cylinder along the Y axis.

| Attribute | Type  | Default | Description |
|-----------|-------|---------|-------------|
| `radius`  | float | 50      | Radius in cm |
| `height`  | float | 100     | Height along Y in cm |
| `tube`    | float | 0       | Wall thickness in cm for a hollow tube with open ends |
| `sides`   | int   | 24      | Number of radial segments |

### `<arch>`

Round-arched solid or frame extruded along Z. This is useful for Roman-arch or round-top windows and doors.

| Attribute | Type  | Default | Description |
|-----------|-------|---------|-------------|
| `width`   | float | 100     | Full outer width in cm |
| `height`  | float | 150     | Full outer height in cm |
| `depth`   | float | 20      | Extrusion depth along Z in cm |
| `tube`    | float | 0       | Frame thickness in cm. `0` yields a solid arch; a positive value yields a hollow arched frame |
| `thickness` | float | 0     | Alias for `tube`, in cm |
| `segments` | int   | 16      | Semicircle subdivisions |
| `inset` | float | 0 | Additional inset between the outer arch and hollow frame opening, in cm |

### `<window>`

A procedural fixed window. One authored element owns the perimeter frame,
optional undivided pane, optional sill and automatic wall opening. Prefer this
primitive for supported window types instead of assembling separate boxes,
arches and cutters. All generated parts select and save as one window.

Use the following schema for exact parameter names and defaults. See
[procedural-windows.md](procedural-windows.md) for runnable examples of every
preset/style, material overrides, recessed placement, open apertures, stacked
windows and reusable prefabs.

#### Window types and styles

These are the complete supported sets; identifiers are case-sensitive.

| `preset` | Outer outline | Default width × height (cm) | Default sill | Height constraint |
|----------|---------------|----------------------------|--------------|-------------------|
| `round-arch` (default) | Straight jambs and a semicircular head | 120 × 180 | Off | `height > width / 2` |
| `cottage` | Rectangle | 120 × 140 | On | Positive height |
| `gothic` | Straight jambs and two circular arcs meeting at a pointed head | 120 × 220 | Off | `height > sqrt(3) * width / 2` |

| `style` | Default frame width | Other effects |
|---------|---------------------|---------------|
| `plain` (default) | `0.06 * min(width, height)` | None |
| `storybook` | `0.10 * min(width, height)` | None; it does not add distortion, ornament or bars |

Style and preset are independent: `preset="gothic" style="storybook"` is valid.
There is no `roman`, `cartoon`, circular, opening/casement, grille or tracery
preset. `round-arch` has fixed semicircular proportions; `gothic` has fixed
pointed-arch proportions. There is no independent arch-rise/radius parameter.
Change `width`/`height` to regenerate the profile. Nonuniform `scale` stretches
finished geometry, including frame thickness; it does not recompute defaults.

#### Window-specific parameters

All lengths are centimetres before transforms. Use `0` or `1` for booleans.
Explicit values override defaults; omitted values are recomputed from the
current dimensions, style and frame width each time the scene is loaded.

| Attribute | Type | Default | Meaning / constraints |
|-----------|------|---------|-----------------------|
| `preset` | enum string | `round-arch` | One of the three presets above |
| `style` | enum string | `plain` | `plain` or `storybook` |
| `width` | positive float, cm | 120 | Full outer width of frame and cutter |
| `height` | positive float, cm | Preset table | Full outer height, including the curved head; must satisfy the preset's height constraint |
| `frameWidth` | positive float, cm | Style table | Inward offset from the sampled outer profile; must leave a nonempty inner opening |
| `depth` | positive float, cm | `1.5 * frameWidth` | Frame extrusion depth, centred on local Z=0 |
| `frameMaterial` | material ID | `material`, or `wood` if neither is set | Material shared by frame and sill |
| `glassMaterial` | material ID | `glass` | Pane material; define custom IDs with `<material>` in the containing scene |
| `pane` | boolean | 1 | Generate the pane; `0` leaves the framed aperture open |
| `paneDepth` | positive float, cm | 2 | Pane extrusion thickness |
| `paneOffset` | finite float, cm | 0 | Pane centre offset along local Z; positive is toward the front |
| `sill` | boolean | 1 for cottage, otherwise 0 | Generate the rectangular sill beneath the opening; does not mean elevation above the floor |
| `sillHeight` | positive float, cm | `frameWidth` | Sill height below the opening |
| `sillProjection` | nonnegative float, cm | `frameWidth` | Extra depth beyond the frame's front and overhang at **each** side; one value controls both |
| `cutWalls` | boolean | 1 | Register the outer profile as a cutter for matching `<wall>` elements |
| `cutDepth` | positive float, cm | `depth` | Depth of the matching volume, centred on the window; does not set the hole's final depth |
| `segments` | integer | 32 | Arc subdivisions, 8–128 inclusive; odd values round up to even; accepted/validated but geometrically unused for cottage |

For example, a 140 × 200 round-arch with storybook style defaults to a 14 cm
frame and 21 cm depth. Setting `frameWidth="8"` changes the default depth to
12 cm. An explicitly authored `depth="20"` stays 20 cm when frame width changes.

With a pane enabled, `abs(paneOffset) + paneDepth / 2 <= depth / 2` must hold.
For `depth="20" paneDepth="2"`, valid offsets are −9 through +9 cm. Positive
length parameters are validated even when their optional component is disabled:
use `pane="0"`, not `paneDepth="0"`; use `sill="0"`, not `sillHeight="0"`.
All dimensions must be finite. Leave practical clearance from the mathematical
height limit and verify that the requested frame inset leaves glass area.

#### Common attributes and their window semantics

| Attribute | Default | Window behaviour |
|-----------|---------|------------------|
| `name` | None | Names the single window element for selection and camera `<transform target="...">` overrides |
| `pos` | `0 0 0` | Centre of the outer opening's bounding rectangle in the parent frame |
| `rot` | `0 0 0` | Degrees, following the common X→Y→Z convention |
| `scale` | `1 1 1` | Unitless geometry/cutter scale; use nonzero components; reflections are supported |
| `pivotOffset` | `0 0 0` | Pivot in local centimetres for transforms |
| `material` | None | Frame/sill material fallback when `frameMaterial` is absent |
| `castShadow` | 1 | Applies to frame and sill; pane shadow casting is always off |
| `renderable` | 1 | Applies to all visible window parts; does **not** disable its wall cut |
| `unlit` | 0 | Applies to all window parts, not just the pane |

Use named materials for color and shininess. Resolved frame/glass materials
supply those values; a bare `color`, `shininess` or prefab tint is not a reliable
way to override the default wood/glass materials. A `<group material="...">`
is not a material override for its window children.

#### Coordinates, sill and wall matching

Window local X is width, Y is height, Z is depth. The front is +Z.
`width`/`height` describe the **outer frame and opening**, not clear glass area.
The optional sill extends beyond that bounding rectangle. For a Z-up scene,
`rot="90 0 0"` maps window height to world Z and its front to world −Y.

For a Z-up window with this rotation, the nominal opening bottom is
`pos.z - height / 2`. To place a 200 cm opening with its bottom at Z=60,
use `pos.z=160`. The wall origin is different: its base is at local Y=0,
not half its height. Do not add a second vertical half-height to a wall.

The sill spans `width + 2*sillProjection` horizontally. In depth it spans
`-depth/2` to `depth/2 + sillProjection`, with its top seated into the frame
by a tiny internal overlap. It does not automatically extend to the wall face.
For a centred window in a wall of thickness T, the visible front projection is
`depth/2 + sillProjection - T/2`. Increase `sillProjection` when this is too
small. This also increases the side overhang; independent dimensions are not
implemented. See the recessed-window recipe for a worked calculation.

A wall matches when its thickness axis is parallel to the window's thickness
axis, its slab intersects the window's `cutDepth` volume, and the outer profile
overlaps the wall in-plane. The window's XY plane must remain parallel to the
wall's XY plane; an in-plane rotation is allowed, an oblique cut is not.
The resulting hole uses the **exact sampled outer profile** and crosses the
complete wall thickness. A thin frame can therefore cut a thick wall without
setting `cutDepth` to that thickness. For a surface-mounted window outside the
slab, enlarge `cutDepth` enough to reach the slab; it can match more than one
wall, so do not use an unnecessarily large value.

The prepass finds windows inside ordinary groups and prefabs, before building
walls. XML order is immaterial. Window/cutter transforms include parent
transforms, nonuniform scale, reflections and named camera overrides. Moving a
window in the editor rebuilds the wall opening. Overlapping cuts remove their
union, stacked windows work, and a profile may extend beyond a wall edge.
These describe **cutting**, not a guarantee that overlapping visible frames
form sensible architecture. Window-bearing walls can also contain legacy
rectangular, round-arch and circular cutters.

#### Supported composition and current limits

- Place `<window>` at scene level or inside `<group>` / prefab content. It is
  a sibling of `<wall>`, not a child of it: a wall's children are `<opening>`
  declarations only. Do not add a duplicate negative shape or `<opening>` for
  a procedural window with `cutWalls="1"`.
- `<window>` must have no children. Any child element, including `<array>`,
  `<extrude>`, `<shape>` or a material definition, rejects the whole window.
  Its extrusion is built in through `depth` and `frameWidth`.
- Do not use `attach` on the window or attach-based placement for a containing
  window assembly. The cutter prepass does not resolve attach placements.
- Repeat windows with explicit `<window>` or `<prefab>` instances. Do not put
  an `<array>` on a window-containing prefab: visible prefab copies are
  expanded, but the cutter prepass does not repeat their cuts.
- Only `<wall>` geometry is cut, not `<box>` or arbitrary meshes. To use an
  insert in an existing authored hole, set `cutWalls="0"` and match the opening.
- `cutWalls="0"` does not hide geometry. `renderable="0"` does not stop cutting.
  `pane="0"` removes only the pane, retaining the frame, sill and cutter.
- Glass uses the renderer's existing opaque material rendering. It does not
  give a transparent view of outside scenery. Panes never cast shadows, so
  lighting may pass through conceptually. No window light is created; author
  a separate sun or motivated point light in the scene/group/prefab.
- Only fixed, undivided panes are supported. Opening sashes, shutters, grids,
  fans, tracery, bevels and custom curve syntax are not window parameters.
- The editor's Create controls offer all three presets, and the property
  browser displays their attributes. The browser is currently read-only;
  configure detailed parameters in XML. The editor can move/rotate/scale and
  save the window as one source element.

Unknown presets/styles, unusable dimensions/insets, out-of-frame panes,
`attach`, or children are diagnosed and produce neither window nor cutter.
Treat these diagnostics as validation failures even if the CLI exits zero.
Unknown attributes may be ignored; do not invent parameter names based on
features available in another modeller.

### `<door>`

A source-backed hinged door: one element owns the frame, fitted leaf, handles,
optional pet passage, optional upper window and exact outer-profile wall cut. All parts select, move
and save together. Dimensions and coordinate conventions match `<window>`:
local X width, Y height, +Z front; the origin is the outer bounding-box centre.
For Z-up scenes use `rot="90 0 0"` and `pos.z=height/2` at floor level.

| Attribute | Default | Meaning |
|-----------|---------|---------|
| `preset` | `rectangular` | `rectangular`, `round-arch`, `gothic`; all default to 100 × 210 cm |
| `style` | `plain` | `plain` or `storybook`, same frame-width ratios as windows |
| `width`, `height` | 100, 210 | Positive outer dimensions in cm; arched height constraints match windows |
| `frameWidth` | 6% of smaller dimension | Inward frame offset; 10% with storybook style |
| `depth` | 1.5 × frameWidth | Positive frame depth in cm |
| `leafDepth` | 4 | Positive leaf thickness, no greater than frame depth |
| `clearance` | 0.2 | Nonnegative gap in cm, inset from the frame aperture, including the floor |
| `hinge` | `left` | `left` or `right`, seen from local +Z |
| `openAngle` | 0 | Degrees, −180…180; positive swings toward +Z for either hinge side |
| `frameMaterial` | `material`, otherwise `wood` | Frame finish |
| `leafMaterial` | `material`, otherwise `wood` | Leaf finish |
| `hardwareMaterial` | `metal` | Handles and pet-passage trim |
| `handle` | 1 | Generate a round handle on both faces |
| `petWidth`, `petHeight` | 0, 0 | Both zero disable the passage; otherwise positive cm, height > width/2 |
| `petFrameWidth` | frameWidth / 2 | Positive trim width; the entire trimmed passage must fit in the leaf |
| `window` | `none` | `none`, `round`, `matching`, `rectangular`, `round-arch` or `gothic`; `matching` follows the door preset |
| `windowWidth` | 40% of door width | Positive outer window width in cm, including trim; diameter for `round` |
| `windowHeight` | 28% of door height | Positive outer height in cm; defaults to width for `round`, which requires equal width and height |
| `windowCenter` | 70% of door height | Window centre elevation in cm above the door's bottom, centred horizontally |
| `windowFrameWidth` | frameWidth / 2 | Positive inward trim width, leaving a nonempty aperture |
| `windowFrameMaterial` | frame material | Window trim finish |
| `windowGlassMaterial` | `glass` | Pane material |
| `windowPane` | 1 | `0` leaves an open aperture; only applies when `window` is enabled |
| `windowPaneDepth` | min(2, leafDepth) | Positive pane thickness in cm, no greater than leaf thickness |
| `cutWalls` | 1 | Cut matching wall slabs using the exact outer frame profile |
| `cutDepth` | depth | Positive wall-matching depth in cm |
| `segments` | 32 | Arc subdivisions, 8…128, rounded up to even |

The frame has two jambs and a head, with no bottom crosspiece. Its leaf is
derived from that same aperture with the requested construction clearance.
The closed leaf sits flush with the frame's front; the hinge axis lies at the
front inner jamb edge. `rot` rotates the entire assembly, while `openAngle`
rotates the leaf, its handles, pet trim and window, leaving the frame and wall cut fixed.
Negative angles are supported, but the author must allow clearance from the
wall/reveal and nearby furniture. Scener does not perform collision resolution.

The optional pet passage is a floor-level round arch cut through the leaf.
Its trim follows the same profile and swings with the leaf. It is an open
passage, not an independently hinged flap. It does not create a second wall cut.
The optional upper window cuts only the leaf, with trim derived from the same
profile and a pane seated inside it. `window="round"` creates a circle on any
door; `window="matching"` creates a rectangle, Roman arch or Gothic arch to
match the door preset. An explicit shape can also be used on any door.
Defaults place it wholly in the upper half of the standard door presets.
The complete outer window must fit in the leaf above the pet passage; invalid
sizes, frame insets, pane depths and overlapping openings reject the whole door.
Window dimensions have no effect while `window="none"`.

`windowPane="0"` leaves a genuine open aperture. With a pane, Scener's glass
material remains opaque and does not cast shadows, as for standalone windows;
it does not simulate transparency or create a light. The window trim spans
twice the leaf depth, with no duplicate inner leaf surfaces. All its parts
follow either hinge direction and `openAngle` as part of the one saved door.
Standalone-window `pane` and sill controls do not apply to doors. Door primitives
reject children and `attach`; use explicit sibling placements inside groups or
prefabs, without prefab arrays, as for procedural windows. Named camera
transforms operate on the whole assembly; per-camera open-angle overrides are
not implemented. The property browser shows source attributes read-only;
author detailed dimensions and opening angles in XML.

```xml
<door name="rear-door" preset="round-arch" style="storybook"
      pos="0 0 135" rot="90 0 0" width="154" height="270"
      frameWidth="10" depth="20" leafDepth="8" clearance="0.2"
      frameMaterial="wood" leafMaterial="wood" hardwareMaterial="metal"
      hinge="left" openAngle="0" petWidth="40" petHeight="48"
      petFrameWidth="2" segments="48" window="round" windowWidth="64"
      windowCenter="202" windowFrameWidth="5" />
```

Inspect [the door fixture](../../../tests/procedural_doors.blks) from front,
oblique and rear cameras to check the fixed wall opening and swung leaf.


### `<capsule>`

Cylinder capped with two hemispheres, centered at origin along the Y axis.
Useful for pill shapes, table legs, railings, handles.

| Attribute | Type  | Default | Description |
|-----------|-------|---------|-------------|
| `radius`  | float | 50      | Radius in cm |
| `height`  | float | 100     | Cylinder height in cm (total = height + 2 × radius) |
| `rings`   | int   | 12      | Subdivision rings for each hemisphere |
| `slices`  | int   | 24      | Radial segments around the Y axis |

### `<prism>`

Regular N-sided prism along Y (flat-shaded).

| Attribute | Type  | Default | Description |
|-----------|-------|---------|-------------|
| `radius`  | float | 50      | Radius of circumscribed circle in cm |
| `height`  | float | 100     | Height along Y in cm |
| `sides`   | int   | 6       | Number of sides (default: hexagonal) |

### `<cone>` / `<pyramid>`

Truncated cone (frustum) along Y. `<pyramid>` is a `4`-sided default.

| Attribute   | Type  | Default | Description |
|-------------|-------|---------|-------------|
| `radius`    | float | 50      | Base radius in cm |
| `radiusTop` | float | 0       | Top radius in cm (0 = pointed) |
| `height`    | float | 100     | Height along Y in cm |
| `sides`     | int   | 24/4    | Radial segments (24 for cone, 4 for pyramid) |

### `<torus>`

Torus lying in the XZ plane, centered at origin. Internally constructed by
sweeping a circular cross-section (2D circle in XY) along a circular path
(circle in XZ) via loft.

| Attribute       | Type  | Default | Description |
|-----------------|-------|---------|-------------|
| `majorRadius`   | float | 50      | Distance from center to tube center, in cm |
| `minorRadius`   | float | 15      | Tube radius in cm |
| `majorSegments` | int   | 24      | Segments around the ring |
| `minorSegments` | int   | 12      | Segments around the tube |

### `<lathe>`

Revolves a named 2D profile around the Y axis to produce a rotationally
symmetric solid (bottle, vase, goblet, column, etc.). Think of it as a
potter's wheel: you define the profile in the XY plane, and the lathe sweeps
it around Y.

The referenced shape **must be open** (`closed="0"`). The first profile vertex
defines the radius of the bottom cap; the last defines the top cap. Ends at
`x="0"` produce a pointed or closed tip (the lathe places a single center
vertex for the cap).

`<lathe>` accepts the [common transform attributes](#common-attributes) and
[modifiers](#modifiers) as child elements.

| Attribute   | Type   | Default | Description |
|-------------|--------|---------|-------------|
| `shape`     | string | (required) | Name of a `<shape id="...">` defined in the scene or prefab |
| `segments`  | int    | 24      | Number of angular subdivisions around the Y axis |

```xml
<shape id="vase" closed="0">
  <v x="0" y="0"/>
  <v x="35" y="0"/>
  <v x="35" y="10"/>
  <v x="28" y="40"/>
  <v x="15" y="160"/>
  <v x="6" y="230"/>
  <v x="6" y="250"/>
  <v x="0" y="250"/>
</shape>

<lathe shape="vase" segments="32" pos="0 0 0" material="glass"/>
```

### `<loft>`

Sweeps a 2D cross-section shape along a 2D path shape to create a 3D mesh
(pipes, rails, molding, swept frames, etc.). The path shape's XY is mapped to
the world XZ plane (with Y=0) to define 3D sweep stations. The cross-section
shape is placed at each station, oriented perpendicular to the path tangent.

When `closed="1"`, the loft wraps the last ring back to the first to form a
closed loop. When `closed="0"`, the first and last rings receive flat end caps.

`<loft>` accepts the [common transform attributes](#common-attributes) and
[modifiers](#modifiers) as child elements.

| Attribute    | Type   | Default | Description |
|--------------|--------|---------|-------------|
| `pathShape`  | string | (required) | Name of a shape defining the 3D sweep path (XY→XZ) |
| `crossShape` | string | (required) | Name of a shape defining the 2D cross-section |
| `closed`     | int    | 0       | 1 to connect end ring back to start ring (loop) |
| `segments`   | int    | *pathShape points* | Number of stations along the path (defaults to the shape vertex count) |

```xml
<shape id="path_L" closed="0">
  <v x="0" y="0"/>
  <v x="200" y="0"/>
  <v x="200" y="100"/>
</shape>

<shape id="circle_ring" closed="1">
  <v x="8" y="0"/>
  <v x="5.6" y="5.6"/>
  <v x="0" y="8"/>
  <v x="-5.6" y="5.6"/>
  <v x="-8" y="0"/>
  <v x="-5.6" y="-5.6"/>
  <v x="0" y="-8"/>
  <v x="5.6" y="-5.6"/>
</shape>

<loft pathShape="path_L" crossShape="circle_ring" closed="0" segments="16" material="metal"/>
```

### `<wall>`

A flat wall with rectangular openings (doors/windows). Uses "boolean via boxes" — the wall is split into box segments around openings. Walls ignore `scale`; `castShadow` applies to the generated wall boxes.

| Attribute   | Type  | Default | Description |
|-------------|-------|---------|-------------|
| `length`    | float | 400     | Wall length along local X, in cm |
| `height`    | float | 270     | Wall height along local Y, in cm |
| `thickness` | float | 20      | Wall depth along local Z, in cm |

Child `<opening>` elements:

| Attribute | Type   | Default     | Description |
|-----------|--------|-------------|-------------|
| `type`    | string | "door"      | "door" or "window" |
| `x`       | float  | 0           | Position along wall length |
| `width`   | float  | 100         | Opening width in cm |
| `height`  | float  | 210/120     | Opening height in cm (door: 210, window: 120) |
| `sill`    | float  | 0/90        | Height from floor in cm (door: 0, window: 90) |

Walls also consume intersecting `<bool-negative-box>` nodes collected from the
scene and instantiated prefabs before wall geometry is built. This makes a
window or door prefab capable of carrying its own rough opening. Document order
does not matter.

### `<bool-negative-box>`

A non-rendered rectangular wall cutter. It accepts the common `pos`, `rot`,
`scale`, and `pivotOffset` transforms plus `size`. Its local X is opening width,
local Y is opening height, and local Z must cross the wall thickness.

This is deliberate wall-opening metadata, not unrestricted mesh CSG: it cuts
only `<wall>` geometry, and its axes must align with the target wall's local
axes. Parent or prefab rotation is supported when the cutter and wall remain
aligned. Oblique cutters are ignored.

```xml
<!-- Custom rectangular insert: for standard windows prefer <window>. -->
<prefab>
  <bool-negative-box size="200 170 30"/>
  <box pos="-94 0 4" size="12 170 16" material="wood"/>
  <box pos="94 0 4" size="12 170 16" material="wood"/>
  <!-- top, bottom, pane, and mullions -->
</prefab>
```

Place the complete insert once: `<prefab source="window" pos="x y z"/>`.
Do not declare a duplicate child `<opening>` on the wall.

### `<bool-negative-arch>`

A non-rendered round-arch wall cutter using the same transform rules and wall-only
scope as `<bool-negative-box>`. It reads `width`, `height` and `depth`; align local
Z to the wall thickness and extend `depth` completely through both wall faces.
The legacy cutter does not read `segments`, so an independently authored arch
frame is not guaranteed to share its sampled boundary. Prefer `<window>` for
windows: it owns both the geometry and exact cutter profile. Keep this primitive
for custom inserts such as doorways.

```xml
<prefab>
  <bool-negative-arch width="220" height="235" depth="34"/>
  <arch width="220" height="235" depth="10" tube="14" segments="16" material="wood"/>
</prefab>
```

### `<bool-negative-cylinder>`

A non-rendered circular wall cutter with the same wall-only and alignment
rules as the other cutters. The cylinder axis is local Z, so `depth` must pass
fully through the wall.

| Attribute | Type | Default | Description |
|-----------|------|---------|-------------|
| `radius` | float | 50 | Opening radius in cm |
| `depth` | float | 30 | Cutter depth along local Z in cm |

```xml
<bool-negative-cylinder pos="0 180 0" radius="45" depth="30"/>
```

### `<group>`

A container that applies its transform to all children. Groups do not render geometry.

```xml
<group pos="0 200 0" rot="0 45 0">
  <box size="100 100 100"/>
  <sphere pos="100 0 0" radius="30"/>
</group>
```

## Modifiers

Modifiers are child elements of mesh-producing shapes: `<box>`, `<sphere>`,
`<cylinder>`, `<capsule>`, `<arch>`, `<prism>`, `<cone>`, `<pyramid>`,
`<torus>`, `<lathe>`, and `<loft>`. They are applied in document order in
local space before the shape transform. `<array>` is also accepted as a child
of a `<prefab>` instance, where it repeats the complete prefab assembly.
`<window>` does not accept modifier children.

Deform modifiers (`taper`, `twist`, `bend`, `stretch`, `skew`) operate relative to the object's bounding box along the chosen axis. The axis range [min, max] is mapped to [0, 1]. Each accepts `axis="y"` (`x`, `y`, or `z`), defaulting to `"y"`.

The `<array>` modifier duplicates the mesh rather than deforming it. Place it last so copies include the effect of any preceding deform modifiers.

### `<taper>`

Scales the two perpendicular axes along the chosen axis.

| Attribute    | Type   | Default | Description |
|--------------|--------|---------|-------------|
| `amount`     | float  | 0.0     | Taper intensity (± values, positive = narrow top along axis) |
| `curvature`  | float  | 1.0     | Non-linearity exponent (1 = linear, >1 = bowed, <1 = pinched) |
| `axis`       | char   | y       | Axis along which taper varies (x, y, or z) |

```xml
<box size="2 3 2">
  <taper amount="0.4" axis="y"/>
</box>
```

### `<twist>`

Rotates the perpendicular axes around the chosen axis proportionally to position along it. Normals are also rotated.

| Attribute | Type  | Default | Description |
|-----------|-------|---------|-------------|
| `angle`   | float | 0.0     | Total twist angle in degrees |
| `axis`    | char  | y       | Axis around which to twist (x, y, or z) |

```xml
<box size="2 4 2">
  <twist angle="45" axis="y"/>
</box>
```

### `<bend>`

Bends the chosen axis into a circular arc in the plane defined by the axis and its first perpendicular component.

| Attribute | Type  | Default | Description |
|-----------|-------|---------|-------------|
| `angle`   | float | 0.0     | Bend angle in degrees |
| `axis`    | char  | y       | Axis to bend (x, y, or z) |

```xml
<cylinder radius="20" height="300">
  <bend angle="90" axis="y"/>
</cylinder>
```

### `<stretch>`

Non-linear stretch/squash along the chosen axis; pinch the middle, expand the ends (or vice versa).

| Attribute  | Type  | Default | Description |
|------------|-------|---------|-------------|
| `amount`   | float | 0.0     | Stretch amount (positive = squeeze middle) |
| `amplify`  | float | 1.0     | How much perpendicular axes squeeze in response |
| `axis`     | char  | y       | Axis along which to stretch (x, y, or z) |

```xml
<sphere radius="50">
  <stretch amount="0.5" amplify="0.5" axis="y"/>
</sphere>
```

### `<skew>`

Shears the two perpendicular axes proportionally to position along the chosen axis.

| Attribute | Type  | Default | Description |
|-----------|-------|---------|-------------|
| `amount`  | float | 0.0     | Skew intensity (positive = top shifts along perpendiculars) |
| `axis`    | char  | y       | Axis along which to skew (x, y, or z) |

```xml
<box size="1 3 1">
  <skew amount="0.5" axis="y"/>
</box>
```

### `<array>`

Duplicates the mesh `count` times, applying a per-step translation and rotation to each copy. Useful for rows of books, shelf tiers, stair steps, or any repeating geometry. The array operates in local mesh space before the shape's own transform.

| Attribute     | Type  | Default | Description |
|---------------|-------|---------|-------------|
| `count`       | int   | 1       | Total copies (original + duplicates). Must be >= 1 |
| `translation` | vec3  | 0 0 0   | Offset per step |
| `rotation`    | vec3  | 0 0 0   | Euler rotation per step in degrees |

```xml
<!-- 8 books in a row, each 3 cm apart along X -->
<box size="2 30 20" material="fabric">
  <array count="8" translation="3 0 0"/>
</box>

<!-- Spiral staircase: 12 steps, each raised 20 cm in Y and rotated 15° around Y -->
<box size="80 5 30" material="wood">
  <array count="12" translation="0 20 0" rotation="0 15 0"/>
</box>
```

### `<extrude>`

Duplicates the mesh offset along an axis and bridges boundary edges with quads.
Turns flat or open shapes into solids with depth.

| Attribute | Type  | Default | Description |
|-----------|-------|---------|-------------|
| `amount`  | float | 10      | Extrusion distance in cm |
| `axis`    | char  | y       | Extrusion axis (x, y, or z) |

```xml
<box size="200 5 100">
  <extrude amount="20" axis="y"/>
</box>
```

### `<mirror>`

Creates a mirrored copy of the mesh across a plane, optionally welding vertices
that lie on (or very near) the mirror plane. Useful for building symmetric
objects from one half.

| Attribute | Type  | Default | Description |
|-----------|-------|---------|-------------|
| `axis`    | char  | y       | Mirror plane normal axis (x=YZ plane, y=XZ plane, z=XY plane) |
| `weld`    | float | 0.1     | Distance threshold in cm for welding vertices on the mirror plane |

```xml
<box size="100 200 50" pos="-50 0 0">
  <mirror axis="x" weld="0.001"/>
</box>
```

### `<noise>`

Displaces each vertex by a random vector for organic, uneven surfaces. Valid on
any shape. Keep strength small on smooth primitives and larger on terrain or
foliage. Seeded for deterministic output.

| Attribute  | Type  | Default | Description |
|------------|-------|---------|-------------|
| `strength` | float | 0.1     | Maximum displacement magnitude per axis |
| `seed`     | int   | 1       | Random seed for reproducible noise |

```xml
<sphere radius="50">
  <noise strength="8" seed="42"/>
</sphere>
```

### `<shell>`

Thickens a mesh by pushing each face outward along its vertex-averaged normals
and bridging boundary edges. Turns a flat plane into a slab, or adds wall
thickness to an open container.

| Attribute | Type  | Default | Description |
|-----------|-------|---------|-------------|
| `amount`  | float | 5       | Thickness amount pushed along normals, in cm |

```xml
  <box size="200 2 100">
  <shell amount="10"/>
</box>
```

## Prefabs

Prefabs are reusable object definitions stored in `prefabs/name.blk`. Each prefab file begins with an XML comment describing its default orientation (which way it "faces" at `rot="0 0 0"`). Prefabs are loaded lazily (on first reference) and cached for reuse.

Prefabs may contain point lights. Each instance creates its own transformed
light, allowing one fixture definition to keep its cord, shade, bulb, and light
source aligned under translation, rotation, and scale.

**Orientation convention:** every prefab that has a natural "front" (e.g. chair, sofa) must document which direction it faces at default rotation. Use the rotation convention above to place and orient prefabs.

### `<prefab>`

Instantiate a prefab. `pos`, `rot`, `scale`, `pivotOffset`, and `attach` apply
to the complete instance. Instance `color` is a selective tint as described
below. Prefab-wide `material`, `shininess`, `castShadow`, and `renderable`
inheritance are not currently implemented; define those on child shapes.

| Attribute | Type   | Default | Description |
|-----------|--------|---------|-------------|
| `source`  | string | (required) | Prefab file name, loads from `prefabs/<source>.blk` |
| `name`    | string | (none)     | Instance name for `attach` references — only needed when something attaches to this instance |

Prefab `scale` applies to its complete local transform, including named attach
points. A prefab-level `color` selectively replaces the diffuse color of
descendant shapes marked `tint="1"`; unmarked parts keep their own material or
color. Tinting does not replace material shininess. This lets one book prefab
produce different cover colors while its page block remains paper-colored:

```xml
<!-- prefabs/book.blk -->
<prefab>
  <box size="44 3 32" material="book_cover" tint="1"/>
  <box pos="0 6 0" size="39 6 28" material="paper"/>
</prefab>

<!-- scene -->
<prefab source="items/book" color="0.65 0.08 0.06"/>
<prefab source="items/book" pos="50 0 0" color="0.06 0.20 0.62"/>
```

Example: place 4 chairs around a dining table (table spans X=±80 cm, Z=-100 to -200 cm):

```xml
<prefab source="furniture/dining_table" name="dining_table" pos="0 0 -150"/>
<!-- chair faces +Z by default; rotY(180) = faces -Z (into table) -->
<prefab source="furniture/chair" pos="0 0 -75" rot="0 180 0"/>
<!-- chair at back: default +Z is already toward table -->
<prefab source="furniture/chair" pos="0 0 -225"/>
<!-- chair at left: rotY(90) maps +Z -> +X (toward table right) -->
<prefab source="furniture/chair" pos="-105 0 -150" rot="0 90 0"/>
<!-- chair at right: rotY(-90) maps +Z -> -X (toward table left) -->
<prefab source="furniture/chair" pos=" 105 0 -150" rot="0 -90 0"/>
```

### Attach points

Prefabs can declare named reference points using `<attach>` elements. These enable placing objects on surfaces without manual Y calculation.

**Defining attach points** — in the prefab file (`prefabs/dining_table.blk`):

```xml
<prefab>
  <attach name="center" pos="0 78 0"/>
  <attach name="edge_n" pos="0 78 -53"/>
  <attach name="edge_s" pos="0 78 53"/>
  <attach name="edge_e" pos="83 78 0"/>
  <attach name="edge_w" pos="-83 78 0"/>
  <box pos="0 75 0" size="160 6 100" material="wood"/>
  ...
</prefab>
```

Attach point positions are in the prefab's local coordinate space.

**Using attach points** — in any scene element via `attach="instanceName:slotName"`:

```xml
<prefab source="furniture/dining_table" name="dining_table" pos="0 0 -150"/>
<!-- Sphere placed on the table's center without calculating y=0.78 -->
<sphere attach="dining_table:center" radius="14" material="fabric"/>
```

When `attach` is present, the element's `pos`, `rot`, and `scale` are applied
**in the referenced instance's local frame** at the named attach point. The
element automatically inherits the instance's world-space rotation and scale.
This means objects placed with `attach` on a rotated surface remain correctly
flat on that surface without any manual rotation calculation:

```xml
<prefab source="furniture/workbench" name="main_bench" pos="-445 0 -450" rot="0 90 0"/>

<!-- Children automatically inherit the bench's rotated frame -->
<group attach="main_bench:top_surface">
  <prefab source="items/repair_book" pos="23 0 0"/> <!-- X spreads along bench length -->
</group>
```

Without `attach`, or for elements that need the old world-space behavior, omit
the attribute and place the element directly.

Prefab file `prefabs/chair.blk`:

```xml
<!-- chair: backrest at z=-0.2, front faces +Z. rotY(+90)->+X, rotY(-90)->-X -->
<prefab>
  <attach name="seat" pos="0 47.5 0"/>
  <box pos="0 45 0" size="45 5 45" material="wood"/>
  <box pos="0 75 -20" size="45 55 5" material="wood"/>
  <prism pos="-18 22 -18" radius="2.5" height="45" sides="4" material="wood"/>
  <prism pos=" 18 22 -18" radius="2.5" height="45" sides="4" material="wood"/>
  <prism pos="-18 22  18" radius="2.5" height="45" sides="4" material="wood"/>
  <prism pos=" 18 22  18" radius="2.5" height="45" sides="4" material="wood"/>
</prefab>
```

## Complete example

```xml
<scene ambient="0.1 0.1 0.12" background="0.05 0.05 0.07">
  <camera name="Front" comment="Frontal view from outside the scene" pos="0 300 800" look="0 150 0" fov="55"/>
  <camera name="Back"  comment="Rear view from behind" pos="0 300 -800" look="0 150 0" fov="55"/>

  <material id="brass" color="0.8 0.7 0.2" shininess="40"/>
  <material id="steel" color="0.6 0.6 0.65" shininess="80"/>

  <light pos="200 400 200" color="1.0 0.9 0.8" intensity="1.2" castShadows="1"/>
  <light pos="-300 200 0" color="0.8 0.8 1.0" intensity="0.6" castShadows="0"/>

  <!-- twisted brass column -->
  <box pos="-200 150 0" size="50 300 50" material="brass">
    <twist angle="45"/>
  </box>

  <!-- tapered steel pedestal -->
  <cylinder pos="0 100 0" radius="60" height="200" material="steel">
    <taper amount="0.4"/>
  </cylinder>

  <!-- group with nested transforms -->
  <group pos="200 300 0" rot="0 30 0">
    <sphere radius="50" color="0.9 0.3 0.3"/>
    <box pos="0 -80 0" size="30 30 30" color="0.3 0.3 0.9"/>
  </group>
</scene>
```

## Overlay lines and dummies

Overlay lines are rendered in a final pass with depth testing disabled, so they
appear on top of all geometry. Use them for annotation: character pose
reference, light indicators, and camera frustum visualisation.

### `<line>`

A single line segment. Inherits parent `<group>` transforms.

| Attribute | Type | Default | Description |
|-----------|------|---------|-------------|
| `start`   | vec3 | 0 0 0   | Start point in local space |
| `end`     | vec3 | 0 1 0   | End point in local space |
| `color`   | vec3 | 0.85 0.15 0.15 | Line colour |
| `camera`  | string | (all) | Optional camera name; render this annotation only while that camera is active |

```xml
<line start="0 0 0" end="0 2 0" color="1 0 0"/>
```

### `<dummy>`

Generates annotation geometry for characters, lights, or cameras. Each type
produces a fixed set of overlay lines. Inherits parent `<group>` and `attach`
transforms.

| Attribute | Type   | Default            | Description |
|-----------|--------|--------------------|-------------|
| `type`    | string | "character"        | `"character"`, `"lamp"`, or `"camera"` |
| `color`   | vec3   | 0.85 0.15 0.15     | Line colour |
| `camera`  | string | (all)               | Optional camera name; render this dummy only while that camera is active |

**`type="character"`** — a vertical spine from baseline to top, plus four
horizontal landmark circles at top, neck, pelvis, and feet:

| Attribute | Type  | Default | Description |
|-----------|-------|---------|-------------|
| `height`  | float | 50      | Character height from baseline to head, in cm |
| `ref`     | string | (none) | Optional top-level `<chardef>` name; when omitted, inline proportions are used |
| `radius`  | float | 10% of height | Landmark-circle radius for an inline character definition |
| `pose`    | string | stand | `stand`, `walk`, `look`, `reach`, `inspect`, `crouch`, `work`, or `climb` |
| `target`  | vec3 | (none) | World-space action target used to aim gaze or hand gestures |

Character dummies face local `+Z`, inherit their complete transform, and gain
simple limb lines appropriate to `pose`. Use `rot` to orient the body and
`target` to make the action legible. Define a shared character once when many
shots use the same proportions:

```xml
<chardef name="Pip" height="30" radius="3.4"
         top="1" neck="0.72" pelvis="0.34" feet="0"/>
<dummy type="character" ref="Pip" camera="OilCanDiscovery"
       pos="258 0 -862" rot="0 153 0" pose="crouch"
       target="306 8 -912" color="0.16 0.72 0.95"/>
```

Camera binding is important in a one-scene/many-camera project. Without it,
every alternate pose is visible in every camera and one actor appears as a
crowd of duplicates.

**`type="lamp"`** — three orthogonal circles (XY, XZ, YZ planes) forming a
wireframe sphere cage around the light:

| Attribute | Type  | Default | Description |
|-----------|-------|---------|-------------|
| `radius`  | float | 15      | Cage radius in cm |

**`type="camera"`** — a pyramid from camera position toward the view
 target, with a rectangular base at distance 100 cm:

| Attribute | Type  | Default | Description |
|-----------|-------|---------|-------------|
| `look`    | vec3  | 0 0 -1  | Look-at target |
| `fov`     | float | 60      | Vertical field of view in degrees |

Lamp and camera dummies are **auto-generated** for every point light and
camera in the scene — no manual XML needed. Place `<dummy type="character">`
manually at each pose reference location:

```xml
<dummy type="character" pos="-100 0 -550" height="50" color="0.85 0.15 0.15"/>
<dummy type="character" attach="tool_bench:top_surface" pos="2 0 2" height="90"/>
```
