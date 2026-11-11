# ARCHITECTURE.md

## Overview

SimpleGL is a pure-C stencil-shadow demo renderer: a single executable reads an XML scene description, parses it into a geometry database, then renders it in real time using a GLSL PBR lighting shader plus fixed-function ambient, overlay, and stencil-shadow-volume passes on OpenGL 2.1.

## Modules

```
main.c ──► scene.c ──► shadow.c ──► render.c ──► shader.c
  │            │            │            │
  ▼            ▼            ▼            ▼
math.c      mesh.c       mesh.c       OpenGL 2.1
```

### math.c — Linear algebra

Provides `vec3` (3D vector) and `mat4` (4x4 column-major matrix). Core operations:

- **vec3**: add, sub, scale, mul, dot, cross, len, norm
- **mat4**: identity, mul, translate, scale, rot_x/y/z, rot_xyz (Euler, X→Y→Z order), perspective, lookat, xform_point, xform_dir
- **DA_PUSH**: generic dynamic-array push macro (`DA_PUSH(arr, len, cap, value)`).

All types are value types — passed and returned by value on the stack.

### mesh.c — Geometry primitives

Defines `Mesh` as a triangle soup: `Vertex*` (position + normal), `Tri*` (3 vertex indices), `Edge*` (2 vertex indices + triangle refs t0/t1). Supplied primitives:

| Function | Shape |
|----------|-------|
| `gen_box(w,h,d)` | Axis-aligned box centered at origin |
| `gen_sphere(r, rings, slices)` | UV sphere |
| `gen_cylinder(r, h, sides)` | Closed cylinder along Y |
| `gen_prism(r, h, sides)` | Regular N-gonal prism along Y |
| `gen_cone(rb, rt, h, sides, smooth)` | Truncated cone/frustum along Y; smooth normals follow the slope |
| `gen_torus(R, r, majSeg, minSeg)` | Torus in XZ plane |
| `gen_arch(w, h, d, wall, segments, inset)` | Solid Roman arch or hollow arch frame along Z |
| `gen_box_hole_arch(w, h, d, segments)` | Wall lunette above a Roman-arch opening |
| `gen_box_hole_cylinder(w, h, d, r, sides)` | Box profile surrounding a centered circular opening |

Mesh utilities: `mesh_transform` (applies model + rotation matrix), `mesh_compute_face_normals` (cross-product per triangle), `mesh_build_edges` (for shadow-volume silhouette detection), `mesh_flip_winding` (fixes inside-out geometry), `mesh_signed_volume` (winding consistency check).

#### Mesh construction policy

Primitive generators should work at the highest useful level. Prefer composing complete meshes from 2D profiles, symmetry, extrusion, and transforms over emitting individual vertices and triangles. The normal construction order is:

1. Describe the smallest non-repeating 2D profile.
2. Mirror that profile across X or Y when the shape is symmetric.
3. Extrude the profile to create caps and boundary walls.
4. Compose the resulting mesh regions, keeping shared boundary segmentation identical.
5. Apply modifiers to the finished local-space mesh.
6. Transform the mesh into world space only when it is added to the scene.

`extrude_polygon()` is the common implementation for boxes, cylinders, cylinder tubes, Roman arches, and circular or arched wall-opening pieces. It triangulates convex or concave caps, can emit caps or boundary walls independently, supports inward-facing hole boundaries, and can smooth circular side normals. This keeps triangulation and winding rules in one place.

Renderable 2D profile elements (`rect`, `circle`, `ellipse`,
`star`) use `<extrude>` to create a closed local-Z solid. A following `<bevel>`
insets the cap outline and adds rounded perimeter rings before later mesh
modifiers run. The same extrusion and bevel generator consumes each profile.
A `<shape>` with `<extrude>` renders through its own modifier stack
(`parse_shape_node()`): `shape2d_fillet()` rounds a line's bends or an
outline's corners, `<shell>` is `shape2d_stroke()` thickening the extruded line
to one side, and `shape2d_outline()` normalises the result before the same
extrusion and bevel generator builds the solid.
Inset profiles are checked
for collapsed or crossing edges so sharp concave shapes cannot create open
shadow meshes.

For symmetric shapes, generate one half or one 90-degree corner and mirror it. A solid Roman arch extrudes one mirrored outline. A framed arch composes mirrored quarter-arch cap profiles with rectangular sill and leg cap profiles, then extrudes only its outer and inner boundaries. A cylinder tube uses one annular quarter for its caps plus extruded outer and inner circles. A box around a circular opening uses one corner profile mirrored across both axes plus extruded box and circle boundaries.

