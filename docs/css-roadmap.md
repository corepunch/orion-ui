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

A theme today is a CSS sheet plus a `theme_t` struct that caches what the sheet
says. The struct goes:

- Replace the 206 `get_theme()->…` reads with cached style queries at the use
  site (`ui_style_px("Scrollbar", NULL, "width")`), then delete the fields one
  group at a time, comparing screenshots after each.
- Replace the `theme_t` vtable (`draw_part`, `foreground`, `draw_card`, …) with
  direct calls to the painter; the window chrome and labels become parts.
- "Switching theme" becomes loading a sheet: `ui_set_stylesheet("porcelain")`.
  The built-in sheets stay embedded; an app may add its own (`gitclient.css`),
  read after the theme's, as a page's sheet follows the user-agent sheet.
- `class=` and `id` on `.orion` elements reach the selectors, so an app styles
  one table without a new part.

## 2. Text from CSS, not roles

`FONT_SYSTEM`, `FONT_SMALL`, `FONT_SMALLEST`, `FONT_TITLE`, `FONT_DISPLAY` and
`FONT_HEADING` (188 call sites) are fixed roles from the bitmap-font era. Each
control should ask for the font of its own element and part:
`TableView::header { font: 600 11px "IBM Plex Sans"; text-transform: uppercase;
letter-spacing: .05em }`.

- A font cache keyed by face and size, created on first use, replaces the six
  role atlases.
- Text properties: `font`, `color`, `text-transform`, `letter-spacing`,
  `text-align`, `line-height`, `text-overflow: ellipsis`, `font-variant-numeric:
  tabular-nums` (figures in tables).
- The VGA bitmap font (`vga_font.c`, `vga_text.c`) goes: the terminal and the
  diff view draw a monospace TTF (IBM Plex Mono) on the same grid.

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

1. Style queries replace `theme_t` metrics; the vtable becomes direct calls.
2. Font cache by face; `font` and text properties on elements; roles removed.
3. VGA font replaced by IBM Plex Mono in the terminal and diff view.
4. App sheets and `class=` from `.orion`.
5. Browser conformance fixtures for boxes, then text, then layout.
6. Layout properties in the sheet.
7. Git Client's Run page (rail, process table, console, ⌘K) built from these,
   with its behaviour in Lua.
