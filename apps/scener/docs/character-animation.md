# Animating characters in Scener

Scener characters are [bone skeletons](character-authoring.md). Animation adds
time to them: a scene-level `<clip>` is evaluated at a moment in seconds and
drives the same poses, IK and bone volumes that stills use. Muscles are
Hercules-style strands between bones that bulge as joints close, swell when
flexed and wobble after fast motion, and a generated skin covers them.

The worked example is [hercules_study.blks](../scenes/hercules_study.blks),
which drives the anatomical [hercules.blk](../prefabs/characters/hercules.blk)
three ways, beside an écorché twin. The
kitten in [kitten_study.blks](../scenes/kitten_study.blks) also has a
four-legged walk.

## Three kinds of clip

Choose the clip kind from what the shot needs:

| Clip | Use it for | Authoring |
|---|---|---|
| Keyframed (`<key>` children) | Gestures, acting, poses held and released | Timed poses, interpolated |
| Gait (`gait="walk"`) | Walking bipeds and quadrupeds | A few numbers; foot IK is automatic |
| Motion capture (`bvh="…"`) | Dances, sports, complex human motion | A BVH file; retargeted by bone names |

Assign a clip per camera with `<use-clip>`, or to an instance for every camera
with `clip="…"`. `offset` (or the instance's `clipOffset`) shifts its time so
two instances do not move in step.

```xml
<camera name="Walk" pos="-340 -260 130" look="0 -60 90" fov="38">
  <use-clip instance="Hercules" name="Walk" offset="0.25"/>
</camera>
<prefab source="characters/hercules" name="Hercules"/>
```

### Keyframes

A key is a pose at a time. It may name a scene `<pose>`, hold its own `<joint>`,
`<ik>` and `<flex>` children, or both; its own children win. An empty key is
the rest pose.

```xml
<clip name="Flex" length="4" loop="1" ease="spline">
  <key time="0"/>
  <key time="1" pose="DoubleBiceps"/>
  <key time="1.6" pose="DoubleBiceps"><flex muscle="left_bicep" amount="1"/></key>
  <key time="3.4"/>
</clip>
```

- `aim`, `rot` and `pos` interpolate per channel. A joint missing from a key
  returns to its rest value there, so partial keys blend toward rest.
- `ease="spline"` (Catmull-Rom, default) keeps motion flowing through keys,
  `smooth` eases in and out of every key, `linear` is mechanical.
- IK targets interpolate between keys that set them and hold their nearest
  value elsewhere.
- Looping clips wrap from the last key back to the first over `length`.

### Procedural gait

A gait needs only a skeleton with `foot="1"` bones. Each foot follows a stance
and swing cycle and is placed by two-bone IK. During stance the foot moves back
at exactly the travel speed, so planted feet never slide. Two feet alternate;
four feet walk a lateral sequence (left hind, left fore, right hind, right
fore). Bones named `left_*arm*`/`left_*shoulder*` that carry no foot swing
opposite the legs, and `tail*` bones sway.

```xml
<clip name="Walk" gait="walk" cycle="1.1" stride="54" lift="11" duty="0.62"
      bob="2.5" crouch="3" sway="5" armSwing="24" lean="-4"/>
```

The body travels forward (local −Y) at `stride / (duty × cycle)`; `travel="0"`
walks in place. If stderr reports `IK … out of reach`, the stride is longer than
the legs allow: raise `crouch` or shorten `stride`.

### Motion capture

`bvh` names a BVH file relative to the asset root (`apps/scener`) or an
absolute path. `rig="humanoid"` maps the joint names shared by CMU (cgspeed
BVH conversions), Mixamo and most exporters onto the bone names
`pelvis`, `spine`, `ribcage`, `neck`, `head`, `left_clavicle`, `left_upper_arm`,
`left_forearm`, `left_hand`, `left_thigh`, `left_shin`, `left_foot` and their `right_` partners. Joint names may carry a
`namespace:` prefix. Add `<map joint="…" bone="…" end="…"/>` children for other
names; `end` is the joint the bone points at (default: the first child or the
end site).

```xml
<clip name="JumpingJacks" bvh="mocap/jumping_jacks.bvh" rig="humanoid" travel="0"/>
```

Retargeting aims every mapped bone along its captured segment and orients the
root from the spine and hip line. The character keeps its own proportions and
rest pose, so a T-pose capture drives an A-pose skeleton. Root travel is scaled
by the ratio of the character's hip height to the capture's on the first frame
unless `scale` gives metres per BVH unit. `heading` turns the capture around
the vertical axis. Bone roll (forearm twist, head tilt about its own axis) is
not transferred.

`mocap/jumping_jacks.bvh` is synthetic test data written by
[`tools/make_test_bvh.py`](../tools/make_test_bvh.py). Real captures load the
same way; check each dataset's licence before adding it to the repository.

## Muscles and skin

Muscles follow the muscle primitive of Di-O-Matic's Hercules plugin for 3ds Max
(and CAT's Muscle Strand): a strand between an **origin** on one bone and an
**insertion** on another, shaped by a **profile curve**, able to **bend around
other objects**, with **squash/stretch** and **secondary motion**. As in
Hercules, the muscles shape a skin: a prefab `<skin>` fuses bones and muscles
into one surface, so the body reads as muscle forms under skin instead of
separate balls.

Build a character in anatomical layers, like
[hercules.blk](../prefabs/characters/hercules.blk):

1. **Skeleton.** Real bones with their own volumes: pelvis, lumbar `spine`,
   `ribcage`, `neck`, `head`, clavicles, scapulae (`volume="0"`, attachment
   only), humeri, forearms, hands, femurs, tibiae and feet. Bone volumes are the
   bony mass (ribcage, pelvis, skull, knees, hands); keep long-bone radii small
   and let muscles provide limb bulk.
2. **Muscles.** Group them as artist anatomy books do: one `<muscle>` per
   form the eye reads (pectoralis, deltoid, trapezius, latissimus, rectus
   abdominis, external oblique, gluteus maximus and medius, quadriceps,
   sartorius, adductors, hamstrings, calf, tibialis, biceps, triceps, forearm
   flexors and extensors, sternocleidomastoid), not per anatomical head.
   Author the left side only; every `left_*` muscle gets a `right_*` twin.
3. **Skin.** One `<skin>` that turns everything into a single surface.

### Placing a muscle

Each end is one anchor: `"bone at azimuth elevation"`, a fraction along the
bone and a body direction from its axis to its surface (0 front, 90 the
character's left, −90 medial on the left limbs, 180 back). Add
`".. at azimuth elevation"` to span the end across a second point on the same
bone; with `fibers`, a flat muscle fans from that line into one sheet.

```xml
<muscle name="left_biceps" origin="left_upper_arm 0.05 0 0" insertion="left_forearm 0.12 0 0"
        radius="3 3.4" tendon="0.1 0.2" profile="0.4 0.8 1 0.7 0.3" bulge="1.4" contract="1" jiggle="0.4"/>
<muscle name="left_pectoral" origin="ribcage 0.3 10 0 .. 1 30 25" insertion="left_upper_arm 0.1 30 0 .. 0.22 30 0"
        fibers="9" radius="3.4 2.4" wrap="ribcage"/>
```

| Shape control | Meaning |
|---|---|
| `radius="w t"` | Belly half width across the body and half thickness out of it, in cm |
| `profile` | Thickness curve from origin to insertion, any number of points (default `0.35 0.8 1 0.8 0.35`) |
| `tendon="a b"` | Fractions at the origin and insertion ends that are tendon (thin cords such as the Achilles) |
| `fibers` | Strands fanned across the end spans; they fuse into one sheet in the skin |
| `via="bone at az el lift; …"` | Anchors the path passes through: over a bony landmark, in front of the knee |
| `wrap="bone …"` | Bones the belly slides over instead of cutting through (pecs and lats over the ribcage) |
| `bands` | Tendinous grooves across the belly: the rectus abdominis "six pack" |

### Behaviour

- **Squash and stretch.** The belly's section scales with `(rest length /
  length)^(bulge/2)`: `bulge="1"` keeps volume, higher values exaggerate.
- **Contraction.** `contract` flexes a muscle automatically as it shortens
  (biceps as the elbow closes, calves as the heel rises). `<flex muscle="…"
  amount="0..1"/>` in poses, keys and instances flexes it on command. A flexed
  belly bunches toward its peak and rises off the bone.
- **Jiggle.** `jiggle` (0–1) lets the belly lag behind motion on a damped
  spring, strongest mid-belly and zero at the tendons.
  It runs frame to frame in sequences.
- Mirroring reflects names, anchors, vias and wrap lists.

### Skin

```xml
<skin material="skin" muscleMaterial="muscle"/>
```

The skin is the smooth union of every bone volume and muscle, polygonized with
surface nets (`resolution`, default 1.1 cm) and rebuilt each frame, so bulges,
flexes and jiggles show through. Fanned fibres fuse softly; separate muscles
keep a groove between them. Bones with
`skin="0"` (sandals) and non-bone details (hair, eyes, cloth) stay separate
meshes. Set `show="muscles"` on the skin or on one prefab instance for an
écorché view of the individual muscles over the skeleton; the study scene
places such a twin beside the skinned character. Without a `<skin>`, muscles
render as individual strands.

A skinned character costs about half a second per frame to rebuild.

### Check the rig with numbers

`scener --list-joints SCENE [--camera NAME] [--time SECONDS]` prints every
joint's world position in cm and each muscle's length, rest length, flex and
volume. Compare the landmarks with human proportions for the character's
height H: hip joints about ±0.05 H apart sideways, shoulder joints below the
neck base (about 0.79 H), the neck base near 0.82 H and the chin near 0.87 H.
Surface attachment used to couple the skeleton to the flesh: near the tip of a
parent ellipsoid (`at` close to 0 or 1) a surface point lands
close to its axis. That is why joints now take an explicit `out` distance
instead of sitting on the parent's surface.

## Render a sequence

```sh
make build/bin/scener
DYLD_LIBRARY_PATH="$PWD/build/lib" ./build/bin/scener --render apps/scener/scenes/hercules_study.blks \
  --camera Walk --frames all --fps 24 --size 960x720 --output-dir /tmp/walk
```

- `--frames N` renders frames 0…N−1, `--frames 10-40` a range, and
  `--frames all` the longest clip in the scene. Files are named
  `CAMERA_0001.jpg`, … at `--fps` (default 24).
- `--time SECONDS` renders a still at that moment in every selected camera.
- Frames render in order, which the muscle springs rely on. Assemble them with
  any encoder, for example `ffmpeg -framerate 24 -i /tmp/walk/Walk_%04d.jpg walk.mp4`.

Review a few frames at full size and the whole sequence in motion. Treat
`IK … out of reach` and unknown clip messages as failures, and watch for feet
passing through each other, hands through the body and muscles that pop.

## Current limits

- Clips replace the static pose for their instance; there is no layering or
  blending between clips.
- Aims interpolate as azimuth/elevation numbers; keep neighbouring keys within
  180° of azimuth of each other.
- The gait generator walks straight ahead on flat ground; turns, slopes, runs
  and stairs need keyframes or capture.
- Retargeting transfers bone directions, not roll, and does not correct
  foot contact for differing proportions.
- Muscles do not collide with each other; `wrap` only slides them over the
  bones it lists. The skin is not UV-mapped.
- The editor viewport shows time 0; playback happens through rendered
  sequences.
