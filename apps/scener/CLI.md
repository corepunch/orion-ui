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
scener --render /absolute/path/walk.blks --camera Track --frames 0:7.5:24 --output-dir render/walk
scener --render /absolute/path/walk.blks --camera Side --time 2.2 --output-dir render/still
scener --list-joints /absolute/path/walk.blks --camera Side --time 2.2
```

`--time SECONDS` evaluates character gaits, layers and clips at that moment.
`--frames START:END:FPS` with `--render` writes one image per frame from START
to END inclusive, named `CAMERA_0000.jpg`, `CAMERA_0001.jpg` …; combine it with
`--camera` to render a single shot. `--list-joints` prints every posed rig
joint as `instance joint x y z` in world centimetres, for the given camera and
time; it needs no graphical session.

`tools/render_mocap_videos.py SCENER OUTPUT_DIR` renders one MP4 per fetched CMU
clip through `--frames` and `tools/frames_to_mp4.swift`; the Swift encoder also
turns any `--frames` sequence into a video:
`swift tools/frames_to_mp4.swift FRAME_DIR CAMERA 30 out.mp4`. In the editor, Animation → Play / Pause
(Space) plays the timeline, looping after `<scene duration>`, and Go to Start
(Shift+Space) rewinds.

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

## Animated infographic export

The IK/FK example combines the existing frame renderer and joint inspection
with a Python validation driver and Swift annotation compositor. From the
repository root on macOS, after building Scener:

```sh
python3 apps/scener/tools/render_ik_fk_infographic.py --help
python3 apps/scener/tools/render_ik_fk_infographic.py --preview
python3 apps/scener/tools/render_ik_fk_infographic.py
```

The default output is the ignored `video/ik-fk/` directory. A full run writes
`ik-vs-fk.mp4`, `ik-vs-fk.png`, sampled `measurements.json`, original frames,
annotated frames, and diagnostics. The six-second timeline includes its end
frame: 145 frames at 24 fps produce a 6.04-second, 1600×1000 MP4.

`--preview` samples 0 and 1.5 seconds and renders only the 1.5-second poster.
Use `--output video/ik-fk-preview` to keep preview metadata separate from a
completed full export. Existing files at the selected destination are replaced;
preview mode does not update an existing MP4 there. `--scener PATH` overrides
the default `build/bin/scener`; the script supplies `build/lib` as its library
search path. The Swift helpers require macOS command-line tools, AppKit, and
AVFoundation. Shadowed rendering requires GPU access.

This helper currently targets one scene, camera, character, and layout. It
does not add native CLI label or infographic flags. See the
[studio guide](docs/infographic-studio.md) for the pipeline, file contracts,
extension points, and current limitations, and the
[example notes](scenes/infographics/README.md) for its motion and scale.

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
