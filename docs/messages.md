---
layout: default
title: Messages & Events
nav_order: 7
---

# Messages & Events

## Sending Messages

```c
// Synchronous: calls win->proc immediately; returns proc's return value
int  send_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

// Asynchronous: queued, delivered on the next repost_messages(-1) call
void post_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
```

## Standard Window Messages

| Constant | When sent | wparam / lparam |
|---|---|---|
| `evCreate` | Window just created | `lparam` = value from `create_window` |
| `evDestroy` | Window about to be freed | – |
| `evPaint` | Repaint requested | – |
| `evNCPaint` | Non-client area repaint | – |
| `evSetFocus` | Window gains focus | – |
| `evKillFocus` | Window loses focus | – |
| `evLeftButtonDown` | LMB pressed | `LOWORD`=x, `HIWORD`=y (window-local) |
| `evLeftButtonUp` | LMB released | same |
| `evRightButtonDown` | RMB pressed | same |
| `evMouseMove` | Mouse moved | same |
| `evMouseLeave` | Mouse left window | – |
| `evKeyDown` | Key pressed | Orion `AX_KEY_*` value in `wparam` |
| `evKeyUp` | Key released | Orion `AX_KEY_*` value in `wparam` |
| `evTextInput` | Text character input | `lparam` = `const char *` UTF-8 |
| `evWheel` | Mouse wheel | `wparam` = MAKEDWORD(x,y) mouse pos; `lparam` = MAKEDWORD(dx,dy) scroll deltas |
| `evCommand` | Control notification | `LOWORD`=id, `HIWORD`=notification code |
| `evResize` | Window resized / moved | – |
| `evStatusBar` | Update status bar text | `lparam` = `(void *)const char *` |
| `evHScroll` | Built-in H scrollbar moved | `wparam` = new scroll position |
| `evVScroll` | Built-in V scrollbar moved | `wparam` = new scroll position |
| `evHitTest` | Find child at point | `lparam` = `window_t **` |
| `evRefreshStencil` | Stencil buffer needs update | – |
| `evUser` (1000) | First app-defined message | – |

## Control Notification Codes

Sent to the **root window** via `evCommand`:

| Code | Control | Meaning |
|---|---|---|
| `btnClicked` | Button / Checkbox | Button was clicked |
| `edUpdate` | Text edit | Text content changed |
| `cbSelectionChange` | Combobox | Selected item changed |
| `RVN_SELCHANGE` | ColumnView | Single-click selection change |
| `RVN_DBLCLK` | ColumnView | Double-click on item |
| `RVN_ITEMCHECK` | ReportView / TableView | Row checkbox state changed |
| `kMenuBarNotificationItemClick` | MenuBar | Menu item selected |

Decoding in the parent window procedure:

```c
case evCommand: {
    uint16_t notif = HIWORD(wparam);  // notification code
    uint16_t id    = LOWORD(wparam);  // item ID
    window_t *ctrl = (window_t *)lparam;

    if (notif == btnClicked) {
        if (strcmp(ctrl->title, "OK") == 0) { /* … */ }
    }
    if (notif == kMenuBarNotificationItemClick) {
        switch (id) {
            case MY_MENU_OPEN:  open_file(); break;
            case MY_MENU_QUIT:  ui_request_quit(); break;
        }
    }
    return true;
}
```

## Toolbar Button Clicks

```c
// Set up toolbar buttons once in evCreate
toolbar_button_t buttons[] = {
    { .icon = icon16_folder, .ident = ID_OPEN, .flags = 0 },
    { .icon = icon16_save,   .ident = ID_SAVE, .flags = 0 },
};
send_message(win, tbAddButtons,
             sizeof(buttons)/sizeof(buttons[0]), buttons);

// In window proc – receive toolbar click
case evCommand:
    if (HIWORD(wparam) != btnClicked) break;
    switch (LOWORD(wparam)) {  // ident
        case ID_OPEN: open_file(); break;
        case ID_SAVE: save_file(); break;
    }
    return true;
```

## Mouse Wheel Handling

The `evWheel` message is sent when the user scrolls the mouse wheel. The message uses a WinAPI-style convention:

- **wparam**: Mouse position as `MAKEDWORD(x, y)` in window-local scaled coordinates
- **lparam**: Scroll deltas as `MAKEDWORD(dx, dy)` (cast from `void*`)
  - `dx`: Horizontal scroll amount (positive = scroll right, negative = scroll left)
  - `dy`: Vertical scroll amount (positive = wheel up, negative = wheel down)
  - Values are already multiplied by `SCROLL_SENSITIVITY` (3)

**Extracting scroll deltas from lparam:**

```c
case evWheel: {
    int dx = (int16_t)LOWORD((uintptr_t)lparam);
    int dy = (int16_t)HIWORD((uintptr_t)lparam);
    
    // Horizontal: positive dx scrolls right
    scroll_x += dx;
    // Vertical: positive dy = wheel up, so subtract for natural content movement
    scroll_y -= dy;
    
    // Clamp to valid range
    if (scroll_y < 0) scroll_y = 0;
    if (scroll_y > max_scroll) scroll_y = max_scroll;
    
    invalidate_window(win);
    return true;
}
```

**Built-in scrollbar integration:**

Windows with `WINDOW_HSCROLL` or `WINDOW_VSCROLL` flags automatically handle wheel scrolling via their built-in scrollbars. The framework extracts deltas from lparam and calls `sb_try_scroll()` to update the scrollbar position and fire `evHScroll`/`evVScroll` notifications.

If a window cannot handle wheel events (no scrollbars or not enabled), the event bubbles to the parent window, following WinAPI behavior.

## Keyboard Input

Use accelerators for application shortcuts so keyboard, menu, toolbar, and
context-menu actions share the same `evCommand` path. Handle `evKeyDown`
directly only for focused control behavior or raw input.

```c
case evKeyDown:
    switch (wparam) {
        case AX_KEY_ESCAPE: cancel_operation(); break;
    }
    return true;

case evTextInput:
    append_char(win, (const char *)lparam);
    return true;
```

## Event Loop

```c
ui_event_t e;
while (ui_is_running()) {
    while (get_message(&e))   // blocks until event, then drains queue
        dispatch_message(&e);
    repost_messages();        // process posted messages and repaint
}
```

`get_message()` waits through the native Platform event backend when the queue
is empty, so the process yields the CPU instead of spinning. Calls to
`post_message()` (including `invalidate_window()`) wake the event loop so the
internal message queue is processed promptly.

## Message Hooks

Register a global hook to intercept any message before it reaches its target
window:

```c
void register_window_hook(uint32_t msg, winhook_func_t func, void *userdata);
void deregister_window_hook(uint32_t msg, winhook_func_t func, void *userdata);
```
