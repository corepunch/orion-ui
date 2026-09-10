# Workshop Floor visual room design

## Inputs

- Canonical source brief: `scenes/books/wondertown/briefs/workshop-source.md`
- Visual references:
  - `/Users/igor/Developer/orca/samples/Book/Images/Generated image 1.png`
  - `/Users/igor/Developer/orca/samples/Book/Images/room-1.png`
  - `/Users/igor/Developer/orca/samples/Book/Images/Generated image 3.png`
  - `/Users/igor/Developer/orca/samples/Book/Images/WORKSHOP-FLOOR.png`
- Project direction: Wondertown ZIL prose, map, object registry, puzzles and story-state documents under `/Users/igor/Developer/orca/samples/Book/libs/zilscript/books/wondertown/work/`

## Visual thesis

Tolliver's workshop is an old, beloved toymaker's cathedral of work: ordinary furniture becomes monumental architecture at Pip's scale, while every wall and surface shows purposeful storage, repair and accumulated history. The room should feel dense and inhabited without obscuring the tutorial landmarks or Pip's navigable routes.

## Reference decomposition

### Generated image 1

- Useful: dominant carved bench, exaggerated legs, vertical wall posts, rafters, high circular and arched windows, shelves of toys and jars, monumental door, cuckoo clock, layered foreground shavings.
- Adapt: retain the heroic bench, vertical articulation and high-window rhythm; reduce the number of competing toy figures so story props stay legible.

### room-1

- Useful: drawer workbench, small tools in clustered holders, deep wall shelves, jars, clock, door, low-angle Pip scale and warm pool of light.
- Adapt: use the drawer/handle foothold logic and work clusters; preserve more breathing space than the reference around interactive landmarks.

### Generated image 3

- Useful: extreme vertical scale, columns, upper shelves, suspended workshop artifacts, massive bench apron, low-angle composition and tiny character.
- Reject: aircraft, balloon and excessive ceiling suspension because they create unsupported story claims and visual noise.

### WORKSHOP-FLOOR

- Useful: decorated plank door with pet flap, cuckoo clock, small drawer cabinet, window-side shelves, tool rack, drawer-and-vise bench, floor chest, wall framing, warm window/lamp contrast.
- Adapt: use this as the clearest furnishing grammar while retaining the canonical arched night window already established in 3D.

### Unified language

- Retain: heavy dark wood, warm brass/copper, worn plaster, exposed framing, drawer fronts and handles, wall shelves, jars, tool silhouettes, high beams, shavings and an ornate arched window.
- Adapt: keep the existing room footprint and story cameras while increasing large and medium-scale masses.
- Reject: incompatible alternate window shapes, multiple dominant clocks, aircraft, excessive hanging objects and any clutter that blocks canonical climb paths.

## Parameterized architecture (September 2026 direction)

Use automatic wall finishes: desaturated green lower sections to 180 cm, warm
plaster above, dark wood baseboards, divider rails and ceiling trims. The wall
owns these finishes and cuts them together at doors and windows. Retain the
exposed vertical posts, braces and ceiling rafters as structural members.
Use a procedural board floor at the existing Y=0 support plane, with 22 cm
courses, 160 cm board lengths, 0.3 cm joints and 18% deterministic color variation.
Keep room dimensions, furniture placement, lights and story cameras unchanged.

## Categorized inventory

