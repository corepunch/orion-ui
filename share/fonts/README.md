# Orion fonts

Orion's proportional UI text uses separate roles chosen for readability at
their target sizes:

- `FONT_SYSTEM`: `NotoSans-Medium.ttf` at 12 px for window chrome and controls
- `FONT_SMALL`: `NotoSans-Regular.ttf` at 12 px for content
- `FONT_SMALLEST`: `NotoSans-Regular.ttf` at 9 px for compact labels

Sizes are em sizes in logical UI pixels, not the font's full ascent-to-descent
height. Retina increases atlas resolution without changing logical text size
or advances. Line heights include the typeface's ascent, descent, and line gap.

Noto Sans is distributed under the SIL Open Font License 1.1 in
`NotoSans-OFL.txt`. Its Latin, Greek, and Cyrillic coverage is suitable for the
framework's general UI text. Glyphs are rasterized from the TTF files into empty
runtime atlases on first use; generated bitmap font sheets are not source
assets.

`monoid.ttf` remains the fixed-width face for the terminal/VGA text renderer.

Orion requests high-DPI surfaces by default, matching SDL's opt-in window flag
model. Build with `make -B ALLOW_HIGHDPI=0` to omit that request and exercise a
1x surface on Retina displays. Use `make -B` to restore the default afterward.
