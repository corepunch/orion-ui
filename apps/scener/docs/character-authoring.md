# Building and posing characters in Scener

Scener draws stylized characters from ellipsoids, in the spirit of Ecstatica:
soft overlapping volumes and silhouettes that read at a glance. Build them as
`<bone>` skeletons. A bone is a joint plus its body volume, placed by
constraints rather than coordinates. You say which way a bone points, how long
and thick it is, and where on its parent it grows from. Scener derives every
joint position, joins the volumes, mirrors limbs and puts the feet on the
ground. You never type an xyz coordinate or rotate an ellipsoid by hand.

The worked example is [kitten.blk](../prefabs/characters/kitten.blk) in
[kitten_study.blks](../scenes/kitten_study.blks). The canonical attribute table
is in [scene-format.md](../skills/populate-simplegl-scenes/references/scene-format.md#bone).
The older hand-placed group rig ([ellipsoid_actor.blk](../prefabs/characters/ellipsoid_actor.blk))
is still supported; see [Legacy group rigs](#legacy-group-rigs).

## Body frame and directions

- The prefab's body frame is Z up, facing local −Y, with the character's left
  at +X. Rotate the instance around Z to face it anywhere in the scene.
- Directions are `"azimuth elevation"` in degrees. Azimuth 0 is forward, 90 the
  character's left, −90 its right, 180 backward. Elevation 90 is up, −90 down.
  `aim="0 -90"` is a leg pointing straight down; `aim="180 35"` is a tail
  rising backward.
- Lengths and radii are centimetres. Every joint frame starts aligned with the
  body, so pose rotations and re-aims always read in body terms.

## Build the skeleton

Work from the root outward, like a CAT rig: hub, spine, neck and head, then
limbs, then tail and small parts.

1. **Root and spine.** The root bone is usually the spine, from hips toward the
   chest. Give it `segments` and `aimEnd` to curve it (`aim="0 0" aimEnd="0 8"`
   for a quadruped back rising toward the shoulders; `aim="0 90"` for an upright
   biped torso). The root grounds itself: its lowest rest volume touches Z=0.
2. **Neck and head.** Chain them tip-to-tip (no `at`). A head is a short, fat
   bone aimed forward, so its volume sits ahead of the neck.
3. **Limbs.** Attach the first limb bone to the body surface with `at` (fraction
   along the parent) and `from` (direction to the surface point). Chain the rest
   tip-to-tip down to a hand or foot. Author only the left side, named `left_*`,
   with `mirror="1"` on the limb root. Mark feet with `foot="1"`.
4. **Tail, ears, snout.** Use `segments`, `aimEnd` and `taper` for curves and
   points. A cat's ears are short bones with `taper="0.15"`; a curling tail is
   one bone with `segments="3" aimEnd="165 85" taper="0.6"`.
5. **Details.** Put eyes, noses and buttons inside their bone with
   `on="azimuth elevation"` and `at`. They snap to the bone's surface. Author
   both eyes explicitly (`on="34 12"` and `on="-34 12"`); `mirror` applies to
   bones only.

Volumes overlap their neighbours automatically (`overlap`, `sink`), which keeps
bent joints closed. Raise `sink` when a limb looks glued on, and lower it when
a joint bulges.

### Mapping CAT rig parts

| CAT part | Skeleton equivalent |
|---|---|
| Pelvis / ribcage hub | A short fat bone, or the ends of a segmented spine |
| Spine, neck, tail (N links) | One bone with `segments="N"` and `aimEnd` |
| Leg / arm | Two or three chained bones plus a foot or hand, `mirror="1"` |
| Palm / ankle | A short end bone aimed forward (`aim="0 -8"`) |
| Digits | Usually omit. Add small `taper` bones only when a close shot needs them |
| Limb IK | `<ik tip="left_front_paw">` infers the limb chain from its tip |

### Proportions that read

- Establish the character's scale sheet first: height, length and head size in
  centimetres, and how big it must appear in wide shots.
- Keep storybook proportions bold: a large head (a third to half of standing
  height for young animals), clear gaps between legs, and ears and tails that
  break the body silhouette.
- Check at thumbnail size. Ecstatica characters read because each part is a
  distinct, simple volume; do not add small ellipsoids that blur the outline.

### Fix feet with numbers, not by eye

With `foot="1"`, loading the prefab prints each foot's rest clearance:

```
warning: skeleton spine: foot left_hind_paw rests 1.0 cm above the ground
```

Correct the leg lengths from that number: add `gap / sin(|elevation|)` to the
most vertical limb bone. Repeat until no warnings remain. Any change to the
spine's slope can move the feet again.

## Pose a character

Define poses in the scene and select them per camera:

```xml
<pose name="Stretch">
  <joint target="spine" aim="0 -12"/>
  <ik tip="left_hind_paw" plant="1" pole="0 -1 0" keepOrientation="1"/>
  <ik tip="right_hind_paw" plant="1" pole="0 -1 0" keepOrientation="1"/>
  <ik tip="left_front_paw" target="5 -18 2" pole="0 -1 0" keepOrientation="1"/>
</pose>
<camera name="Stretching" pos="-80 -45 30" look="0 0 10" fov="40">
  <use-pose instance="Kitten" name="Stretch"/>
</camera>
<prefab source="characters/kitten" name="Kitten"/>
```

- **Re-aim, don't rotate.** `<joint target="head" aim="20 25"/>` points the
  head up and to its left. Aims are relative to the parent's current frame, so
  aim parents first: spine, then neck, then head. Use `rot` only for roll and
  small corrections; its Euler axes are body X (left), Y (back) and Z (up).
- **IK from the tip.** `<ik tip="left_front_paw" target="…"/>` solves the two
  joints above the paw. `target` is a world position in centimetres; `pole` is
  the world direction the knee or elbow should bend toward. Add
  `keepOrientation="1"` to keep a foot flat.
- **Plant feet.** `plant="1"` without `target` pins a foot where it stands at
  rest. Bend or lower the body and the planted feet stay on the floor.
- **Segment links pose by name.** A three-link tail exposes `tail`, `tail_2` and
  `tail_3`; aim the later links for a curl.
- **Mirror a pose.** The editor's Mirror button copies `rot` and `aim` to the
  `left_`/`right_` partner. Mirrored bones need no `pair` attribute.

Pose in this order: root and spine, planted feet, head and gaze, reaching
limbs, then tail and ears. Check the pose from the story camera and at least
one side view, because a reach can look right from one angle and float in
another.

Choose a camera where the action reads from the silhouette. Show the actor,
gesture and target together. A three-quarter view usually reads better than a
straight front view. Keep props from hiding hands, feet or key joints.

Poses are still states: they have no time, interpolation, blending or playback.
IK targets are fixed world positions, not live links to props.

## Editor

Select a prefab instance, open the Hierarchy tab and select a joint. Rotation
and Offset edit this instance; Mirror copies the joint pose to its partner.
Save Pose stores reusable pose data, Use Pose applies it and Shot Pose assigns
it to the active camera. The viewport move and rotate gizmos act at the joint
pivot. Moving an IK tip moves its world target. Bone pivots and parents come
from the XML (aim, length, at), so Pivot and Parent edits refuse bones; edit
the prefab source instead.

## Legacy group rigs

Older characters use named `<group>` joints holding scaled `<sphere>` parts.
Each group origin is an articulation, children are positioned in xyz, and
paired groups need explicit `pair` attributes. The pose, IK and editor tools
work on them too. A scaled sphere shades incorrectly when stretched; use
`<ellipsoid radii="…">` for new standalone ellipsoids. Prefer converting such
rigs to bones when a character is revised.

## Validate and review

From the repository root, replacing the example paths and camera name:

```sh
xmllint --noout apps/scener/prefabs/characters/kitten.blk apps/scener/scenes/kitten_study.blks
make build/bin/scener
DYLD_LIBRARY_PATH="$PWD/build/lib" ./build/bin/scener --render apps/scener/scenes/kitten_study.blks \
  --camera Stretching --size 960x720 --format png --output-dir /tmp/character-review
```

Treat `foot … rests` warnings and `IK … out of reach` messages as failures.
Inspect the render at full size and at thumbnail size. Check height,
silhouette, hand-to-target contact, foot support, joint seams, facial
readability and shadows. If the software renderer rejects stencil shadows, run
the final render with hardware GPU access; `-no-shadows` is fine for geometry
review but does not verify contact shadows.
