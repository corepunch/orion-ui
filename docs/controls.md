---
layout: default
title: Controls
nav_order: 8
---

# Controls

All controls are window procedures registered with `commctl.h`.  Create them
as child windows of a parent; notifications are sent to the **root window**
via `kWindowMessageCommand`.

## Scrollbar

See [Scrollbars](scrollbars) for the complete scrollbar documentation covering both **built-in window scrollbars** (`WINDOW_HSCROLL` / `WINDOW_VSCROLL` + `set_scroll_info()`) and the **standalone `win_scrollbar` control**.

```c
// Standalone scrollbar – orientation set via lparam: 0=horizontal, 1=vertical
window_t *vsb = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL,
    MAKERECT(w - 8, 0, 8, h - 8),
    parent, win_scrollbar, (void *)1 /* SB_VERT */);

scrollbar_info_t info = { .min_val = 0, .max_val = 200, .page = 50, .pos = 0 };
send_message(vsb, kScrollBarMessageSetInfo, 0, &info);

// Receive position-change notification in the parent proc:
case kWindowMessageCommand:
    if (HIWORD(wparam) == kScrollBarNotificationChanged) {
        int new_pos = (int)(intptr_t)lparam;
    }
```

**Important:** Do **not** set `WINDOW_HSCROLL` or `WINDOW_VSCROLL` on a
`win_scrollbar` window.  Pass `(void *)0` (horizontal) or `(void *)1`
(vertical) as `lparam` to `create_window`.  The flags `WINDOW_HSCROLL` /
`WINDOW_VSCROLL` are reserved for parent windows that want the framework's
built-in scrollbars.

## Button

```c
window_t *btn = create_window("Click Me", 0,
    MAKERECT(10, 10, 80, BUTTON_HEIGHT),
    parent, win_button, NULL);

// Receive click in parent's proc:
case kWindowMessageCommand:
    if (HIWORD(wparam) == kButtonNotificationClicked)
        handle_click((window_t *)lparam);
```

## Checkbox

```c
window_t *chk = create_window("Enable fog", 0,
    MAKERECT(10, 30, 120, BUTTON_HEIGHT),
    parent, win_checkbox, NULL);

// Query / set checked state
send_message(chk, kButtonMessageSetCheck, kButtonStateChecked, NULL);
int state = send_message(chk, kButtonMessageGetCheck, 0, NULL);
// state == kButtonStateChecked or kButtonStateUnchecked
```

## Text Edit

```c
window_t *ed = create_window("", WINDOW_NOTITLE,
    MAKERECT(10, 50, 200, CONTROL_HEIGHT),
    parent, win_textedit, NULL);

// Read current text
const char *text = ed->title;

// Notification when text changes
case kWindowMessageCommand:
    if (HIWORD(wparam) == kEditNotificationUpdate)
        on_text_changed(((window_t *)lparam)->title);
```

## Label

```c
create_window("Name:", WINDOW_NOTITLE,
    MAKERECT(10, 10, 60, CONTROL_HEIGHT),
    parent, win_label, NULL);
```

## Combobox

```c
window_t *cb = create_window("", 0,
    MAKERECT(10, 70, 150, BUTTON_HEIGHT),
    parent, win_combobox, NULL);

send_message(cb, kComboBoxMessageAddString, 0, (void *)"Option A");
send_message(cb, kComboBoxMessageAddString, 0, (void *)"Option B");

int sel = send_message(cb, kComboBoxMessageGetCurrentSelection, 0, NULL);

// Selection-change notification:
case kWindowMessageCommand:
    if (HIWORD(wparam) == kComboBoxNotificationSelectionChange) { … }
```

## ColumnView

A multi-column item list with single-click selection and double-click
activation.  Used by the file manager and file-picker dialog.

```c
#include "commctl/columnview.h"

window_t *cv = create_window("", WINDOW_NOTITLE | WINDOW_VSCROLL,
    MAKERECT(0, 0, 300, 200),
    parent, win_reportview, NULL);

// Add items
reportview_item_t item = {
    .text  = "Documents",
    .icon  = icon8_editor_helmet,
    .color = COLOR_TEXT_NORMAL,
};
send_message(cv, RVM_ADDITEM, 0, &item);

// Clear all items
send_message(cv, RVM_CLEAR, 0, NULL);
// Also reset scroll after clearing
cv->scroll[0] = cv->scroll[1] = 0;

// Selection notification in root proc:
case kWindowMessageCommand:
    if (HIWORD(wparam) == RVN_SELCHANGE || HIWORD(wparam) == RVN_DBLCLK) {
        reportview_item_t *it = (reportview_item_t *)lparam;
        printf("Selected: %s\n", it->text);
    }
```

### ColumnView Messages

| Message | wparam | lparam | Returns |
|---|---|---|---|
| `RVM_ADDITEM` | – | `reportview_item_t *` | index |
| `RVM_DELETEITEM` | index | – | bool |
| `RVM_CLEAR` | – | – | bool |
| `RVM_GETITEMCOUNT` | – | – | count |
| `RVM_GETSELECTION` | – | – | index |
| `RVM_SETSELECTION` | index | – | bool |
| `RVM_SETCOLUMNWIDTH` | px | – | bool |
| `RVM_GETITEMDATA` | index | `reportview_item_t *` | bool |
| `RVM_SETITEMDATA` | index | `reportview_item_t *` | bool |

## Menu Bar

```c
#include "commctl/menubar.h"

static const menu_item_t kFileItems[] = {
    {"New",    ID_NEW},
    {"Open…",  ID_OPEN},
    {NULL,     0},          // separator
    {"Quit",   ID_QUIT},
};
static const menu_def_t kMenus[] = {
    {"File", kFileItems, 4},
};

// Create menu bar (ALWAYSONTOP, full screen width)
window_t *mb = create_window("",
    WINDOW_NOTITLE | WINDOW_ALWAYSONTOP | WINDOW_NORESIZE | WINDOW_NOTRAYBUTTON,
    MAKERECT(0, 0, screen_w, MENUBAR_HEIGHT),
    NULL, my_menubar_proc, NULL);
send_message(mb, kMenuBarMessageSetMenus, 1, (void *)kMenus);
show_window(mb, true);

// In my_menubar_proc – chain to win_menubar then handle selection:
static result_t my_menubar_proc(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam) {
    if (msg == kWindowMessageCommand &&
        HIWORD(wparam) == kMenuBarNotificationItemClick) {
        switch (LOWORD(wparam)) {
            case ID_NEW:  new_file();    break;
            case ID_OPEN: open_file();   break;
            case ID_QUIT: running=false; break;
        }
        return true;
    }
    return win_menubar(win, msg, wparam, lparam);
}
```

## Console

```c
// Global console overlay (toggle with F1 or backtick)
init_console();
conprintf("Hello from console\n");
draw_console();        // call each frame to draw when visible
toggle_console();
shutdown_console();
```

## Terminal (Lua)

```c
window_t *term = create_window("Lua Terminal", 0,
    MAKERECT(50, 50, 500, 300),
    NULL, win_terminal, NULL);
show_window(term, true);

// Read the Lua output buffer
const char *buf = terminal_get_buffer(term);
```
