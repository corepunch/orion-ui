# Penciltest video converter

`penciltest-video` converts MP4 and other FFmpeg-readable video into editable
Penciltest `.ptf` projects, PNG frame sequences, or both. It is a C99 command-line
tool for macOS and Linux. Build-time dependencies are a C compiler, `pkg-config`
and `libpng`; conversion requires FFmpeg on `PATH`, in `$FFMPEG`, or supplied
with `--ffmpeg PATH`. The converter launches FFmpeg directly without a shell.

```sh
make penciltest-video
build/bin/penciltest-video --help
```

## Conversion modes

| Mode | Native layer | Pixel transfer | PNG output |
|---|---|---|---|
| `--mode pencil` (default) | Pencil | 8-bit luminance stored reversibly as `255 - luminance` coverage | Lossless 8-bit single-channel grayscale |
| `--mode black` | Color | Luminance below `--threshold` becomes opaque RGB black; other pixels become transparent | 8-bit indexed black and transparency |
| `--mode palette --colors N` | Color | One adaptive palette for the whole video, without dithering | 8-bit indexed color with the same palette |

Supported palette budgets are **32, 64, 128, 256**. For 32/64/128, the budget is
an upper bound on opaque colors. At 256, Penciltest's reserved transparent index
255 leaves **255 opaque colors plus transparency**. All other unused entries
are transparent. Palette mode uses a 5-bit RGB histogram, weighted median-cut
boxes, and nearest palette colors for histogram bins. A shared palette prevents
palette changes between frames; palette conversion is lossy.

Pencil mode preserves every decoded luminance sample, including antialiased
edges and gray shading. Luminance is `(77*R + 150*G + 29*B + 128) >> 8`; neutral
RGB grays remain exact. This does not recover detail already lost by MP4
compression, and converting a color source to grayscale discards chroma.
Penciltest displays Pencil coverage with its configured purple ink and paper
style, including its normal compositing behavior. The saved coverage bytes and
grayscale PNGs are lossless; the app's styled display is not a visually exact
black-and-white copy. Use black or palette mode for RGB black on the Color layer.

PNG supports one-channel 8-bit grayscale and lossless compression; JPEG would
introduce another lossy encoding. The generated grayscale PNGs are readable by
macOS's image subsystem and Preview. See the [PNG specification](https://www.w3.org/TR/2003/REC-PNG-20031110/).

## Examples

Use the supplied video at its original dimensions and timing:

```sh
build/bin/penciltest-video --mode pencil \
  --output build/pencil.ptf --frames build/pencil-frames \
  /Users/igor/Downloads/generated_video-2.mp4

build/bin/penciltest-video --mode black --threshold 128 \
  --output build/black.ptf --frames build/black-frames \
  /Users/igor/Downloads/generated_video-2.mp4

build/bin/penciltest-video --mode palette --colors 64 \
  --output build/palette-64.ptf --frames build/palette-64-frames \
  /Users/igor/Downloads/generated_video-2.mp4
```

Specify `--ffmpeg /path/to/ffmpeg` before the input when FFmpeg is not on `PATH`.
Outputs must not already exist. Parent directories must exist. PNG files are
named `frame-000001.png`, `frame-000002.png`, etc. Their directory also contains
`timing.tsv` with source timestamps and native frame delays.

For a smaller editable project, explicitly choose dimensions and/or frame rate:

```sh
build/bin/penciltest-video --mode palette --colors 32 \
  --size 584x392 --fps 12 --output build/small.ptf input.mp4
```

`--size` resizes to exactly the requested dimensions using Lanczos filtering;
choose dimensions with the source aspect ratio to avoid stretching. `--fps`
resamples the timeline. `--max-frames N` limits the number of output frames.
Omit `--output` to export only PNGs:

```sh
build/bin/penciltest-video --mode pencil --frames build/gray-frames input.mp4
```

## Timing and storage

