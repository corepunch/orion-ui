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
| `evNonClientPaint` | Non-client area repaint | – |
| `evSetFocus` | Window gains focus | – |
| `evKillFocus` | Window loses focus | – |
| `evLeftButtonDown` | LMB pressed | `LOWORD`=x, `HIWORD`=y (window-local) |
| `evLeftButtonUp` | LMB released | same |
| `evRightButtonDown` | RMB pressed | same |
| `evMouseMove` | Mouse moved | same |
| `evMouseLeave` | Mouse left window | – |
| `evKeyDown` | Key pressed | SDL scancode |
| `evKeyUp` | Key released | SDL scancode |
| `evTextInput` | Text character input | `lparam` = `const char *` UTF-8 |
| `evWheel` | Mouse wheel | `LOWORD`=dx, `HIWORD`=dy |
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
| `kButtonNotificationClicked` | Button / Checkbox | Button was clicked |
| `kEditNotificationUpdate` | Text edit | Text content changed |
| `kComboBoxNotificationSelectionChange` | Combobox | Selected item changed |
| `RVN_SELCHANGE` | ColumnView | Single-click selection change |
| `RVN_DBLCLK` | ColumnView | Double-click on item |
| `kMenuBarNotificationItemClick` | MenuBar | Menu item selected |

Decoding in the parent window procedure:

```c
case evCommand: {
    uint16_t notif = HIWORD(wparam);  // notification code
    uint16_t id    = LOWORD(wparam);  // item ID
    window_t *ctrl = (window_t *)lparam;

    if (notif == kButtonNotificationClicked) {
        if (strcmp(ctrl->title, "OK") == 0) { /* … */ }
    }
    if (notif == kMenuBarNotificationItemClick) {
        switch (id) {
            case MY_MENU_OPEN:  open_file(); break;
            case MY_MENU_QUIT:  running = false; break;
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
case tbButtonClick:
    switch (wparam) {  // ident
        case ID_OPEN: open_file(); break;
        case ID_SAVE: save_file(); break;
    }
    return true;
```

## Keyboard Input

```c
case evKeyDown:
    switch (wparam) {
        case SDL_SCANCODE_ESCAPE: running = false; break;
        case SDL_SCANCODE_S:     save_file();     break;
    }
    return true;

case evTextInput:
    append_char(win, (const char *)lparam);
    return true;
```

## Event Loop

```c
extern bool running;

ui_event_t e;
while (running) {
    while (get_message(&e))   // blocks until event, then drains queue
        dispatch_message(&e);
    repost_messages();        // process posted (async) messages + repaint
}
```

`get_message()` sleeps with `SDL_WaitEvent` when the SDL queue is empty, so
the process yields the CPU instead of spinning.  Calls to `post_message()`
(including `invalidate_window()`) push a lightweight wakeup event into the
SDL queue so that the wait is interrupted and the internal message queue is
processed promptly.

## Message Hooks

Register a global hook to intercept any message before it reaches its target
window:

```c
void register_hook(uint32_t msg, winhook_func_t func, void *userdata);
void unregister_hook(uint32_t msg, winhook_func_t func);
```
