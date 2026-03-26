---
layout: default
title: Window System
nav_order: 4
---

# Window System

## Window Structure

```c
typedef struct window_s {
  rect_t      frame;          // content-area position and size (logical px)
  char        title[64];
  flags_t     flags;          // WINDOW_NOTITLE | WINDOW_TOOLBAR | …
  winproc_t   proc;           // window procedure
  void       *userdata;       // application state
  window_t   *parent;         // NULL for top-level windows
  window_t   *children;       // linked list of child windows
  window_t   *next;           // sibling / global list link
  bool        visible;
  bool        disabled;
  int         scroll[2];      // scroll offsets [x, y]
  /* … */
} window_t;
```

## Creating Windows

```c
window_t *create_window(
    const char *title,
    flags_t     flags,
    rect_t const *frame,     // MAKERECT(x, y, w, h)
    window_t   *parent,      // NULL = top-level
    winproc_t   proc,
    void       *lparam       // forwarded to kWindowMessageCreate
);
```

`frame` coordinates are in **logical pixels** (screen pixels ÷ `UI_WINDOW_SCALE`).
For top-level windows `frame.x/y` is the absolute screen position.
For child windows `frame.x/y` is relative to the **parent's content area**.

## Window Flags

| Flag | Meaning |
|---|---|
| `WINDOW_NOTITLE` | No title bar |
| `WINDOW_NOFILL` | Don't fill background |
| `WINDOW_NORESIZE` | Disable resize handle |
| `WINDOW_TOOLBAR` | Add toolbar strip above content area |
| `WINDOW_STATUSBAR` | Add status bar below content area |
| `WINDOW_VSCROLL` | Enable vertical scroll |
| `WINDOW_HSCROLL` | Enable horizontal scroll |
| `WINDOW_ALWAYSONTOP` | Always rendered / hit-tested above regular windows |
| `WINDOW_ALWAYSINBACK` | Never raised by `move_to_top` |
| `WINDOW_DIALOG` | Modal dialog (closed by `end_dialog`) |
| `WINDOW_HIDDEN` | Starts hidden |
| `WINDOW_TRANSPARENT` | Skip background fill in non-client paint |
| `WINDOW_NOTRAYBUTTON` | Don't appear in tray / taskbar |

## Window Procedure

```c
typedef int (*winproc_t)(window_t *win, uint32_t msg,
                         uint32_t wparam, void *lparam);
```

Return `true` (non-zero) if the message was handled; `false` to allow
default processing or child dispatch.

```c
static result_t my_proc(window_t *win, uint32_t msg,
                        uint32_t wparam, void *lparam) {
  switch (msg) {
    case kWindowMessageCreate:
      /* one-time initialisation; lparam = value passed to create_window */
      return true;
    case kWindowMessagePaint:
      fill_rect(0xff202020, 0, 0, win->frame.w, win->frame.h);
      return true;
    case kWindowMessageDestroy:
      free(win->userdata);
      return true;
    default:
      return false;
  }
}
```

## Lifecycle

```c
// Create
window_t *win = create_window("Title", 0, MAKERECT(100,100,400,300),
                               NULL, my_proc, NULL);
show_window(win, true);

// Destroy (sends kWindowMessageDestroy, frees children, then frees win)
destroy_window(win);
```

## Dialogs

```c
// Show a modal dialog; blocks until end_dialog() is called
uint32_t result = show_dialog("Open File",
                              MAKERECT(50, 50, 400, 300),
                              parent_win, dialog_proc, init_data);

// Inside dialog_proc – close with a result code
end_dialog(win, 1);  // 0 = cancel, non-zero = accept
```

## Mouse Coordinate Notes

For **top-level** windows, `LOWORD(wparam)` and `HIWORD(wparam)` in mouse
messages are **window-local** (0,0 = top-left of content area).

For **child** windows found via `kWindowMessageHitTest`, the coordinates are
absolute-logical.  Convert to child-local with:

```c
window_t *root = get_root_window(win);
int lx = (int16_t)LOWORD(wparam) - root->frame.x - win->frame.x;
int ly = (int16_t)HIWORD(wparam) - root->frame.y - win->frame.y;
```

## Useful Helpers

```c
void  show_window(window_t *win, bool visible);
void  move_window(window_t *win, int x, int y);
void  resize_window(window_t *win, int w, int h);
void  invalidate_window(window_t *win);   // request repaint
void  set_focus(window_t *win);
void  set_capture(window_t *win);         // capture all mouse events
bool  is_window(window_t *win);           // safe existence check
window_t *get_root_window(window_t *win);

// Allocate and zero window userdata (freed automatically on destroy)
void *allocate_window_data(window_t *win, size_t size);
```
