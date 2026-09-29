# Scener as a studio for 3D infographics

Scener's direction is an editable studio for explaining ideas through 3D
subjects, motion, labels, and measurements. Character demonstrations, product
breakdowns, process illustrations, and animated comparisons can share the
same scene, camera, lighting, and export foundations. Scene layout and
illustration rendering remain supported uses of that foundation.

The first worked example is [IK vs FK](../scenes/infographics/README.md): two
copies of Ecstatica II's Joe perform the same torso turn while one hand follows
FK and the other holds an IK target. The 3D scene is native Scener; typography
and explanatory graphics are composed by an external helper. Opening the
`.blks` file shows the animated 3D content, not the finished labeled layout.

## What exists today

| Capability | Implementation and scope |
| --- | --- |
| Editable 3D content | XML scenes/prefabs, primitives, profile extrusion/beveling, materials, named cameras, and lights |
| Character library | Source-derived Ecstatica models and calibrated CAT controls; source geometry/provenance retained |
| FK | Joint rotations and re-aims, hierarchical transforms, reusable poses |
| IK | Two-bone limb/tip constraints, world targets or body-frame offsets, bend/pole controls, orientation preservation, reach diagnostics |
| Animation | Timed clips, smooth/linear/step interpolation, layers, masks, fades, additive motion, procedural gait, BVH retargeting |
| Source action previews | Experimental decoded Ecstatica actions; timing/transforms are approximations, not game-runtime emulation |
| Scene inspection | `--list-cameras`, `--list-joints`, and evaluation at `--time`; joint output is world centimetres rounded to 0.1 cm |
| Frame rendering | `--render --frames START:END:FPS`, PNG/JPEG, supersampling, GPU lighting and stencil shadows |
| Infographic graphics | Example compositor draws titles, captions, projected joint chains, target crosshairs, leaders, a hand trail, numeric readouts, and time/progress |
| Motion checks | Example driver checks wrist-target error, arm lengths, fixed foot positions, and visible FK displacement at output times |
| Delivery | Annotated PNG poster and H.264 MP4 through the existing macOS AVFoundation encoder |

The IK/FK work added an example scene, render/validation driver, annotation
compositor, and documentation. It did **not** change the C renderer, rig solver,
animation system, XML schema, or editor UI. Native scene features in the table
predate this example; infographic composition and checks currently live in
the helper tools.

## Source and output map

| Path under `apps/scener/` | Responsibility |
| --- | --- |
| [scenes/infographics/ik_fk.blks](../scenes/infographics/ik_fk.blks) | Studio geometry, lights, Comparison camera, two Joe instances, torso clip, FK pose, and wrist IK target |
| [characters/joe/prefabs/character.blk](../characters/joe/prefabs/character.blk) | Existing character geometry and calibrated CAT rig; unchanged by the demonstration |
| [tools/render_ik_fk_infographic.py](../tools/render_ik_fk_infographic.py) | Sample joints, check motion, render frames, compile/run the compositor and encoder |
| [tools/compose_ik_fk_infographic.swift](../tools/compose_ik_fk_infographic.swift) | Project measured joints into the image and draw the explanatory layout with AppKit/CoreGraphics |
| [tools/frames_to_mp4.swift](../tools/frames_to_mp4.swift) | Existing image-sequence encoder using AVFoundation |
| [scenes/infographics/README.md](../scenes/infographics/README.md) | Example-specific motion, staging, scale, commands, and review criteria |

Source scenes, tools, and docs belong in Git. Generated media and intermediate
files default to repository-root `video/ik-fk/`, covered by the `/video/`
ignore rule. A custom output outside that directory needs its own ignore rule
if it is inside the repository.

## Render workflow

Run from the repository root on macOS:

```sh
make build/bin/scener build/bin/test_scener_input_test
DYLD_LIBRARY_PATH="$PWD/build/lib" ./build/bin/test_scener_input_test

# One annotated poster; preserve an existing full export in another directory.
python3 apps/scener/tools/render_ik_fk_infographic.py \
  --preview --output video/ik-fk-preview

# Full motion, measurements, annotated frames, poster, and MP4.
python3 apps/scener/tools/render_ik_fk_infographic.py
```

