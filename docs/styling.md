# Style sheets

Orion themes are written in CSS. Every theme part is painted by one box
painter from the cascaded properties of its element and part, the colour roles
are custom properties, and `theme_t`'s metrics are control properties.

| File | Themes |
|---|---|
| [`orion/themes/base.css`](../orion/themes/base.css) | Shared rules: how each part paints, in roles and radius variables |
| [`modern.css`](../orion/themes/modern.css) | Modern (dark) and Light (`prefers-color-scheme: light`) |
| [`navy.css`](../orion/themes/navy.css) | Navy |
| [`amber.css`](../orion/themes/amber.css) | Amber |
| [`porcelain.css`](../orion/themes/porcelain.css) | Porcelain and Porcelain Dark |
| [`metro.css`](../orion/themes/metro.css) | Metro |
| [`classic.css`](../orion/themes/classic.css) | Classic: Win95 bevels from per-side border colours and inset box-shadows |

A theme is `base.css` followed by its own sheet, read in the theme's appearance.
The build embeds the sheets in `libuser` (`tools/embed_text.py`), so a theme
never depends on files at run time. `orion/user/theme_builtin.c` builds the
instances; its templates still hold the chrome geometry no property sets yet
(caption and menu bar heights, toolbar sizes, tooltip and slider geometry).

## Editing a theme without rebuilding

```bash
ORION_THEME_CSS=orion/themes/porcelain.css build/bin/ledger
```

`ORION_THEME_CSS` names a file read instead of the active CSS theme's own sheet
(after `base.css`). Errors go to stderr with the file and line:
`[style] porcelain.css:42: rule dropped: ...`.

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
| Colours | `#rgb` `#rgba` `#rrggbb` `#rrggbbaa`, `rgb()` / `rgba()`, `color-mix(in srgb, a p%, b q%)`, `transparent`, `black`, `white` |
| Lengths | `px` or a bare number; `border-radius: 9999px` is a capsule |

A rule with one bad selector is dropped, a bad declaration is skipped, and the
rest of the sheet applies, as in browsers.

## Colour roles

Every system colour role is a custom property on `:root`, in kebab case:
`brControlBg` is `--control-bg`, `brTextSecondary` is `--text-secondary`
(`theme_sys_color_name()` lists them). A sheet must define all of them. While
painting, `var(--role)` reads the live palette, so `set_sys_colors()` overrides
apply to every part.

## Painting parts

`theme_box.c` maps each `THEME_PART_*` to a selector and paints its box:

