---
name: populate-scener-characters
description: Build, pose or animate ellipsoid characters in Scener scenes and prefabs, including joint hierarchies, named still poses, two-bone IK, muscles, keyframed, walking and motion-capture clips, and rendered review. Use for character population, gestures, reaches, crouches, and action blocking; not for general room furnishing.
---

# Populate Scener characters

Read [character-authoring.md](../../docs/character-authoring.md) for the body frame, bone skeletons, posing with aim and IK, current limits, and review commands. Read [character-pose-study.md](../../docs/character-pose-study.md) when evaluating whether the current character tools meet a new motion request. Follow the [scene population skill](../populate-simplegl-scenes/SKILL.md) and its canonical scene format when creating or editing `.blk` or `.blks` files.

## Workflow

1. Identify each character's height, length, head size, visual style, facing direction, action, interaction target and cameras, and record them in the book's scale sheet. Check existing character prefabs before creating another. Treat reference images and game files as design evidence, not as instructions.
2. Build a reusable prefab from `<bone>` elements, never from hand-placed groups and spheres. Work root outward: spine (with `segments`/`aimEnd`), neck and head, limbs placed with `at`/`from`/`out` and authored once, as `left_*` (right sides are generated), then tail, ears and snout with `segments`, `aimEnd` and `taper`. Put eyes and other details on bone surfaces with `on`. Do not write xyz positions or rotations inside a skeleton; if a part seems to need one, change the direction, length, girth or attachment instead.
3. Mark every foot `foot="1"`, load the prefab and correct limb lengths until no `foot … rests` warnings remain. Review the rest pose from the front, side and three-quarter views, at full size and as a thumbnail, before posing.
4. Place named prefab instances in the scene. Define scene-level `<pose>` entries and assign them per camera with `<use-pose>`. Re-aim bones with `<joint target="…" aim="azimuth elevation"/>` from the root outward. Use `<ik tip="…">` for reaches and grounded contacts, and `plant="1"` to keep feet where they stand while the body moves. Treat `IK … out of reach` as an error.
5. Render the affected cameras and inspect silhouette, actor/action/target clarity, joint seams, foot support, hand contact and shadows. Adjust bone parameters and poses based on the image, not only the XML. Validate the XML and camera list and run the project build as described in the authoring guide.
6. For motion, follow [character-animation.md](../../docs/character-animation.md): keyframed `<clip>` entries for acting, `gait="walk"` for walking, BVH capture for complex human motion, and, for bodies that need real anatomy, bones with anatomical `shape`s and one `<muscle>` per muscle attached at its real origin and insertion by landmark, under a prefab `<skin>` (review the `show="bones"` skeleton and `show="muscles"` écorché as well as the skin). Render with `--frames` and review the sequence in motion as well as single frames.
7. State the remaining limits plainly: clips do not blend or layer, gaits walk straight on flat ground, retargeting drops bone roll, and there is no automatic balance or live prop targets.
