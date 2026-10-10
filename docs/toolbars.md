# Toolbars

Orion toolbars are **declarative descriptor arrays** rendered as a non-client band
above a window's client area.  The pattern used by the image editor is the
canonical way to build application-level and per-window toolbars.

## Overview

### Activity bars

An activity bar is a vertical navigation toolbar. It uses the same button size
as PencilTest's tool palette (`TB_SPACING`), theme painting, tooltips, and
`TBSTYLE_CHECKGROUP` selection that Groove uses for its instrument filters.
Buttons send ordinary `evCommand` / `btnClicked` notifications to the parent;
the app decides whether that command selects a page, a tool, or a filter.

Declare its items in the form, using existing commands when appropriate:

```xml
<ActivityBar name="activity">
  <Toolbar>
    <Button command="view.overview" icon="view-grid" checked-icon="view-grid-fill" text="Overview" tooltip="Overview (Ctrl+0)" />
    <Button command="view.changes" icon="git-commit" text="Changes" tooltip="Changes (Ctrl+1)" />
  </Toolbar>
</ActivityBar>
```

`ActivityBar` is registered with both the runtime and FormEditor. Its buttons
automatically form radio groups. Use `tbCheckButton` to synchronize selection
when a menu or accelerator changes the active view. Supply `checked-icon` for
an authored filled variant while selected; `icon` remains the outline variant.
Dock the control with
`DOCK_LEFT`, `DOCK_TOOLBAR | DOCK_NOFLOAT`; dock the app's pages with `DOCK_FILL`
and show the active page. The dock manager owns sizing and resizing.

### Toolbar bands

```text
┌─────────────────────────────────────────────────────┐
│  Menu bar                                           │
├─────────────────────────────────────────────────────┤
│  [New] [Open] [Save] │ [Undo] [Redo] │ [Zoom] ...  │  ← toolbar band
├─────────────────────────────────────────────────────┤
│                                                     │
│              client area / content                   │
│                                                     │
└─────────────────────────────────────────────────────┘
```

A toolbar is a window with the `WINDOW_TOOLBAR` flag.  Items are described by
`toolbar_item_t` structs and loaded with the `tbSetItems` message.  When a
button is clicked the toolbar flips its checked state (for `TBSTYLE_CHECK` /
`TBSTYLE_CHECKGROUP` buttons) and sends
`evCommand(MAKEDWORD(ident, btnClicked), toolbar)` to its owner. For application actions, that identifier must be the command
ID of a menu-declared action.

## Menus are the application capability map

The `.orion` menu tree is the canonical map of user-invokable application
capabilities. A menu item declares the action, its label, and optional
`shortcut="Ctrl+K"`; toolbars, context menus, and future command palettes
reference that item. They do not create parallel command IDs.

New manifests should use fully qualified references such as
`command="repo.refresh"`. The older `menu="repo"` scope form remains supported
for compatibility, but does not make the target action as explicit.

## The descriptor

Defined in `orion/user/messages.h`:

```c
typedef struct {
  toolbar_item_type_t type;    // BUTTON, LABEL, COMBOBOX, TEXTEDIT, SEGMENTED, SEPARATOR, SPACER, DROPDOWN
  int                 ident;   // command ID / button identifier
  const char         *icon;    // named icon or "strip:N" atlas index; NULL = missing icon
  int                 w;       // explicit width in pixels (0 = automatic)
  uint32_t            style;   // TBSTYLE_CHECK, TBSTYLE_CHECKGROUP, TOOLBAR_ITEM_FLAG_*, CONTROL_SIZE_*
  const char         *text;    // label text, or combobox/textedit initial text
  const char         *tooltip; // hover tooltip; NULL = none
  uint32_t            color;   // plastic face colour; 0 = theme accent
  const char         *checked_icon; // icon while TBSTATE_CHECKED; NULL = same icon
  uint32_t            state;   // initial TBSTATE_CHECKED / TBSTATE_DISABLED
} toolbar_item_t;
```

Item types:

