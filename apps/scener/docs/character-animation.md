# Animating characters in Scener

Scener characters are [bone skeletons](character-authoring.md). Animation adds
time to them: a scene-level `<clip>` is evaluated at a moment in seconds and
drives the same poses, IK and bone volumes that stills use. Muscles are
Hercules-style strands between bones that bulge as joints close, swell when
flexed and wobble after fast motion, and a generated skin covers them.

The worked example is [hercules_study.blks](../scenes/hercules_study.blks). It
drives the anatomical [hercules.blk](../prefabs/characters/hercules.blk) through
the kinds of demos that shipped with the Hercules plugin, with a skinned figure,
an écorché twin and a skeleton twin side by side. The
kitten in [kitten_study.blks](../scenes/kitten_study.blks) also has a
four-legged walk.

## Three kinds of clip

Choose the clip kind from what the shot needs:

| Clip | Use it for | Authoring |
|---|---|---|
| Keyframed (`<key>` children) | Gestures, acting, poses held and released | Timed poses, interpolated |
| Gait (`gait="walk"`) | Walking bipeds and quadrupeds | A few numbers; foot IK is automatic |
| Motion capture (`bvh="…"`) | Dances, sports, complex human motion | A BVH file; retargeted by bone names |

### Showcase clips

The study scene has one camera per clip, and every camera drives all three
twins. The first five follow the demos Di-O-Matic used to show Hercules: arm
flexing, "four muscles per leg", biped stretching and secondary motion.

| Camera | What it shows |
|---|---|
| `Curl` | Alternate elbow flexion with supination. The hand's `twist` rolls the radius over the ulna, and the biceps, inserting on the radius, bunches as it flexes |
| `Squat` | The pelvis drops and sits back over planted feet (IK). Quadriceps, glutes and hamstrings flex and stretch |
| `Stretch` | Arms overhead, then side bends and a twist spread over the five lumbar and seven cervical vertebrae |
| `Hop` | Crouch, jump and land in place (IK `offset`). Pecs, glutes and calves lag and wobble after the landing |
| `Roar` | The jaw opens on its own bone, the shoulders shrug, and the head turns through the neck vertebrae |
| `Walk`, `Flex`, `Mocap` | Procedural gait, the double-biceps pose and BVH retargeting |

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

- `aim`, `rot`, `pos` and `twist` interpolate per channel. `twist` rolls a
  bone about its own axis; positive pronates on both sides.
- A `rot` or `twist` on a segmented bone (`spine`, `neck`, a tail) is shared
  evenly by its links, so the chain curves instead of hinging at its base. A joint missing from a key
  returns to its rest value there, so partial keys blend toward rest.
- `ease="spline"` (Catmull-Rom, default) keeps motion flowing through keys,
  `smooth` eases in and out of every key, `linear` is mechanical.
- IK targets interpolate between keys that set them and hold their nearest
  value elsewhere. `offset` (cm, instance frame) moves a planted or targeted
  goal and returns to zero where a key leaves it out, so one clip lifts the
  feet of every instance wherever it stands.
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

1. **Skeleton.** Real bones, each drawn by an anatomical `shape` (below).
   Soft mass that no bone explains (belly, neck, the chest under the ribs) is
   the bone's `radius` ellipsoid with `envelope="1"`.
2. **Muscles.** Group them as artist anatomy books do: one `<muscle>` per
   form the eye reads, attached where the real muscle attaches. Hercules has
   42 a side: sternocleidomastoid, trapezius (upper and middle/lower),
   levator scapulae, masseter, temporalis, pectoralis (sternal and
   clavicular), serratus anterior, latissimus dorsi, teres major,
   infraspinatus, erector spinae, rectus abdominis, external oblique,
   gluteus maximus and medius, tensor fasciae latae with the iliotibial band,
   sartorius, rectus femoris, vastus lateralis and medialis, adductors,
   gracilis, biceps femoris, semitendinosus, gastrocnemius, soleus, tibialis
   anterior, peroneus, extensor digitorum longus, deltoid (anterior and
   middle/posterior), biceps, brachialis, triceps, brachioradialis, extensor
   carpi radialis, forearm extensors and flexors, pronator teres and the
   thenar eminence. Author the left side only; every `left_*` muscle gets a
   `right_*` twin.
3. **Skin.** One `<skin>` that turns everything into a single surface.

### Anatomical skeleton

`shape` replaces a bone's ellipsoid with a real bone built from the skin's
primitives, so the same parts draw the skeleton and shape the skin. Shapes are
drawn at rest in the body frame and follow the bone's `length`; the thorax and
pelvis also follow its `radius`. Right bones are mirrored.