| Element | Class | Function | State | Zone | Visual priority | Evidence |
|---|---|---|---|---|---|---|
| Enormous drawer workbench | CANON + REFERENCE | Hero landmark, climb route and work surface | Persistent | Back-right work zone | Hero | Source brief; all references |
| Empty brass hook and string | CANON | Missing-key clue | String present/taken | Back wall, isolated | Hero detail | Source brief |
| Main plank door with pet flap | CANON + REFERENCE | Exit and scale contrast | Flap closed/swinging | Back-left | Hero | Source brief; WORKSHOP-FLOOR |
| Cuckoo clock and hidden stair | CANON + REFERENCE | Time landmark and secret route | Closed/swung open | Back wall | Hero | Source brief; all references |
| Folding loft ladder and winch | CANON | Traversal puzzle | Rust-stuck/oiled-raised | Left wall to upper space | Hero | Source brief |
| Golden sawdust and shavings | CANON + REFERENCE | Atmosphere and grounding | Persistent | Floor and benches | Secondary | Source brief; all references |
| Copper oil can | CANON | First reward | Under bench/carried | Main bench under-space | Hero detail | Source brief |
| Pip's broom | CANON | Personal prop | Leaning/carried | Main bench edge | Secondary | Source brief |
| Tool-bench climb route | CANON | Route to countertop | Bertrand blocks/moves | Right-side zone | Secondary | Source brief |
| Drawer cabinet/commode | REFERENCE | Medium mass and storage | Closed | Beneath wall landmarks | Secondary | WORKSHOP-FLOOR; Generated image 3 |
| Drawer fronts and brass pulls | REFERENCE + INFERRED | Bench construction and footholds | Closed | Both benches | Secondary | room-1; WORKSHOP-FLOOR; canon climb prose |
| Bench vise | REFERENCE + INFERRED | Trade identity and strong silhouette | Fixed | Main bench front-right | Secondary | Generated image 1; WORKSHOP-FLOOR |
| Wall shelves with jars and boxes | REFERENCE + INFERRED | Upper-wall occupation | Persistent | Back-left and right wall | Secondary | All references |
| Tool rack with saws, chisels and mallets | CANON-adjacent + REFERENCE | Functional workshop identity | Persistent | Right wall above tool bench | Secondary | Tool-bench source; room-1; WORKSHOP-FLOOR |
| Wall posts, rails and braces | REFERENCE | Express height and construction | Persistent | Back and side walls | Secondary | Generated images 1 and 3; WORKSHOP-FLOOR |
| Cross rafters | REFERENCE + INFERRED | Articulate high ceiling | Persistent | Upper room | Secondary | Generated images 1 and 3 |
| Jars, tool pots, books and parts trays | REFERENCE + INFERRED | Work clusters | Persistent | Shelves and benches | Support | All references |
| Toy train, boat and unfinished pieces | CANON-adjacent | Toymaker identity | Persistent | Main bench | Support | Workbench-top prose |
| Crates, timber and offcuts | REFERENCE + INFERRED | Floor depth and storage | Persistent | Corners and foreground | Support | Generated image 1; WORKSHOP-FLOOR |
| Ornamental arched window rosette | REFERENCE | Distinctive light source | Persistent | Back-right wall | Secondary | Generated images 1 and 3; existing scene language |

## Spatial plan

- Rebuild the roughly rectangular workshop with a 6-unit high shell so the loft, rafters, tall clock and hanging work read as a true vertical volume. This supersedes the earlier 4.2-unit preservation constraint after the explicit restart direction.
- Treat the back wall as the principal readable elevation: monumental main door at left, storage/landmark zone in the center, ornate arched window and main bench at right.
- Keep the main bench centered under the window so directional light patterns cross its active work clusters.
- Place the drawer cabinet in the center-left wall bay, leaving visible plaster between it, the hook, clock, door and window.
- Place the folding ladder on the left wall so its base and upper destination read together from a dedicated low camera without blocking the door.
- Occupy upper walls with one back-wall shelf and two right-wall shelves. Keep the clock and key hook isolated as narrative landmarks.
- Strengthen wall bays with posts, a top rail and lower panel rails. Keep trims proud of plaster to avoid coplanar faces.
- Add cross rafters above the existing transverse beams so the ceiling reads high rather than blank.
- Keep the center floor circulation open for Pip. Concentrate floor clutter along walls, under benches and at frame edges.
- Preserve assembly contact inside furniture, shelves and climb routes; maintain breathing space between clock/window, clock/shelves, hook/cabinet and door/ladder.

## Density plan

1. Structural articulation: add back-wall posts and rails, cross rafters, a full-sized door casing and deeper window ornament. Target visible structure in the upper third and around every major opening without filling text zones.
2. Hero furniture and silhouettes: replace slab benches with thick legs, aprons, drawers, handles and vise geometry; add a medium drawer cabinet and canonical loft ladder. These masses must read at thumbnail scale.
3. Functional storage and wall occupation: add a tool rack and multiple wall shelves with brackets, leaving narrative wall objects isolated.
4. Surface and floor clusters: arrange three authored work clusters on the main bench, two on the tool bench, shelf clusters, wood offcuts and shavings along perimeter zones.
5. Hardware and accents: add drawer pulls, strap hinges, door braces, vise screw, ladder wheels and window rosette. Stop when accents support silhouettes rather than becoming uniform noise.

Density percentages refer to projected visible silhouette/tonal-mass area in the named camera, not object bounding boxes. The establishing frame should contain one dominant bench mass, three or four medium masses, articulated upper space and clear foreground/midground/background layers. No more than roughly one quarter of the frame should be featureless wall unless reserved as a text zone.

## State variants

| Canonical state | Visible geometry | Transform/visibility | Prop changes |
|---|---|---|---|
| Initial | Clock closed; ladder visibly connected to rusty winch; pet flap closed; hook empty | Default transforms | Oil can under main bench; broom leaning nearby |
| Oil can taken | Same room | Default transforms | Hide/move oil can from under bench |
| Ladder oiled | Ladder raised toward loft; winch reads aligned and operational | Ladder hinge/rotation variant | Oil can with Pip |
| Key found | Clock still closed but becomes stronger story focus through framing | No forced geometry change | Key carried |
| Study accessible | Clock swings outward; narrow stair appears behind wall position | Clock hinge rotation and stair visibility variant | Helper may be staged near latch |
| Endgame | Existing geometry retained | Warmer light/intensity variant | Companion staging may change |