| Type | Description |
|---|---|
| `TOOLBAR_ITEM_BUTTON` | Icon-only button (owner-drawn) |
| `TOOLBAR_ITEM_LABEL` | Static text label |
| `TOOLBAR_ITEM_COMBOBOX` | Drop-down combobox (embedded child window) |
| `TOOLBAR_ITEM_TEXTEDIT` | Single-line text input (embedded child window) |
| `TOOLBAR_ITEM_SLIDER` | Slider occupying exactly three icon slots along the toolbar orientation |
| `TOOLBAR_ITEM_SEGMENTED` | `SegmentedControl` radio group (embedded child window); `text` holds the labels, `"One|Two|Three"` |
| `TOOLBAR_ITEM_SEPARATOR` | Narrow vertical divider |
| `TOOLBAR_ITEM_SPACER` | Invisible gap (no interaction) |
| `TOOLBAR_ITEM_DROPDOWN` | Split button: left fires `btnClicked`, right arrow fires `tbDropdown` |

## Floating options toolbars

Use `WINDOW_TOOLBAR | WINDOW_NOTITLE | WINDOW_NORESIZE | WINDOW_NOCLOSE` for a
movable options toolbar. Set `tbSetOrientation` to `TOOLBAR_VERTICAL` and
`tbSetStyle` to `TOOLBAR_STYLE_GRIP` to reserve a drag grip above its items.
Use the same button size and padding as the main tool strip.

`TOOLBAR_ITEM_SLIDER` creates a framework-owned `Slider` in the toolbar's
embedded child list. Its length is fixed at three icon slots, including the
two inter-icon gaps; its width matches one icon in a vertical toolbar.
Orientation follows the toolbar automatically. Configure its range and value
through `slSetRange` / `slSetPos` on `get_window_item(toolbar, ident)` and handle
`sliderValueChanged` through `evCommand`. Declarative toolbars can use `<slider>`.

`TOOLBAR_ITEM_SEGMENTED` embeds a `SegmentedControl` at field height, next to
text fields. With `w` = 0 the item takes the width the control measures for
its labels. Read it with `sgGetSelection` on `get_window_item(toolbar, ident)`
and handle `sgnSelChange` through `evCommand` in the toolbar's owner.
Declarative toolbars can use `<SegmentedControl text="All|Open|Closed" />`.
After changing the control's content (`sgAddSegment`, icons, style), send
`tbFitItem` with its ident: the toolbar re-measures it and lays the row out
again, like `TB_AUTOSIZE`. Over an icon-only segment the toolbar shows the
segment's own tooltip; elsewhere it shows the item's `tooltip`.
Hover feedback is not routed to controls embedded in a toolbar.

Item *style* is fixed by the descriptor; item *state* changes at runtime.
`TBSTYLE_CHECK` buttons toggle `TBSTATE_CHECKED` themselves on each click.
Consecutive `TBSTYLE_CHECKGROUP` buttons form a radio group: clicking one checks
it and clears the rest of the run (separators and ordinary buttons end a run).
Read state back with `tbGetState` / `toolbar_is_button_checked()` and write it with
`tbSetState`. `tbCheckButton` (`wparam` = ident, `lparam` = checked) sets one
button's checked state like `TB_CHECKBUTTON`; checking a `CHECKGROUP` button
clears its group. Apps only call it when the state changes for a reason other
than a click on that button (a menu command, undo, a document switch).

`TOOLBAR_ITEM_FLAG_ARTWORK` marks a button whose icon is full-colour artwork,
such as a `strip:N` cell. It is drawn as large as the button allows, over the
theme's toolbar-button background (empty at rest, accent when checked), never
on a plastic body.

## Two ways to define items

### 1. Static `const` array (per-window toolbars)

For toolbars that belong to a single window and don't change, define a file-scope
`static const` array:

