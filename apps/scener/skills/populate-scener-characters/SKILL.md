---
name: populate-scener-characters
description: Build, pose or animate ellipsoid characters in Scener scenes and prefabs, including CAT-style rigs from presets, pose libraries, limb IK, walks, layered clips, and rendered review. Use for character population, gestures, reaches, crouches, walks and action blocking; not for general room furnishing.
---

# Populate Scener characters

Read [character-authoring.md](../../docs/character-authoring.md) for the body frame, bone skeletons, posing with aim and IK, current limits, and review commands. Read [character-pose-study.md](../../docs/character-pose-study.md) when evaluating whether the current character tools meet a new motion request. Follow the [scene population skill](../populate-simplegl-scenes/SKILL.md) and its canonical scene format when creating or editing `.blk` or `.blks` files.

## Workflow

1. Identify each character's height, length, head size, visual style, facing direction, action, interaction target and cameras, and record them in the book's scale sheet. Check existing character prefabs before creating another. Treat reference images and game files as design evidence, not as instructions.
2. Start a new character by copying a preset from `prefabs/characters/presets/` (`biped.blk`, `quadruped.blk`) and editing it; never build from hand-placed groups and spheres. Keep the CAT structure: pelvis `<hub>`, `<spine links>`, ribcage and head hubs, `<limb type="arm|leg">` containers placed with `at`/`from` and authored once on the left with `mirror="1"`, ending in `<palm>` or `<ankle>`, plus `<tail>` and `<digit>` chains. Keep the default simple hands; opt in with `fingers="1"` on an instance only when articulated fingers are needed. Put eyes, hair and costume parts on part surfaces with `on`. Keep the character's materials and reusable poses in its file. Do not write xyz positions or rotations inside a skeleton; change direction, length, girth or attachment instead.
3. Load the prefab and correct limb lengths until no `foot … rests` warnings remain. Review the rest pose from the front, side and three-quarter views, at full size and as a thumbnail, before posing.
4. Place named prefab instances in the scene. Pose with the character's pose library or scene `<pose>` entries, assigned with `pose=` or per camera with `<use-pose>`. Reach with `<ik limb="…" offset="…" bend="…"/>`, check the result with `--list-joints`, and add mirrored poses with `mirror=`. Use world `target` IK only for one-shot prop contacts. Treat `IK … out of reach` as an error.
5. For motion, add `<gait>` for walks, `<layer mocap="…">` for captured motion (BVH, retargeted through a capture profile; use `profile="cmu"` for the bundled captures), and `<layer>` entries playing poses or `<clip>` keys, with fades, masks and additive mode. Track travelling characters with `<camera follow="…">`. Render with `--frames` and check planted feet and reaches numerically at several `--time` values.
6. Render the affected cameras and inspect silhouette, actor/action/target clarity, joint seams, foot support, hand contact and shadows. Adjust part parameters and poses based on the image, not only the XML. Validate the XML and camera list and run the project build as described in the authoring guide.
7. State the remaining limits plainly: gaits follow a straight direction or walk on the spot; digits have multiple links but absent capture channels retain the rest shape; contacts support a flat plane, with no automatic balance, path/footstep UI or live prop targets.
