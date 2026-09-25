# World logic reference

A blockout is the backbone for a finished illustration. An illustrator will
paint over it and a reader will believe it, so every object must be physically
and narratively plausible. Fairy-tale premises (a talking kitten, a star that
glows) are allowed; furniture, stacks and routes still obey ordinary household
physics. A scene that passes every geometric rule but contains a staircase of
books on stilts is a failed scene.

## 1. Write the resident story first

Before placing props, write two or three sentences in the design brief:

- Who lives or works here, and what they were doing just before the story.
- What the room is for (attic storage, study, nursery) and how old it is.
- What the story's action needs from the room (surfaces at given heights,
  something to hide behind, a route to the window).

Every noncanonical prop must be explainable from that story. "An inkwell,
because the grandfather writes letters at this desk" is a reason. "Something
blue to fill the corner" is not. Delete props that have no reason.

## 2. Support: nothing floats

- Every object rests on exactly identifiable supports: floor, a furniture
  surface, a shelf, a hook, a nail, a rail, or another object that can bear it.
  Name the support in the prefab comment or group name when it is not obvious.
- Hanging objects hang from something visible (cord, hook, rod, nail).
- Leaning objects touch both the surface under them and the thing they lean
  on, at a stable angle (roughly 60–80° from horizontal for a leaning book or
  board, 65–75° for a ladder).
- A load goes down through structure. A tabletop rests on legs or a frame; a
  shelf on brackets or sides. Do not add hidden stands, invisible stilts or
  "timber platforms" only to hold up something that would not otherwise be
  there.
- Only the story may break support (a floating star, a spell). Comment the
  exception in XML with the story reason.

## 3. Routes: a character uses the room, not a purpose-built stair

When a character must travel between heights, build the route from things a
resident would plausibly have left there:

- Floor → chair seat → desk top; floor → trunk → windowsill; a drawer pulled
  half out as a step; a stool beside a shelf; a basket, a pile of cushions, a
  crate pushed against furniture; a curtain, a hanging cord or a tablecloth
  to climb; a board or ruler leaning as a ramp.
- Each hop must fit the character's reach from the book's scale sheet
  (section 6). If a hop is too high, move an existing piece of furniture closer
  or add one more plausible object, not a custom step.
- Never assemble stairs or ladders out of props (books, postcards, boxes)
  arranged in a regular staircase. A single real ladder or step stool is fine
  when the room would own one (a loft, a library, a workshop).
- Prefer routes the camera can show in one establishing shot: takeoff,
  intermediate supports and destination together.

## 4. Stacks and clutter follow how people put things down

- **Books.** Mixed heights, widths and thicknesses; the largest at the bottom;
  spines alternating or mostly one way; small yaw offsets of 1–6°; no more than
  about 8–12 in one free-standing pile. On a shelf they stand upright, lean
  against their neighbours or a bookend, and one or two lie flat on top.
- **Paper, letters, postcards.** Bundled with string, in a box or tray, fanned
  out after being read, or a single sheet left mid-task. Never a perfectly
  aligned slab of identical cards.
- **Loose objects** end up where they would fall or be set down: along edges,
  in corners, beside the chair where someone sat. A thrown blanket drapes, it
  does not form a box.
- Do not use `<array>` for loose props. Arrays are for regular construction:
  shelf boards, stair treads, floorboards, balusters, drawer rows.
- Vary everything deterministically: scale ±5–15 %, yaw, spacing, colour
  within a palette.

## 5. Use real furniture with real proportions

- Start from library prefabs under `prefabs/furniture`, `fixtures` and `items`
  before modelling from raw boxes. Extend the library when something is
  missing so later books can reuse it.
- Keep human-scale proportions even when the protagonist is small: chair seat
  ~45 cm, desk ~75 cm, windowsill 80–100 cm, door 200 cm, ceiling 240–280 cm
  (lower under an attic slope).
- Furniture relates to its use. A chair faces or is pushed back from its desk;
  a lamp lights the work surface; a bed has a nightstand; storage sits against
  walls, not in circulation paths.
- Build recognisable silhouettes: legs, aprons, rails, backrests, handles,
  frames. One box per object is only acceptable for distant filler.

## 6. Book scale sheet

Each book's design brief states:

- Protagonist height and length in cm, and the heights of its hands, eyes and
  reach.
- Maximum step or jump height (e.g. a storybook kitten 25 cm tall: 60 cm jump,
  15 cm step without jumping).
- The heights of every surface used in the route, checked against those
  limits.
- The protagonist's minimum on-screen height in establishing shots (aim for at
  least 6–8 % of frame height at 1536×1152) and which cameras meet it.

## 7. Review gate

For every rendered camera, answer in writing before accepting:

1. Point at every object that is floating, unsupported, intersecting or
   resting on a hidden stand. Fix or justify each one.
2. For every object in frame: why is it here? Remove what has no answer.
3. Could the character actually make each move in this shot at the scale sheet
   limits?
4. Would a person walking into this room find it odd? Regular stacks, prop
   staircases, furniture in the middle of paths and empty monolithic boxes all
   count as odd.
5. Do cast shadows confirm contact? A shadow detached from its object means the
   object is floating.