| Region | Bones (`name` → `shape`) | Why it is split this way |
|---|---|---|
| Spine | `pelvis` → `pelvis`; `spine` → `vertebra lumbar`, `segments="5"`; `ribcage` → `thorax`; `neck` → `vertebra cervical`, `segments="7"` | The spine is an array of bones: five lumbar and seven cervical vertebrae are links of two chains, so a bend on `spine` or `neck` spreads over every vertebra. The twelve thoracic vertebrae move with the ribs as one `thorax` |
| Head | `head` → `skull`; `jaw` → `mandible` | The jaw is its own bone so the mouth opens and the masseter stretches |
| Shoulder | `left_clavicle` → `clavicle`; `left_scapula` → `scapula` | Both move together when the clavicle shrugs |
| Arm | `left_upper_arm` → `humerus`; `left_forearm` → `ulna`; `left_radius` → `radius`; `left_hand` → `hand` | The forearm has two bones. The ulna is the elbow hinge and carries the hand, as the mocap map expects. The radius rolls over it with the hand's `twist` (`twistWith="left_hand"`), about the line from the radial head to the ulna's head, as in pronation |
| Leg | `left_thigh` → `femur`; `left_shin` → `tibia`; `left_fibula` → `fibula`; `left_patella` → `patella`; `left_foot` → `foot` | The fibula rides beside the tibia. The patella hangs from the tibial tuberosity on its ligament, so it tracks the knee as it bends |

Every shape names the landmarks muscles attach to. Bilateral landmarks of
midline bones carry `left_`/`right_`, which mirroring swaps:

| Shape | Landmarks |
|---|---|
| `skull` | `occiput`, `glabella`, `vertex`; `left_mastoid`, `left_nuchal`, `left_nuchal_lateral`, `left_zygomatic_arch_front`, `left_zygomatic_arch_back`, `left_temporal_front`, `left_temporal_back`, `left_eye` |
| `mandible` | `chin`; `left_angle`, `left_ramus`, `left_coronoid` |
| `vertebra` | `spinous`, `front`; `left_transverse`; lumbar also `left_lamina` |
| `thorax` | `sternum_top`, `sternum_mid`, `xiphoid`, `t1_spine` … `t12_spine`; `left_manubrium`, `left_sternal_top`, `left_sternal_bottom`, `left_costal_6`, `left_rectus_medial`, `left_rectus_lateral`, `left_erector_top`, `left_erector_mid`, and `left_rib1` … `left_rib12` (side), `_front` and `_back` |
| `pelvis` | `coccyx`; `left_asis`, `left_aiis`, `left_iliac_crest_front`, `_mid`, `_back`, `left_psis`, `left_glute_med_front`, `_mid`, `_back`, `left_sacrum`, `left_pubic_tubercle`, `left_pubic_crest`, `left_pubic_body`, `left_ischial_ramus`, `left_ischial_tuberosity`, `left_erector_base` |
| `clavicle` | `sternal`, `medial_front_1`, `medial_front_2`, `lateral_front`, `lateral_end`, `lateral_top` |
| `scapula` | `acromion`, `acromion_back`, `spine_lateral`, `spine_mid`, `spine_root`, `coracoid`, `supraglenoid`, `infraglenoid`, `supraspinous`, `infraspinous`, `teres`, `inferior_angle`, `medial_top`, `medial_mid`, `medial_bottom`, `medial_front_top`, `medial_front_bottom` |
| `humerus` | `head_front`, `head_top`, `head_back`, `greater_tubercle`, `greater_tubercle_back`, `lesser_tubercle`, `bicipital_groove`, `crest_top`, `crest_mid`, `crest_low`, `medial_lip`, `intertubercular_floor`, `deltoid_tuberosity`, `anterior_upper`, `anterior_distal_upper`, `anterior_distal_lower`, `posterior_upper`, `posterior_lower`, `posterior_distal`, `lateral_supracondylar_upper`, `_lower`, `lateral_epicondyle`, `lateral_epicondyle_front`, `medial_epicondyle`, `medial_epicondyle_top` |
| `ulna` | `olecranon`, `coronoid`, `posterior_border`, `medial_upper`, `ulnar_head_dorsal`, `ulnar_head_palmar` |
| `radius` | `radial_tuberosity`, `mid_lateral`, `mid_front`, `styloid_base`, `distal_dorsal`, `distal_palmar` |
| `hand` | `dorsum`, `palm`, `palm_radial`, `palm_ulnar`, `knuckles_back_index`, `knuckles_back_little`, `metacarpal2_base_back`, `metacarpal3_base_back`, `metacarpal5_base`, `thenar_base`, `thumb_base`, `pisiform` |
| `femur` | `greater_trochanter`, `greater_trochanter_low`, `gluteal_tuberosity`, `it_band_top`, `lesser_trochanter`, `intertrochanteric_front`, `intertrochanteric_low`, `linea_aspera_upper`, `_mid`, `_lower`, `_lateral`, `_medial_low`, `adductor_tubercle`, `medial_epicondyle`, `medial_epicondyle_back`, `lateral_epicondyle`, `medial_condyle_back`, `lateral_condyle_back`, `anterior_mid`, `anterior_distal`, `lateral_mid`, `lateral_distal`, `medial_mid`, `medial_distal` |
| `patella` | `top`, `top_lateral`, `top_medial`, `front` |
| `tibia` | `tuberosity`, `gerdy`, `medial_condyle`, `medial_condyle_back`, `pes_anserinus`, `pes_anserinus_back`, `lateral_shaft_upper`, `lateral_shaft_mid`, `anterior_distal`, `anterior_distal_lateral`, `soleal_line`, `posterior_mid`, `posterior_medial`, `posterior_lateral`, `medial_malleolus` |
| `fibula` | `fibular_head`, `posterior_upper`, `lateral_upper`, `lateral_mid`, `anterior_upper`, `lateral_malleolus`, `lateral_malleolus_back` |
| `foot` | `calcaneus_back`, `medial_cuneiform`, `fifth_metatarsal_base`, `first_metatarsal_base_under`, `toes_top`, `dorsum` |

