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
| `gen_cone(rb, rt, h, sides)` | Truncated cone/frustum along Y |
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

Renderable 2D profile elements (`rect`, `rounded-rect`, `circle`, `ellipse`,
`star`) use `<extrude>` to create a closed local-Z solid. A following `<bevel>`
insets the cap outline and adds rounded perimeter rings before later mesh
modifiers run. The same extrusion and bevel generator consumes each profile.
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
| `mesh_apply_taper(m, amount, curvature)` | Scale X,Z non-uniformly along Y |
| `mesh_apply_twist(m, angle_deg)` | Rotate around Y proportional to Y |
| `mesh_apply_bend(m, angle_deg)` | Map Y axis into circular arc in XY plane |
| `mesh_apply_stretch(m, amount, amplify)` | Non-linear squash/stretch along Y |
| `mesh_apply_skew(m, amount)` | Shear X,Z proportional to Y |
| `mesh_apply_array(m, count, translation, rotation)` | Duplicate and transform a complete mesh |

Deformation modifiers accept an X, Y, or Z axis, compute the mesh bounds on that axis, and deform the already-complete mesh. XML child order is modifier order, so each modifier consumes the result of the previous one. Modifiers must preserve matching positions along welded edges; a modifier that separates formerly coincident boundary vertices will unseal the mesh. `array` operates on complete meshes rather than reconstructing their triangles.

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

### shadow.c — Stencil shadow volumes

Implements the classic vertex-shader-free algorithm:
1. For each triangle edge: classify as silhouette edge if one adjacent face faces the light and the other faces away.
2. Extrude silhouette edges away from the light as homogeneous directions with `w=0`.
3. Close each volume with finite light caps and infinite dark caps.
4. Store the sides and caps as a homogeneous triangle list in `ShadowVolume.verts`.

`scene_build_all_shadow_volumes()` builds a shadow volume per light for every shadow-casting object.

Shadow construction depends on the sealed-mesh invariant. `mesh_build_edges()` welds coincident positions and records the two adjacent triangles for each edge. `build_shadow_volume()` compares those two face directions against the light; an edge is a silhouette only when one face points toward the light and the other points away. It extrudes only those silhouette edges, then uses the original mesh triangles as the finite and infinite caps for Z-fail.

An open mesh gives an edge only one adjacent face, so the shadow builder treats it as a boundary silhouette. The resulting volume is generally not closed and stencil increments cannot cancel reliably. Fix the primitive instead of special-casing its shadows. `castShadow="0"` is for intentionally non-shadowing geometry, not for hiding invalid topology.

### render.c — OpenGL rasterizer

Uses a hybrid OpenGL 2.1 pipeline. Rendering passes:

1. **Ambient pass**: Enable depth-write, disable stencil-write. Draw all objects with fixed-function vertex colors containing the linear material × ambient product.
2. **Per-light pass** (for each light):
   - Clear stencil buffer to 0.
   - **Shadow volume pass (Z-fail)**: Disable color writes and depth writes, enable required depth clamp, and update stencil on depth failure. Render the closed, infinitely extruded volume. If depth clamp is unavailable, log that stencil shadows are unsupported and render the lights without shadows.
   - **Lighting pass**: Enable additive blending, depth-test LEQUAL, and stencil-test EQUAL to 0. Draw diffuse plus GGX Cook-Torrance specular lighting through `shader.c`.
3. **Debug modes**: wireframe shadow volumes (red), stencil != 0 overlay (red).

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
