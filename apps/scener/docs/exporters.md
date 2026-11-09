# Exporter plugins

`scener --export OUT.ext SCENE` writes the built scene through an exporter
plugin, the way a 3ds Max `SceneExport` plugin writes a file: the plugin
declares its extensions, descriptions and options, and writes the file itself.

```sh
make exporters                       # build/exporters/export_*.dylib|.so|.dll
export SCENER_EXPORTER_PATH=$PWD/build/exporters
scener --list-exporters
scener --export room.obj apps/scener/scenes/sample_room.blks
scener --export room.glb apps/scener/scenes/sample_room.blks --opt merge=1
```

`make scener` builds the exporters too, and `apps/scener/deploy.py` copies them
into the bundle and points the launcher at them.

## What is exported

Every renderable object of the built scene (booleans, modifiers and prefabs
applied, characters posed at `--time`). Shadow-only nodes (`renderable="0"`),
lights, lines and overlays are left out. Geometry is in metres, Y up
(`<scene up="z">` scenes are turned upright), triangles with counter-clockwise
front faces, per-vertex normals and UVs. Colours are the authored RGB values.

## Bundled exporters

| id | extension | output | options |
|---|---|---|---|
| `obj` | `.obj` | Wavefront OBJ plus a `.mtl` beside it, one `o` group per authored node | `up=y,z` (default `y`), `merge=0\|1` one group for the scene |
| `glb` | `.glb` | glTF 2.0 binary, one node per object, PBR base colour (sRGB converted to linear), roughness derived from shininess, V flipped to glTF's origin | `merge=0\|1` all objects as primitives of one mesh |

`--opt KEY=VALUE` may be repeated. An undeclared option or a value outside the
declared choices is rejected with exit status 1.

## Discovery

At startup of `--export` and `--list-exporters` Scener scans, in order:

1. each directory in `SCENER_EXPORTER_PATH` (`:` separated, `;` on Windows),
2. `<library directory>/exporters`,
3. `<settings directory>/exporters`.

Every `.dylib`, `.so` or `.dll` found is opened and asked for
`scener_get_exporter`. A file that cannot be opened, lacks the symbol, was built
for another ABI version or repeats an id already registered is reported on
stderr and skipped; it never prevents the others from loading.

## Writing an exporter

Include [`exporter.h`](../exporter.h) only. It has no dependency on Scener or
Orion. Fill a `scener_exporter_t` and export one function:

```c
#include "exporter.h"

static const char *const extensions[] = { ".ply", NULL };
static const scener_option_t options[] = { { "binary", "Binary file", NULL, "1" } };

static bool export_scene(const char *path, const scener_export_scene_t *scene,
                         const scener_export_options_t *opts) {
  bool binary = !strcmp(scener_option(opts, "binary", "1"), "1");
  /* write path from scene->objects[i].positions / normals / uvs / indices */
  return true;                       /* false: Scener reports the failure, exit 1 */
}

static const scener_exporter_t exporter = {
  SCENER_EXPORTER_ABI, "ply", "Stanford PLY", "Triangles with normals",
  extensions, options, 1, export_scene,
};
SCENER_EXPORTER_API const scener_exporter_t *scener_get_exporter(void) { return &exporter; }
```

Put it in `apps/scener/exporters/<id>/export_<id>.c`; the Makefile builds every
such file into `build/exporters/export_<id>.<ext>`. The scene arrays are
read-only and valid only during `export_scene`; report problems through
`scene->log(message)`. A change to `scener_export_scene_t` or
`scener_exporter_t` that is not backward compatible raises
`SCENER_EXPORTER_ABI`.

Options are declarative (`choices` is a comma-separated list, or `NULL` for an
on/off flag), so an editor Export dialog can be generated from them.

## Tests

```sh
make scener
DYLD_LIBRARY_PATH="$PWD/build/lib" python3 apps/scener/tests/test_export.py build/bin/scener build/exporters
```

The test exports the sample room through both plugins and checks index ranges,
bounds against `--query`, the `.mtl`, the glB container, option handling,
failure messages, and that a directory of broken libraries does not hide the
good exporters.
