---
name: populate-simplegl-scenes
description: Implement, populate, edit, compose, or validate SimpleGL XML scenes and prefab XML files from an approved visual room design or direct scene request. Use for room layout, walls and openings, window or door inserts, furniture placement, set dressing, materials, lights, cameras, groups, primitive selection, transforms, prefab authoring, design-coverage audits, and diagnosing sparse, misplaced, floating, intersecting, incorrectly oriented, or incorrectly scaled scene objects.
---

# Populate SimpleGL XML Scenes

For ellipsoid character geometry and posing, also use
[populate-scener-characters](../populate-scener-characters/SKILL.md).

Build scenes in stable local coordinate frames and verify them with CLI checks and tests. Author lengths in centimetres, rotations in degrees and scales as unitless values. New scenes declare `<scene up="z">`; primitive local axes remain unchanged.

## Read the relevant references

- Read [references/scene-format.md](references/scene-format.md) for supported XML tags, attributes, defaults, rotations, modifiers, and prefabs.
- For windows, read the complete [window schema](references/scene-format.md#window) and [procedural-window recipes](references/procedural-windows.md). These list every supported preset/style and parameter, with runnable placement examples and current limitations.
- Read [../../CLI.md](../../CLI.md) for the active Scener commands; historical `simplegl` and standalone `screenshot` commands are obsolete.
- Read [references/world-logic.md](references/world-logic.md) before any placement. It defines the resident story, support, character routes, stacking and scale-sheet rules that make a scene physically believable.
- Read [references/layout-and-validation.md](references/layout-and-validation.md) whenever placing walls, openings, inserts, furniture, cameras, lights, or prefabs.
- Read [references/shot-composition-guide.md](references/shot-composition-guide.md) whenever placing or revising cameras, and use it to define each shot's story purpose, framing, continuity, negative space, and field of view.
- Read [references/alone-in-the-dark-layout-study.md](references/alone-in-the-dark-layout-study.md) whenever planning a multi-room floor, fixed-camera coverage regions and handoffs, corridors, column rhythms, or stair traversal shots.

## Workflow

1. Locate the approved visual room design and its canonical source brief for story-driven or reference-driven work. If either is missing, use the appropriate source adapter and `$art-direct-room` before implementation. Do not derive the full environment directly from raw story objects when a design brief is required.
2. Inspect the target scene, referenced prefabs, existing materials, brief inventory, state variants, priorities, and camera coverage matrix before editing. When explicitly rebuilding from scratch, retain the approved briefs and reusable generic assets only; do not copy the discarded scene's transforms, clusters, or composition.
3. Write the resident story and confirm the book's scale sheet (see [world-logic](references/world-logic.md)). Plan character routes through existing furniture before placing props.
4. Establish the room coordinate system, floor height, wall centers, local axes, spatial zones, circulation and story-action clearances.
5. Create the structural shell first: floor, walls, ceiling or roof, openings, architectural articulation, cameras, and lights. An interior room is enclosed unless the design explicitly calls for an open or roofless space.
6. Build hero furniture and large silhouettes before storage systems, prop clusters and accents. Give every hero a distinctive outer silhouette, secondary construction hierarchy, and story-specific detail; a generic box with an emblem is not a finished hero asset. Reuse a prefab when an object appears more than once or has a natural front direction.
7. Place related geometry in a shared `<group>` coordinate frame. Never duplicate a rotated parent's world-space transform by hand when a group can express it.
8. Keep all naturally grounded objects at the documented prefab baseline. Calculate primitive centers from half-height; do not guess vertical positions.
9. Populate every density pass named by the visual design while preserving its intentional rest areas and text zones. Fill visible drawers, cubbies, shelves, bins, racks, and under-furniture storage with plausible contents unless emptiness is deliberate and narratively legible. Do not use raw object count as proof that a room is sufficiently authored.
10. Audit every visible contact: joined assembly parts terminate cleanly against their supports, while unrelated objects retain deliberate negative space without accidental overlap or tangency.
11. Audit coverage: every `CANON` element and required initial state is represented, every hero/secondary design element reads in at least one intended camera, and any deliberate omission is documented. For story cameras, audit the actor/action/target as well as the environment: place one camera-scoped character dummy for each character who must appear, choose a readable pose aimed toward the interaction target, and never rely on a camera merely pointing at an empty object to imply the action.
12. Validate XML, build the project, and run the tests.
13. Load the scene through the CLI and check its declared cameras.
14. When composition, lighting, or references are part of the request, render the affected cameras with `scener --render scenes/scene.blks --camera CameraName --size 1536x1024 --format jpg --output-dir /tmp/scene-review`, then inspect the image before accepting the edit. Review the result as an art-direction problem: identify generic hero silhouettes, empty functional volumes, weak upper-space occupation, flat front-on staging, repetitive prop rhythm, implausible scale, missing reference motifs, blocked actions, tangencies, and false grouping. Correct the scene and render again. Batch renders hide editor overlays by default. For every camera requiring a character, also render a blocking review with `-d 8`, which hides camera/lamp helpers but keeps character gizmos visible; confirm the correct actor appears once, at the correct support height, with a readable pose and gesture toward the focal target. Screenshot review complements, but does not replace, CLI validation.
15. For every interior room, place at least one motivated practical or window light that casts readable shadows. Match visible lamp geometry to its light position, establish a clear key direction, and use weaker fill only where needed to keep important actions legible.
16. Correct every invariant violation found through brief coverage, coordinate calculations, XML validation, scene loading, screenshot review, or tests.

## Render critique gates

- Apply the [world-logic review gate](references/world-logic.md#7-review-gate) to every rendered camera: no floating or stilt-supported objects, no prop staircases, a reason for every prop, and every route hop within the scale sheet.
- Judge hero assets in close view and at thumbnail scale. Replace generic inherited assets when their silhouette, construction, or ornament does not express the room's identity.
- Check practical-light reach against the floor, hero furniture, storage interiors, ceiling/rafters, and story props. A technically lit room still fails when upper space becomes a black void or lower storage collapses into silhouette.
- Check open storage in its intended camera, not only in XML. Contents need readable color/value separation, depth layering, and enough scale to register.
- Check low cameras for foreground takeover. Foreground framing should lead toward the hero; move or reduce anything that becomes the dominant mass by accident.
- Calibrate camera height against the declared character height. A supposed tiny-character viewpoint placed at twice the character's height weakens furniture monumentality even when the geometry is correctly scaled.
- Check traversal shots for both endpoints. Showing a ladder or mechanism is insufficient unless the takeoff, complete route, and recognizable destination read together.
- Check actor/action/target completeness. A shot of an interactive object without the acting character is object coverage, not action coverage. Keep alternate poses camera-scoped so a shared scene never shows duplicate copies of one actor.
- Check small canonical props for discoverability. First improve placement, opening, silhouette, and camera angle; then add a restrained motivated bounce/glint only when the physical staging still needs contrast.
- Compare the render against the reference decomposition after every major pass and name what remains absent. Add missing architectural or occupational motifs selectively rather than accepting a valid but generic room.

## Brief handoff

- Treat the canonical source brief as factual authority for names, relationships, interactions, scale language and required states.
- Treat the visual room design as authority for architecture, noncanonical furnishings, density, hierarchy, lighting, zones and camera intent.
- Preserve the design classification of `CANON`, `REFERENCE`, `INFERRED` and `ATMOSPHERE`; noninteractive visual dressing does not need a game-parser object.
- Implement the approved initial state unless the request names another state. Keep future state geometry feasible and document what remains unimplemented.
- When a later explicit user direction supersedes an approved design decision, update the design brief before implementation and record the change in coverage. Never let a later coverage document silently override the design.
- When a current scene conflicts with the briefs, correct the scene or record a deliberate renderer limitation. Do not silently weaken the brief to match existing assets.

## Prefab architecture

- Organize authored families with semantic subfolders: use `prefabs/workshop/clock.blk` or `prefabs/workshop/desks/main.blk`, never category or room prefixes joined into filenames such as `workshop_clock.blk` or `desk_with_items.blk`.
- Keep generic undecorated construction in category folders such as `furniture/`, `fixtures/`, and `items/`. Put room-specific dressing and combinations under the room family.
- Author furniture-with-contents, stocked shelves, dressed desks, and other meaningful object clusters as composite `.blk` prefabs that reference smaller prefabs. The scene should place the complete authored object, not rebuild its contents item by item.
- Use scene-level groups only for relationships unique to room geography or story staging. If a cluster could move as one object or recur coherently, make it a prefab.
- Preserve useful attach points on both base and composite prefabs so later state variants can relocate story props without dismantling the asset.

Declare scene-wide ambient light and background only as attributes on the root
element: `<scene ambient="r g b" background="preset-or-rgb">`. Never emit
`<ambient>` or `<background>` child elements. The XML parser accepts those
unknown nodes but ignores them, silently falling back to its defaults.

## Color-space contract

- Author every XML value that represents a visible RGB color in sRGB space,
  using the same `0..1` values a color picker displays. This includes scene
  `ambient` and `background`, material and shape `color`, light and sun
  `color`, unlit emitters, and colored dummy/overlay geometry.
- Do not pre-linearize, gamma-correct, square, or otherwise transform authored
  color values. The renderer converts sRGB colors to linear values exactly
  once at its input boundary, performs lighting in linear space, and relies on
  the sRGB framebuffer to encode the final output for display.
- Treat light `intensity` as a linear scalar, not a color. Never apply an sRGB
  conversion to intensity or fold intensity into the XML `color` value.
  `intensity="2"` supplies twice the linear light energy of `intensity="1"`,
  although the displayed pixel value is not necessarily twice as large after
  lighting, clipping, and sRGB output encoding.
- Treat positions, directions, radius, shininess, transforms, and every other
  non-color number as linear data with no color-space conversion.
- Keep light color channels normally within `0..1`; use `intensity` for HDR
  brightness above white. Use `color="1 0.75 0.4" intensity="2"`, not
  `color="2 1.5 0.8" intensity="1"`.

See [references/scene-format.md](references/scene-format.md#color-space-and-numeric-units)
for the complete attribute classification and renderer data flow.

## Procedural windows

- Use the existing presets and parameters from the schema; do not infer features from other modellers. `style="storybook"` is valid with any of the three presets. `style="cartoon"`, bars, tracery and opening sashes are not implemented.
- Author windows as siblings of walls or inside a shared group/prefab, never as wall children. Use `rot="90 0 0"` for an otherwise unrotated Z-up window; keep its cutter plane parallel to its wall.
- Specify outer dimensions; the frame, inner pane and exact wall cut derive from them. Use `frameWidth` and `depth` directly; `<window>` rejects all child elements, including modifiers.
- Treat `pane`, `sill`, `cutWalls`, `renderable` and `castShadow` as independent controls. Omit a pane with `pane="0"`, not a zero thickness. `sill` is a boolean on a window but an elevation on a legacy `<opening>`.
- For recessed windows, calculate whether `depth/2 + sillProjection` clears the wall's front face. The sill does not find that face automatically. Use named frame/glass materials; the pane is opaque and no light is generated automatically.
- Consult the recipe reference for explicit prefab repetition, stacked windows, dimension constraints, material precedence and troubleshooting before constructing a custom workaround.

## Spatial invariants

- Treat `pos`, `rot`, and `scale` as transforms in the parent group's coordinate frame.
- Treat a wall's local X as length, local Y as height, and local Z as thickness.
- Express wall inserts in the same local frame as the wall. For an opening, calculate:
  - center X: `opening.x + opening.width / 2 - wall.length / 2`
  - center Y: `opening.sill + opening.height / 2`
  - center Z: `0`
- Align the smallest dimension of a thin insert with the wall's local Z thickness axis.
- Prefer `<window preset="round-arch|cottage|gothic">` for supported fixed windows; choose one literal preset, not the pipe-separated list. Its outer profile already cuts matching walls. Do not add a duplicate `<opening>` or negative shape. For custom door/other inserts, keep the correctly shaped cutter and visible geometry together in one prefab.
- Keep inserts smaller than their opening only when visible construction clearance is intentional.
- Treat a gap or penetration larger than `0.001` scene units as an error unless the design explicitly requires it.
- Make structural and decorative members terminate deliberately. Bars, mullions, rails, legs, cords, and similar joined parts must meet their intended frame or support within `0.001`; never leave endpoints floating visibly inside open space. For a member ending at a curved boundary, calculate the curve intersection at the member's full width instead of extending or shortening it by eye.
- Distinguish physical assemblies from unrelated neighbors. Parts meant to function together may touch or visually overlap where construction requires it; separate unrelated fixtures, furniture, and decorations with readable negative space. Avoid silhouette tangencies, near-tangencies, and shadow mergers that make separate objects look accidentally grouped.
- Evaluate spacing in both world space and the affected camera views. As a starting point, give unrelated neighboring silhouettes a visible gap at least as wide as the smaller object's nearby trim or structural member, then increase it when perspective or cast shadows close the gap.
- Never overlap coplanar visible faces. OpenGL depth settings cannot reliably order surfaces at the same depth; resize or reposition the parts so their exterior faces occupy distinct regions. Adjacent parts may meet at a shared edge.
- Prefer swapping box dimensions over adding a rotation when both describe the same axis-aligned shape in the current local frame.
- Document the default front direction in every directional prefab's leading XML comment.
- Orient prefab instances toward their intended target using the documented front direction; never infer it only from the prefab name.
- Use `attach="name:slot"` for placing objects on prefab surfaces rather than manual vertical position calculations. The attached element inherits the instance's full world transform — its `pos`, `rot`, and `scale` are applied in the instance's local frame at the attach point. Objects placed on a rotated workbench automatically stay flat on the surface without the author needing to match rotations.
- Put surface attach points at the usable surface center, not its front or side edge. Apply deliberate offsets in a shared local group, and keep each object's complete rotated footprint inside the support boundary with visible margin.
- Make lived-in prop clusters irregular but authored: vary yaw, spacing, depth, and scale slightly instead of aligning every center on one axis. Keep the variation deterministic, preserve contact, prevent intersections, and do not tilt an object away from its support unless it pivots plausibly from a contact edge.
- Use `pivotOffset` for hinged rotations (book covers, open drawers) instead of `group` nesting workarounds.
- Use the `<array>` modifier for regular construction (shelf boards, stair treads, floorboards, balusters). Do not use it for loose props such as book piles or letters; vary those per [world-logic](references/world-logic.md#4-stacks-and-clutter-follow-how-people-put-things-down).
- Prefer built-in preset materials (`wall`, `floor`, `wood`, `metal`, `glass`) and backgrounds (`midnight`, `dusk`, `neutral`, `black`). Define `<material>` tags only for custom materials.
- Close interior shells with a ceiling or roof at the wall-top elevation unless an opening is intentional. Keep overhead cameras below a visible ceiling or provide a deliberate non-production plan view.
- Motivate every light with visible or implied scene geometry such as a lamp, window, fire, or doorway. Put a reusable practical's `<light>` inside its prefab so geometry and illumination share one transform.
- Mark visible bulbs, flames, and other self-luminous source geometry `unlit="1" castShadow="0"`. Place the point light inside that source volume and below any opaque shade or lamp body so the fixture does not block its own useful light.
- Light the scene with its lights, never with ambient. Ambient is shadowless fill: keep each `ambient` channel at or below about 0.35 (sRGB). When a render is too dark, find the light that is not arriving (sealed in a shade, too small a `radius`, blocked by a wall or ceiling) instead of raising ambient; high ambient flattens every cast shadow. Point-light falloff is `1 / (1 + 2d/R + d²/R²)`, which is only 25 % at `d = R`, so set `radius` to at least twice the distance to the farthest surface the light must reach.
- Never seal a shadow-casting light inside a closed shadow-casting mesh. `<cone>`, `<cylinder>`, `<box>` and `<sphere>` are closed solids, so a bulb inside a cone shade lights nothing. Use an open `<cylinder tube="…">` drum shade held by a socket, or put the light below the shade's opening. Scener warns at load when a light is sealed in, and when a scene has no `<light>` or `<sun>`; treat both warnings as errors.
- Check the key light once per scene by rendering with `ambient="0 0 0"`. Every story area must still read, and cast shadows must be clearly visible. Then restore a low fill ambient.
- Every scene object and architectural element must cast shadows (`castShadow="1"`) unless it is self-luminous emitter geometry. Ceilings, floors, walls, furniture, and props all contribute to the stencil shadow volumes. The only legitimate `castShadow="0"` exceptions are: unlit light bulbs/flames, glass panes (which are opaque in fixed-function but conceptually transparent), shadow-catcher placeholder planes, and deliberately composited scene boundaries.
- Give each room a dominant shadow-casting key light. Keep ambient light low enough for shape, but never leave the hero subject or interaction in featureless darkness; add a weaker motivated fill or rim when required for readability.
- Aim directional exterior light through an actual opening. Use a **45–60 degree downward** angle and offset it **15–45 degrees from the wall axis** so cast shadows fall diagonally rather than parallel to walls. `dir="-0.6 -1 1"` gives ~45° down and ~30° horizontal offset; `dir="0 -1.7 1"` gives ~60° down. Never set the horizontal component to zero — that produces shadows aligned to walls, which reads as flat and uninteresting. Check that the ceiling and wall shell do not accidentally block the intended window-light path.
- Treat a window as both a compositional subject and a lighting instrument, not background decoration. In at least one establishing or action camera, frame the window itself or its bright spill so the source of the key is legible. Place the window on the side of the hero work surface that the camera can plausibly see; a distant window behind the action often lights only an empty floor and contributes neither story nor depth.
- Solve daylight placement against the hero surface before committing to the room layout. For a window-center ray `p + t * dir` (where `dir.y < 0`) and a tabletop at height `h`, use `t = (h - p.y) / dir.y`, then check the resulting X/Z point lands inside the tabletop footprint. Move the opening or adjust the sun direction until it crosses a prop cluster rather than bare floor. Window frames and mullions must cast shadows so this spill creates readable, crisp patterned shapes across the table and its objects.
- When the camera must view a closed room from the outside, mark the camera-facing wall `renderable="0" castShadow="1"`. It remains invisible while preserving correct interior shadow and light-blocking behavior.
- Build character routes from furniture and objects the resident would own (chair, drawer, trunk, curtain), never from props arranged as a staircase. Verify every traversal step, including a real ladder or lift when the room owns one, is positioned adjacent to its destination platform. The base should sit near the lower platform and the top should reach near the upper platform so a single shot can capture both ends of the traversal.

## Required CLI validation

For scene/prefab authoring, use the deployed Scener CLI from the consuming
project's working directory (replace paths/camera names with the actual target):

```sh
xmllint --noout scenes/scene.blks
xmllint --xpath 'count(/scene/ambient | /scene/background)' scenes/scene.blks
scener --list-cameras scenes/scene.blks
scener --render scenes/scene.blks --camera CameraName --size 1536x1024 --format jpg --output-dir /tmp/scene-review
```

Run `xmllint` on every edited scene and prefab. The XPath count must be `0`.
The active CLI has no `-test` mode; camera listing loads the scene but is not a
layout/geometry audit. Inspect stderr as well as exit status: unsupported tags,
invalid window parameters, unresolved materials or prefabs are failures even
when loading returns zero. Inspect affected raster views to validate actual
contacts, silhouettes and through-wall openings. Use the GPU-backed shadow
rendering described in [../../CLI.md](../../CLI.md).

When Scener code changes, build from the Orion UI root and run the focused tests:

```sh
cd ~/Developer/mapview/ui
make build/bin/scener build/bin/test_scener_input_test
DYLD_LIBRARY_PATH="$PWD/build/lib" ./build/bin/test_scener_input_test
python3 apps/scener/deploy.py --prefix "$HOME/.local"
python3 apps/scener/tests/test_cli.py "$HOME/.local/bin/scener"
```

Run `test_shadow_backend.py` as described in `CLI.md` for renderer/backend
changes. Artwork or documentation changes alone do not require rebuilding an
unchanged engine. Do not rerender unrelated artwork to verify instructions.

## Prefab rules

- Keep prefab geometry centered around a useful placement origin, normally floor center.
- Use semantic directory hierarchy instead of filename prefixes: `workshop/clock`, `workshop/desks/main`, and `workshop/shelves/jars`, not `workshop_clock`, `workshop_main_desk`, or `workshop_jars_shelf`.
- Keep prefab materials externally resolvable by the containing scene.
- State footprint, baseline, and front direction in the leading comment.
- Declare `<attach>` elements on prefabs that have meaningful surface reference points (tabletop center, seat surface, shelf height). Name the primary work surface `top_surface`; use `under_center`, `shelf_lower`, `shelf_upper`, or `edge_n`/`edge_s` for secondary slots.
- Define a shelf attach at the center of each usable shelf surface. Name them by tier (`shelf_lower`, `shelf_upper`). Treat edge anchors as separate, explicitly named slots rather than using an edge as the default surface anchor.
- Keep a practical light, its unlit emitter, and its shadow-casting shade in one prefab. Verify transformed and scaled instances keep the point light inside the emitter and on the emitting side of the shade lip.
- A window prefab contains `<window>` as its insert and cutter. Keep it centred on the opening, with local Z aligned to wall thickness. For custom inserts without a procedural window, keep the cutter and visible frame in one prefab and size the cutter to cross the wall. Repeat window prefabs with explicit instances: prefab `<array>` copies do not repeat cutters. Do not use attach-based placement for window assemblies.
- Use `source=` on `<prefab>` to specify the file; `name=` only when something references this instance via `attach`.
- Use `sanityIgnore="1"` only for a documented intentional overlap that the proxy checker cannot model, such as a wall insert owning its cutter. Never exempt a hero, furniture assembly, traversal mechanism, or practical light merely to obtain a passing scene test; correct its placement or explain the exact checker limitation.
- Verify a directional prefab's `0`, `90`, `-90`, and `180` orientation mappings numerically before using it repeatedly.
- Prefer a prefab over copied groups so later corrections propagate to every instance.
- Make visible storage believable. An open commode, cubby, shelf, drawer, bin, rack, or under-desk bay should contain grouped objects with varied scale, yaw, depth, and silhouette unless its emptiness is a deliberate focal statement.
- Make composite prefabs reference their component prefabs. A dressed desk owns its work clusters; a stocked commode owns its shelf contents; a lamp owns its emitter and light. Keep room XML focused on spatial relationships between these authored objects.

## Completion standard

Do not report a scene as complete until its source/design coverage is audited, its XML validates, the project builds, relevant tests pass, and the scene loads through the CLI. For an interior, also verify that every density pass in the approved design is represented, the shell includes its intended ceiling or roof, visible practicals own aligned prefab-local lights, emitter geometry is unlit and shadow-free, and every story camera has a readable focal subject with deliberate cast shadows. Report unimplemented state variants and any limitation that CLI validation cannot establish.