Do not create overlapping or duplicate internal faces when composing profiles. Cap regions may share an edge, but that edge must appear once in each neighboring region with opposite directions. Boundary profiles must contain matching split points so cap edges and side-wall edges have identical endpoints.

#### Sealed-mesh invariant

Every renderable primitive should be a consistently wound, sealed mesh, and every shadow-casting mesh must be sealed. Each directed triangle edge must have exactly one oppositely directed mate. A valid outward-wound closed mesh has positive signed volume.

Open edges, crossed cap triangles, duplicate faces, mismatched boundary segmentation, and non-manifold seams break silhouette classification and can create unbounded stencil shadows. Primitive tests must therefore check positive volume, expected approximate volume, and zero open edges. Hole generators must also verify that no cap triangle crosses the intended opening.

**Modifiers** — mesh-level operations applied to local-space vertices before the scene transform:

| Function | Effect |
|----------|--------|
| `mesh_apply_taper(m, gizmo, amount, curve, symmetry, effect)` | Max's Taper: scale the cross-section by 1 + amount·z + curve·z(1−z) |
| `mesh_apply_twist(m, gizmo, angle, bias)` | Max's Twist: turn about the axis by angle·z, biased |
| `mesh_apply_bend(m, gizmo, angle, direction)` | Max's Bend: curl the axis through angle about the gizmo centre |
| `mesh_apply_stretch(m, gizmo, stretch, amplify)` | Max's Stretch: scale along the axis, squash across |
| `mesh_apply_skew(m, gizmo, amount, direction)` | Max's Skew: slide the cross-section by amount·z |
| `mesh_apply_noise`, `mesh_apply_shell`, `mesh_apply_mirror` | Max's Noise (smooth gradient noise, fractal), Shell (inner/outer), Mirror (planes, offset, copy) |
| `mesh_apply_array(m, count, translation, rotation)` | Duplicate and transform a complete mesh |

