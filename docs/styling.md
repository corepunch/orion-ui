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
never depends on files at run time. `base.css` is Orion's user-agent sheet: it
sets every part and the chrome geometry, and a theme overrides what it needs.
`orion/user/theme_builtin.c` builds the instances; they carry no numbers.

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
| `border`, `border-<side>`, `border-width/-style/-color`, `border-<side>-<property>` | Solid sides; a uniform border follows the corner radius; unequal sides meet in mitred corners, as in a browser; a rounded box may have a `border-left` alone (an accent edge), which follows the corner as a browser draws it: its padding edge has the corner ellipse (r − w, r) |
| `border-radius` | 1–4 lengths; rounded top corners with square bottom ones draw a tab |
| `margin` | 1–4 lengths: the painted box inside the part's bounds |
| `outline` | `width style colour`, filled behind the box, outside it and following its corners (`radius + width`), as a focus halo or a selection ring |
| `box-shadow` | `[inset] dx dy [blur] colour, ...`: outer shadows paint behind the box, blurred as CSS blurs them (a Gaussian of σ = blur / 2), and overflow the window's frame (its `ink_overflow`); inset ones with no blur paint the edges they offset into, inside the border (a bevel's inner line) |
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
| `CARD` | `Card` (`:hover`, `:checked`) | `BADGE`, `BADGE_ICON` | `Badge`, `Badge::icon` (`fill`) |
| `TOOLBAR_BUTTON` (`_LABELED`, `_SPLIT`) | `Toolbar::button` (`.labeled`, `.split`) | `TITLEBAR`, `TITLE_TOOLBAR` | `Window::titlebar`, `Window::title-toolbar` |
| `TOOLBAR_SPLIT_ARROW`, `_SEPARATOR`, `_GRIP` | `Toolbar::split-arrow`, `::separator`, `::grip` | `WINDOW_CLOSE`, `RESIZE_GRIP` | `Window::close`, `Window::resize-grip` |
| `SLIDER_TRACK`, `_THUMB`, `_TROUGH`, `_KNOB` | `Slider::track`, `::thumb`, `::trough`, `::knob` | `SCROLLBAR_*` | `Scrollbar::track`, `::thumb`, `::arrow-up` … `::corner` |

Layout containers are boxes too, like a `div`: `StackView`, `GridView` and
`FlowView` paint their own element's box under their children, and nothing
unless a sheet styles them (`StackView#summary { background: …; border-radius: 8px }`).

The painter says what it cannot draw yet instead of drawing it differently:
a `box-shadow` spread, a blurred inset shadow, corner radii other than one
radius or the top two, and unequal border sides on a rounded box (but a lone
`border-left`) each print
`[theme] part …: … is not supported yet`.

### Cards and badges

A `Card` and a `Badge` are ordinary boxes; `base.css` styles them:

```css
Card         { margin: var(--ring-width); background: var(--card-face); border-radius: var(--card-radius);
               border-left: var(--edge-width) solid var(--tone, transparent); --card-face: var(--control-bg); }
Card:hover   { background: color-mix(in srgb, var(--card-face), var(--button-hover)); }
Card:checked { outline: var(--ring-width) solid var(--accent); }
Badge        { background: color-mix(in srgb, var(--tone) var(--tint), transparent);
               color: color-mix(in srgb, var(--tone) var(--ink), var(--text-normal)); border-radius: 9999px; }
```

The accent of one card and the tone of one badge are data, so they are
*instance variables*: custom properties set on the window, as `style="--tone: …"`
sets them on an element. They answer `var(--name)` in that window's queries
before the sheet does:

```c
window_set_style_var_color(card, "--tone", color_with_alpha(get_sys_color(brTextWarning), 0xFF));
window_set_style_var(badge, "--tone", "var(--text-success)");   // a role: follows the palette
```

`cdSetEdgeColor` and `bdSetColor` are these calls. A part that is not a window
(a status badge in a table cell) passes the variables itself:
`theme_draw_with(win, &vars, THEME_PART_BADGE, r, state)`, with `vars` built by
`ui_style_vars_set()` (at most four, names `--x`, values up to 47 characters).
The painter caches a box per distinct set of variables.

A card reserves its `margin` and `border-left` whether or not it has an accent,
so a selection or an accent never shifts content; `theme_content_insets_in()`
reports that reservation. Porcelain draws the card's hairline as its `outline`
and moves the box one pixel in (`margin: 2px`) so that selecting can swap it for
the accent ring.

States map to pseudo-classes: hover `:hover`, pressed `:active`, selected
`:checked`, disabled `:disabled`, keyboard focus `:focus`, default button
`:default`, a selection whose window is not focused `:window-inactive`.

## Computed style

Code asks the active sheet for the values it lays out and paints with, by
selector and property, as a page asks `getComputedStyle()`
(`orion/user/ui_style.h`):

```c
int gutter = ui_px("Scrollbar", "width");
int radius = ui_radius("Button");                  // CORNER_RADIUS_CIRCULAR for 9999px
ui_shadow_t s = ui_shadow("Tooltip", "box-shadow");
bool ruled = ui_flag("TableView::row", "--divider-width");
```

Answers are cached until the sheet or the palette changes. `padding-left` and
its kin read the longhand or the `padding` shorthand, whichever cascades higher.
A property the sheet leaves unset is reported (`[ui_style] Scrollbar { width }
is not set`) and reads as zero, so `base.css` sets every value the framework
asks for: it is Orion's user-agent sheet, and a theme overrides what it needs.

