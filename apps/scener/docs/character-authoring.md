# Building, posing and animating characters in Scener

Scener draws stylized characters from ellipsoids, in the spirit of Ecstatica:
soft overlapping volumes and silhouettes that read at a glance. A character is
a rig in the style of 3ds Max CAT: hubs joined by spines, limbs ending in a
palm or ankle, digits and tails. Every part is a bone: a joint plus its body
volume, placed by constraints rather than coordinates. You say which way a part
points, how long and thick it is, and where on its parent it grows from. Scener
derives every joint position, joins the volumes, mirrors limbs and puts the feet
on the ground. You never type an xyz coordinate or rotate an ellipsoid by hand.

Start from a preset: [biped.blk](../prefabs/characters/presets/biped.blk) or
[quadruped.blk](../prefabs/characters/presets/quadruped.blk), shown in
[biped_study.blks](../scenes/biped_study.blks),
[quadruped_study.blks](../scenes/quadruped_study.blks) and the walking
[biped_walk.blks](../scenes/biped_walk.blks). The canonical attribute tables are
in [scene-format.md](../skills/populate-simplegl-scenes/references/scene-format.md#cat-rig-parts).
Plain `<bone>` skeletons ([mira.blk](../prefabs/characters/mira.blk)) and the
older hand-placed group rig ([ellipsoid_actor.blk](../prefabs/characters/ellipsoid_actor.blk))
still work; see [Legacy group rigs](#legacy-group-rigs).

For a source-dimensioned Ecstatica example with independently bending abdomen and
ribcage volumes, see [the articulated biped study](ecstatica-biped.md), its
[scene](../scenes/ecstatica_biped_study.blks) and [prefab](../prefabs/characters/ecstatica_biped.blk).

## Start from a preset

Like a CAT rig preset, a preset is a starting file, not a base class. Copy it,
rename it and edit it; nothing is inherited afterwards.

```sh
cp apps/scener/prefabs/characters/presets/biped.blk apps/scener/prefabs/characters/steve.blk
```

A character file carries everything that belongs to the character: its
skeleton and volumes, its `<material>` definitions, facial features, costume
parts, a pose library and clips. A scene restyles a character by defining a
material with the same id; scene materials, poses and clips win over the
character's own.

## Body frame and directions

- The prefab's body frame is Z up, facing local −Y, with the character's left
  at +X. Rotate the instance around Z to face it anywhere in the scene.
- Directions are `"azimuth elevation"` in degrees. Azimuth 0 is forward, 90 the
  character's left, −90 its right, 180 backward. Elevation 90 is up, −90 down.
  `aim="0 -90"` is a leg pointing straight down; `aim="180 35"` is a tail
  rising backward.
- Lengths and radii are centimetres. Every joint frame starts aligned with the
  body, so pose rotations and re-aims always read in body terms.

## CAT rig parts

| CAT part | Element | Notes |
|---|---|---|
| Pelvis, ribcage, head | `<hub>` | The root hub is the pelvis; it grounds the whole rig |
| Spine, neck | `<spine links="N">` | N links named `spine`, `spine_2` …; the next hub chains from its tip |
| Tail, ears on a chain | `<tail links="N">` | Curve with `aimEnd`, point with `taper` |
| Arm or leg | `<limb type="arm">`, `<limb type="leg">` | A zero-length container on its hub, placed with `at` and `from` |
| Collarbone | `<collarbone>` | Optional first bone of an arm |
| Limb bones | `<bone>` | Upper and lower bones, chained tip to tip |
| Hand, foot | `<palm>`, `<ankle>` | Ends the limb; an ankle reports ground clearance |
| Fingers, toes | `<digit links="N">` | Branch off a palm or ankle with `at` and `from` |

Every part takes the same placement and girth attributes as a
[`<bone>`](../skills/populate-simplegl-scenes/references/scene-format.md#bone).
`links` is `segments` under its CAT name. A limb's role drives IK and walking:
legs step, arms swing, and `<ik limb="…">` finds the limb's bones by itself.

The biped preset follows CAT's Base Human rig part for part. The reference is
CAT's own mocap mapping for that preset, as shipped with Mixamo's AutoCAT script
(`Mixamo_AutoCAT_MappingPreset.cam`):

| Base Human (CAT address) | `biped.blk` |
|---|---|
| Pelvis (`SceneRootNode.Hub`) | `<hub name="pelvis">` |
| Spine1–3 (`Hub.Spine[0].SpineLink[0..2]`) | `<spine name="spine" links="3">`: `spine`, `spine_2`, `spine_3` |
| Ribcage (`Spine[0].Hub`) | `<hub name="ribcage">` |
| Neck (`Ribcage.Spine[0].SpineLink[0]`) | `<spine name="neck">` |
| Head, HeadBone001 (`….Hub`, `ArbBone[0]`) | `<hub name="head">`, extra `<bone name="hair">` |
| LCollarbone, LUpperarm, LForearm, LPalm (`Ribcage.Limb[0]`) | `left_collarbone`, `left_upperarm`, `left_forearm`, `left_palm` |
| LFinger1x–5x (`Palm.Digit[0..4]`, three knuckles) | `left_thumb`, `left_index`, `left_middle`, `left_ring`, `left_pinky`, `links="3"` |
| LThigh, LCalf, LFoot (`Pelvis.Limb[0]`, the foot is the Palm) | `left_thigh`, `left_calf`, `<ankle name="left_foot">` |
| LToe1x (`Palm.Digit[0]`, two knuckles) | `<digit name="left_toe" links="2">` |
| Platform (the leg's IK target) | `<ik limb="left_leg"/>`: the ankle's rest point plus `offset` |
| Limb[1] (the second limb of a hub is the right) | the `mirror="1"` partner, `right_*` |

Not carried over: CAT's twist segments (`BoneSeg`) only matter for skinned
meshes, and CAT's middle limbs (L/M/R) are limbs without a `left_`/`right_`
prefix.

## Build the skeleton

Work from the root outward: pelvis hub, spine, ribcage hub, neck and head hub,
then limbs, then tail and small parts.

1. **Pelvis and spine.** The pelvis hub is the root. Give the spine `links` and
   `aimEnd` to curve it (`aim="0 0" aimEnd="0 4"` for a quadruped back rising
   toward the shoulders; `aim="0 90"` for an upright biped torso). The root
   grounds itself: its lowest rest volume touches Z=0.
2. **Ribcage, neck and head.** Hubs and spines chain tip to tip (no `at`). A
   quadruped head is a short, fat hub aimed forward, so its volume sits ahead of
   the neck.
3. **Limbs.** Put a `<limb>` on its hub with `at` (fraction along the hub) and
   `from` (direction to the surface point), then chain the limb's bones inside
   it down to a `<palm>` or `<ankle>`. Author only the left side, named `left_*`,
   with `mirror="1"` on the limb. Set `bend` on a limb whose middle joint bends
   backward, like a quadruped's front elbow (`bend="180 0"`).
4. **Tail, ears, snout.** Use `links`, `aimEnd` and `taper` for curves and
   points. A cat's ears are short bones with `taper="0.15"`; a curling tail is
   `<tail links="3" aimEnd="165 85" taper="0.6">`.
5. **Details.** Put eyes, noses and buttons inside their part with
   `on="azimuth elevation"` and `at`. They snap to the part's surface. Author
   both eyes explicitly (`on="34 12"` and `on="-34 12"`); `mirror` applies to
   rig parts only. Hair, glasses and collars are ellipsoids on the head or
   ribcage the same way.

Volumes overlap their neighbours automatically (`overlap`, `sink`), which keeps
bent joints closed. Raise `sink` when a limb looks glued on, and lower it when
a joint bulges.

### Proportions that read

- Establish the character's scale sheet first: height, length and head size in
  centimetres, and how big it must appear in wide shots.
- Keep storybook proportions bold: a large head (a third to half of standing
  height for young animals), clear gaps between legs, and ears and tails that
  break the body silhouette.
- Check at thumbnail size. Ecstatica characters read because each part is a
  distinct, simple volume; do not add small ellipsoids that blur the outline.

The biped preset separates the rig's joints from its visible body masses.
Its thigh and calf volumes overlap at the knee, with a projecting kneecap volume that
follows the calf. Paired hip masses join the pelvis to the upper legs. The
ribcage carries two broad pectoral ellipsoids, and the upper arms carry shoulder
caps. These are attached shapes, so they follow the existing rig without adding
motion-capture joints.

The three-link spine uses `volume="0"` and carries one overlapping abdomen
ellipsoid. This keeps its articulation while avoiding three visible rings at
the waist. Generated links inherit `volume`, and attached shapes remain visible.
The simple hands use broad, rounded palms overlapping the wrists. The shoes
use overlapping ankle and toe volumes, with a fuller heel and short toe links;
the toe joints still articulate. These proportions keep the extremities readable
without requiring individual fingers.

Use [biped_form_study.blks](../scenes/biped_form_study.blks) to inspect front,
side and rear silhouettes, a planted crouch, and hand/foot close-ups. Fingers
remain off in that study; `biped_study.blks` shows the articulated option.

### Fix feet with numbers, not by eye

Every `<ankle>` (and any bone with `foot="1"`) reports its rest clearance when
the prefab loads:

```
warning: skeleton pelvis: foot left_hind_paw rests 3.1 cm above the ground
```

Correct the leg lengths from that number: add `gap / sin(|elevation|)` to the
most vertical limb bone. Repeat until no warnings remain. Any change to the
spine's slope can move the feet again.

`--list-joints` prints every posed joint in world centimetres, at any camera
and time, so contacts and reaches can be checked by number:

```sh
scener --list-joints apps/scener/scenes/biped_study.blks --camera Point
scener --list-joints apps/scener/scenes/biped_walk.blks --time 2.4
```

## Pose a character

A pose is a set of joint re-aims and IK goals. Keep a character's reusable
poses in its own file, as its pose library; put one-off story poses in the
scene. Select a pose for an instance with `pose="…"`, or per camera:

```xml
<!-- in the character file -->
<pose name="WaveLeft">
  <ik limb="left_arm" offset="4 -8 72" bend="150 -20"/>
  <joint target="left_palm" aim="0 90"/>
  <joint target="head" rot="0 0 15"/>
</pose>
<pose name="WaveRight" mirror="WaveLeft"/>

<!-- in the scene -->
<camera name="Hello" pos="-160 -330 140" look="0 0 100" fov="35">
  <use-pose instance="Adam" name="WaveRight"/>
</camera>
<prefab source="characters/presets/biped" name="Adam"/>
```

- **Reach with limb IK.** `<ik limb="left_arm" offset="x y z"/>` moves the hand
  from where it rests by an offset in the character's body frame (cm), so the
  pose works for any instance anywhere. The IK finds the limb's upper and lower
  bones and keeps the palm or ankle's orientation. `bend="azimuth elevation"`
  is the body direction the elbow or knee points toward; legs default to
  forward, arms to backward, and a limb's own `bend` overrides both.
  `<ik limb="left_leg"/>` with nothing else plants the foot where it stands.
  `weight` (0–1) blends between the unposed limb and the goal.
- **Mirror poses.** `<pose name="WaveRight" mirror="WaveLeft"/>` reflects a
  pose: `left_`/`right_` names swap, aims, bends and offsets turn to the other
  side, and rotations reflect. Saving skips the generated entries.
- **Re-aim, don't rotate.** `<joint target="head" aim="20 25"/>` points a part
  up and to its left. Aims are relative to the parent's current frame, so aim
  parents first: spine, then neck, then head. Use `rot` for turns about the
  part's own axis, such as turning an upright head (`rot="0 0 15"`), and for
  small corrections; its Euler axes are body X (left), Y (back) and Z (up).
  After limb IK, a palm or ankle `aim` reads in body terms, because the IK keeps
  its orientation.
- **World targets.** `<ik tip="left_front_paw" target="…"/>` solves the two
  joints above any named tip toward a world position in centimetres; `pole` is
  the world direction the knee or elbow bends toward. Use it for contacts with
  props in one shot; prefer limb offsets in reusable poses.
- **Plant feet.** `plant="1"` pins a foot where it stands at rest. Bend or lower
  the body and the planted feet stay on the floor.
- **Links pose by name.** A three-link tail exposes `tail`, `tail_2` and
  `tail_3`; aim the later links for a curl.

Pose in this order: root and spine, planted feet, head and gaze, reaching
limbs, then tail and ears. Check the pose from the story camera and at least
one side view, because a reach can look right from one angle and float in
another.

Choose a camera where the action reads from the silhouette. Show the actor,
gesture and target together. A three-quarter view usually reads better than a
straight front view. Keep props from hiding hands, feet or key joints.

## Animate over time

Scenes have a timeline in seconds. An instance's `<gait>` and `<layer>`
children play over it, in document order, on top of the instance's pose:

```xml
<scene up="z" duration="7.5">
  <clip name="Nod" length="1.2">
    <key t="0"/>
    <key t="0.4"><joint target="head" rot="14 0 0"/></key>
    <key t="1.2"/>
  </clip>
  <prefab source="characters/presets/biped" name="Adam">
    <gait start="0.5" distance="300"/>
    <layer pose="WaveRight" start="5.2" end="6.8" fadeIn="0.4" fadeOut="0.4"/>
    <layer clip="Nod" start="5.4" mode="additive"/>
  </prefab>
</scene>
```

- **Walk with `<gait>`.** Like CATMotion, a gait walks the character
  `distance` cm along its facing, starting at `start` seconds. Legs step
  through IK and planted feet never slide; the pelvis dips at double support,
  sways and twists, the spine counter-twists and arms swing. Walks start and
  stop with shorter steps. Bipeds alternate legs; quadrupeds use a lateral
  walk, front legs a quarter cycle after the hind legs. The controls follow
  CATMotion's: `stride` and `speed` (Max Stride Length, Max Step Time),
  `direction` (degrees from the facing: 90 walks sideways, 180 backwards) and
  `mode="spot"` (Walk On Spot), a limb's `phase` (LimbPhases), `footRoll`
  (FootPlatform pitch: toe-off and heel strike), `armSwing`, `armBend` and
  `armOut` (Arm Swing, Bend, CrossSwing), and `hipTwist`, `pelvisRoll`, `sway`
  and `bounce` (Pelvis Twist, Roll, WeightShift, Lift). Defaults scale with hip
  height. After the walk the instance stays where it arrived.
- **Play a pose or clip with `<layer>`.** `start`, `end`, `fadeIn` and
  `fadeOut` shape its weight over time. An `absolute` layer pulls the joints it
  keys toward its pose; an `additive` layer adds on top, so a nod plays over a
  walk. `mask="right_arm head"` limits a layer to those joints and everything
  below them, and `mirror="1"` plays it on the other side.
- **Key poses with `<clip>`.** A clip's `<key t="…">` holds a named `pose`, its
  own `<joint>`/`<ik>` entries, or both. Keys blend with smooth easing, or
  `ease="linear"`/`"step"`. `loop="1"` repeats over `length`; otherwise the
  last key holds.

## Motion capture

`<layer profile="cmu" mocap="mocap/cmu/02_01.bvh"/>` attaches a capture to a
CAT-style rig. It shares the other layers' timing, fades, masks and mirroring.
Use `profile="cmu"` for the supplied CMU conversion: frame 0 is calibration and
playback starts at frame 1. The default `profile="bvh"` uses the hierarchy's
OFFSET rest pose and plays frame 0; it does not assume the first frame is a T-pose.

A saved `<capture-profile>` in the scene, character prefab, or separate XML file
can specify `up`, `forward`, `referenceFrame`, `firstFrame`, and `<map>` entries
for source names, target joints and orientation offsets. Scale derives from the
sum of thigh and calf lengths, including bent calibration poses; an explicit
`scale` is target centimetres per source unit. See the canonical scene-format
reference for all fields. Correct the profile once and reuse it for that source rig.

Fingers are **off by default**: characters keep simple palm shapes and wrist
animation. Opt in only for close-ups or gestures that need articulated fingers:

```xml
<prefab source="characters/presets/biped" name="Adam" fingers="1">
  <layer profile="cmu" mocap="mocap/cmu/02_01.bvh"/>
</prefab>
```

Set `fingers="1"` on a character file's root to change its default, or use
`fingers="0"` on an instance to override it. The option affects hand digits,
including thumbs, in geometry, joint lists, retargeting and contact support;
toes and other body motion are unaffected. It never deletes the authored rig.

BVH can contain finger joints, but their availability depends on the capture.
Limb rotations preserve axial twist. With fingers enabled, available finger
channels transfer and untracked fingers retain the preset's relaxed shape.
CMU 02_01 has only six of thirty finger joint tracks, not a complete hand
performance. Choose `handPose="HandsOpen|HandsRelaxed|HandsFist"` (one name)
to override articulated hands, or layer a masked pose for individual corrections.

`legs="ik"` keeps source knee planes, including inverted motion; `legs="fk"`
uses transferred rotations. Low, slow feet and hands produce cached support
intervals. The solver anchors support points in the character's ground frame,
fits pelvis translation to limb reach, and preserves captured roll at a pivot.
`hands="fk"` disables hand constraints; `contacts="none"` disables automatic
contacts. Authored contacts override detection for that limb:

```xml
<layer profile="cmu" mocap="mocap/cmu/02_01.bvh" handPose="HandsRelaxed">
  <contact limb="left_leg" start="0.2" end="0.8" mode="plant" fade="0.05"/>
</layer>
```

Contact times are source-clip seconds. `plant` fixes position and orientation,
`pivot` retains orientation changes around the support, and `slide` preserves
horizontal travel while enforcing support height. Explicit tracks are useful
for intentional slides and captures where automatic thresholds misclassify a
contact. The supported surface is a flat local ground plane (`ground`, cm), not
terrain or a moving prop. Pelvis fitting enforces reach; it is not a balance or
collision simulation. Contradictory simultaneous contacts may remain unreachable.

`from` / `to` select a source range. `loop="1"` accumulates root translation and
heading by default, with a short pose blend (`loopBlend`, seconds, default 0.12).
Use `rootMotion="repeat"` for an intentional reset or `rootMotion="inPlace"`
(the older `inPlace="1"` alias also works) to remove horizontal root travel.
Crop a compatible cycle before looping: a seam blend cannot make arbitrary
performances into a natural gait. Contacts release at the range boundary.

This is XML-based capture and straight/spot gait authoring. A mapping-preview UI,
path following, editable viewport footsteps and baking remain future work.
`mode="adjustment"` currently aliases local additive layers; it does not implement
CAT's world-space adjustment layers. Gait `footRoll` now rotates about heel/toe
pivots and offsets the ankle to preserve the planted point.

A camera with `follow="Adam"` keeps its `pos` and `look` relative to that
instance's root over the ground, so it tracks walks, gaits and mocap travel.

```xml
<camera name="Front" follow="Adam" pos="-260 -420 170" look="0 0 90" fov="40"/>
<prefab source="characters/presets/biped" name="Adam">
  <layer profile="cmu" mocap="mocap/cmu/13_27.bvh"/>
  <layer pose="Think" start="4" fadeIn="0.5" mask="head"/>
</prefab>
```

[mocap_study.blks](../scenes/mocap_study.blks) is the worked example.
`tools/fetch_cmu_mocap.py` downloads twenty CMU clips and
`tools/render_mocap_videos.py` renders one MP4 per clip, encoding with
`tools/frames_to_mp4.swift` (AVFoundation, no ffmpeg needed).

Render a sequence, or scrub a single moment:

```sh
scener --render apps/scener/scenes/biped_walk.blks --camera Track --frames 0:7.5:24 --output-dir render/walk
scener --render apps/scener/scenes/biped_walk.blks --camera Side --time 2.2 --output-dir render/still
```

In the editor, **Animation → Play / Pause** (Space) plays the timeline in the
viewport, looping after the scene's `duration`; **Go to Start** (Shift+Space)
rewinds.

## Editor

Select a prefab instance, open the Hierarchy tab and select a joint. Rotation
and Offset edit this instance; Mirror copies the joint pose to its partner.
Save Pose stores reusable pose data, Use Pose applies it and Shot Pose assigns
it to the active camera. The viewport move and rotate gizmos act at the joint
pivot. Moving an IK tip moves its world target. Bone pivots and parents come
from the XML (aim, length, at), so Pivot and Parent edits refuse bones; edit
the prefab source instead.

## Legacy group rigs

Plain `<bone>` skeletons without CAT parts, such as Mira, keep working. Tip IK,
poses and layers apply to them; `<ik limb="…">` and `<gait>` need `<limb>`
parts.

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
