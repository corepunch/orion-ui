# Style sheets

Orion themes are moving from C to CSS. A theme's colours and metrics live in a
style sheet under `orion/themes/`; the build embeds it in `libuser`
(`tools/embed_text.py`), so a theme never depends on files at run time.
Porcelain and Porcelain Dark are the first: both are
[`orion/themes/porcelain.css`](../orion/themes/porcelain.css), read in the
light and the dark appearance.

The drawing itself is still C (`theme_modern.c`). The next steps move it into a
generic box painter driven by the same properties, then the other themes, and
then drop the per-theme C drawing.

## Editing a theme without rebuilding

```bash
ORION_THEME_CSS=orion/themes/porcelain.css build/bin/ledger
```

`ORION_THEME_CSS` names a file read instead of the embedded sheet. Errors go to
stderr with the file and line: `[style] porcelain.css:42: rule dropped: ...`.

## The CSS subset

Parser and cascade: `orion/user/style.c` (`style.h`).

| Feature | Supported |
|---|---|
| Selectors | One compound selector: `Element`, `*`, `:root`, `.class`, `#id`, `::part`, pseudo-classes; comma lists |
| Pseudo-classes | `:hover` `:active` `:checked` `:disabled` `:focus` `:default` `:window-inactive` |
| Combinators | None. Style the element or its `::part` (`TableView::row`, not `TableView tr`) |
| Cascade | `!important`, then specificity (ids; classes and pseudo-classes; elements and parts), then source order |
| Custom properties | `--name: value`, `var(--name, fallback)`; looked up on the element, then `:root` |
| At-rules | `@media (prefers-color-scheme: dark)` and `light`. Others are skipped with a diagnostic |
| Colours | `#rgb` `#rgba` `#rrggbb` `#rrggbbaa`, `rgb()` / `rgba()` (commas or spaces, `/ alpha`), `transparent`, `black`, `white` |
| Lengths | `px` or a bare number; `border-radius: 9999px` is a capsule |

A rule with one bad selector is dropped, a bad declaration is skipped, and the
rest of the sheet applies, as in browsers.

## Colour roles

Every system colour role is a custom property on `:root`, in kebab case:
`brControlBg` is `--control-bg`, `brTextSecondary` is `--text-secondary`
(`theme_sys_color_name()` lists them). A sheet must define all of them; a
missing role is logged and the theme falls back to a built-in palette.

## Control properties

The properties a sheet sets override the matching `theme_t` metric; the rest
keep the base theme's value (`theme_metrics_from_stylesheet()` in
`orion/user/theme_css.c`).

| Selector | Property | `theme_t` field |
|---|---|---|
| `Window` | `border-radius` | `window.corner_radius` |
| `Button` | `border-radius` | `button.corner_radius` |
| `TextEdit` | `border-radius` | `field.corner_radius` |
| `MultiEdit` | `border-radius` | `field.multiline_corner_radius` |
| `TabView::tab` | `border-radius` | `tab.corner_radius` |
| `Card` | `border-radius`, `background-color: var(--role)`, `border-color: var(--role)` | `card.corner_radius`, `card.face`, `card.outline` |
| `TableView` | `border-radius` | `table.corner_radius` |
| `TableView::header` | `min-height` | `report.header_height` |
| `TableView::row` | `min-height`, `border-bottom-width` | `report.row_height`, `report.row_dividers` |
| `Badge` | `border-radius`, `padding-left`, `--tint-alpha`, `--ink-mix` (0..1) | `badge.corner_radius`, `badge.padding`, `badge.tint_alpha`, `badge.ink_mix` |
| `Badge::icon` | `width`, `margin-right` | `badge.icon_size`, `badge.icon_gap` |
