# Penciltest video converter

`penciltest-video` converts MP4 and other FFmpeg-readable video into editable
Penciltest `.ptf` projects, PNG frame sequences, or both. It is a C99 command-line
tool for macOS and Linux. Build dependencies are a C compiler, `pkg-config` and
`libpng`; conversion requires FFmpeg on `PATH`, in `$FFMPEG`, or supplied with
`--ffmpeg PATH`. Black-ink cleanup additionally uses Python 3, NumPy and OpenCV.
FFmpeg and the Python filter are launched directly without a shell.

```sh
make penciltest-video
build/bin/penciltest-video --help
python3 -m venv build/lineart-python
build/lineart-python/bin/python3 -m pip install -r tools/lineart_requirements.txt
```

## Conversion and palette choices

| Mode | Native layer | Pixel transfer | PNG output |
|---|---|---|---|
| `--mode pencil` (default) | Pencil | All 256 luminance levels stored reversibly as `255 - luminance` coverage | Lossless 8-bit grayscale |
| `--mode black` | Color | Stroke-aware simplification; one exact RGB black entry and transparency, with no antialiasing | 1-bit indexed black/transparency |
| `--mode palette` | Color | RGB video quantized into the chosen document palette, without dithering | Indexed colors |

Choose the document palette independently of the conversion mode:

| Option | Behavior |
|---|---|
| `--palette default` (default) | Load the same 32-color RIFF palette used by Penciltest. |
| `--palette FILE.pal` | Use the ordered RGB entries in an existing binary RIFF PAL file. |
| `--palette generated --colors 32` | Derive one adaptive palette from the entire video. |

The shared resource is `apps/imageeditor/share/palettes/default.pal`, packaged
beside the binaries in `build/share/imageeditor/palettes/`. It contains 16 dark/light
pairs; the original eight pairs are unchanged. No palette RGB table is duplicated
in the app or converter. Every project embeds its editing palette, so later
changes to the default resource cannot recolor an existing project.

RIFF PAL files contain RGB and Windows flags, not alpha. Existing palettes may
have 1–255 entries; native index 255 remains reserved for transparency. Unused
native entries are empty, not extra grayscale colors. Malformed or oversized
palette files are rejected. Black mode requires an exact black entry in an
existing palette; the default includes one. Generated black-mode palettes reserve
black within the requested budget.

Generated budgets are 32 (default), 64, 128 or 256; 256 means 255 opaque colors
plus transparency. `--colors` without `--palette` selects generated palettes for
compatibility with earlier commands; combining it with default/existing palettes
is rejected. Generation uses a 5-bit RGB histogram, weighted median-cut boxes and
nearest palette colors for occupied histogram bins. Color quantization is lossy;
one palette for the whole video prevents frame-to-frame palette flicker.

Pencil coverage and grayscale PNG samples are independent of the editing palette.
Luminance is `(77*R + 150*G + 29*B + 128) >> 8`; neutral RGB grays remain exact.
Penciltest displays that coverage using its configured purple ink and paper style.
The compatibility FLC playback stream retains a grayscale palette for pencil
mode; that playback palette never replaces the embedded editing palette.

## Stroke-aware ink cleanup

Black mode uses the selected stroke-aware algorithm with defaults
`--threshold 170 --min-width 3.5 --min-length 20`. The converter keeps video
handling, timing, palette transfer, PNG packing and native PTF writing in C99.
It runs the packaged `simplify_lineart.py` filter once per clip, feeding decoded
RGB through temporary files. The filter uses OpenCV's Euclidean distance
transform, Zhang–Suen skeletonization and branch-width selection to remove thin
terminal and isolated strokes while retaining loops and connections between
contours. Surviving source pixels are assigned to their closest skeleton sample.
No hatch filling, weak-connection pruning, SDF or morphological-opening mode is
included. This is the restored pre-fill variant.

`make penciltest-video` packages both Python files beside the executable. The
converter finds `build/lineart-python/bin/python3` when installed as above,
otherwise uses `python3`. Override this with `--python PATH` or
`PENCILTEST_PYTHON`; `--stroke-filter PATH` overrides the script location.
Missing dependencies, worker errors and incomplete frames abort conversion
before any outputs are published. Black mode supports at most 16 million pixels
per frame in the OpenCV filter; other modes retain their previous limits.

`--protect-region X,Y,W,H` preserves the original thresholded ink in an important
region and can be repeated. Aggressive cleanup cannot distinguish a nose from a
scratch, so the approved skateboard example protects the face with
`--protect-region 525,105,165,140`. No face coordinates are built into the
algorithm. Regions follow estimated Farnebäck motion by default; use
`--fixed-protection` for stationary regions. Motion tracking is a geometric
estimate, not automatic face recognition.

`--temporal` optionally uses motion-aligned previous selection and width
hysteresis to reduce borderline stroke changes. It is experimental and disabled
by default. The selected reference frame and restored export use ordinary
per-frame selection. Soft pencil coverage and full-color palette quantization
retain their existing behavior.