Source frames, dimensions and presentation timing are preserved by default.
FFmpeg applies the input's display rotation. The first moving video stream is
selected, excluding attached cover pictures. Audio, subtitles and additional
video streams are omitted. Frame delays use native millisecond precision with
cumulative rounding, so fractional rates do not accumulate rounding drift.
The native timeline's default integer FPS is the nearest source rate; each
frame retains its own delay, including variable frame durations. FFmpeg's
[showinfo filter](https://ffmpeg.org/ffmpeg-filters.html#showinfo) supplies the
source timestamps without requiring `ffprobe`.

The output follows the FLC container and Penciltest `PTA1` / `PTL5`
editing trailers implemented in `apps/imageeditor/io/flc.c`. It includes a ring
frame, the palette, frame names, delays, and separate Background, Color, Pencil
and FX planes. The imported layer is active; the other animation layers are
empty. Paper is white. Both playback frames and editable planes use lossless
FLI-style scanline RLE and frame differences, with raw COPY only when smaller.
Unchanged layers need only a tiny record. The app and converter share the codec
in `apps/imageeditor/io/flc_codec.h`. Old raw `PTL2`/`PTL3`/`PTL4` projects are
not supported by the new app; ordinary external FLC/FLI imports still work.

Native projects must fit Penciltest's 512 MiB decoded-pixel limit, 65535 frames,
16384 pixels per dimension, and 1–65535 ms per frame. The converter rejects
oversized native projects; it never silently resizes or drops frames. PNG-only
export bypasses the native frame-count and aggregate pixel limits. Decoded RGB
frames are spooled to temporary disk files, so memory usage does not grow with
the full decoded movie. Allow disk space for the RGB spool, compressed layer spool and
outputs. Completed outputs are published only after conversion succeeds;
existing projects are never replaced.

For `generated_video-2.mp4`, all three full-resolution conversions produced
**145 frames at 1168×784**, with **6042 ms** total native playback time. The
original rate is 24 fps. Native project sizes after RLE are **91.69 MiB** for
Pencil, **17.61 MiB** for solid black, and **55.00 MiB** for 64-color, down from
508.52 MiB each in the raw format. All original dimensions, frames, delays and
converted pixel bytes are preserved.
The PNG sequences occupy approximately 34 MiB for grayscale, 3.7 MiB for
thresholded black, and 17 MiB for 64-color, including filesystem allocation.
Generated examples are in `build/penciltest-video-example/` and are untracked.

ZIP with DEFLATE level 9 gives the following sizes for this same clip. Extracted
files were verified against their originals with SHA-256.

| Mode | Native RLE | RLE plus ZIP | Old raw format plus ZIP |
|---|---:|---:|---:|
| Pencil | 91.69 MiB | 68.38 MiB | 67.28 MiB |
| Solid black | 17.61 MiB | 7.15 MiB | 6.63 MiB |
| 64-color | 55.00 MiB | 35.28 MiB | 34.43 MiB |

ZIP provides another lossless reduction. In this clip, ZIP alone compresses the
old raw layout slightly better than RLE followed by ZIP. The new RLE files save
space while remaining directly editable by the app. ZIP archives must be
extracted before opening; ZIP support is not part of the native loader.

## Validation

```sh
make build/bin/test_penciltest_video_test
build/bin/test_penciltest_video_test
make build/bin/test_penciltest_rle_test
build/bin/test_penciltest_rle_test

# Also reopen the generated full example projects and compare every cel to its PNG:
PENCILTEST_VIDEO_EXAMPLES=build/penciltest-video-example \
  build/bin/test_penciltest_video_test
```

The tests exercise all 256 grayscale levels, threshold boundaries, all palette
budgets, fractional and variable timing, decoder failures, quoted file paths,
PNG pixel roundtrips, existing-file protection, and odd-size multi-frame native
projects reopened through Penciltest's actual loader. The separate
`test_penciltest_rle_test` suite checks FLIC packet semantics, long runs and skips,
wide rows, incompressible pixels, temporal deltas, malformed packets, native
compression size, and rejection of old layer versions.
