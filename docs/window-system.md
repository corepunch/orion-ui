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
    void       *lparam       // forwarded to evCreate
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
| `WINDOW_VSCROLL` | Enable vertical built-in scrollbar |
| `WINDOW_HSCROLL` | Enable horizontal built-in scrollbar |
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
    case evCreate:
      /* one-time initialisation; lparam = value passed to create_window */
      return true;
    case evPaint:
      fill_rect(0xff202020, 0, 0, win->frame.w, win->frame.h);
      return true;
    case evDestroy:
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

// Destroy (sends evDestroy, frees children, then frees win)
destroy_window(win);
```

## Dialogs

For the complete dialog API — including modal dialogs, declarative forms, and
the Dialog Data Exchange (DDX) helpers (`dialog_push` / `dialog_pull`) — see
[Dialogs & DDX](dialogs).

Modal dialogs follow the same pattern as Win32 `DialogBoxParam` / `EndDialog`:
`show_dialog` runs a **nested message loop** that blocks the caller until
`end_dialog` is called, then returns the numeric result code.

### API

```c
// Create and display a modal dialog.
// Blocks until end_dialog() closes the dialog.
// Returns the code passed to end_dialog() (0 on X-button close).
uint32_t show_dialog(
    const char  *title,    // title bar text
    rect_t const *frame,   // MAKERECT(x, y, w, h) – logical pixels
    window_t    *parent,   // owner window, or NULL
    winproc_t    proc,     // dialog window procedure
    void        *param     // forwarded as lparam to evCreate
);

// Close the dialog and return a result code to show_dialog's caller.
// 'win' can be the dialog window itself or any child (e.g. a button).
// The result code 0 conventionally means "cancelled".
void end_dialog(window_t *win, uint32_t code);
```

### How it works

1. `show_dialog` creates a top-level `WINDOW_DIALOG` window, calls
   `enable_window(parent, false)` to block mouse/keyboard input to the owner,
   then enters an inner `get_message` / `dispatch_message` loop.
2. The loop runs until either `end_dialog` destroys the dialog window **or**
   `running` becomes `false` (application quit).
3. `end_dialog` writes the code into the `uint32_t` pointed to by
   `dlg->userdata2`, then calls `destroy_window`.  Once the dialog is gone
   `is_window(dlg)` returns false and the loop exits.
4. `show_dialog` re-enables the parent and returns the recorded result code.

Each `show_dialog` call stores its result on its **own stack frame**, so
nested dialogs are fully reentrant — closing an inner dialog never corrupts
the outer dialog's result.

### Minimal example

```c
typedef struct { char path[512]; } open_state_t;

static result_t open_proc(window_t *win, uint32_t msg,
                           uint32_t wparam, void *lparam) {
  open_state_t *s = (open_state_t *)win->userdata;
  switch (msg) {
    case evCreate:
      win->userdata = lparam;  // open_state_t * passed via param
      // Create child controls
      create_window("OK", 0, MAKERECT(10, 60, 60, BUTTON_HEIGHT),
                    win, win_button, NULL);
      create_window("Cancel", 0, MAKERECT(80, 60, 60, BUTTON_HEIGHT),
                    win, win_button, NULL);
      return true;

    case evCommand:
      if (HIWORD(wparam) == btnClicked) {
        window_t *btn = (window_t *)lparam;
        if (strcmp(btn->title, "OK") == 0) {
          strncpy(s->path, "chosen.png", sizeof(s->path) - 1);
          end_dialog(win, 1);   // 1 = accepted
        } else {
          end_dialog(win, 0);   // 0 = cancelled
        }
      }
      return true;

    default:
      return false;
  }
}

// Caller
open_state_t state = {0};
uint32_t ok = show_dialog("Open File",
                           MAKERECT(100, 80, 200, 100),
                           my_win, open_proc, &state);
if (ok)
  load_file(state.path);
```

### Conventions

| Result code | Meaning |
|---|---|
| `0` | Cancelled (user pressed Cancel or closed the X button) |
| `1` | Accepted (user pressed OK / Open / Save) |
| Any other | Application-defined (e.g. multi-button confirmation dialogs) |

### Real-world usage

The image-editor's file picker (`examples/imageeditor/filepicker.c`) shows a
complete dialog with a `win_filelist` browser, a filename edit box, and
Open/Save/Cancel buttons — all driven by `show_dialog` / `end_dialog`.

```c
// Invoked from the File > Open and File > Save As menu handlers:
static bool show_file_picker(window_t *parent, bool save_mode,
                              char *out_path, size_t out_sz) {
  picker_state_t ps = {0};
  ps.mode = save_mode ? PICKER_SAVE : PICKER_OPEN;

  uint32_t result = show_dialog(save_mode ? "Save PNG" : "Open PNG",
      MAKERECT(50, 50, PICKER_WIN_W, PICKER_WIN_H),
      parent, picker_proc, &ps);

  if (result && ps.accepted) {
    strncpy(out_path, ps.result, out_sz - 1);
    return true;
  }
  return false;
}
```

## Mouse Coordinate Notes

For **top-level** windows, `LOWORD(wparam)` and `HIWORD(wparam)` in mouse
messages are **window-local** (0,0 = top-left of content area).

For **child** windows found via `evHitTest`, the coordinates are
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
rect_t center_window_rect(rect_t frame_rect, window_t const *owner);

// Allocate and zero window userdata (freed automatically on destroy)
void *allocate_window_data(window_t *win, size_t size);
```

`center_window_rect()` centers a full window-frame rect inside an owner window's
frame, or on the screen when `owner == NULL`. The input rect is treated as the
actual top-left/titlebar origin and full frame size, not as a client rect.
This is the preferred helper for centering modeless windows and any custom
window created with `create_window()` / `create_window_from_form()`.

```c
rect_t wr = {0, 0, 320, 120};
adjust_window_rect(&wr, WINDOW_DIALOG | WINDOW_NORESIZE);
wr = center_window_rect(wr, owner_win);

window_t *dlg = create_window("Options",
  WINDOW_DIALOG | WINDOW_NORESIZE,
  &wr, NULL, options_proc, 0, state);
```

## Built-in Scrollbars

Set `WINDOW_HSCROLL` and/or `WINDOW_VSCROLL` at creation time and call
`set_scroll_info()` to drive the bars.  The framework paints them and sends
`evHScroll` / `evVScroll` when the user moves a thumb.

See [Scrollbars](scrollbars) for the full API and usage examples.
