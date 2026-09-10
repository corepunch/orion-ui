# Scener CLI and local deployment

Build from the Orion UI root (`~/Developer/mapview/ui`), two levels above this
app directory. The earlier standalone `simplegl`/`screenshot` Makefiles are not
the active build.

```sh
make build/bin/scener build/bin/test_scener_input_test
DYLD_LIBRARY_PATH="$PWD/build/lib" ./build/bin/test_scener_input_test
python3 apps/scener/deploy.py --prefix "$HOME/.local"
python3 apps/scener/tests/test_cli.py "$HOME/.local/bin/scener"
python3 apps/scener/tests/test_shadow_backend.py "$HOME/.local/bin/scener"
```

The macOS deployment helper copies the executable, all non-system dylib
dependencies, and Orion/Scener resources into a versioned directory under
`PREFIX/lib/scener/`. `PREFIX/bin/scener` is a launcher that sets that bundle's
library search path; callers need no environment setup or special working
directory. Old bundles remain available. `BUILD.txt` records the source revision
and local changes. The upstream `make install PREFIX=...` target installs the complete Orion suite;
this helper provides a separate, versioned Scener bundle.

```sh
scener --help
scener --version
scener --list-cameras /absolute/path/room.blks
scener --render /absolute/path/room.blks --size 1536x1024 --format jpg --output-dir render/room
scener --render /absolute/path/room.blks --camera room-main --size 1536x1024 --format png --output-dir render/room
scener --layout /absolute/path/room.blks --scale 2 --format jpg --output-dir render/room
scener /absolute/path/room.blks --cam room-main --screenshot /tmp/preview.png
```

Batch outputs use each camera name. `--layout` writes `layout.jpg` or
`layout.png`: a flat-color orthographic cutaway plan (without lighting or shadows) clipped at 85% of the scene's vertical
bounds, with image dimensions derived from `--scale` pixels per centimeter.
This is diagnostic art, not a runtime camera. Story render dimensions are exact.
The output directory is created as needed. Invalid flags, camera names, sizes,
formats and conflicting modes return a nonzero exit status.

Both output formats use real encoding. Perspective and layout renders default
to 2x supersampling on each axis, followed by linear-light downsampling.
`--supersample 1`, `2`, `3` or `4` controls this; intermediate dimensions are
limited to 8192 on either axis. Shadows are enabled by default. `-no-shadows`
disables them, `-wireframe` draws unlit white geometry, and `-d FLAGS` retains
the editor debug-bit interface. Helper overlays are hidden by default.

A logged-in graphical session and OpenGL context are needed for rendering;
help, version and camera listing need no graphical startup.

Scenes default to Y up. `<scene up="z">` changes camera views, interactive
navigation and plan orientation to Z up without changing primitive local axes
or authored transforms. Cylinders and walls keep local Y height; rotate
`rot="90 0 0"` to align that with world Z. Prefabs resolve relative to a scene's
own directory for arbitrary scene paths; the existing `scenes/` / `prefabs/`
asset-root convention remains supported.

Scenes authored for main’s 3ds Max coordinate conversion can declare
`<scene convention="3dsmax">`. This maps authored position/rotation `(x,y,z)`
to renderer `(x,z,-y)` and box sizes to `(x,z,y)`, retaining the upstream
conversion. It takes precedence over `up`; omit it for native Y/Z-up scenes.

## GPU access and shadow validation

Every graphical run reports OpenGL vendor, renderer and version. Shadow exports
reject `Apple Software Renderer` with a nonzero exit status before creating
output images: that backend reproducibly rasterizes invalid stencil shadows.
On the tested Mac, restricted execution selects `Apple Software Renderer`
(`4.1 APPLE-23.1.1`), while execution with GPU access selects `Apple M1`
(`4.1 Metal - 90.5`) and renders correctly. Run shadow exports with GPU access;
for an agent tool, that means requesting its approved unsandboxed execution.
Normal desktop invocation uses the available hardware renderer.

`-no-shadows`, `-wireframe`, and flat diagnostic layouts remain available on the
software backend. They are not substitutes for final shadowed backgrounds.
The hardware stencil-shadow algorithm and shader position math are unchanged.
Shader compile/link failures are reported explicitly.

`test_shadow_backend.py` uses four boxes, two lights and nine camera variations.
On the GPU it checks lit/shadow pixel regions and a uniformly lit floor patch;
on Apple Software Renderer it verifies rejection before image creation and
successful diagnostic exports. Run it in both execution environments when
changing backend handling. `test_cli.py` also checks encodings, dimensions,
supersampled framebuffer coverage, mode errors and output naming.
