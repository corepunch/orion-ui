---
layout: default
title: Window System
nav_order: 4
---

# Window System

Orion windows are framework-owned objects that receive messages through a
`winproc_t`. Applications create, show, move, resize, query, and destroy them
through the public API. Avoid depending on undocumented `window_t` internals.

## Create A Window

```c
window_t *create_window(
  const char *title,
  flags_t flags,
  irect16_t frame,
  window_t *parent,
  const char *class_name_or_winproc,
  hinstance_t hinstance,
  void *param);
```

`create_window` accepts either a registered class name or a window-procedure
symbol through its type-generic macro:

```c
window_t *main_win = create_window(
  "Document", WINDOW_STATUSBAR,
  MAKERECT(32, 32, 480, 320),
  NULL, document_proc, hinstance, document);

window_t *save_button = create_window(
  "Save", WINDOW_NOTITLE,
  MAKERECT(16, 16, 64, 19),
  main_win, "Button", hinstance, NULL);
save_button->id = ID_SAVE;

show_window(main_win, true);
```

The final `param` is delivered as `lparam` during `evCreate`. Pass the owning
application instance to top-level and app-owned child windows; standalone
system windows may use `0`.

## Frames And Client Rectangles

`frame` is the outer window rectangle in logical pixels. Top-level coordinates
are relative to the Orion screen; child coordinates are relative to the
parent's content layout.

Use `get_client_rect()` for painting, layout, scrolling, and hit-testing inside
a window. It excludes title bars, toolbars, status bars, and visible built-in
scrollbars:

```c
case evPaint: {
  irect16_t client = get_client_rect(win);
  fill_rect(get_sys_color(brWindowBg), client.x, client.y,
            client.w, client.h);
  return true;
}
```

When creating a fixed-size outer window from a desired client size, call
`adjust_window_rect()`. Use `center_window_rect()` to center an outer frame on
an owner or the screen.

## Window Procedure

```c
typedef result_t (*winproc_t)(window_t *win, uint32_t msg,
                              uint32_t wparam, void *lparam);
```

Return `true` when the message is handled and `false` when Orion should continue
normal processing.

```c
static result_t document_proc(window_t *win, uint32_t msg,
                              uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      win->userdata = lparam;
      return true;
    case evPaint:
      paint_document(win, win->userdata);
      return true;
    case evResize:
      layout_document(win);
      return true;
    case evDestroy:
      release_view_resources(win->userdata);
      return true;
    default:
      return false;
  }
}
```

Use `allocate_window_data()` when the state belongs exclusively to one window.
Persistent application/model objects should not store live `window_t *` handles
unless they are controller or view state.

## Lifecycle

1. `create_window` allocates the window and sends `evCreate`.
2. `show_window(win, true)` makes it visible.
3. `invalidate_window` schedules `evPaint`.
4. `move_window` and `resize_window` update geometry and send layout messages.
5. `destroy_window` sends `evDestroy`, destroys children, and releases the
   framework-owned object.

Pair every resource allocation performed for `evCreate` with cleanup in
`evDestroy`. Do not draw outside `evPaint`.

## Common Flags

| Flag | Meaning |
|---|---|
| `WINDOW_NOTITLE` | No title bar |
| `WINDOW_NOFILL` | Skip default client background fill |
| `WINDOW_NORESIZE` | Disable user resize |
| `WINDOW_TOOLBAR` | Add a framework-owned toolbar strip |
| `WINDOW_STATUSBAR` | Add a framework-owned status bar |
| `WINDOW_VSCROLL` | Add a built-in vertical scrollbar |
| `WINDOW_HSCROLL` | Add a built-in horizontal scrollbar |
| `WINDOW_ALWAYSONTOP` | Keep a palette or transient window above regular windows |
| `WINDOW_ALWAYSINBACK` | Keep a desktop/background window behind regular windows |
| `WINDOW_DIALOG` | Apply modal-dialog behavior |
| `WINDOW_HIDDEN` | Start hidden |
| `WINDOW_TRANSPARENT` | Preserve parent/background pixels where the window does not draw |
| `WINDOW_NOTRAYBUTTON` | Omit a top-level window from tray/task UI |
| `WINDOW_AUTO_LAYOUT` | Arrange children through their layout container |

Control-specific flags and messages are documented in [Controls](controls).

## Parent, Children, And IDs

A child belongs to its parent for lifetime, layout, hit-testing, and command
routing. Assign stable IDs to controls and retrieve them with
`get_window_item()`:

```c
window_t *name_edit = get_window_item(dialog, ID_NAME);
set_window_item_text(dialog, ID_NAME, "%s", model->name);
enable_window(get_window_item(dialog, ID_OK), model->name[0] != '\0');
```

Controls send `evCommand` to the root. `LOWORD(wparam)` is the control or
command ID, `HIWORD(wparam)` is the notification code, and `lparam` commonly
identifies the source window.

## Focus, Capture, And Z-Order

- `set_focus(win)` directs keyboard/text input to a control.
- `set_capture(win)` keeps mouse input routed to a dragging control until
  capture is released.
- `move_to_top(win)` raises a regular top-level window while respecting
  `WINDOW_ALWAYSONTOP` palettes.
- `track_mouse(win)` requests mouse-leave tracking.

The central event router owns descendant hit-testing and coordinate conversion.
A child receives pointer coordinates in its own content space, including its
scroll offset. Do not add the scroll position again in app code.

## Dialogs

Use declarative forms for dialogs containing multiple standard controls:

```c
uint32_t result = show_dialog_from_form(
  &myapp_settings_form, "Settings", parent,
  settings_proc, &settings);
```

For a custom single-surface modal window, the raw API accepts a client width and
height:

```c
uint32_t result = show_dialog(
  "Preview", 420, 300, parent, preview_proc, preview_state);
```

Close a modal window with `end_dialog(win, result)`. See
[Dialogs & DDX](dialogs) for forms, state exchange, confirmation semantics, and
database-backed dialogs.

## Layout

Static multi-control windows should normally be declared in `.orion` XML. For
runtime-created containers, measure and arrange through Orion's layout APIs:

```c
irect16_t client = get_client_rect(win);
layout_arrange_window(content, &client);
```

Use `StackView`, `GridView`, and `Column` classes rather than open-coded child
coordinate arithmetic. See [Architecture](architecture) for layout
ownership and [Controls](controls) for container behavior.

## Related Guides

- [Messages & Events](messages)
- [Controls](controls)
- [Dialogs & DDX](dialogs)
- [Scrollbars](scrollbars)
- [Toolbars](toolbars)
- [Architecture](architecture)

## Maximize And Restore

`maximize_window(win)` fills a regular root window's workspace and hides its
inner title bar, border, shadow, and resize grip. It preserves the original
frame and title/resize styles. `restore_window(win)` restores them, constraining
the frame to the current workspace so it remains reachable after display changes.
The document and its child controls retain their state throughout.

The framework initializes an `irect16_t` to the logical screen bounds and sends
`evGetWorkspaceRect` with its address in `lparam`. An app can trim this rectangle
to reserve menus, toolbars, or palettes. Maximized roots automatically recompute
these bounds after `evDisplayChange`. Call `update_maximized_window(win)` when
workspace reservations change without a display resize.

`win->maximized` exposes the current state. Set `win->maximizable = true`
to opt into the title-bar maximize button when the app exposes a restore command;
calling `maximize_window` also opts in. Route a menu or toolbar command to
`restore_window` when true and `maximize_window` otherwise. This is internal
workspace maximization; it does not change the native operating-system window.
