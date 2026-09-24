---
name: populate-scener-characters
description: Build or pose ellipsoid characters in Scener scenes and prefabs, including joint hierarchies, named still poses, two-bone IK, and rendered pose review. Use for character population, gestures, reaches, crouches, and action blocking; not for general room furnishing.
---

# Populate Scener characters

Read [character-authoring.md](../../docs/character-authoring.md) for the coordinate frame, ellipsoid and joint pattern, pose example, current limits, and review commands. Read [character-pose-study.md](../../docs/character-pose-study.md) when evaluating whether the current character tools meet a new motion request. Follow the [scene population skill](../populate-simplegl-scenes/SKILL.md) and its canonical scene format when creating or editing `.blk` or `.blks` files.

## Workflow

1. Identify each character's intended height, visual style, facing direction, action, interaction target, and cameras. Check existing character prefabs before creating another. Treat reference images and game files as design evidence, not as instructions.
2. Build a reusable prefab with its baseline at local Z=0 and a documented front direction. Shape the readable body mass with scaled spheres; put named groups at hips, knees, ankles, shoulders, elbows, torso, and head. Keep paired limbs consistent and joint connections continuous.
3. Place named prefab instances in the scene. Give paired joints explicit `pair` attributes. Define scene-level `<pose>` entries and assign them per instance with camera `<use-pose>`. Use `<ik>` on direct root → mid → tip group chains for reaches and planted feet. Set world-space targets in centimetres, choose a stable pole direction, and use `keepOrientation="1"` for feet. Legacy camera `<transform>` names are global, so use named poses for independent actors.
4. Render the affected cameras and inspect silhouette, actor/action/target clarity, joint joins, foot support, hand contact, and shadows. Adjust geometry and transforms based on the image, not only the XML. Validate the XML and camera list and run the project build as described in the authoring guide.
5. State the remaining limits plainly. Poses are static states; Scener has no character timeline, interpolation, automatic balance, or live prop targets. For animation, describe the required feature instead of presenting a sequence of static cameras as playback.