```c
// win_layers.c
static const toolbar_item_t kLayersToolbar[] = {
  { TOOLBAR_ITEM_BUTTON,    ID_LAYER_NEW,       sysicon_image_add,  0, 0, NULL, "New layer" },
  { TOOLBAR_ITEM_BUTTON,    ID_LAYER_DUPLICATE, sysicon_page_copy,  0, 0, NULL, "Duplicate layer" },
  { TOOLBAR_ITEM_BUTTON,    ID_LAYER_DELETE,    sysicon_delete,     0, 0, NULL, "Delete layer" },
  { TOOLBAR_ITEM_SPACER,    0, 0, 0, 0, NULL, NULL },
  { TOOLBAR_ITEM_BUTTON,    ID_LAYER_MOVE_UP,   sysicon_arrow_up,   0, 0, NULL, "Move layer up" },
  { TOOLBAR_ITEM_BUTTON,    ID_LAYER_MOVE_DOWN, sysicon_arrow_down, 0, 0, NULL, "Move layer down" },
  { TOOLBAR_ITEM_SPACER,    0, 0, 8, 0, NULL, NULL },
  { TOOLBAR_ITEM_COMBOBOX,  ID_LAYER_BLEND_COMBO, -1, 120, 0, "Normal", NULL },
};
```

### 2. `.orion` XML (application toolbar)

Declare the application's main toolbar directly under `<orion>`, alongside
`<menus>` and `<forms>`. It is application metadata, not a form:

```xml
<orion name="imageeditor">
  <toolbar presentation="compact">
    <Button name="new" command="file.new" icon="page-plus"
            text="New" tooltip="New image" />
  </toolbar>
  <!-- menus declare file.new; forms declare document windows and dialogs -->
</orion>
```

`presentation="normal"` (the default) creates the usual toolbar band below
the menu. `presentation="compact"` puts small icon buttons at the right of
the menu row, leaving the far-right restore-button slot free. Icons have a
background highlight while pressed or checked (`tbCheckButton`, for toggles),
never on hover. A toggle with `checked-icon="..."` shows that icon instead of
the checked highlight. Compact
icons are 16 logical pixels; square pressed backgrounds have 2 pixels of
vertical padding, with 6 pixels between button boxes. If there is
insufficient room beside the menu labels, the toolbar uses the normal band
and returns to compact placement when space permits. Hosts with a shell-owned
menu (GEMs) also use the normal band; this API does not contribute icons to
the shell's shared menu.

The compiler emits `<prefix>_application_toolbar`, an `application_toolbar_t`
descriptor containing items, count, and presentation. Pass it to
`create_application_chrome(title, menubar_proc, menus, menu_count, toolbar_proc,
&imageeditor_application_toolbar, hinstance)`. Application chrome loads the
items and owns layout and lifetime; `toolbar_proc` handles the existing
`btnClicked` commands. There is no synthetic toolbar form or separate item
loading in the application's `evCreate` handler.

Only one root toolbar is allowed. Unknown presentations and unresolved command
references are compile errors. Existing form-owned toolbars remain supported.

### 3. `.orion` XML (window-owned toolbar)

Declare a toolbar inside the form that owns it. The compiler stores the
generated items in that form's `toolbar_items` and `toolbar_count` metadata:

```xml
<!-- imageeditor.orion -->
<forms>
  <form name="document" title="Document" width="640" height="480">
    <Toolbar>
    <Button name="new"  command="file.new" icon="sysicon_page_add"      text="New"  tooltip="New image" />
    <Button name="open" command="file.open" icon="sysicon_folder_page"   text="Open" tooltip="Open image" />
    <Button name="save" command="file.save" icon="sysicon_disk_save"     text="Save" tooltip="Save image" />
    <spacer w="10" />
    <Button name="undo" command="edit.undo" icon="sysicon_undo"          text="Undo" tooltip="Undo" />
    <Button name="redo" command="edit.redo" icon="sysicon_redo"          text="Redo" tooltip="Redo" />
    <spacer w="10" />
    <Button name="zoom_in"  command="view.zoom_in" icon="sysicon_magnifier_zoom_in"  text="+"   tooltip="Zoom in" />
    <Button name="zoom_out" command="view.zoom_out" icon="sysicon_magnifier_zoom_out" text="-"   tooltip="Zoom out" />
    <spacer w="10" />
    <Button name="show_background" command="view.show_background" icon="sysicon_eye_show"
            flags="BUTTON_PUSHLIKE" text="BG" tooltip="Toggle background" />
    </Toolbar>
  </form>
</forms>
```

