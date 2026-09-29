# IK vs FK motion infographic

See the [infographic studio guide](../../docs/infographic-studio.md) for the
overall direction, existing engine capabilities, new helper tools, and
extension plan. This page records this example's composition and motion.

`ik_fk.blks` compares the same Ecstatica II Joe character under forward and inverse kinematics. Both figures start with the same arm pose and receive the same six-second torso rotation, from neutral to −22°, through neutral to +22°, and back. The left figure retains its authored shoulder and elbow rotations; the right solves its two-bone arm to a fixed world-space wrist target.

The motion is authored for this explanation, not an imported Ecstatica action or BVH clip. The character is the existing source-derived Joe with calibrated CAT controls; its geometry and bind transforms are unchanged. The hand target constrains wrist position. Hand orientation follows the underlying pose, and this demonstration does not simulate grasping, balance, or collisions.

## Design and scale

An open studio comparison keeps the two figures separated on warm and cool floor discs. The floor supports the discs at Z=−2 cm; their tops and the character ground plane are Z=0. Three off-camera studio lights provide key, fill, and rim lighting. The only floating elements are explanatory graphics, composited from projected joint coordinates.

Source scale is retained: the demonstrated upper arm is 22.356 cm and the forearm 22.149 cm. The IK wrist target is `(178.7, −15, 118.5)` cm. The camera shows both complete figures, hands, floor contact and shadows. Typography occupies separate header/footer areas; leader lines identify the active wrist and its reference point without covering faces. Orange denotes FK; teal denotes IK.

## Reproduce

From the repository root on macOS:

```sh
make build/bin/scener build/bin/test_scener_input_test
DYLD_LIBRARY_PATH="$PWD/build/lib" ./build/bin/test_scener_input_test
python3 apps/scener/tools/render_ik_fk_infographic.py --preview
python3 apps/scener/tools/render_ik_fk_infographic.py
```

GPU access is required for the shadowed render. The existing Swift/AVFoundation encoder and an AppKit compositor need the macOS command-line tools; no Python packages are required. `--scener PATH` selects another compatible binary; `--output PATH` changes the destination.

Preview mode replaces the poster, measurements, and render log at its destination;
it leaves any earlier MP4 unchanged. Use `--output video/ik-fk-preview` when
reviewing changes alongside an existing full export.

Outputs are ignored under `video/ik-fk/`:

- `ik-vs-fk.mp4`: 1600×1000, 24 fps, 145 frames (6.04 seconds including the endpoint).
- `ik-vs-fk.png`: annotated poster at 1.5 seconds.
- `measurements.json`: per-frame joint positions, FK hand displacement, and IK target error.
- `raw/`, `frames/`, and render logs: original 3D renders, annotated frames, and diagnostics.

The compositor projects the scene's fixed Comparison camera (`0 −650 210`, look `0 0 75`, vertical FOV 20°). Update that projection when changing the camera or raw frame dimensions.

## Verification

The renderer checks every sampled pose for a reachable IK target, unchanged arm segment lengths and stationary feet. Numeric checks allow 0.18 cm because `--list-joints` prints coordinates to 0.1 cm. The readout reports IK error as less than 0.2 cm rather than claiming exact solver precision. FK displacement is measured from the starting wrist, not an IK error.

Review the neutral pose and both turn extremes before accepting changes. Joint lines are explanatory overlays and intentionally remain visible through the character geometry. The reference crosshair marks the initial wrist on the FK side and the fixed target on the IK side.