Black PNGs have two 1-bit indices (opaque black and transparent white). Native
Color cels retain the editable 8-bit document palette, using only its black index
and transparent index 255. Cleanup does not repair source contour gaps. Existing
construction lines can remain when they join contours; that conservative behavior
is retained in the selected pre-fill version.

The same filter can export standalone 1-bit black/white PNGs on white paper:

```sh
build/lineart-python/bin/python3 tools/simplify_lineart.py input.png output.png \
  --threshold 170 --min-width 3.5 --min-length 20 --protect-region 525,105,165,140
```

## Examples

Preserve the source dimensions and timing:

```sh
build/bin/penciltest-video --mode pencil --palette default \
  --output build/pencil.ptf --frames build/pencil-frames input.mp4

build/bin/penciltest-video --mode black --palette default --threshold 170 --min-width 3.5 --min-length 20 \
  --output build/black.ptf --frames build/black-frames input.mp4

build/bin/penciltest-video --mode palette --palette generated --colors 32 \
  --output build/palette-32.ptf --frames build/palette-32-frames input.mp4

build/bin/penciltest-video --mode palette --palette default \
  --output build/palette-default.ptf input.mp4

build/bin/penciltest-video --mode palette \
  --palette apps/imageeditor/share/palettes/default.pal \
  --output build/palette-existing.ptf input.mp4
```

For a smaller project, explicitly add `--size 584x392 --fps 12`. Resizing uses
Lanczos filtering at exactly the requested dimensions; preserve the aspect ratio
when choosing them. `--max-frames N` limits conversion. Omit `--output` to export
only PNGs. Outputs must not already exist; parent directories must exist. PNGs
are named `frame-000001.png`, etc., with source timing in `timing.tsv`.

## Timing and storage

Source dimensions, display rotation and timing are preserved by default. The
first moving video stream is selected, excluding attached covers. Audio,
subtitles and additional video streams are omitted. Native frame delays use
millisecond precision with cumulative rounding, including fractional and variable
rates. FFmpeg's `showinfo` supplies timestamps without requiring `ffprobe`.

Native projects use Penciltest's FLC container with `PTA1` editing metadata and
`PTL5` layer chunks. Editable Color/Pencil/FX cels use independent lossless scanline
RLE, with raw COPY when smaller. Background is shared. Flattened playback frames
may use temporal differences. The app retains compressed frames, four working
byte planes and visible onion textures; there is no timeline thumbnail pipeline.

Native limits are 512 MiB encoded cels, 65535 frames, 16384 pixels per dimension,
1–65535 ms per frame, and 512 MiB for three decoded working cels. PNG-only export
bypasses native frame-count and encoded-layer limits. Decoded RGB is spooled to
temporary disk files, so memory does not grow with the movie. Allow disk space for
that spool, compressed layers and outputs. Outputs are published only after a
successful conversion; existing files are never replaced by the tool.

## Regenerated video examples

`build/penciltest-video-example/` contains full-resolution conversions of
`/Users/igor/Downloads/generated_video-2.mp4`: pencil, black, generated 32-color,
fixed default-palette color, and fixed existing-palette color. Each project has
145 frames at 1168×784, 24 fps, and 6042 ms total duration. Every project includes
its PNG sequence and timing, plus a ZIP of the PTF. These generated assets remain
untracked. The original examples are preserved in the sibling backup directory.

`build/penciltest-video-ink-example/` contains the re-export with binary ink
opening (previous example): `black.ptf`, `black.ptf.zip`, and `black-frames/` with 1-bit PNGs and
unchanged timing. It uses the same source at 1168×784, 145 frames, and 6042 ms.

`build/penciltest-video-stroke-example/` contains the restored stroke-aware
export, with the same 145 source frames and timing. Its first frame matches the
approved `build/lineart-comparison/strong-face-protected.png` pixel for pixel.
The original comparison assets and all generated exports remain untracked.

## Validation

```sh
make build/bin/test_palette_file_test build/bin/test_penciltest_video_test
build/bin/test_palette_file_test
PENCILTEST_VIDEO_EXAMPLES=build/penciltest-video-example \
  build/bin/test_penciltest_video_test
PENCILTEST_VIDEO_INK_EXAMPLE=build/penciltest-video-stroke-example \
  build/bin/test_penciltest_video_test

make build/bin/test_penciltest_layout_test build/bin/test_penciltest_memory_test
build/bin/test_penciltest_layout_test
PENCILTEST_MEMORY_EXAMPLE=build/penciltest-video-example/pencil.ptf \
  build/bin/test_penciltest_memory_test
```

Tests cover all grayscale levels, threshold boundaries, worker failures and
truncation, true 1-bit PNG round trips, palette sources and
budgets, corrupt RIFF files, fractional/variable timing, decoder failures, quoted
paths, native/PNG round trips, and existing-output protection. Full-example
verification decodes every cel and compares it with its PNG. Sidebar tests verify
all 32 default swatches fit, larger document palettes page, duplicate colors
retain their selected index, and selection never mutates palettes or history.

Stroke selection, protected features, contour connectivity, binary masks and the
RGB worker protocol are covered by:

```sh
build/lineart-python/bin/python3 tools/test_lineart_strokes.py
```