Include the generated header:

```c
#include "build/generated/apps/imageeditor/imageeditor.h"
```

The `command=` attribute links each button to a fully qualified menu item for
consistent command IDs. `<Toolbar>` is chrome metadata, not a content child.
Top-level `<toolbars>` resources and `toolbar="name"` references are not
supported. A root `<toolbar>` belongs to the application; a nested `<Toolbar>`
belongs to its containing form.

For normal windows and hosts, a nested `<Toolbar>` automatically enables
`WINDOW_TOOLBAR`. A `role="page"` form publishes the same metadata without
reserving its own toolbar band because the active host renders it.

## Creating a toolbar window

```c
window_t *win = create_window(
    "Toolbar",
    WINDOW_TOOLBAR | WINDOW_NOTITLE | WINDOW_ALWAYSONTOP |
    WINDOW_NORESIZE | WINDOW_NOTRAYBUTTON | WINDOW_NODRAG,
    MAKERECT(0, 0, screen_w, TOOLBAR_BAND_HEIGHT),
    NULL, my_toolbar_proc, hinstance, NULL);
show_window(win, true);
```

The `WINDOW_TOOLBAR` flag tells the framework to render the window as a
non-client band.  The band height is computed automatically from button size +
padding + bevels (`TOOLBAR_BAND_HEIGHT` = 28px at default 22px buttons).

A toolbar window can also be an ordinary child of a content window: pass its
parent to `create_window()` and arrange its frame like other children. Its
height is `titlebar_height(toolbar)`. This is useful for a transport row above
a sound library. The child retains the framework's toolbar input routing,
tooltips, artwork states and embedded controls without reserving a toolbar
band in the main window.

Use `TOOLBAR_ITEM_TEXTEDIT` for a search field. Its `icon` names an optional
SVG drawn inside the field before the placeholder and entered text (for
example, `"search"`). The text edit reserves that space for text and cursor
placement; standalone fields can use `edSetLeadingIcon` for the same behavior.
A `TOOLBAR_ITEM_SPACER` with
`TOOLBAR_ITEM_FLAG_FLEXSPACE` shares the remaining horizontal width with other
flexible spacers, keeping following items aligned to the right as the toolbar
resizes. Vertical toolbars retain the spacer's ordinary size.

To change a button's icon without recreating embedded controls, send
`tbSetItemIcon` with the command ID in `wparam` and an icon name in `lparam`
(`NULL` clears the icon). The toolbar copies the name and preserves the
field's text, focus and selection. Use `tbCheckButton` / `tbSetState` for toggle state; reserve `tbSetItems` for changes to the
item list.

A toggle whose icon changes with its state (play/pause, repeat/stop-at-end)
sets `checked_icon` (`checked-icon` in `.orion`). While checked the button draws
that icon and no checked highlight; the owner only sends `tbCheckButton`.

For a WinAPI-style large toolbar with captions below icons, enable the label
style after setting the items:

```c
send_message(win, tbSetStyle, TOOLBAR_STYLE_SHOW_LABELS, NULL);
```

The existing `toolbar_item_t.text` supplies each caption. In this mode button
widths expand to fit their captions and the non-client toolbar band grows by
one small-font text row; client layout and input routing use the new height
automatically.

## Loading items

Bitmap artwork is supplied as a caller-owned strip texture through `tbSetStrip`;
reference its tiles with `icon="strip:0"`, `"strip:1"`, etc. Strip icons preserve
authored colours, scale to the available button area, and fade when disabled.
Named SVG icons keep their theme tint.

For a programmatic toolbar, send `tbSetItems` in `evCreate`. Application chrome
and declarative form creation load their toolbar metadata automatically.
The framework copies the array internally — the caller does not need to keep
it alive after the call.

