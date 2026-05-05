---
layout: default
title: Toolbox
nav_order: 12
---

# Toolbox (`win_toolbox`)

The **toolbox** is a floating palette of icon buttons arranged in a fixed
**2-column grid** — exactly like the tool panels in Photoshop, Visual Basic 3,
or MS Paint.  It is the correct control to use whenever you need a dockable /
floating tool-selector panel.

> **Do not use `WINDOW_TOOLBAR` for a toolbox.**
> `WINDOW_TOOLBAR` is a *horizontal* band at the top of a window (think "Save /
> Open / Undo" bar).  A toolbox is a *vertical floating panel* of tool icons.
> The two controls serve different purposes and must not be confused.

---

## When to use a toolbox vs. a toolbar

| | `WINDOW_TOOLBAR` | `win_toolbox` |
|---|---|---|
| Layout | Single horizontal row (wraps to more rows) | Always 2 columns, N rows |
| Lives in | **Non-client area** of a parent window | **Client area** of a standalone floating window |
| Typical use | File, Edit, View action buttons | Tool palette (Select, Pencil, Brush …) |
| Examples | taskmanager, filepicker toolbars | imageeditor, formeditor |

---

## Quick-start

```c
#include "ui.h"           // includes commctl/commctl.h → win_toolbox

// 1. Define your items.
//    icon  = strip tile index (0-based), or a sysicon_* value (>= SYSICON_BASE)
//    ident = command ID sent when the button is clicked
static const toolbox_item_t kMyTools[] = {
    { ID_TOOL_SELECT, 0 },
    { ID_TOOL_PENCIL, 1 },
    { ID_TOOL_BRUSH,  2 },
    { ID_TOOL_ERASER, 3 },
    { ID_TOOL_FILL,   4 },
    { ID_TOOL_ZOOM,   5 },
};
#define MY_TOOL_COUNT  (sizeof(kMyTools) / sizeof(kMyTools[0]))

// 2. Create the floating window.
//    Width  = TOOLBOX_COLS * TOOLBOX_BTN_SIZE  (= 2 * 22 = 44 px by default)
//    Height = TITLEBAR_HEIGHT + ceil(n/2) * TOOLBOX_BTN_SIZE
int rows = (MY_TOOL_COUNT + TOOLBOX_COLS - 1) / TOOLBOX_COLS;
window_t *tool_win = create_window(
    "Tools",
    WINDOW_NORESIZE | WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON,
    MAKERECT(4, 40,
             TOOLBOX_COLS * TOOLBOX_BTN_SIZE,
             TITLEBAR_HEIGHT + rows * TOOLBOX_BTN_SIZE),
    NULL, my_toolbox_proc, hinstance, NULL);
```

---

## Window procedure wrapper

Every app that uses `win_toolbox` must provide a **wrapper proc** that handles
at minimum the `bxClicked` command.  All other messages are
forwarded to `win_toolbox`:

```c
static result_t my_toolbox_proc(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      // 1. Let win_toolbox initialise its internal state first.
      win_toolbox(win, msg, wparam, lparam);

      // 2. Load a PNG sprite sheet (square tiles; wparam = tile size in px).
      char path[512];
      snprintf(path, sizeof(path), "%s/../share/imageeditor/image-editor.png",
               ui_get_exe_dir());
      send_message(win, bxLoadStrip, 24 /* tile px */, path);

      // 3. Set items and mark the default active tool.
      send_message(win, bxSetItems, MY_TOOL_COUNT, kMyTools);
      send_message(win, bxSetActiveItem, ID_TOOL_SELECT, NULL);
      return true;
    }

    case evCommand:
      if (HIWORD(wparam) == bxClicked) {
        int tool_id = (int)(int16_t)LOWORD(wparam);
        // Forward to the app's root window / menubar.
        if (app->menubar_win)
          send_message(app->menubar_win, evCommand,
                       MAKEDWORD((uint16_t)tool_id, btnClicked),
                       lparam);
        return true;
      }
      return false;

    default:
      return win_toolbox(win, msg, wparam, lparam);
  }
}
```

`win_toolbox` handles all rendering, hit-testing, pressed/active state, and
lifecycle automatically.  You only need to intercept the messages you care about.

---

## Using system icons

If you don't have a custom sprite sheet, use `sysicon_*` values from
`user/icons.h`.  Any `icon >= SYSICON_BASE` is drawn from the built-in 16x16
icon sheet — **no `bxLoadStrip` call needed**:

```c
static const toolbox_item_t kSysTools[] = {
    { ID_TOOL_SELECT,  sysicon_cursor   },
    { ID_TOOL_PENCIL,  sysicon_pen      },
    { ID_TOOL_BRUSH,   sysicon_brush    },
    { ID_TOOL_FILL,    sysicon_bucket   },
};
send_message(win, bxSetItems, 4, kSysTools);
```

---

## Custom button size

The default button size is `TOOLBOX_BTN_SIZE` (= `TB_SPACING` = 22 px), which
fits a 16x16 icon with 3 px of margin on the left/right and 3 px on top/bottom
(total button interior = 16 + 6 = 22 px).

If your icons are larger (e.g., 21 px), call `bxSetButtonSize`
**before** `bxSetItems` so the grid height is computed correctly:

```c
case evCreate: {
    win_toolbox(win, msg, wparam, lparam);
    send_message(win, bxSetButtonSize, 26, NULL); // 21px icon + margin
    send_message(win, bxLoadStrip, 21, path);
    send_message(win, bxSetItems, count, items);
    return true;
}
```

When using a custom button size, compute the window height accordingly:

```c
#define MY_BTN_SIZE 26
int rows = (count + TOOLBOX_COLS - 1) / TOOLBOX_COLS;
int win_h = TITLEBAR_HEIGHT + rows * MY_BTN_SIZE;
```

---

## Optional extra content below the grid

Applications can paint **additional content** (colour swatches, property
panels, etc.) below the button grid by wrapping `evPaint` and
`evLeftButtonDown`:

```c
#define EXTRA_H  50  // height of your custom content area

// When creating the window, add EXTRA_H to the client height:
//   win_h = TITLEBAR_HEIGHT + rows * TOOLBOX_BTN_SIZE + EXTRA_H

case evPaint: {
    // 1. Let win_toolbox fill the background and draw the button grid.
    win_toolbox(win, msg, wparam, lparam);

    // 2. Paint your content at y = grid_bottom.
    int gy = toolbox_grid_height(win);   // y where the grid ends
    fill_rect(get_sys_color(brWindowDarkBg), 0, gy, win->frame.w, EXTRA_H);
    draw_text_small("FG", 2, gy + 4, get_sys_color(brTextDisabled));
    // ... draw swatches, sliders, etc.
    return true;
}

case evLeftButtonDown: {
    int my = (int)(int16_t)HIWORD(wparam);
    int gy = toolbox_grid_height(win);
    if (my >= gy) {
        // Handle click in your extra content area.
        handle_swatch_click((int)(int16_t)LOWORD(wparam), my - gy);
        return true;
    }
    return win_toolbox(win, msg, wparam, lparam);  // pass grid clicks to toolbox
}
```

`toolbox_grid_height(win)` returns the pixel height consumed by the button
grid (= `ceil(n/2) * btn_size`).  It is declared in `commctl/commctl.h`.

---

## Message reference

| Message | wparam | lparam | Effect |
|---|---|---|---|
| `bxSetItems` | count | `toolbox_item_t[]` | Replace item list |
| `bxSetActiveItem` | ident (or -1) | — | Mark active button |
| `bxSetStrip` | 0 | `bitmap_strip_t*` or NULL | Set external sprite strip |
| `bxLoadStrip` | tile_size_px | `const char*` path | Load PNG and own the texture |
| `bxSetButtonSize` | size_px (0=default) | — | Override button size |

### `toolbox_item_t`

```c
typedef struct {
    int ident;  // command ID echoed in bxClicked
    int icon;   // strip tile index, or sysicon_* value (>= SYSICON_BASE)
} toolbox_item_t;
```

### Notification

When the user clicks a button the toolbox sends `evCommand` to
itself:

```c
wparam = MAKEDWORD(ident, bxClicked);
lparam = win;  // the toolbox window
```

Intercept this in your wrapper proc's `evCommand` case.

---

## Helper function

```c
// Returns the height in client pixels occupied by the button grid.
// Useful for positioning extra content below the grid.
int toolbox_grid_height(window_t *win);
```

---

## Layout constants

```c
#define TOOLBOX_COLS      2            // always 2 columns
#define TOOLBOX_BTN_SIZE  TB_SPACING   // default square button = 22 px
```

---

## Real-world examples

- **`examples/imageeditor/win_toolpalette.c`** — wraps `win_toolbox` and adds
  colour swatches + shape-mode toggles below the grid.
- **`examples/formeditor/win_toolpalette.c`** — uses `win_toolbox` with a
  custom 26 px button size for 21-px VB3-style icons.
