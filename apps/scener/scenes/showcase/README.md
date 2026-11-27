# Showcase scenes

Two large scenes written for the [Scener website](../../../../docs/scener/index.html).
They stress instancing, shadows and the checker, and both pass `--check` with no warnings.
Run the commands from the Orion UI root after `make scener`.

| Scene | Objects | Shows |
|---|---|---|
| `great_library.blks` | 19,801 | 40 double-faced stacks from one `<define>`, 400 `study/bookshelf` prefabs placed by `<array>`, 10 shadow-casting pendants, gothic windows with glazing bars |
| `hypostyle_hall.blks` | 2,737 | 96 lathed papyrus columns, one `<lathe>` plus `<array>` per row, a ruined roof that lets the sun stripe the floor, a wall opening cut with `<bool-negative-box>` |

```sh
./build/bin/scener --check apps/scener/scenes/showcase/great_library.blks
./build/bin/scener --render apps/scener/scenes/showcase/great_library.blks --size 1600x1000 --format png --ssao --output-dir render/library
./build/bin/scener --render apps/scener/scenes/showcase/hypostyle_hall.blks --size 1600x1000 --format png --ssao --output-dir render/hypostyle
./build/bin/scener apps/scener/scenes/christmas_market_buildings.blks --cam Overview --ui-shot /tmp/editor-market.png --size 1440x900 -no-shadows --ssao
cwebp -q 90 -sharp_yuv /tmp/editor-market.png -o docs/scener/shots/editor-market.webp
```

Measured on an Apple M1 (November 2026): the library checks in 0.53 s at 63 MB and renders
its `nave` camera in 2.7 s at 314 MB peak resident. The same scene exported with
`--export library.glb` took Blender 5.2 183.7 s and 2.51 GB to import and render with Eevee,
and 274.7 s and 2.62 GB with Cycles (64 samples, Metal).
