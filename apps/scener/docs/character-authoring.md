# Building and posing characters in Scener

Scener can render stylized characters from ellipsoids today. Build a reusable `.blk` prefab from scaled spheres and nested groups, then place and pose it in a `.blks` scene. This workflow makes still poses for specific cameras. Scener does not yet play animation clips or solve inverse kinematics.

The working example is [ellipsoid_actor.blk](../prefabs/characters/ellipsoid_actor.blk) in [character_pose_study.blks](../scenes/character_pose_study.blks). The [pose study](character-pose-study.md) shows the rendered result and the remaining product gaps.

## Character coordinate frame

- Use centimetres in XML. New scenes declare `up="z"`; this changes camera navigation, not primitive geometry.
- Put the character's ground baseline at local `Z=0` and document its height and front direction in the prefab's leading comment. The example actor is about 180 cm tall and faces local `-Y`.
- Place the prefab instance at the actor's location. Rotate the instance around Z to choose its facing; keep joint coordinates in the prefab's local frame.
- Put a group origin at each joint. A child part's transform is relative to that joint, so rotating a shoulder moves its elbow, forearm, and hand together. Use a pelvis or root group for body height and weight shifts.

## Model from ellipsoids

A sphere with `radius="1"` and nonuniform `scale` is an ellipsoid. Scale values are its half-widths in centimetres:

```xml
<!-- Torso: 46 cm wide, 26 cm deep, 72 cm tall. -->
<sphere pos="0 0 28" radius="1" scale="23 13 36"
        color="0.11 0.32 0.36" rings="12" slices="16"/>
```

Build the major silhouette first: pelvis, torso, head, paired thighs, shins, upper arms, forearms, hands, and feet. Add hair, eyes, clothing, or equipment only after the silhouette reads at the intended camera size. Make connected body masses overlap enough to avoid visible cracks at joints; keep the eye and clothing surfaces distinct enough to avoid coplanar flicker. `rings="12" slices="16"` gives the example a soft retro shape without an excessive mesh count.

Give each limb a parent group at its proximal joint and a child group at its distal joint. For a local Z-up leg, this places a thigh between hip and knee and a shin between knee and ankle:

```xml
<group name="left_hip" pos="-11 0 -4">
  <sphere pos="0 0 -19" radius="1" scale="11 10 24" color="0.24 0.37 0.54"/>
  <group name="left_knee" pos="0 0 -42">
    <sphere pos="0 0 -19" radius="1" scale="7.5 8 23" color="0.28 0.42 0.62"/>
    <group name="left_ankle" pos="0 0 -39">
      <sphere pos="0 -8 -1" radius="1" scale="9 17 6" color="0.10 0.09 0.08"/>
    </group>
  </group>
</group>
```

Use paired left/right names and consistent limb lengths. Keep feet flat at the baseline in the neutral pose. Scener currently rotates sphere shading normals without correcting for nonuniform scale, so strongly stretched parts may show imperfect lighting. The geometry and silhouettes still render correctly.

## Pose a character for a camera

Give the groups names, then use a camera's `<transform>` children to add joint rotations for that shot. Scener restores the authored pose when switching cameras. A named transform affects every node with that name, including nodes inside multiple instances of the same prefab; use one instance of a rig per scene or unique joint names when actors need different poses.

```xml
<camera name="Reaching" pos="-260 -450 210" look="0 -10 94" fov="39">
  <transform target="right_shoulder" rot="-100 0 0"/>
  <transform target="right_elbow" rot="10 0 0"/>
  <transform target="head" rot="12 0 0"/>
</camera>
<prefab source="characters/ellipsoid_actor" name="Actor"/>
```

Start with the root and torso, then set hips and knees, shoulders and elbows, ankles, and head. Check the pose from the actual camera after each pass. For a hand reaching a prop, compare shoulder-to-target distance with the combined upper-arm and forearm lengths before adjusting angles; moving the target into reach was necessary in the example. For a crouch, adjust root height and both leg chains together, then check each foot against the floor and its cast shadow. Rotate ankles to keep the feet level. An arm or leg that appears correct from one view may float or intersect the body from another, so review at least one side view for a reusable pose.

Choose a camera where the character's action reads from its silhouette. Show the actor, gesture, and target together. A three-quarter view usually gives a clearer read than a straight front view. Keep props and foreground elements from hiding the hands, feet, or important joints.

Camera transforms are still poses, not animation keys: they have no time, interpolation, clip reuse, or target constraint. Author hand and foot placement manually. If an action needs the hand to follow a moving prop or a foot to stay planted while the body moves, the missing feature is a joint/clip system with a two-bone IK constraint, not another XML rotation recipe.

## Validate and review

From the repository root, replace the example paths and camera name with the character scene being edited:

```sh
xmllint --noout apps/scener/prefabs/characters/ellipsoid_actor.blk apps/scener/scenes/character_pose_study.blks
make scener
./build/bin/scener --list-cameras apps/scener/scenes/character_pose_study.blks
./build/bin/scener --render apps/scener/scenes/character_pose_study.blks \
  --camera Reaching --size 960x720 --format png --output-dir /tmp/character-review
```

Inspect the render at full size and thumbnail size. Check height, silhouette, hand-to-target contact, foot support, joint intersections, facial readability, and shadows. If the software renderer rejects stencil shadows, run the final render in a session with hardware GPU access. `-no-shadows` is useful for geometry review but does not verify contact shadows.

For the XML schema, color and transform rules, and scene validation, use [scene-format.md](../skills/populate-simplegl-scenes/references/scene-format.md) and the [scene population skill](../skills/populate-simplegl-scenes/SKILL.md).
