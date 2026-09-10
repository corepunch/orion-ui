# Workshop implementation coverage

## Implemented initial-state canon

| Requirement | Implementation | Reading camera |
|---|---|---|
| Enormous climbable workbench | Composite dressed drawer workbench with thick legs, apron, pulls, lower shelf and vise | `WorkshopEstablishing`, `WorkbenchClimb` |
| Empty hook and string clue | Isolated carved hook/string landmark with stocked commode below | `EmptyHookReveal` |
| Copper oil can under bench | Small copper can in the front edge of a readable under-bench niche | `OilCanDiscovery` |
| Sawdust and shavings | Perimeter clusters, curls and composite bench-surface dressing | `WorkshopEstablishing`, `WorkbenchClimb` |
| Main exit and Pip-sized flap | Full plank door with casing, braces, hardware and brass-framed flap | `DoorExit` |
| Cuckoo clock and hidden study stair | Peaked faceted-arch case, cuckoo opening, dial ticks, near-midnight hands, pendulum and weights; modeled wall opening and rising stair behind it | `ClockHero`, `EmptyHookReveal` |
| Folding loft ladder and mechanism | Ladder, rungs, winch and crank in initial stuck configuration | `LadderTraversal` |
| Pip's broom | Broom retained near the main bench route | `WorkshopEstablishing` |
| Tool-bench climb geography | Crate/chair/book route leads into a composite dressed desk and tool wall | `ToolRoute` |
| Pip action blocking | One camera-scoped Pip gizmo per declared camera, posed toward that shot's focal action or object | All cameras |

## Implemented art-direction passes

| Pass | Representation |
|---|---|
| Structural articulation | Six-unit shell, full door, arched window, wall posts/braces, cross rafters and continuous stocked loft |
| Hero furniture | Composite main/tool desks, stocked commode, door, architectural clock and ladder |
| Functional storage | Stocked commode bays, stocked shelves, cubby, tool rack and populated loft |
| Work and floor clusters | Composite desk contents, books, jars, boxes, toy parts, hanging toy and sawdust clusters |
| Hardware and accents | Drawer pulls, vise, door straps/ring, ladder winch, detailed clock with three cylindrical brass hinge barrels, hook and window rosette |

## Deliberate spacing audit

- Window mullions reach and terminate inside the arch.
- Clock, window and cabinet read as separate wall landmarks with visible plaster buffers.
- Hook/string has a quiet halo and does not merge with cabinet hardware.
- Door swing/read area and ladder base remain distinct.
- Workbench props live in composite prefabs, form clusters, and leave the climb landing and vise clear.
- The open center floor preserves Pip's circulation.
- Character blocking is camera-local, so alternate Pip poses never appear together as duplicates in a story shot.
- The named clock uses an authored hinge pivot and opens only in `ClockHero`; camera selection rebuilds the base scene so `EmptyHookReveal` and all other shots retain the closed clock.

## Deferred state assets

- Oil-can-removed variant and residual dust silhouette.
- Oiled/raised ladder and operational mechanism.
- Pet-flap movement variant.
- Endgame lighting variant.

These are intentionally not conflated with the initial-state scene. Their mechanical and adaptation decisions remain in `workshop-design.md` under `Unresolved decisions`.

## Procedural surface coverage

The workshop shell uses two-section `<wall>` finishes with automatically cut
baseboards, divider rails and ceiling trims on each visible wall. The old rear
ceiling rail box is replaced by the wall's top trim; posts and diagonal braces
remain explicit structural geometry. One `<floor style="boards">` replaces the
plain slab and generates staggered boards, recessed joints and seeded color
variation. The walkable top stays at Y=0 and all existing scene placements stay
at their original coordinates.