```c
result_t my_toolbar_proc(window_t *win, uint32_t msg,
                         uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      send_message(win, tbSetItems,
                   ARRAY_LEN(kLayersToolbar), (void *)kLayersToolbar);
      return true;
    case evCommand:
      if (HIWORD(wparam) != btnClicked) return false;
      handle_menu_command(LOWORD(wparam));
      return true;
  }
  return false;
}
```

For a static array:

```c
case evCreate:
  send_message(win, tbSetItems,
               sizeof(kLayersToolbar) / sizeof(kLayersToolbar[0]),
               (void *)kLayersToolbar);
  return true;
```

## Single command dispatch

The key design rule: **every toolbar click routes through the same
`handle_menu_command()` function as menu items and keyboard shortcuts.**

```c
case evCommand:
  if (HIWORD(wparam) == btnClicked) { handle_menu_command(LOWORD(wparam)); return true; }
  return false;
```

This means:
- A toolbar button, a menu item, and a keyboard accelerator all execute the
  exact same code path.
- Adding a new button requires only: (1) define the command ID, (2) add a
  `toolbar_item_t` entry, (3) add a `case` in `handle_menu_command()`.
- No toolbar-specific logic is needed per button.

## Syncing toggle state

For toggle buttons (e.g., show/hide background), define a sync function that
reads application state and updates individual buttons:

```c
void imageeditor_sync_main_toolbar(void) {
  if (!g_app || !g_app->main_toolbar_win) return;
  bitmap_strip_t *strip = ui_get_sysicon_strip();
  window_t *bg_btn = get_window_item(g_app->main_toolbar_win, ID_VIEW_SHOW_BACKGROUND);

  if (bg_btn) {
    bool checked = !g_app->active_doc || g_app->active_doc->background.show;
    send_message(bg_btn, btnSetCheck,
                 checked ? btnStateChecked : btnStateUnchecked, NULL);
    if (strip) {
      int icon = checked ? (sysicon_eye_show - SYSICON_BASE)
                         : (sysicon_eye_hide - SYSICON_BASE);
      send_message(bg_btn, btnSetImage, (uint32_t)icon, strip);
    }
  }
}
```

Call the sync function:
- After `tbSetItems` (initial load)
- After any state change that affects toggle buttons
- After handling a `btnClicked` command that changes state the toolbar cannot see

## `app_chrome` — menubar + toolbar wrapper

For standalone applications, `app_chrome` combines the menubar and main toolbar
into a single window that manages both:

```c
g_app->chrome_win = create_app_chrome(
    "Image Editor Chrome",
    editor_menubar_proc, kMenus, kNumMenus,
    main_toolbar_proc, hinstance);

g_app->menubar_win      = app_chrome_menubar(g_app->chrome_win);
g_app->main_toolbar_win = app_chrome_toolbar(g_app->chrome_win);
```

The chrome window:
- Automatically resizes both children on `evDisplayChange`
- Routes `evPaint` to both children
- Provides accessor functions: `app_chrome_menubar()`, `app_chrome_toolbar()`

## Summary of the pattern

```text
┌──────────────────────────────────────────────────────────┐
│ .orion XML            static const toolbar_item_t[]     │
│       ↓                         ↓                        │
│ form.toolbar_items         kLayersToolbar[]               │
│       ↓                         ↓                        │
│         evCreate → tbSetItems → toolbar                  │
│                          ↓                               │
│        evCommand(ident, btnClicked)                       │
│                          ↓                               │
│               handle_menu_command(id)                     │
│                          ↓                               │
│                   sync_toolbar()                          │
└──────────────────────────────────────────────────────────┘
```

Every toolbar — main, layers, timeline — follows this exact sequence.  The
pattern scales from a 3-button strip to a full application toolbar with
comboboxes, dropdowns, and toggle buttons.

## Orientation and custom items

