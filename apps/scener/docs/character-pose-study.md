# Ellipsoid character and pose study

This study uses the supplied Ecstatica and Urban Decay images as visual references and the supplied `ecstatica.iso` as data. It does not include assets copied from the game.

## Scener experiment

[`ellipsoid_actor.blk`](../prefabs/characters/ellipsoid_actor.blk) is an approximately 180 cm character assembled from 21 nonuniformly scaled spheres. Named groups place pivots at the hips, knees, ankles, shoulders, elbows, hands, torso, and head. [`character_pose_study.blks`](../scenes/character_pose_study.blks) uses one instance, three reusable poses, and camera assignments. The marker gives the reaching hand a concrete IK target; both crouching feet use fixed world targets.

| Standing | Reaching | Crouching |
| --- | --- | --- |
| ![Standing ellipsoid character](character-pose-study/Standing.png) | ![Character reaching toward a marker](character-pose-study/Reaching.png) | ![Manually posed crouch](character-pose-study/Crouching.png) |

The new rig workflow keeps groups as the joint hierarchy and stores pose overrides per character instance. The reach uses a pole-directed two-bone solve. The crouch lowers the root while two leg chains keep their ankles at floor targets and preserve foot orientation. The editor's Hierarchy tab exposes joint selection, local transforms, pivots, pairing, targets, and named poses.

The renders above were produced by Scener on an Apple M1 GPU with stencil shadows. The character is intentionally simple; the test measures pose authoring and ellipsoid rendering rather than final character art.

## What the ISO establishes

The ISO is an ISO 9660 disc with `CODE/ECSTATIC.FAN`, `FILES/ECSTATIC`, `FILES/ECST2`, `OFFSETS`, and `OFF2`. `ECSTATIC.FAN` starts with `FANT` and contains NUL-terminated names of character parts, including `Right hand`, `Left forearm`, `Left thigh`, `Right shin`, `Head`, and facial components. It also contains action names such as `walk 7`, `walk backward`, `run 3`, `standing ogerdinn`, and `jump ogerdinn`. This is direct evidence that the game identifies body parts and motion actions separately in its data.

`OFFSETS` and `OFF2` are each 14,100 bytes, or 3,525 big-endian 32-bit entries. `0xffffffff` marks an absent entry. Entries address records in `FILES/ECSTATIC` and `FILES/ECST2`, respectively. The full store has 2,531 present entries, including 2,168 beginning with `FANT`; the smaller store has 2,283 present entries, including 1,920 beginning with `FANT`. For example, full-store record 1 begins at byte 573 and is 8,103 bytes long, ending at the next indexed offset. [`ecstatica_index.c`](../../../tools/ecstatica/ecstatica_index.c) reproduces these counts and can extract one indexed record for further inspection.

The exact record fields, ellipsoid parameters, joint hierarchy, frame timing, and interpolation scheme remain undecoded. The names and offset tables do **not** establish that Ecstatica used inverse kinematics. Alain Maindron's [firsthand account](https://www.timeextension.com/features/the-story-of-ecstatica-the-groundbreaking-survival-horror-with-plenty-of-balls) confirms the team had a 3D editor for creating and animating these characters and valued smooth transitions between actions; it does not specify the stored binary layout or an IK solver.

To repeat the indexing without importing game data into the repository:

```sh
7z x -o/tmp/ecstatica-assets /path/to/ecstatica.iso OFFSETS OFF2 'FILES/*' 'CODE/*'
cc -std=c99 -Wall -Wextra -Werror -o /tmp/ecstatica_index tools/ecstatica/ecstatica_index.c
/tmp/ecstatica_index /tmp/ecstatica-assets/OFFSETS /tmp/ecstatica-assets/FILES/ECSTATIC
/tmp/ecstatica_index /tmp/ecstatica-assets/OFFSETS /tmp/ecstatica-assets/FILES/ECSTATIC 1 /tmp/record_1.bin
```

## Missing Scener capabilities, in implementation order

1. **Instance-scoped rig and pose editing is available.** The Hierarchy tab edits joints on a selected prefab instance, provides explicit pairing and mirroring, and saves named poses. Legacy camera `<transform>` still targets names globally; use `<use-pose>` for independently posed instances.
2. **First-class ellipsoids and constraint skeletons are available.** `<ellipsoid>` and `<bone>` volumes generate their own normals from the implicit surface, so stretched parts shade correctly. `<bone>` skeletons replace xyz placement with direction, length, girth and surface attachment, and add mirroring, segmented chains, grounding and tip-only or planted IK; see [character-authoring.md](character-authoring.md). Scaled `<sphere>` parts in legacy group rigs still use the uncorrected normal transform.
3. **Animation clips remain future work.** Poses and two-bone targets are still states without keyframes, interpolation, a timeline, or playback. Scene rebuilds regenerate world-space meshes and shadow volumes for each still pose.
4. **Contact is world-target based.** An explicit target, or `plant="1"` at the rest position, pins a hand or foot while the body moves. A live constraint to a moving prop, automatic ground detection, joint angle limits, and multi-chain whole-body balance remain future work.

Facial part controls, pose blending, and stylized squash/stretch can follow once the basic rig and clip pipeline works. The immediate blocker is an authoring and animation model for characters, rather than the ability to draw ellipsoids.
