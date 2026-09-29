# IK vs FK motion infographic

`ik_fk.reel` is the finished piece; `ik_fk.blks` is its 3D scene. Everything,
including type, joint chains, callouts, the hand trail and the readouts, is
rendered by Scener. See [reels](../../docs/reels.md) for the format and the
[infographic studio guide](../../docs/infographic-studio.md) for the direction.

`ik_fk.blks` compares the same Ecstatica II Joe character under forward and inverse kinematics. Both figures start with the same arm pose and receive the same six-second torso rotation, from neutral to −22°, through neutral to +22°, and back. The left figure retains its authored shoulder and elbow rotations; the right solves its two-bone arm to a fixed world-space wrist target.

The motion is authored for this explanation, not an imported Ecstatica action or BVH clip. The character is the existing source-derived Joe with calibrated CAT controls; its geometry and bind transforms are unchanged. The hand target constrains wrist position. Hand orientation follows the underlying pose, and this demonstration does not simulate grasping, balance, or collisions.

## Design and scale

An open studio comparison keeps the two figures separated on warm and cool floor discs. The floor supports the discs at Z=−2 cm; their tops and the character ground plane are Z=0. Three off-camera studio lights provide key, fill, and rim lighting. Orange denotes FK; teal denotes IK.

Source scale is retained: the demonstrated upper arm is 22.356 cm and the forearm 22.149 cm. The IK wrist target is `(178.7, −15, 118.5)` cm. The scene layer occupies 1600×640 of the 1600×1000 canvas, with typography above and below it. Joint chains, crosshairs and leaders are anchored to the evaluated joints (`FK.left_palm.sx`), projected through the layer's own camera, so changing the camera or the layer's placement keeps them attached.

## Reproduce

From the repository root, after `make build/bin/scener`:

```sh
scener --reel apps/scener/scenes/infographics/ik_fk.reel --check
scener --reel apps/scener/scenes/infographics/ik_fk.reel \
  --output video/ik-fk/ik-vs-fk.mp4 --poster video/ik-fk/ik-vs-fk.png
```

(`DYLD_LIBRARY_PATH="$PWD/build/lib" ./build/bin/scener` for an undeployed build.) The video is 1600×1000, 24 fps, 144 frames, exactly 6 s, and loops without a repeated frame; the poster is the 1.5 s frame. Shadowed rendering needs GPU access; outputs under `video/` are ignored by Git.

## Verification

Ten `<check>` rules run on all 145 sampled times (0–6 s inclusive) before anything is written: the IK wrist stays within 0.01 cm of its target, all four arm segments keep their lengths within 0.01 cm, all four feet stay within 0.01 cm of their start, and the FK hand moves at least 10 cm (it peaks at 16.4 cm at 1.5 s). Joints are read at full float precision, not rounded CLI output. The readouts show the same measurements live: FK hand displacement from its start and IK wrist distance from its target.

Review the neutral pose and both turn extremes before accepting changes. Joint lines are explanatory overlays and intentionally remain visible through the character geometry. The crosshair marks the initial wrist on the FK side and the fixed target on the IK side.