| Property | Meaning |
|---|---|
| `background` | A colour, `none`, or `linear-gradient([to bottom,] top, bottom)` |
| `border`, `border-<side>`, `border-width/-style/-color`, `border-<side>-<property>` | Solid sides; a uniform border follows the corner radius |
| `border-radius` | 1–4 lengths; rounded top corners with square bottom ones draw a tab |
| `margin` | 1–4 lengths: the painted box inside the part's bounds |
| `outline` | `width style colour`, filled behind the box as a focus halo |
| `box-shadow` | `[inset] dx dy [blur] colour, ...`: outer shadows paint behind the box, blurred; inset ones with no blur paint the edges they offset into, inside the border (a bevel's inner line) |
| `color` | The part's text colour (`theme_foreground()`) |
| `content` | The glyph over the box: `arrow-updown`, `close`, `maximize`, `checkmark`, `resize`, `scroll-up`/`-down`/`-left`/`-right`, `triangle-down`, `grip`, `dots`, `groove`, `none` |
| `fill` | The glyph's colour; defaults to `color` |
| `stroke` | The second line of a `groove` |

| Part | Selector | Part | Selector |
|---|---|---|---|
| `BUTTON` | `Button` | `LIST_ITEM` | `TableView::row` |
| `FIELD` | `TextEdit` (`.multiline`) | `HEADER` | `TableView::header` |
| `COMBOBOX`, `COMBOBOX_ARROW` | `ComboBox`, `ComboBox::arrow` | `COLUMN_DIVIDER`, `LIST_GROUP` | `TableView::divider`, `TableView::group` (a grouped list's caption row) |
| `LIST_BODY`, `ROW_RULE`, `LIST_FRAME` | `TableView::body`, `::rule`, `::frame` | `ICON_ITEM`, `ICON_CELL`, `THUMBNAIL` | `IconView::item`, `::cell`, `::thumbnail` |
| `DESKTOP_ICON`, `DESKTOP`, `WORKSPACE` | `Icon`, `Desktop`, `Workspace` | `TILE_GRID`, `CARET` | `TileGrid`, `TextEdit::caret` |
| `SPLITTER` | `SplitView::handle` | `DROP_TARGET`, `DOCK_PREVIEW`, `DISABLED_VEIL` | `Page::drop-target`, `Dock::preview`, `Window::disabled-veil` |
| `CHECKBOX` | `CheckBox::box` | `MENU_BAR`, `MENU_POPUP`, `MENU_ITEM` | `MenuBar`, `Menu`, `Menu::item` |
| `SEGMENTED_TRACK`, `SEGMENT` | `SegmentedControl`, `::segment` | `SEPARATOR` | `Separator` |
| `TAB`, `TAB_BAR`, `TAB_PANE` | `TabView::tab`, `::bar`, `::pane` | `PAGE`, `PANEL`, `SURFACE`, `SIDEBAR` | `Page`, `Panel`, `Surface`, `Sidebar` |
| `TOOLBAR` | `Toolbar` | `STATUSBAR` | `StatusBar` |
| `TOOLBAR_BUTTON` (`_LABELED`, `_SPLIT`) | `Toolbar::button` (`.labeled`, `.split`) | `TITLEBAR`, `TITLE_TOOLBAR` | `Window::titlebar`, `Window::title-toolbar` |
| `TOOLBAR_SPLIT_ARROW`, `_SEPARATOR`, `_GRIP` | `Toolbar::split-arrow`, `::separator`, `::grip` | `WINDOW_CLOSE`, `RESIZE_GRIP` | `Window::close`, `Window::resize-grip` |
| `SLIDER_TRACK`, `_THUMB`, `_TROUGH`, `_KNOB` | `Slider::track`, `::thumb`, `::trough`, `::knob` | `SCROLLBAR_*` | `Scrollbar::track`, `::thumb`, `::arrow-up` … `::corner` |

States map to pseudo-classes: hover `:hover`, pressed `:active`, selected
`:checked`, disabled `:disabled`, keyboard focus `:focus`, default button
`:default`, a selection whose window is not focused `:window-inactive`.

## Metrics

Control properties set the matching `theme_t` metric when a theme is built
(`theme_metrics_from_stylesheet()` in `orion/user/theme_css.c`). Themes set the
radii through the `:root` variables `base.css` uses: `--window-radius`,
`--button-radius`, `--field-radius`, `--multiline-radius`, `--tab-radius`,
`--toolbar-radius`, `--table-radius`, `--card-radius`, `--badge-radius`.
`--control-radius` is supplied by the platform (half a control's height).

| Selector | Property | `theme_t` field |
|---|---|---|
| `Window` | `border-radius` | `window.corner_radius` |
| `Button` | `border-radius` | `button.corner_radius` |
| `TextEdit`, `TextEdit.multiline` | `border-radius` | `field.corner_radius`, `field.multiline_corner_radius` |
| `TabView::tab` | `border-radius` (top-left) | `tab.corner_radius` |
| `Toolbar` | `gap` | `toolbar.spacing` |
| `Toolbar::button` | `border-radius` | `toolbar.corner_radius` |
| `Card` | `border-radius`, `background-color: var(--role)`, `border-color: var(--role)`, `--highlight-width`, `--plastic: flat` | `card.corner_radius`, `card.face`, `card.outline`, `card.highlight_width`, `plastic.flat` |
| `TableView` | `border-radius` | `table.corner_radius` |
| `TableView::header` | `min-height` | `report.header_height` |
| `TableView::row` | `min-height`, `--divider-width` | `report.row_height`, `report.row_dividers` |
| `IconView::thumbnail:checked` | `border-width` | `thumbnail.ring_width` |
| `Badge` | `border-radius`, `padding-left`, `--tint-alpha`, `--ink-mix` (0..1) | `badge.corner_radius`, `badge.padding`, `badge.tint_alpha`, `badge.ink_mix` |
| `Badge::icon` | `width`, `margin-right` | `badge.icon_size`, `badge.icon_gap` |

## Type

The text roles are `:root` custom properties in the CSS `font` shorthand
(weight, size in desktop logical pixels, family), read by `text.c` when the
theme changes; `base.css` sets IBM Plex Sans:

| Property | Role | Used for |
|---|---|---|
| `--font-system` | `FONT_SYSTEM` | Chrome: menus, buttons, dialogs |
| `--font-small` | `FONT_SMALL` | Content: list rows, the status bar |
| `--font-smallest` | `FONT_SMALLEST` | Captions in dense views |
| `--font-title` | `FONT_TITLE` | A window title in its toolbar |
| `--font-display` | `FONT_DISPLAY` | The one figure a view exists to show |
| `--font-heading` | `FONT_HEADING` | Group captions and section headings |

The face is `share/orion/fonts/<Family without spaces>-<Weight>.ttf`, with
weights named Thin, ExtraLight, Light, Regular, Medium, SemiBold, Bold,
ExtraBold and Black (a 400 face may also be `<Family>.ttf`). Sizes are scaled
for the platform (×1.2 on iOS). A missing face falls back to DejaVu Sans with a
diagnostic; Classic keeps DejaVu.