The shared scene keeps the initial workshop as its authored base state. Shot-specific state changes use explicit camera transforms: `ClockHero` opens the named clock around its hinge while every other camera restores the closed pose. Future variants should follow the same explicit camera-scoped pattern or use separate scene files when they require more than transforms.

## Lighting and palette

- Keep warm amber practical light as dominant interior key and cooler green-blue window glass as secondary contrast.
- Let the arched window cast crisp mullion/rosette patterns across the main bench and floor.
- Retain low ambient so drawers, posts, tools and shelf clusters model clearly through shadow.
- Use dark wood for structural hierarchy, warmer mid-wood for work surfaces, brass/copper accents for story clues and desaturated plaster so silhouettes remain distinct.
- Ensure the canonical oil can and hook catch enough light to register in their detail cameras without making every jar equally bright.

## Camera coverage matrix

| Camera/story beat | Focal subject | Required canon | Reference-driven context | Depth planes | Text zone | Density risks |
|---|---|---|---|---|---|---|
| WorkshopEstablishing | Pip approaches the giant bench | Bench, hook, clock, sawdust, pet-door direction | Drawers, cabinet, wall framing, shelves, rafters | Foreground sawhorse; Pip and bench; back wall | Upper-left plaster/beam bay | Tiny props may vanish; wall must not remain blank |
| EmptyHookReveal | Pip reaches toward the empty hook and string | Hook/string | Cabinet edge, clock and shelf context | Foreground wall detail; Pip; clock/window edge behind | Left field | Do not merge hook with cabinet or clock |
| ClimbWorkbenchAction | Pip climbs toward a drawer handle | Bench, Pip route | Thick carved leg, apron, drawer pulls, vise | Foreground leg; Pip; tabletop/window | Upper-left | Added drawers must remain plausible footholds |
| WorkbenchTopEstablishing | Pip works among the repair landscape | Tools, toys, repair book, shavings | Tool rack and window spill | Foreground props; Pip/tabletop; wall storage | Upper-left | Cluster scale must read without intersections |
| OilCanCloseup | Pip crouches toward the copper can | Oil can, bench under-space | Drawer/apron silhouette and shelf stock | Foreground leg; Pip/can; lower shelf | Upper-right shadow | Do not close discovery sightline |
| ToolBenchEstablishing | Pip climbs the crate-chair-book route | Crate, chair, books, Bertrand | Tool rack, shelves, drawers | Foreground crate; Pip/route; tool wall | Upper-left | Wall dressing must not obscure route silhouette |
| LayoutPlan | Navigable room geography | All canonical zones | Perimeter storage and open center | Whole room | Not production text | Clutter must stay outside circulation |
| LadderTraversal | Pip works the rusty ladder mechanism | Ladder and mechanism | High posts, rafters, loft implication | Winch/Pip foreground; ladder mid; beams high | Right wall bay | Both ends of traversal must fit |

## Implementation priorities

1. Upgrade reusable workbench prefab with thick construction, drawers, handles and vise.
2. Replace the small door insert with a full main workshop door and integrated Pip-sized flap.
3. Build canonical ladder/winch, workshop cabinet, wall shelf and tool-rack prefabs.
4. Add wall framing and cross rafters to the scene shell.
5. Place storage systems and populate them with existing reusable jar/book/tool prefabs.
6. Enhance the window with an ornamental rosette while preserving working mullions and light casting.
7. Rebalance main/tool-bench work clusters for larger readable silhouettes.
8. Add a dedicated ladder camera and adjust the establishing camera only if the new masses do not read.
9. Validate canonical coverage, geometry, XML and every story camera render.

## Unresolved decisions

- Narrative adaptation: decide whether the hidden study entrance follows the executable workshop-floor `IN` exit or the prose reveal behind Old Tick in the loft.
- Art direction: decide whether the reference-driven arched window remains part of final canon-adjacent geography.
- Implementation: define the exact ladder hinge/folding sequence and visible loft landing before authoring its raised state.
- Implementation: define the clock hinge side and how its pendulum and weights clear the concealed stair before authoring its open state.
- Art direction: choose still versus gently swinging initial pet-flap staging per shot.
- Production: confirm final page aspect ratio and per-camera text-side preference.

## Acceptance checklist

- Canonical inventory and initial states remain intact.
- Major reference motifs have explicit retain/adapt/reject decisions.
- Bench, door, window, clock and ladder read at thumbnail scale.
- Back wall and ceiling express height and occupation.
- Bench construction supports the described drawer-handle climb.
- Density appears in foreground, midground, background and upper space.
- Clock, window, hook, cabinet and door preserve deliberate breathing space.
- Story cameras retain focal hierarchy and text zones.
- State-dependent geometry is documented for future variants.
