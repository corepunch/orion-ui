---
layout: default
title: Messages & Events
nav_order: 6
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
| `kWindowMessageCreate` | Window just created | `lparam` = value from `create_window` |
| `kWindowMessageDestroy` | Window about to be freed | – |
| `kWindowMessagePaint` | Repaint requested | – |
| `kWindowMessageNonClientPaint` | Non-client area repaint | – |
| `kWindowMessageSetFocus` | Window gains focus | – |
| `kWindowMessageKillFocus` | Window loses focus | – |
| `kWindowMessageLeftButtonDown` | LMB pressed | `LOWORD`=x, `HIWORD`=y (window-local) |
| `kWindowMessageLeftButtonUp` | LMB released | same |
| `kWindowMessageRightButtonDown` | RMB pressed | same |
| `kWindowMessageMouseMove` | Mouse moved | same |
| `kWindowMessageMouseLeave` | Mouse left window | – |
| `kWindowMessageKeyDown` | Key pressed | SDL scancode |
| `kWindowMessageKeyUp` | Key released | SDL scancode |
| `kWindowMessageTextInput` | Text character input | `lparam` = `const char *` UTF-8 |
| `kWindowMessageWheel` | Mouse wheel | `LOWORD`=dx, `HIWORD`=dy |
| `kWindowMessageCommand` | Control notification | `LOWORD`=id, `HIWORD`=notification code |
| `kWindowMessageResize` | Window resized / moved | – |
| `kWindowMessageStatusBar` | Update status bar text | `lparam` = `(void *)const char *` |
| `kWindowMessageHitTest` | Find child at point | `lparam` = `window_t **` |
| `kWindowMessageRefreshStencil` | Stencil buffer needs update | – |
| `kWindowMessageUser` (1000) | First app-defined message | – |

## Control Notification Codes

Sent to the **root window** via `kWindowMessageCommand`:

| Code | Control | Meaning |
|---|---|---|
| `kButtonNotificationClicked` | Button / Checkbox | Button was clicked |
| `kEditNotificationUpdate` | Text edit | Text content changed |
| `kComboBoxNotificationSelectionChange` | Combobox | Selected item changed |
| `CVN_SELCHANGE` | ColumnView | Single-click selection change |
| `CVN_DBLCLK` | ColumnView | Double-click on item |
| `kMenuBarNotificationItemClick` | MenuBar | Menu item selected |

Decoding in the parent window procedure:

```c
case kWindowMessageCommand: {
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
// Set up toolbar buttons once in kWindowMessageCreate
toolbar_button_t buttons[] = {
    { .icon = icon16_folder, .ident = ID_OPEN },
    { .icon = icon16_save,   .ident = ID_SAVE },
};
send_message(win, kToolBarMessageAddButtons,
             sizeof(buttons)/sizeof(buttons[0]), buttons);

// In window proc – receive toolbar click
case kToolBarMessageButtonClick:
    switch (wparam) {  // ident
        case ID_OPEN: open_file(); break;
        case ID_SAVE: save_file(); break;
    }
    return true;
```

## Keyboard Input

```c
case kWindowMessageKeyDown:
    switch (wparam) {
        case SDL_SCANCODE_ESCAPE: running = false; break;
        case SDL_SCANCODE_S:     save_file();     break;
    }
    return true;

case kWindowMessageTextInput:
    append_char(win, (const char *)lparam);
    return true;
```

## Event Loop

```c
extern bool running;

ui_event_t e;
while (running) {
    while (get_message(&e))   // drain SDL event queue
        dispatch_message(&e);
    repost_messages(-1);        // process posted (async) messages + repaint
}
```

## Message Hooks

Register a global hook to intercept any message before it reaches its target
window:

```c
void register_hook(uint32_t msg, winhook_func_t func, void *userdata);
void unregister_hook(uint32_t msg, winhook_func_t func);
```
