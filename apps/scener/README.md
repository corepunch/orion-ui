# Scener

Current Orion build, batch rendering and deployment instructions are in
[CLI.md](CLI.md).

A very small modular OpenGL scene renderer with real-time **stencil shadow
volumes**. Shadows are a first-class composition tool: position lights and
casters to create long silhouettes, pools of light, strong contrast, and
dramatic architectural shots. Scenes are described in XML and built from
walls, furniture, and basic primitives.

Scener is an Orion application. Its renderer is implemented by small C modules
with shared declarations in **`simplegl.h`**. Scene parsing has no external XML
or shader-file dependency; windowing, input, image output, and UI are provided
by Orion and its platform layer.

## Workflow

![SimpleGL workflow: AI prompt → 3D scene → render → AI overpaint](docs/workflow.jpeg)

First, generate a 3D scene with AI via a prompt. Use SimpleGL to render it
from multiple cameras — the same lighting, shadows, and spatial relationships
stay consistent across every angle. Then feed those renders back to AI as
image-to-image references to draw over into final art. The 3D scene acts as a
**multi-view anchor** that keeps compositions, silhouettes, occlusions, and
lighting coherent when an illustration needs multiple shots of the same
location.

The [cinematic overpaint section](#from-scene-layout-to-cinematic-overpaint)
below walks through a concrete example using the Workshop scene.

## Build & run

```sh
make scener
./build/bin/scener apps/scener/scenes/sample_room.blks
```

Run these commands from the Orion repository root. Scener uses Orion's native
platform backend and OpenGL renderer on macOS, Linux, and Windows.

For XML authoring, start with the canonical
[`scene-format.md`](skills/populate-simplegl-scenes/references/scene-format.md)
reference. It documents `.blks` scenes, `.blk` prefabs, every supported tag
and attribute, defaults, units, transforms, materials, lights, cameras,
primitives, groups, cutters, modifiers, attachments, validation, and rendering.

For ellipsoid characters and camera-specific poses, use the
[character authoring guide](docs/character-authoring.md) and
[character population skill](skills/populate-scener-characters/SKILL.md).
The [pose study](docs/character-pose-study.md) contains a working prefab,
three rendered poses, and an assessment of animation and IK gaps.

Install Scener together with every Orion application, GEM, runtime library,
asset, example, and offline documentation file with:

```sh
make install
```

`PREFIX` defaults to `/opt/orion`; add `/opt/orion/bin` to `PATH` after
installation. Package recipes can override the prefix and stage without
modifying the host, for example
`make install PREFIX=/usr DESTDIR="$PWD/package-root"`. A dependent project
should treat Scener as an external tool:

```make
SCENER ?= scener

.PHONY: check-scener render-scenes
check-scener:
  @command -v "$(SCENER)" >/dev/null || { \
    echo "scener is required"; exit 1; \
  }

render-scenes: check-scener
	$(SCENER) --render scenes/main.blks --camera Main --output-dir build/render
```

This keeps Orion and its platform implementation private to the installed
suite; downstream projects do not need either repository as a submodule. A
Homebrew formula can call the same target with `PREFIX=#{prefix}`.

**Controls:** mouse looks around, `W A S D` move, `Q`/`E` move down/up,
`Shift` moves faster, and `Esc` quits. Rendering diagnostics are selected from
the View menu. Shadow-volume and stencil diagnostics are rendered as red
overlays; they are not the normal camera view.

## Reusable prefab variation

Prefab instances support complete `pos`, `rot`, `scale`, and `pivotOffset`
transforms. Their named attach points follow the same transform. A prefab may
mark selected child shapes with `tint="1"`; `color` on an instance then
replaces only those shapes' diffuse color while preserving their material
shininess and leaving unmarked parts unchanged.

```xml
<!-- The book prefab marks covers tintable, but not its paper page block. -->
<prefab source="items/book" color="0.58 0.08 0.06"/>
<prefab source="items/book" pos="0.5 0 0" color="0.08 0.22 0.56"/>
```

Important authoring features still missing are named multi-color material
slots, per-camera visibility or state variants, animation, object/layer names
for CLI inspection, orthographic and aspect-safe cameras, textures and alpha,
and area lights or soft shadows. Prefab-wide `material`, `shininess`,
`castShadow`, and `renderable` inheritance are also not implemented; child
shapes continue to own those properties.

## Rendering modes

### Agent and batch rendering

Use `--render` after modifying a `.blks` scene or any referenced `.blk`
prefab. Scener creates an OpenGL context without displaying application UI,
loads the scene, and writes one clean render per camera without editor overlays:

```sh
./build/bin/scener --render apps/scener/scenes/sample_room.blks
```

The required argument is a `.blks` scene or `.blk` prefab. A prefab opened
directly uses Scener's default preview camera and lights. Resolution defaults
to `1280x800`, JPEG is the default format, all scene cameras are rendered by
default, and output defaults to the current directory. Camera names
become filenames such as `Main.jpg`. Inspect scene cameras or override
each optional value:

```sh
./build/bin/scener --list-cameras apps/scener/scenes/sample_room.blks
./build/bin/scener --render apps/scener/scenes/sample_room.blks \
  --size 1600x900 --camera Main --format jpg --output-dir build/room-renders
```

Use `--format png` or `--format jpg`; the selected extension is added to every
camera filename automatically.

Inspect the complete command syntax or installed version without creating a
window or OpenGL context:

```sh
./build/bin/scener --help
./build/bin/scener --version
```

The process exits nonzero when the scene, camera, output directory, graphics
context, or image write fails, so it can be used directly as an automated
re-render step.

Normal rendering uses filled materials, every scene light, and each light's
`castShadows` setting to produce the full lit stencil-shadow result. This is
the default for both interactive and batch camera views:

```sh
./build/bin/scener --render apps/scener/scenes/sample_room.blks
```

Render with the same filled materials and lighting but disable stencil
shadows. This is useful for determining whether a visual artifact comes from
scene geometry or a shadow volume:

```sh
./build/bin/scener --render apps/scener/scenes/sample_room.blks -no-shadows
```

Render scene geometry as unlit white wireframes:

```sh
./build/bin/scener --render apps/scener/scenes/sample_room.blks -wireframe
```

The same batch interface makes direct mode comparisons reproducible:

```sh
./build/bin/scener --render apps/scener/scenes/sample_room.blks --camera Main --output-dir render/shaded
./build/bin/scener --render apps/scener/scenes/sample_room.blks --camera Main -no-shadows --output-dir render/unshadowed
./build/bin/scener --render apps/scener/scenes/sample_room.blks --camera Main -wireframe --output-dir render/wireframe
```

Interpret these outputs as follows:

- `render/shaded/Main.jpg` is the expected production camera view: filled,
  material-colored, lit, and shadowed.
- `render/unshadowed/Main.jpg` is still filled and lit, but receives no stencil
  shadows.
- `render/wireframe/Main.jpg` shows unlit white scene geometry. For the
  upstream red shadow-volume diagnostic, use `-d 2`.
- Dense black triangular streaks in a default render are not wireframe mode.
  On a supported GPU they can indicate an invalid or open shadow-casting mesh.
  The known-bad Apple Software Renderer is rejected for shadow exports.
  Compare against `-no-shadows`, then repair the
  caster topology or set `castShadow="0"` only when the object intentionally
  must not cast a shadow.

## From scene layout to cinematic overpaint

[`scenes/books/wondertown/workshop.blks`](scenes/books/wondertown/workshop.blks)
is a fully dressed toymaker's workshop assembled from reusable prefabs and
simple primitives. A monumental desk with carved legs dominates the back wall;
a stocked commode and ornate cuckoo clock hang beside a moonlit window. On the
opposite side, a workbench bristles with tools, books, jars, and curios, while
a wooden ladder climbs to a loft stacked with crates. Sawdust scatters the
floorboards. A single blue lamp and a cold moonbeam through the door flap
provide dramatic low-key lighting that pushes long shadows across every
surface.

![Overhead wireframe of the workshop layout](docs/crossfade.jpeg)

Eleven named cameras follow Pip — a thumb-scale toymaker figure — through every
story beat: entering through the moonlit door, discovering clues on the
commode, crouching under the desk toward a copper oil can, climbing the
workbench leg, and traversing the ladder into the loft. Each camera reuses the
same geometry, materials, and lights, so spatial relationships lock regardless
of framing.

The comparisons below use the untouched SimpleGL output as an image-to-image
guide for an AI overpaint. Material detail and atmosphere are added, but the
camera, silhouettes, object placement, occlusion, lighting direction, and
major cast-shadow shapes are constrained by the source render.

| SimpleGL camera render | AI overpaint |
|---|---|
| ![Raw LadderTraversal camera render](docs/preview.jpeg) | ![AI overpaint of the LadderTraversal camera render](docs/final.jpeg) |
| `LadderTraversal` — Pip works the rusty winch from the ladder, framed through a wide vertical composition | Lighting, silhouette, and spatial cues from the 3D render anchor the overpaint. |

Reproduce any camera in the scene with:

```sh
./build/bin/scener --render apps/scener/scenes/books/wondertown/workshop.blks --camera LadderTraversal
```

## Why these choices

- **Core-profile GL with explicit vertex buffers.** Meshes, overlays, gizmos,
  and homogeneous stencil-shadow volumes use dedicated VAOs/VBOs and GLSL
  programs, so the renderer does not depend on compatibility-only immediate
  mode calls.
- **Own tiny XML parser**, not tinyxml/libxml. Pulling in libxml2 means
  linking a large C library (and its dependency chain) for a schema that's
  really just `<tag attr="val">`. tinyxml2 is closer to reasonable, but it's
  C++ and still an extra file/dependency to vendor. The recursive-descent C
  parser covers the schema below without another runtime dependency. If your
  scenes get much more complex you'd
  want to swap in a real parser — the loader code is isolated in
  `xml_parse()`/`load_scene()` so that's a contained change.
- **Wall profiles share one cutout path.** Wall sections and trims subtract the
  same door/window/negative profiles before extrusion. This preserves matching
  apertures across colors and projecting trim bands, including curved, stacked
  and overlapping openings. It remains wall-specific geometry, not arbitrary CSG.
- **Static scene ⇒ shadow volumes precomputed once.** Nothing in the scene
  format can move, so silhouette/volume computation happens once at load
  (`scene_build_all_shadow_volumes`), not per frame. If you add moving
  lights or objects later, call `build_shadow_volume()` again per-frame for
  whatever changed — the function is already factored out for that; only
  `main()`'s "compute once" call site needs to move into the render loop.

## Shadow algorithm

Classic robust **z-fail (Carmack's Reverse)** stencil shadow volumes, one
point light per pass:

The side-only, infinitely extruded Z-pass implementation is available with
`make zpass`, `make run-zpass`, and `make test-zpass`. These targets compile
with `USE_ZPASS` and write separate `*-zpass` binaries under `build/bin`.

1. **Ambient pass** – draw everything flat-shaded at `ambient * color`, with
   depth writes on. This seeds the depth buffer and the base (unlit) look.
2. For each shadow-casting light:
   a. **Stencil pass** – draw the light's closed shadow volume (silhouette
      side quads + light/dark caps) with color writes off, depth writes off,
      `glStencilOpSeparate`: back faces `INCR_WRAP` / front faces
      `DECR_WRAP` on depth-fail. This is the part that's still correct even
      when the camera itself is inside a shadow.
      Extruded vertices use homogeneous directions (`w=0`), and depth clamp
      prevents near/far clipping. If depth clamp is unavailable, stencil
      shadows are disabled and the lights render without shadows.
   b. **Lit pass** – redraw the scene fully lit (diffuse+specular, ambient
      zeroed to avoid double-counting), additively blended (`GL_ONE,
      GL_ONE`), only where the stencil value is `0` (i.e. *not* in shadow).
3. Non-shadow-casting lights just get an unconditional additive lit pass, no
   stencil test.

Silhouette detection needs a *flat* face normal per triangle
(`Mesh.triN`, computed straight from triangle vertex positions) — this is
kept separate from the shading normal (`Vertex.nrm`, smooth for
sphere/torus, per-facet for box/prism/cone) so the shadow math never cares
how a mesh happens to be shaded.

## XML scene authoring

Scene construction, placement conventions, prefab rules, the complete XML
schema, and required CLI validation live in the canonical
[`scene-format.md`](skills/populate-simplegl-scenes/references/scene-format.md)
reference. Agents creating or changing scene and prefab XML must also follow
the [`populate-simplegl-scenes`](skills/populate-simplegl-scenes/SKILL.md)
workflow.

## Known limitations (all fixable, kept out on purpose for scope)

- One shadow-casting point light is exercised in the sample and is what the
  algorithm is written for; multiple lights work (the render loop already
  iterates `<light>` entries) but cost is O(lights × objects) shadow-volume
  triangles per frame relatively fast since they're precomputed once.
- Non-uniform scale (`scale="1 1 3"` on a rotated object) will slightly
  distort shading normals — normals use the rotation part of the transform
  only, not a full inverse-transpose. Fine for boxes/furniture; would matter
  for a heavily stretched sphere.
- No texturing — flat/vertex colors only.
- Wall cutters must be parallel to the wall; oblique and arbitrary mesh CSG are unsupported.

## Procedural windows

Use `<window preset="round-arch|cottage|gothic">` for a fixed window whose
outer profile also cuts matching walls. Frame, pane and optional sill share one
source element; `style="storybook"` supplies a thicker frame default.
The Create menu includes all three presets. See the [window format reference](skills/populate-simplegl-scenes/references/scene-format.md#window)
and [three-window review scene](tests/procedural_windows.blks).

## Procedural walls and floors

`<wall>` supports `lowerHeight`, separate `lowerMaterial`/`upperMaterial` or
`lowerColor`/`upperColor`, and optional `bottomTrimHeight`, `middleTrimHeight`,
`topTrimHeight`. Set `trimMaterial`, `trimDepth` and `trimSide` once, with optional
per-trim overrides. Door and window openings cut all sections and trims together.

`<floor style="boards|squares|hexes|stones">` (choose one style) fills its footprint
with clipped edge pieces over a backing slab. Set `tileWidth`, `tileLength`,
`gap`, `colorVariation` and `seed`; the top remains at local Y=0. The workshop now
uses these elements for its two-tone walls, rails and staggered board floor.

See the [wall/floor schema](skills/populate-simplegl-scenes/references/scene-format.md#wall)
and [surface review scene](tests/procedural_surfaces.blks) for parameters and examples.
