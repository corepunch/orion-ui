# CSS conformance fixtures

Same CSS, same picture: each fixture is a form and its sheet, painted by Orion
and by headless Chrome, and `tests/css_conformance_test.c` (in `make test`)
compares the two.

```
<suite>/<name>.orion   the form: one <form name="fixture"> with a width and height
<suite>/<name>.css     its sheet, shared by both
<suite>/<name>.html    the same form as a page (generated; do not edit)
<suite>/<name>.png     Chrome's rendering (generated, checked in)
```

`tools/conformance/render.py` writes the `.html` and `.png`: forms and stacks
become flexboxes, `width`/`height` on an empty container its size, `name` the
`id`, `class` the class. `spacing`, `padding` and `orientation` become inline
styles only where the form declares them; the sheet decides the rest, over a rule
standing for `base.css`'s container defaults. A GridView with `<Column>`s becomes
a CSS grid (`width="auto"` columns `auto`, the others `1fr`). Run it after adding or changing a fixture:

```
python3 tools/conformance/render.py tests/conformance/boxes/shadow.orion
```

It needs Chrome: `$ORION_CHROME`, `google-chrome`/`chromium` on the path, or
`npx @puppeteer/browsers install chrome-headless-shell@stable --path ~/.cache/orion-conformance`
(about 200 MB, outside the checkout).

Chrome is given the faces in `share/fonts` through `@font-face` rules that
`render.py` writes into each page (Orion's sheet parser skips `@font-face`, so
fixture sheets never carry them). A `Label` is inline-level, so a flex item
blockified to one box of line boxes; a `width` makes it wrap.

A `boxes` fixture passes when no channel differs by more than 48 and at most 0.4% of
pixels by more than 12. A failure leaves `build/conformance/<suite>-<name>.orion.png`
and `.diff.png` (red: off by more than 48). Divergences Orion has not closed
are listed in `k_known` with their reason; those fixtures must still agree in
geometry, and the test fails once one matches outright.

The `text` suite cannot compare pixels: Chrome hints glyphs and places them on
fractions of a pixel, Orion on whole pixels, and light text on dark is heavier in
Chrome (up to 17% more ink here). It compares where the ink is. Per image, a
line of ink is a run of rows holding pixels that differ from the row's most
common colour by 96 or more; Orion and Chrome must find the same number of lines,
each with a bounding box within 1 pixel on every side, and the total distance
from the background must agree to within 20%. A failure names the line that
moved. That is what text placement means here: the same line box, half-leading,
baseline, width and alignment. Fractional line heights are not fixtures: Chrome
lays them out in 1/64 pixels and Orion in whole ones.