The deformers follow 3ds Max: a `deform_gizmo_t` gives the axis, the gizmo centre (by default on the axis at the
base of the bounds, where a Max primitive's pivot sits) and optional limits; z = a / H is a vertex's distance
from the centre over the gizmo's extent. Normals take the inverse transpose of each deformation's Jacobian.
Modifiers deform the already-complete mesh. XML child order is modifier order, so each modifier consumes the result of the previous one. Modifiers must preserve matching positions along welded edges; a modifier that separates formerly coincident boundary vertices will unseal the mesh. `array` operates on complete meshes rather than reconstructing their triangles.

### scene.c — XML parser + scene loader

**Tiny XML parser** (no external deps): `XmlNode` tree with `XmlAttr` key-value pairs. Handles tags, attributes with quoted values, comments, self-closing tags, and text content (ignored). Parser is recursive-descent operating on a `const char**` pointer.

**Scene data model** (`simplegl.h`):
- `Camera` — named viewpoint with position, look-at target, FOV
- `Material` — named material with RGB color and Phong shininess
- `Light` — point light with position, color, intensity, shadow-casting flag
- `SceneObj` — a transformed `Mesh` with resolved material/color, shininess, shadow flag
- `Scene` — aggregates: active camera (convenience fields), `Camera*` array, ambient color, background color, dynamic arrays of lights/materials/objects, plus one `ShadowVolume` per light

**Loading flow:**
1. `read_file()` reads the entire XML into a null-terminated buffer.
2. `xml_parse()` builds the `XmlNode` tree.
3. `load_scene()` reads `ambient` and `background` from `<scene>` attributes, then iterates root children through `scene_tags[]` dispatch table for `camera`, `material`, `sun`.
4. `parse_nodes()` iterates root children through `shape_parsers[]` dispatch table for `box`, `sphere`, `cylinder`, `prism`, `cone`, `pyramid`, `torus`, `group`, `wall`.
5. Each shape parser reads shape-specific attributes, generates a `Mesh`, calls `apply_modifiers()` to process any child `<taper>`, `<twist>`, `<bend>`, `<stretch>`, `<skew>`, or `<array>` elements, then calls `scene_add_obj()` to transform it, fix winding, compute normals, and build edges.

The complete mesh pipeline is:

```
profile composition → mirror/extrude → local-space modifiers → world transform
                    → winding correction → face normals → welded edge adjacency
                    → rendering and shadow-volume construction
```

**Dispatch tables** use static C arrays of `{ "tagname", parser_function }` to eliminate if-else chains:
```c
static const struct { const char *tag; shape_parser_fn parse; } shape_parsers[] = {
    { "box",      parse_box },
    { "sphere",   parse_sphere },
    ...
};
static const struct { const char *tag; modifier_parser_fn parse; } modifier_parsers[] = {
    { "taper",   parse_mod_taper },
    { "twist",   parse_mod_twist },
    ...
};
```

**Multiple cameras:** Each `<camera>` tag is stored in a `Camera*` array with a `name` attribute. `scene_select_camera()` chooses the active camera. Command-line `--camera Name` selects one render camera, while `--list-cameras` prints the names and comments without initializing graphics. If no cameras are defined, a default "Camera1" is created.

**Group support:** `parse_group` calls `parse_nodes` recursively, passing its accumulated `M` (model matrix with scale) and `R` (rotation-only matrix for normals). This enables nested coordinate-space hierarchies.

**Wall support:** `parse_wall` handles a `<wall>` tag with child `<opening>` tags. It performs "boolean via boxes" — the wall length axis is partitioned at opening boundaries, and each segment produces rectangular boxes below/above openings or across uninterrupted spans. Walls ignore scale; `castShadow` applies to the generated boxes.

### The renderer follows Doom 3

Lighting, shadows, memory layout and frame structure are modelled on Doom 3 BFG (`neo/renderer` in the GPL source).
When changing the renderer, read the Doom 3 code named below first and keep the correspondence.

| Scener | Doom 3 BFG | What it does |
|---|---|---|
| front end then back end in `render_frame()` | `tr_frontend_*.cpp`, `tr_backend_draw.cpp` | The front end decides what is drawn, builds and uploads it; the back end only draws |
| `Scene.models`, `SceneObj.mesh/model/normalM` | `idRenderModel`, `idRenderEntityLocal::modelMatrix` | Geometry stored once, placed by matrices |
| `vertex_cache.c` | `VertexCache.h` | Models and shadow indices are ranges of a few large buffers |
| 20-byte GPU vertex, 16-bit indices | `idDrawVert`, `triIndex_t` | Compact vertices, packed normals |
| `light_bounds()`, `box_screen_rect()` | `R_AddSingleLight`, `ProjectedFullyClippedBounds` | A light's scissor from its near-clipped bounds; empty drops the light |
| lit list and its union per light | `R_OptimizeViewLightsList` | Nothing lit on screen: no shadows drawn either |
| `shadow_bounds()` | `R_ShadowBounds` | A caster is drawn only if the box its shadow can reach meets the lit rectangle |
| `view_may_be_inside()` | `R_ViewPotentiallyInsideInfiniteShadowVolume`, `renderZFail` | Z-fail with caps only where the view may be inside the volume, z-pass with sides elsewhere |
| `build_shadow_indices()`, `Edge` | dynamic shadow volume job, `silEdge_t` | A volume is indices only: 2i is vertex i, 2i+1 the same vertex at infinity |
| `s_shadow_vs` | `renderprogs/shadow.vertex` | The shader pushes the 2i+1 vertices away from the light |
| light carried into model space | `localLightOrigin` in `Interaction.cpp` | Silhouettes are found on the shared model, not on a world copy |
| batches sorted by model and state | draw surfaces sorted by material | Fewer state changes |
| `<light range>`, `LIGHT_CUTOFF` | light volumes with a hard edge | What makes culling by light bounds possible |

Not taken from Doom 3, and why: the depth-bounds test (macOS OpenGL has no such extension); portal areas (scenes
are small and open); precomputed pre-light shadow volumes and parallel jobs (not needed yet). Per-frame shadow
volumes in a frame-temporary buffer, which Doom 3 uses for moving geometry, become the right design once animated
characters stand near shadow-casting lamps; static pairs should stay cached as they are. Added on top of Doom 3:
instancing.

Measured dead ends, so they are not tried again: drawing a model's whole edge set instanced and picking the
silhouette in the vertex shader cut shadow draws 3.5 times but cost 50 MB more on a tile-based GPU and no time;
one GL buffer per mesh or per shadow volume cost over a gigabyte in driver regions; `glFinish` per light lowers peak
driver memory but blocks the interactive viewport.

### Shared models

`scene_add_obj()` looks the generated mesh up by content (`Scene.models`); every `SceneObj` keeps a pointer to
that shared model in its own space plus `model`, `normalM` and `mirrored`, and `bmin`/`bmax` are its world bounds.
Code that needs world-space triangles (`--check`, exporters, tests) asks `scene_obj_world_mesh()` for a copy.
Dragging changes the matrix, never the shared vertices. A model is immutable once shared.

### vertex_cache.c — GPU storage

Models (20-byte vertices with packed normals, 16-bit indices) and shadow index lists are sub-allocated from a few
large buffers. The macOS driver gives every GL buffer its own memory region, so one buffer per mesh costs
gigabytes. Data is staged on the CPU and sent once per chunk by `vcache_flush()`: every upload to a buffer the GPU
is using makes the driver keep another copy, and an upload between draws ends its render pass.

### shadow.c — Stencil shadow volumes

1. `mesh_build_edges()` welds coincident positions and records the two triangles of each edge.
2. `build_shadow_indices()` classifies triangles against the light (in model space); an edge is a silhouette when one
   of its triangles faces the light and the other does not. It emits a quad per silhouette edge, then the caps.
3. The shadow shader reads positions from the model's own buffer; odd indices are pushed to infinity (`w=0`).

Volumes are built the first time a view needs a (light, caster) pair and kept until `Scene.shadowRev` changes.
Casters the view cannot be inside draw z-pass with sides only; the others draw z-fail with caps, under depth clamp.

Shadow construction depends on the sealed-mesh invariant. An open mesh gives an edge only one adjacent face, so
the builder treats it as a boundary silhouette; the resulting volume is generally not closed and stencil counts
cannot cancel reliably. Fix the primitive instead of special-casing its shadows. `castShadow="0"` is for
intentionally non-shadowing geometry, not for hiding invalid topology.

### render.c — OpenGL rasterizer

Core-profile OpenGL 3.2. A frame is:

1. **Front end** (no drawing): frustum-cull objects; for each light compute its screen rectangle, its lit objects
   and its shadow casters; sort objects into batches of one model and state; fill the instance buffer (matrix,
   normal matrix, colour, material, gradient per object, read by the vertex shader from a buffer texture); build
   missing shadow indices; `vcache_flush()` and upload instances once.
2. **Depth-filling pass**: one instanced draw per batch: ambient light plus the sky mirrored in glossy surfaces
   (`skyReflection`). Screens with an image draw as images.
3. **Per light**, under the light's scissor: clear stencil; draw its casters' volumes with color and depth writes
   off (z-pass or z-fail per caster, each under its own scissor); then one instanced, additively blended draw per
   lit batch where stencil is 0: diffuse plus GGX specular (`specular`, `metallic`), with the light's `range` window.
4. **Debug modes**: wireframe, flat, wireframe shadow volumes.

### Color-space boundary

Scene XML and the in-memory `Scene`, `Material`, `Light`, and `SceneObj` color
fields hold author-facing sRGB values. Light intensity is a linear scalar and
is stored separately; it is never subjected to a color-space conversion.

`render.c` and `shader.c` convert RGB colors from sRGB to linear exactly once
when preparing fixed-function draw colors or shader uniforms. All ambient,
diffuse, specular, attenuation, and additive-light calculations therefore use
linear values. `GL_FRAMEBUFFER_SRGB` converts the completed linear fragment to
sRGB when it is written to the display framebuffer. Non-color values—light
intensity, positions, directions, radius, shininess, normals, and transforms—
remain unchanged.

The current `srgb_to_linear()` helpers use `pow(component, 2.2)` as the sRGB
transfer-curve approximation. Keep that implementation detail at the renderer
boundary; scene authors still provide ordinary sRGB picker values.

Do not move gamma conversion into scene loading: keeping the scene model in
authored sRGB form preserves a clear serialization contract and prevents code
that consumes scene colors from accidentally converting them twice. Do not
gamma-correct light intensity or combine it with authored light color before
the renderer boundary.

### main.c — Application loop

Orion application lifecycle, command accelerators, document creation, and
offscreen screenshot dispatch. Window and input events arrive through Orion's
message loop; viewport interaction is owned by `win_viewport.c`.

## Data flow

```
XML file
  │
  ▼
scene.c: load_scene() ──► Scene (lights[], objs[], mats[])
  │
  ▼
shadow.c: scene_build_all_shadow_volumes() ──► ShadowVolume per light
  │
  ▼
render.c: render_frame(scene, w, h, proj, view, cameraPosition)
  │
  ▼
OpenGL framebuffer ──► Orion platform renderer
```

## Adding features

- **New primitive shape**: Add the `gen_*` declaration to `simplegl.h`, implement it in `mesh.c`, and add `parse_*` to `shape_parsers[]` in `scene.c`.
- **New scene tag**: Add `parse_*_tag` to `scene_tags[]` in `scene.c`.
- **New render pass**: Modify `render_frame()` in `render.c`.
- **New shadow technique**: Modify `build_shadow_volume()` in `shadow.c`.