The skull is an approximation: cranium, brow, orbit rims with dark sockets,
cheekbones and arches, nasal aperture, teeth, mastoids and a separate mandible.
Soft parts that shape the face in the skin only (nose, lips, cheeks, ears)
belong to the skull and jaw shapes. Hair and beard are not modelled.

### Placing a muscle

Each end is a polyline of points joined by `..`. A point is either a
**landmark**, `"bone landmark [lift]"`, or a cast from the bone's axis,
`"bone at azimuth elevation [lift]"`: a fraction along the bone and a body
direction to its surface (0 front, 90 the character's left, −90 medial on the
left limbs, 180 back). Later points may name another bone or drop it to stay on
the same bone, so one muscle can arise from several bones. With `fibers`, a
flat muscle fans along the polyline into one sheet.

```xml
<muscle name="left_biceps" origin="left_scapula coracoid .. supraglenoid" insertion="left_radius radial_tuberosity"
        via="left_upper_arm anterior_upper 2.4 .. bicipital_groove 1" fibers="2" radius="2.4 2.8"
        tendon="0.12 0.14" profile="0.4 0.8 1 0.7 0.3" bulge="1.4" contract="1" jiggle="0.4"/>
<muscle name="left_lat" origin="pelvis left_iliac_crest_back .. left_psis .. spine_3 spinous .. ribcage t10_spine .. t7_spine"
        insertion="left_upper_arm intertubercular_floor" via="left_scapula inferior_angle 1.2" fibers="7" wrap="ribcage spine spine_3"/>
```

| Shape control | Meaning |
|---|---|
| `radius="w t"` | Belly half width across the body and half thickness out of it, in cm |
| `profile` | Thickness curve from origin to insertion, any number of points (default `0.35 0.8 1 0.8 0.35`) |
| `tendon="a b"` | Fractions at the origin and insertion ends that are tendon (thin cords such as the Achilles) |
| `fibers` | Strands fanned across the end spans; they fuse into one sheet in the skin |
| `via="anchor; …"` | Anchors the path passes through: over a bony landmark, in front of the knee. A `..` span spreads with the fibres |
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
écorché view of the individual muscles over the skeleton, or `show="bones"`
for the skeleton alone; the study scene places both twins beside the skinned
character. Details inside bones may carry `view="skin muscles"` to appear only
in those views, like Hercules' loincloth and eyes. Without a `<skin>`, muscles
render as individual strands.

Hercules uses `resolution="0.6"` so the face and fingers read. A skinned
character costs one to two seconds per frame to rebuild; the distance field
fills on all cores.

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
- The thoracic vertebrae move as one `thorax`; the hand's fingers and the
  foot's toes are fixed in a relaxed pose.
- Muscles do not collide with each other; `wrap` only slides them over the
  bones it lists. The skin is not UV-mapped.
- The editor viewport shows time 0; playback happens through rendered
  sequences.
