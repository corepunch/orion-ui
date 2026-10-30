# Doom Editor

Doom-ED is now a native Orion application. It builds against the current split
Orion libraries and vendored cglm 0.9.6 headers, without a separate Doom-ED
checkout or nested UI dependency. Imported from `corepunch/doom-ed` commit
`0be92cd560120719a085a92854c6bcfffe70e679`. The root GPL-3.0 license applies;
cglm's MIT license is in `third_party/cglm-LICENSE`.

## Build and run

From the Orion repository root:

```sh
make doom-ed
build/bin/doom-ed /path/to/hexen.wad
build/bin/doom-ed /path/to/hexen.wad --map MAP02 --3d
make doom-ed-test
```

`make apps` also builds the editor. Game tables use separate translation
units; `build.mk` contains source and dependency rules. The editor uses the
Hexen game-data profile, as the original source did. This port preserves the
original behavior; it does not add WAD saving or other game-data profiles.

Click a map in Project to open it. The editor retains vertex/sector editing,
thing and texture palettes, property inspectors, 2D rendering and 3D preview.
Tab switches between editor and 3D view; the 3D view uses WASD and mouse capture.
Commercial WADs are excluded from Git. Local copies from the old checkout are
preserved in ignored `local/wads/`, with checksums in `reference/wads.sha256`.
Their upstream repository was `Akbar30Bill/DOOM_wads`, commit
`9b384dc68add3eb2f5eb7754654cafeeaea5103b`.

![2D editor](share/screenshots/editor.jpg)
![3D preview](share/screenshots/preview.jpg)

## Verification

```sh
build/bin/doom-ed --check-wad /path/to/hexen.wad
build/bin/doom-ed /path/to/hexen.wad --offscreen --screenshot /tmp/editor.jpg
build/bin/doom-ed /path/to/hexen.wad --offscreen --3d --screenshot /tmp/preview.jpg
```

The test target covers the actual WAD reader, valid and malformed map imports
through the built executable, production triangulation, and the original seven
suites. Several original suites contain copied functions; they remain as
historical coverage alongside tests of the real importer.

The WAD reader validates directory/lump bounds, record sizes and geometry
references. Backend GPU calls live in Orion's renderer; scene code uses its
`R_Gpu*` API. Routine diagnostics require `-DDOOM_ED_DEBUG=1`.

## Removing the old repository

The application source and local WAD copies no longer require Doom-ED.
**This checkout is still inside the old repository, and its `.git` file points
to `../.git/modules/ui`.** Deleting `~/Developer/mapview` would delete Orion
and its Git metadata too. Save these changes in Orion, then use a standalone
Orion clone outside `mapview` before removing that parent directory. Retiring
the Doom-ED GitHub repository does not require deleting the local directory.
