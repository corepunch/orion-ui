# Building and posing characters in Scener

Scener renders stylized characters from ellipsoids. Build a reusable `.blk` prefab from scaled spheres and named groups, then pose each instance in a `.blks` scene. The editor supports local joint edits, mirrored pairs, reusable still poses, and two-bone IK. There is no animation timeline or clip playback.

The working example is [ellipsoid_actor.blk](../prefabs/characters/ellipsoid_actor.blk) in [character_pose_study.blks](../scenes/character_pose_study.blks). The [pose study](character-pose-study.md) shows the rendered result and the remaining product gaps.

## Character coordinate frame

- Use centimetres in XML. New scenes declare `up="z"`; this changes camera navigation, not primitive geometry.
- Put the character's ground baseline at local `Z=0` and document its height and front direction in the prefab's leading comment. The example actor is about 180 cm tall and faces local `-Y`.
- Place the prefab instance at the actor's location. Rotate the instance around Z to choose its facing; keep joint coordinates in the prefab's local frame.
- Put a group origin at each articulation center. A child part's transform is relative to that joint, so rotating a shoulder moves its elbow, forearm, and hand together. Use a pelvis or root group for body height and weight shifts.

## Model from ellipsoids

A sphere with `radius="1"` and nonuniform `scale` is an ellipsoid. Scale values are its half-widths in centimetres:

```xml
<!-- Torso: 46 cm wide, 26 cm deep, 72 cm tall. -->
<sphere pos="0 0 28" radius="1" scale="23 13 36"
        color="0.11 0.32 0.36" rings="12" slices="16"/>
```

Build the major silhouette first: pelvis, torso, head, paired thighs, shins, upper arms, forearms, hands, and feet. Add hair, eyes, clothing, or equipment only after the silhouette reads at the intended camera size. Make connected body masses overlap enough to avoid visible cracks at joints; keep the eye and clothing surfaces distinct enough to avoid coplanar flicker. `rings="12" slices="16"` gives the example a soft retro shape without an excessive mesh count.

Give each limb a parent group at its proximal joint and a child group at its distal joint. Place the limb segment between these pivots. For a local Z-up leg, this places a thigh between hip and knee and a shin between knee and ankle:

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

Give paired groups explicit `pair` attributes in both directions, such as `left_knee pair="right_knee"`; mirroring does not guess names. Keep feet flat at the baseline in the neutral pose. Overlap ellipsoids enough to hide joint gaps, then inspect bent poses for seams and swollen joints. In the pose study, about 20% overlap of the combined axial radii is a useful starting estimate, not a universal limit. Mesh `<mirror>` affects geometry only; it does not mirror a grouped skeleton or pose.

## Pose a character for a camera

Select a prefab instance in the viewport, open the Hierarchy tab, and select a joint. The Rotation and Offset buttons edit this instance; Pivot and Parent edit the shared prefab rig. The viewport move and rotate gizmos act at the joint pivot. Mirror copies the selected joint's local pose to its explicit partner, after which either side can be edited independently. Save Pose stores reusable still-pose data. Use Pose applies it to the instance, and Shot Pose assigns it only to the active camera.

Pose XML is also useful for review scenes. Define poses independently of cameras, then select one for a named instance in each camera:

```xml
<pose name="Reach">
  <joint target="head" rot="12 0 0"/>
  <ik root="right_shoulder" mid="right_elbow" tip="right_hand"
      target="29 -58 143" pole="0 -1 0" keepOrientation="1"/>
</pose>
<camera name="Reaching" pos="-260 -450 210" look="0 -10 94" fov="39">
  <use-pose instance="Actor" name="Reach"/>
</camera>
<prefab source="characters/ellipsoid_actor" name="Actor"/>
```

An `<ik>` chain needs three directly nested named groups. `target` is a world-space position in centimetres; `pole` is a world-space bend direction. Set `keepOrientation="1"` on a foot chain to hold its facing while the leg solves. Enter the floor contact point once, then adjust the root or hips: the foot stays at that world target. The solver keeps bone lengths fixed and clamps targets outside reach. The Properties panel reports whether the selected tip can reach its target. Move a selected IK tip with the viewport Move tool to reposition its target, or edit IK Target and IK Pole numerically in the Hierarchy tab. Forward posing remains available through joint Rotation and Offset after solving.

Start with the root and torso, then solve hips and knees, shoulders and elbows, ankles, and head. Check the pose from the actual camera after each pass. An arm or leg that appears correct from one view may float or intersect the body from another, so review at least one side view for a reusable pose.

Choose a camera where the character's action reads from its silhouette. Show the actor, gesture, and target together. A three-quarter view usually gives a clearer read than a straight front view. Keep props and foreground elements from hiding the hands, feet, or important joints.

These named poses are still states: they have no time, interpolation, animation blending, or clip playback. World targets are authored positions; there is no live link to a prop.

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
