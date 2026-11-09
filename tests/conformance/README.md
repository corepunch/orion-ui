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
`id`, `class` the class. Run it after adding or changing a fixture:

```
python3 tools/conformance/render.py tests/conformance/boxes/shadow.orion
```

It needs Chrome: `$ORION_CHROME`, `google-chrome`/`chromium` on the path, or
`npx @puppeteer/browsers install chrome-headless-shell@stable --path ~/.cache/orion-conformance`
(about 200 MB, outside the checkout).

A fixture passes when no channel differs by more than 48 and at most 0.4% of
pixels by more than 12. A failure leaves `build/conformance/<suite>-<name>.orion.png`
and `.diff.png` (red: off by more than 48). Divergences Orion has not closed
are listed in `k_known` with their reason; those fixtures must still agree in
geometry, and the test fails once one matches outright.