`toolbar_orientation_t` defines `TOOLBAR_HORIZONTAL` (the default) and
`TOOLBAR_VERTICAL`. Set it with `send_message(win, tbSetOrientation,
TOOLBAR_VERTICAL, NULL)`. Undocked vertical toolbars stack items downward in one column;
separators and spacers consume height instead of width. The non-client band
height follows the item extent. Floating palette windows should be sized to
that extent, plus title height and toolbar padding.

Use `TOOLBAR_ITEM_CUSTOM` for application-rendered items such as imageeditor's
foreground/background swatch. During toolbar painting, the owning window
receives `tbDrawItem`, with the item identifier in `wparam` and a borrowed
`toolbar_draw_item_t *` in `lparam`. Its `rect` is item-local (origin 0,0),
`state` contains `CTRL_*` flags, and `index` identifies the item. Draw only during
this callback; invalidate the toolbar window when the custom content changes.
The toolbar owns hover, pressed state, tooltips, and `btnClicked` delivery.

The former toolbox control and `bx*` messages have been removed. Component
registration uses `toolbar_icon` and `FE_COMPONENT_SHOW_TOOLBAR`.

## Multiple docked toolbars

Ordinary windows can own multiple toolbar bands. Create each band with
`create_docked_toolbar(owner, TOOLBAR_DOCK_TOP, proc)` or
`create_docked_toolbar(owner, TOOLBAR_DOCK_LEFT, proc)`. The returned child has
its own items, state, and command procedure; destroying the owner destroys its
bands. Existing `WINDOW_TOOLBAR` / `tbSetItems` callers remain supported.

In the owner's layout handler, pass its available rectangle to
`layout_docked_toolbars(owner, area)` and lay out content inside the returned
rectangle. Top bands stack first; left bands occupy the remaining height.
Left bands wrap into additional columns when their items exceed that height.
Window resize automatically updates docked bands. Custom content layout should
use the returned rectangle to keep content clear of the bands.

Application chrome uses the same bands through
`app_chrome_add_toolbar(chrome, TOOLBAR_DOCK_LEFT, proc)`. It owns the top and
left toolbars, paints only its bands, and passes workspace hit tests through to
document windows. Pass a NULL menu procedure to `create_app_chrome` when the
shell supplies the application menu. ImageEditor uses this arrangement in both
standalone and GEM builds; its tools are no longer a floating palette window.

## Docking and merged captions

Toolbars use the shared [workspace docking manager](docking.md). Existing
`create_docked_toolbar` callers automatically participate. Add
`WINDOW_TITLETOOLBAR` alongside `WINDOW_TOOLBAR` to share a single row between the
app items and the window controls. The merged row shows no window title. Configure
app items with `tbSetItems` as usual; the framework appends the window controls. Locate items by command ID,
because framework items also occupy positions in the toolbar state.

### Control sizes

A button item takes a `CONTROL_SIZE_*` style (`control-size="mini|small|regular|large"`
in `.orion`), the same intrinsic sizes dialog controls use. The toolbar's button size
(`tbSetButtonSize`, else the theme's `toolbar_button_size`) is the regular cell;
`theme_control_extent()` scales it by the theme's `control_size_pct`, which follows
AppKit's round bezels (regular 21, small 18, mini 15, large 28). The row keeps the
regular height: a bigger cell spreads into the band padding and stops the theme's
`toolbar_large_inset` short of the band edges, and every item is centred on the row's
axis. An iTunes-style transport is three plain buttons: `control-size="small"` rewind and
forward around a `control-size="large"` play that fills the band. Nothing is hand-sized;
the glyph scales with the cell.

### Coloured procedural buttons

Set `TOOLBAR_STYLE_PLASTIC` with `tbSetStyle` to draw toolbar buttons as procedural
plastic with solid white SVG icons. Set `toolbar_item_t.color` to a packed
`0xAABBGGRR` face colour; zero uses the theme accent. Update it without recreating
embedded controls with `send_message(toolbar, tbSetItemColor, command_id, &color)`;
the message copies the `uint32_t` immediately. A null colour pointer restores the
theme accent. Unavailable IDs and non-button items are rejected with diagnostics.
Window caption controls retain the active theme's normal drawing.
