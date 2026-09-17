# Gap-fill regression tests

Run on the host; no iPad or display is required:

```sh
make build/bin/test_fill_gap_test build/bin/test_penciltest_fill_test
build/bin/test_fill_gap_test
build/bin/test_penciltest_fill_test
```

`fill_gap_test.c` exercises the production raster implementation:

- Endpoint/corner detection, gap disabled, openings beyond the selected limit,
  sliver merging, and preservation of larger pockets.
- Whole-image comparisons for boxes in four orientations, three stroke widths,
  and all three gap settings (36 combinations).
- Closed diamonds and openings on each diagonal edge.
- Circles with 1–2 degree openings at eight angles and two resolutions (32 cases).
  Expected pixels come from filling the closed circle with ordinary flood fill.
  Each open circle must leak with gap closing disabled and remain contained with
  it enabled. Rasterization affects the exact width of an angular opening.
- A drawing with more than 4096 endpoints, with the target gap after the old
  candidate-buffer limit.

`penciltest_fill_test.c` compiles with the actual black-and-white indexed-pixel
and Retina settings. It draws with the production pen, invokes the fill tool,
and checks every pixel outside the bridge area plus byte-exact undo/redo at
1×–4× for Small, Medium, and Large. The openings grow with the chosen setting,
so forgetting the Retina multiplier causes leaks. The 4× Large case also
checks that the maximum gap limit scales beyond 32 backing pixels.

Gap settings are logical pixels; the low-level fill takes backing pixels.
Reached stitches receive the fill color without letting traversal cross into
another region. Tests require the thin box and diagonal bridges to be painted;
only the ambiguous boundary area of thick/curved gaps allows paper or fill.

## Screenshot regression

`fixtures/fill_outline_0042.pbm` is derived from the user's IMG_0042.jpeg:
2160×1620 original, crop `(670, 490, 1250, 1040)`, grayscale threshold `<128`
for ink, with no resizing or dithering. The PBM contains only black and white.
JPEG sampling means this is a reproduction fixture, not the original canvas.

The thresholded outline is already closed. Its whole-image reference is an
ordinary four-connected fill from `(270, 270)`. Before the fix, gap sizes
2/4/10/20 left 192/255/310/319 pixels unfilled; size 20 also painted 11 extra
pixels. All four comparisons now have zero missing and zero extra pixels.
The test additionally cuts a three-row break in the same irregular outline:
ordinary fill must leak, while gap fill must preserve the reference interior
and exterior (only erased original ink is excluded from that comparison).

## Implementation comparison

The [Gangnet/Van Thong specification](https://patents.google.com/patent/US5694536A/en)
explicitly describes labeling connected components inside the local stamp,
then connecting distinct components. Our previous nearest-ink search omitted
that condition and stitched raster steps of a stroke back onto itself. We now
exclude the locally connected component, including previous stitches. This is
local connectivity: endpoints may belong to a single contour outside the stamp.

[Krita's gap map](https://github.com/KDE/krita/blob/master/libs/image/floodfill/kis_gap_map.cpp)
credits MyPaint and uses a different, distance-map approach.
Its [closeGapPass](https://github.com/KDE/krita/blob/master/libs/image/floodfill/kis_scanline_fill.cpp)
controls propagation separately from painting accepted gap pixels. Our raster
stitch approach likewise paints reached bridges without resuming flood traversal
across them. Sliver cleanup is restricted to bridges reached by this fill;
unrelated regions elsewhere in the drawing must stay unchanged. The source
comparison informed the corrections; no upstream code was copied.
