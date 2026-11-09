# CSS roadmap

The goal: an agent writes an Orion interface the way it writes a web page,
from forms and a style sheet, and gets what it pictured. Behaviour lives in Lua
(as in Ledger and Lodge), with native plug-ins for the heavy work (git, the
database). C draws: rectangles, rounded rectangles, gradients, shadows, glyphs,
images and text. Everything a style sheet can say, it says.

Where we are: every theme is a CSS sheet over `base.css` (Orion's user-agent
sheet); one box painter draws every part; colours, radii, chrome geometry and
text faces come from CSS; controls no longer paint shapes of their own
([styling.md](styling.md)).

## 1. No themes, only style sheets

- Done: every metric is a computed-style query (`ui_px("Scrollbar", "width")`)
  and `theme_t` holds no numbers or callbacks, only its sheet.
- "Switching theme" becomes loading a sheet: `ui_set_stylesheet("porcelain")`.
- Done: an app links its own sheet from the `.orion` root (`gitclient.css`),
  read after the theme's; a gem's sheet styles only its own windows.
- Done: a control's `name` is its `#id` and `class=` its classes
  ([styling.md](styling.md#application-sheets)), and every control reads
  them through `ui_selector()` / `theme_draw_in()`.

## 2. Text from CSS, not roles

Done: a font is a selector, and each control draws with its element's `font`
and text properties ([styling.md](styling.md#type)):
`TableView::header { font: 500 11px var(--ui-face); text-transform: uppercase;
letter-spacing: .05em }`.

- Done: a font cache keyed by face and size, created on first use.
- Done: `text-transform`, `letter-spacing`, `text-align`, `line-height`.
- Still to come: `text-overflow: ellipsis` as a property (controls ellipsize in
  C today) and `font-variant-numeric: tabular-nums`, which needs OpenType
  features the FreeType-only path does not apply.
- Done: the VGA bitmap font is gone; the terminal and the diff view draw IBM
  Plex Mono as a grid of cells (`draw_text_cells`).

## 3. The shapes still in C

- Cards: the accent edge becomes `border-left` sharing the card's rounded
  silhouette (the painter learns per-side widths on a rounded box).
- Badges: the pill becomes a `Badge` box whose tone is a per-instance variable
  (`background: color-mix(in srgb, var(--tone) 15%, transparent)`), so
  `theme_draw` gains instance variables.
- Tooltips and speech balloons stay in C: the tail is not a box.

## 4. Same CSS, same picture: conformance with the browser

A fixture is an HTML page and the `.orion` form of the same thing, sharing one
style sheet. CI renders the page in headless Chrome and the form in Orion, and
compares the two images with a tolerance (text rasterises differently; edges and
fills must match). Start with boxes (background, border, radius, shadow,
margin), then text placement, then layout. Every supported property gets a
fixture; an unsupported one is a diagnostic, never a silent difference. This is
what lets an agent trust its browser intuition.

Started: `tests/conformance/boxes/` (background, border, radius, shadow,
square) with `tools/conformance/render.py` generating each page from its form
and rendering it in headless Chrome; `tests/css_conformance_test.c` compares
([tests/conformance/README.md](../tests/conformance/README.md)). The first
fixtures fixed sized empty containers, `stretch` on definite sizes, CSS shadow
blur, shadows clipped by their own frame, three-pixel edge antialiasing and
unmitred borders. One divergence remains, listed in the test: Orion composites
in linear light, CSS in sRGB, so translucent colours, gradients and
antialiased edges come out lighter.

## 5. Layout in CSS terms

Orion already lays out like a simple web page: stacks, grids, flow. Name it in
CSS so the knowledge transfers:

| Orion | CSS |
|---|---|
| `StackView orientation="vertical"` | `display: flex; flex-direction: column` |
| `spacing`, `padding`, `flags="flexspace"` | `gap`, `padding`, `flex: 1` |
| `GridView` with `<Column width="auto">` | `display: grid; grid-template-columns: auto 1fr` |
| `FlowView` | `display: flex; flex-wrap: wrap` |
| `h-align`, `v-align` | `justify-content`, `align-items` |

Layout properties in the sheet (`StackView#summary { gap: 6px; padding: 10px
14px }`) let a designer move spacing out of `.orion` attributes. Measuring stays
in C.

## 6. Where Orion should not copy the web

The browser is weak at application chrome; Orion keeps its own, styled by CSS:

- Panes, splitters and docking (master/detail) are windows, not `div`s with
  `resize: horizontal`.
- Tables are virtualised, keyboard-first and bound to datasources, not DOM
  rows.
- Menus, accelerators, the command palette and focus are framework services.
- Hinted text on the device pixel grid instead of fractional antialiasing.
- No floats, absolute positioning, `z-index` or margin collapsing.

## 7. Next steps, in order

Done: style queries replaced `theme_t`'s metrics and vtable; fonts are
selectors with text properties; the VGA font gave way to IBM Plex Mono; apps
link their own sheets and every control is styled by its `#id` and `.class`.

1. Browser conformance fixtures: boxes started; decide sRGB compositing; then text and layout.
2. Layout properties in the sheet.
3. Git Client's Run page (rail, process table, console, ⌘K) built from these,
   with its behaviour in Lua.
