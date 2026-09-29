# Scener as a studio for 3D infographics

Scener's direction is an editable studio for explaining ideas through 3D
subjects, motion, labels, and measurements. Character demonstrations, product
breakdowns, process illustrations, and animated comparisons share the same
scene, camera, lighting, and export foundations. Scene layout and
illustration rendering remain supported uses of that foundation.

Explanations are [reels](reels.md): XML documents that place 3D scenes as
layers and add type, shapes, joint-anchored callouts, trails and measured
readouts, all as expressions of time. Scener renders the whole piece itself
and encodes the MP4 directly; there is no external compositor, overlay script
or image-sequence step. The first worked example is
[IK vs FK](../scenes/infographics/README.md).

## What exists today

| Capability | Implementation and scope |
| --- | --- |
| Editable 3D content | XML scenes/prefabs, primitives, profile extrusion/beveling, materials, named cameras, and lights |
| Character library | Source-derived Ecstatica models and calibrated CAT controls; source geometry/provenance retained |
| FK and IK | Joint rotations and re-aims, reusable poses; two-bone limb/tip constraints with world or body-frame targets, poles and reach diagnostics |
| Animation | Timed clips, smooth/linear/step interpolation, layers, masks, fades, additive motion, procedural gait, BVH retargeting |
| Programmable graphics | Reel expressions of `t`, curves, lets, motion presets, word and character reveals |
| Type | SDF text at any size with kerning, tracking, tabular digits and weight |
| Annotations | Joint anchors projected through each layer's camera: chains with joint markers, leaders, crosshairs, sampled trails, live numeric readouts |
| Measurements and checks | World-space joint positions at float precision, `dist()`, `<check>` rules on every frame (`--check`) |
| Delivery | MP4 (VideoToolbox on macOS, ffmpeg elsewhere), PNG/JPEG stills and sequences, from one `scener --reel` run |
| Scene inspection | `--list-cameras`, `--list-joints` at `--time` |

## Source map

| Path under `apps/scener/` | Responsibility |
| --- | --- |
| [docs/reels.md](reels.md) | Reel format, expressions, elements, CLI, limits |
| `reel.c`, `reel_expr.c` | XML loading, per-frame evaluation, checks; expression compiler and VM |
| `reel_draw.c` | Batched GL renderer: SDF shapes and glyphs, stencil polylines, scene layers |
| `reel_video.c` | H.264 encoding and MP4 muxing |
| `orion/user/font_sdf.c` (repository root) | Signed-distance glyph atlas |
| [scenes/infographics/](../scenes/infographics/README.md) | The IK/FK scene and reel |

## Workflow

1. Author the scene and its motion (`.blks`), and inspect joints with
   `--list-joints` when choosing anchors.
2. Write the reel: a `<scene>` layer with its camera and placement, then
   styles, lets for measurements, `<check>` rules for what the explanation
   claims, and the graphics.
3. `scener --reel FILE --check` validates the motion without a GPU.
4. `scener --reel FILE --output still.png --time T` for review stills at the
   extremes; then `--output FILE.mp4 --poster FILE.png` for delivery.

Checks are the contract of an explanation: if a caption says the feet stay
planted, a check says so to 0.01 cm on every frame. They measure joints, not
mesh contact; silhouettes, hand appearance and shadows still need visual
review. Joint overlays deliberately draw over occluding geometry.

## Development direction

The following are proposed extensions, not implemented capabilities:

1. **Editor preview.** Open a reel in the viewport, scrub its timeline and
   edit attributes live; the evaluation is already a pure function of `t`.
2. **More vocabulary.** Arrowheads, images, gradients, text wrapping and
   callout layout, charts bound to lets, and motion blur by sub-frame
   accumulation.
3. **Camera shots.** Animated cameras and cuts authored in the reel.
4. **Faster evaluation.** Re-posing characters without rebuilding the whole
   scene each frame, and overlapping GPU readback with encoding.

As features land, update [reels.md](reels.md), add them to the IK/FK reel or
a new example, and cover them in `tests/scener_reel_test.c`.
