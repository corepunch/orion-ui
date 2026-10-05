---
layout: default
title: Workspace Docking
nav_order: 6
---

# Workspace docking

A workspace owns document and utility windows. Document content uses `DOCK_FILL`;
menus, toolbars, and utility panes reserve bands at its edges. Floating panes remain
children of the same workspace. Docking changes geometry and presentation without
recreating controls, transferring ownership, or losing text, selection, or focus.
Groove uses a fill arrangement and a bottom Library pane. Pencil Test uses the same
manager through application chrome for its menu, command toolbar, tools, options,
and animation timeline.

The model borrows allowed-edge masks and frame-owned layout from
[MFC control-bar docking](https://learn.microsoft.com/en-us/cpp/mfc/docking-and-floating-toolbars?view=msvc-170),
and the child-window ownership and grippers of
[Win32 rebar bands](https://learn.microsoft.com/en-us/windows/win32/controls/rebar-controls).
Docking was an MFC framework facility; a rebar is the Win32 common control for
arranging bands. Orion keeps its existing C window/message model.

## Registering windows

Include `<orion/user/dock.h>` (also included by `<orion/ui.h>`). Create ordinary
children, then register them:

```c
window_t *library = create_window("Library",
    WINDOW_TOOLBAR | WINDOW_TITLETOOLBAR | WINDOW_NORESIZE,
    MAKERECT(0, 0, 800, 280), workspace, library_proc, instance, NULL);
dock_window(library, DOCK_BOTTOM,
    DOCK_EDGE(DOCK_TOP) | DOCK_EDGE(DOCK_BOTTOM), DOCK_RESIZABLE, 280, 100);
dock_window(document, DOCK_FILL, 0, DOCK_NOFLOAT, 0, 100);
```

`dock_window` takes an initial side, allowed-edge mask, behavior flags, preferred
extent, and minimum extent. Extents and frames use logical pixels. Fill-window
minimums reserve document space when a resizable pane consumes an edge.

| Flag | Behavior |
|---|---|
| `DOCK_RESIZABLE` | Edge splitter, caption collapse, floating resize corner |
| `DOCK_TOOLBAR` | Measures existing toolbar items and switches orientation |
| `DOCK_MENU` | Measures the menu; left/right and floating layouts stack entries vertically |
| `DOCK_NOFLOAT` | Fixed child, including the document fill slot |

Top/bottom bands are arranged before left/right bands. Registration order within
those groups is stable, independent of floating-window stacking. Fill windows take
the remaining rectangle. Hidden and floating children consume no dock space.
`dock_content_rect(host)` returns that rectangle; application chrome publishes it
to the existing maximized-document workspace mechanism. Apps must not subtract
menu, toolbar, or timeline dimensions again.

`evResize` runs docking centrally before the application procedure. A docking,
floating, collapse, visibility, or destruction change also relays `evDockChanged`
to the host after layout. Hosts that manage peer document roots can update their
workspace there. The manager does not use application userdata or own app resources.

## User interaction and programmatic changes

- Drag a toolbar/menu gripper or a pane's title/unused header space. Buttons and
  embedded editors retain their normal input behavior.
- Drop within 32 logical pixels of an allowed workspace edge to dock. The theme
  colors the proposed edge during dragging. Drop elsewhere to float internally.
- Double-click the gripper/title to float or return to the last dock edge.
- Drag an edge splitter to resize a pane, or the bottom-right corner of a floating
  resizable pane. Pointer cancellation, Escape, and focus loss cancel the gesture.
- The merged caption's restore button collapses/restores a resizable pane. Close
  hides the pane; it remains owned and can be shown through an app command.

Use `dock_set_side`, `dock_float`, and `dock_collapse` for the same transitions in
code. `dock_float` accepts a rectangle in the host's client space. Preferred float
geometry survives host resizing while the visible frame is clamped to the host.
Invalid registration, flags, extents, or edge transitions return false and log to
stderr without changing the existing registration.

Floating children paint and hit-test above docked content. Clicking one raises it
within the workspace without disturbing dock order. Destruction releases framework
metadata and cancels any active gesture; ordinary parent destruction owns every
pane in both docked and floating states.

## Merged caption toolbar

`WINDOW_TOOLBAR | WINDOW_TITLETOOLBAR` opts into one non-client row. Unlike ordinary
child controls, an opted-in child retains its caption. Declarative forms can use
`flags="toolbar titletoolbar"`.

`tbSetItems` adds a static window-title label and framework collapse/restore and
close buttons to the application's items. The same item rectangles, theme drawing,
hover/press handling, capture, and embedded-control routing serve all of them.
Framework commands are consumed before application commands. Do not depend on item
indices: use command IDs to locate application items. The title label reads the
window's current title during paint, and the framework reserves the right edge for
caption controls at narrow sizes. An existing flexible spacer is reused.

The toolbar band's origin is `toolbar_content_offset(win)`: zero when merged,
otherwise below the caption. Client content starts at `titlebar_height(win)`.
`window_screen_y`, hit testing, captured input, nested pointer/gesture delivery,
and painting apply these insets centrally. Applications never compensate for them.

## Compatibility and boundaries

`create_docked_toolbar` and `layout_docked_toolbars` remain compatibility entry
points backed by the shared manager. Compact application command bars can still
share the docked top menu row and fall back to their own band on narrow windows.
Shell-contributed menus remain shell-owned in GEM mode.

This implementation supports owned floating windows inside an Orion workspace.
Native detached OS windows, tabbed pane groups, cross-workspace transfers, and
saved layouts across application launches are not implemented. They can build on
the explicit side, ownership, and preferred-geometry state without adding app-level
pointer routing.

Regression coverage: `tests/dock_test.c`, toolbar/menu tests, Groove integration
tests, and Image Editor/Pencil Test layout and UI tests.