`--scener PATH` chooses a compatible renderer; the default is
`build/bin/scener`, with `build/lib` supplied as the library search path.
`--output PATH` changes the destination. `--help` lists these options.
Python uses only its standard library. The compositor and encoder require
macOS command-line tools with Swift, AppKit, and AVFoundation. Shadowed
rendering needs a logged-in graphical session and hardware GPU access; see
[CLI.md](../CLI.md#gpu-access-and-shadow-validation).

The driver performs these steps in order:

1. Evaluate `Comparison` at each sample time with `--list-joints`. A full run
   samples 0 through 6 seconds inclusively at 24 fps; preview samples 0 and 1.5.
2. Require both figures' named arm and foot joints, check wrist contact and
   segment lengths, verify stationary feet, and require visible FK movement.
3. Save `measurements.json`, then render 1600×640 images using the same scene,
   camera, and times. Preview renders only the 1.5-second image.
4. Project the measured joints and compose 1600×1000 frames, with text areas
   above/below the 3D image. Overlays deliberately draw over occluding geometry.
5. Encode the full sequence at 24 fps and select frame 36 as the poster.

The full run writes `ik-vs-fk.mp4`, `ik-vs-fk.png`, `raw/Comparison_NNNN.png`,
`frames/Comparison_NNNN.png`, `measurements.json`, and `render.log`. Temporary
Swift binaries/cache also live in the output directory. The inclusive final
frame gives 145 frames, so the encoded duration is about 6.04 seconds. The
character motion loops; the progress bar and accumulated trail reset when
playback restarts. The player controls whether the MP4 repeats.

Preview writes `preview-raw/Comparison.png`, the poster, measurements, and
render log. Reusing an output directory overwrites those files; preview leaves
an older MP4 unchanged. It is not a cache or freshness check. Use a separate
preview destination, then rerun the full export after accepting changes.

## Measurements and annotation contract

`measurements.json` has top-level `fps`, `seconds`, `target`, `preview`, `scene`,
and `samples` fields. Every sample contains `time`, `joints`, `drift`, and
`error`. `joints` maps the instance names `FK` and `IK` to `left_upperarm`,
`left_forearm`, `left_palm`, `left_foot`, and `right_foot`, each an `[x,y,z]`
position in world centimetres.

`drift` is the Euclidean distance from the FK wrist's initial position.
`error` is the distance from the IK wrist to the authored target. The arm
checks compare shoulder–elbow and elbow–wrist distances to 22.356 and 22.149 cm;
foot checks compare against their initial positions. Tolerance is 0.18 cm
because each printed coordinate is rounded to 0.1 cm. A reported zero is
rounded sample agreement, not proof of zero solver error. The graphic displays
IK error as `< 0.2 cm` after validation.

These foot checks measure joint stability; they do not perform mesh contact
or collision tests. Actual support, silhouette, hand appearance, and shadows
still require rendered review. The trail and markers derive from evaluated
joints; they are not an independently drawn approximation of the motion.

The current compositor assumes camera position `(0,−650,210)`, look point
`(0,0,75)`, vertical FOV 20°, and a 1600×640 image placed at y=205 in the final
layout. It uses those values in `project()` instead of querying Scener for
camera matrices. Its crosshairs use the initial wrist samples; the IK sample
coincides with the validated fixed target. Moving targets need a different
annotation treatment.

## Editing and extending the example

| Change | Files that must agree |
| --- | --- |
| Wording, colors, line styles, or typography | Swift compositor; recompose/review the affected frames |
| Torso motion or arm pose | Scene clip/joints; rerun pose checks and review both motion extremes |
| IK target | Scene `target` and Python `TARGET`; reconsider reach and the fixed-target caption |
| Character or instance names | Scene prefab references, Python `JOINTS`/instance names/arm lengths, and Swift joint/name lookups and copy |
| Camera or framing | Scene camera, Swift projection, Python raw dimensions, and compositor stage dimensions/placement |
| Duration or frame rate | Scene duration/clip keys, Python `FPS`/`SECONDS` and poster selection, and Swift time-label formatting |
| New dataset or readout | A documented measurement definition, validation rule, serialized fields, and compositor display |

The helper is a worked example, not yet a generic infographic engine. Its
scene path, figures, camera, timing, and composition are fixed in source.
For another explanation, keep the editable scene and measurement definitions
together and preserve the existing sample. Reuse the native rig/timeline
features rather than writing a separate animation solver into a compositor.

Imported Ecstatica motion, BVH retargeting, and authored clips are distinct
sources. Label which is used and retain any reconstruction/retargeting limits.
This example uses newly authored motion on an Ecstatica II source-derived
character. `keepOrientation="1"` preserves the hand's pre-IK pose orientation
at each time; it does not lock the hand orientation across the whole timeline.
The constraint holds wrist position, not a simulated grasp.

## Review and verified baseline

Before accepting an export, inspect the neutral pose, both turn extremes,
caption/leader placement, feet and cast shadows. Confirm that annotations
match the projected joints and do not hide the action. Check render diagnostics,
then decode the final video to verify its dimensions and complete frame count.
Numerical pose checks are automated by this driver; visual review and video
decode verification are separate steps.

The initial example was verified on 2026-09-29 using Apple M1 rendering:
the build and all 45 focused Scener tests passed; 145 poses passed the motion
checks; FK wrist displacement reached about 16.4 cm; the IK wrist matched its
target at CLI output precision. All 145 MP4 frames decoded at 1600×1000 and
24 fps. These results describe this example and revision, not every possible
character, target, or animation.

## Development direction

The following are proposed extensions, not implemented capabilities or
scheduled work. They build toward an integrated 3D infographic studio:

1. **Shared projection and precise scene data.** Export camera matrices,
   viewport parameters, stable object/joint identifiers, and structured
   transforms with more precision. Native annotations and external compositors
   should use the same evaluated scene and camera.
2. **Editable annotation layers.** Add labels, leaders, arrows, target marks,
   dimensions, and trails anchored to objects/joints. Specify screen-space
   versus world-space sizing, occlusion policy, safe margins, and export
   visibility; keep editor selection helpers separate from authored graphics.
3. **Reusable layouts and data binding.** Store typography, colors, panel
   layouts, units, measurement definitions, and data sources with the document.
   Support comparison panels, charts, and product/process breakdowns without
   copying a bespoke compositor for each explanation.
4. **Motion and presentation authoring.** Add reusable infographic templates,
   keyframe/shot editing, timed captions, camera motion, and clearer IK/FK,
   mocap, and contact controls. Preserve repeatable evaluation at a given time.
5. **Integrated export.** Bring posters, still sequences, and video presets
   into the studio, including an annotation renderer/encoder path beyond the
   current macOS helpers. Persist render settings and provenance so a saved
   document can reproduce its delivery artifacts.

As these become native features, update the capability table and canonical
[scene schema](../skills/populate-simplegl-scenes/references/scene-format.md)
with their actual supported elements/flags, add a working example, and record
their validation and limits. Keep the existing scene-rendering and character
workflows usable while those features grow.
