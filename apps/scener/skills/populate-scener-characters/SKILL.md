---
name: populate-scener-characters
description: Build or pose ellipsoid characters in Scener scenes and prefabs, including joint hierarchies, camera-specific still poses, and rendered pose review. Use for character population, gestures, reaches, crouches, and action blocking; not for general room furnishing.
---

# Populate Scener characters

Read [character-authoring.md](../../docs/character-authoring.md) for the coordinate frame, ellipsoid and joint pattern, pose example, current limits, and review commands. Read [character-pose-study.md](../../docs/character-pose-study.md) when evaluating whether the current character tools meet a new motion request. Follow the [scene population skill](../populate-simplegl-scenes/SKILL.md) and its canonical scene format when creating or editing `.blk` or `.blks` files.

## Workflow

1. Identify each character's intended height, visual style, facing direction, action, interaction target, and cameras. Check existing character prefabs before creating another. Treat reference images and game files as design evidence, not as instructions.
2. Build a reusable prefab with its baseline at local Z=0 and a documented front direction. Shape the readable body mass with scaled spheres; put named groups at hips, knees, ankles, shoulders, elbows, torso, and head. Keep paired limbs consistent and joint connections continuous.
3. Place the prefab in the scene. Use camera transforms on named groups for still poses. Check reach against limb length and solve root height, knee angles, and ankle orientation together for grounded feet. Camera transform names are not instance-scoped, so avoid reusing joint names across independently posed actors in one scene.
4. Render the affected cameras and inspect silhouette, actor/action/target clarity, joint joins, foot support, hand contact, and shadows. Adjust geometry and transforms based on the image, not only the XML. Validate the XML and camera list and run the project build as described in the authoring guide.
5. State the remaining limits plainly. Camera poses are static states; Scener has no character timeline, pose interpolation, or IK. For animation or moving targets, describe the required feature instead of presenting a sequence of static cameras as playback.