Lengths may use `calc()` and the platform values `--titlebar-height`,
`--menubar-height`, `--toolbar-height`, `--compact-icon-size`, `--font-size`,
`--icon-size`, `--scrollbar-width`, `--control-height`, `--control-radius` and
`--control-padding`, which differ between desktop and touch builds.

## Application sheets

An application's own CSS is read after the theme's, as a page's sheet follows
the browser's. Link it from the `.orion` root; `orionc` embeds the file in the
generated header and every form carries it:

```xml
<orion name="gitclient" root="apps/gitclient" stylesheet="gitclient.css">
```

The first form an app instance creates sets the sheet for that instance
(`ui_set_instance_stylesheet`). A standalone app is instance 0, the process:
its sheet applies to every query. A gem shares the process with the shell and
other gems, so its sheet styles only the windows it owns. Code may also set one
directly: `ui_set_app_stylesheet(css, "myapp.css")` for the process.

A control's form `name` is its `#id` and `class=` its classes:

```xml
<TableView name="checkouts" class="compact" source="workspace.checkouts" />
```

```css
TableView#checkouts::header { text-transform: uppercase; letter-spacing: .05em; }
TableView.compact::row      { min-height: 20px; }
Label.muted                 { color: var(--text-secondary); }
```

Controls look themselves up through their window, as WinAPI controls open
theme data for their `HWND`:

```c
draw_text_clipped(ui_selector(win, "TableView::header"), title, &r, fg, 0);
theme_draw_in(win, THEME_PART_HEADER, r, CTRL_NORMAL);
```

`ui_selector()` returns the element selector with the window's classes and id
(`TableView.compact#checkouts::header`), interned, and remembers which
instance's sheet answers it; an unstyled window gets the element selector back.
`window_set_style(win, id, classes)` restyles a window at run time and sends it
`evThemeChanged`, as a theme switch does, so a control recomputes what it
derived from the sheet (a Card its padding).

Every control reads its own element through its window: Button and
CommandLink, CheckBox, TextEdit (single and multi-line), ComboBox, Label,
TabView, TableView, IconView, SegmentedControl, Slider, Card, SplitView,
Splitter, Separator, MenuBar, Toolbar (through its toolbar window), ActivityBar,
TileGrid, Image and SpeechBubble. Painters a control shares take its window
too: `theme_draw_button_label(win, ...)`, `theme_draw_combobox(win, ...)`,
`draw_status_badge(win, ...)`. Three things still style as their element: popup
menus (measured before their window exists), free text (`Caption`, `Heading`
and the rest) and elements drawn inside a control that are not the control
itself (`Scrollbar`, `Tooltip`).

## Type

A font is a CSS selector: `draw_text("TableView::row", ...)` draws with the
`font` the sheet gives that element and part, or `:root`'s when it sets none
(`ui_font_t` is `const char *`). Controls name their own element; free text
uses the elements `base.css` defines for it: `Text`, `Caption`, `Heading`,
`Title` and `Display`, as HTML has `p`, `small`, `h3`, `h2` and `h1`. A
label's `font=` attribute (`caption`, `heading`, `title`, `display`) is its
class: `Label.title`.

`base.css` names five sizes as tokens and every element uses one of them:

| Token | Value (Porcelain) | Used by |
|---|---|---|
| `--text` | `400 13px var(--ui-face)` | `:root`, so every control's own text |
| `--text-caption` | `400 11px` | `Caption`, `TableView::detail`, `Icon`, `IconView::cell`, `Toolbar::label`, `Window::subtitle` |
| `--text-heading` | `600 12px` | `Heading`, `TableView::group`, `Label.heading` |
| `--text-title` | `600 18px` | `Title`, `Window::title`, `Label.title` |
| `--text-display` | `600 24px` | `Display`, `Label.display` |

`--ui-face` is IBM Plex Sans; Classic sets DejaVu Sans and its own sizes. The
face is `share/orion/fonts/<Family without spaces>-<Weight>.ttf`, with weights
named Thin, ExtraLight, Light, Regular, Medium, SemiBold, Bold, ExtraBold and
Black (a 400 face may also be `<Family>.ttf`). Sizes are scaled for the
platform (×1.2 on iOS). Each face and size gets one glyph atlas, made on first
use; a missing face falls back to DejaVu Sans with a diagnostic.

The element's text properties apply wherever its text is measured, drawn or
wrapped:

| Property | Values | Notes |
|---|---|---|
| `text-transform` | `none`, `uppercase`, `lowercase`, `capitalize` | Latin, Greek and Cyrillic case pairs; the string itself is not changed |
| `letter-spacing` | `normal`, `<px>`, `<em>` | added after every character, as browsers do |
| `text-align` | `start`/`left`, `center`, `end`/`right` | single lines in `draw_text_clipped` (an alignment flag from the caller wins) and every wrapped line; trailing spaces hang |
| `line-height` | `normal`, `<number>`, `<px>`, `<em>` | between wrapped lines; glyphs sit in the middle of the line box |

```css
TableView::header { font: 500 11px var(--ui-face); text-transform: uppercase; letter-spacing: .05em; }
```

An unsupported value is reported on `stderr` and reads as the initial value.
