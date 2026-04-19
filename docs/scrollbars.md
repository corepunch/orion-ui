---
layout: default
title: Scrollbars
nav_order: 9
---

# Scrollbars

Orion supports scrollbars in two ways, mirroring WinAPI's own split between
**window-owned scrollbars** (set via `WINDOW_HSCROLL` / `WINDOW_VSCROLL`) and
**standalone scrollbar controls** (`win_scrollbar`).

---

## Built-in Window Scrollbars

Add scrollbars to any window by setting `WINDOW_HSCROLL` and/or
`WINDOW_VSCROLL` at creation time.  The framework paints and drives the bars
automatically; the window procedure only needs to call `set_scroll_info()` to
describe the content range and handle `evHScroll` /
`evVScroll` when the position changes.

This is the WinAPI equivalent of creating a window with `WS_HSCROLL` /
`WS_VSCROLL` and calling `SetScrollInfo` / handling `WM_HSCROLL` /
`WM_VSCROLL`.

### Enabling built-in scrollbars

```c
window_t *view = create_window("View", WINDOW_HSCROLL | WINDOW_VSCROLL,
    MAKERECT(x, y, w, h), parent, my_view_proc, NULL);
```

### Setting the scroll range and position (`SetScrollInfo` equivalent)

```c
scroll_info_t si;
si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
si.nMin  = 0;
si.nMax  = content_width;   // total content width in pixels
si.nPage = view_width;      // visible area width
si.nPos  = pan_x;           // current scroll position

set_scroll_info(win, SB_HORZ, &si, false /* redraw */);
```

The framework **automatically shows or hides** the bar based on whether
`nPage >= (nMax - nMin)`.  No explicit `show_scroll_bar()` call is needed in
the normal case.

### Handling scroll notifications (`WM_HSCROLL` / `WM_VSCROLL` equivalent)

```c
case evHScroll:
    state->pan_x = (int)wparam;   // wparam = new scroll position
    canvas_sync_scrollbars(win, state);
    invalidate_window(win);
    return true;

case evVScroll:
    state->pan_y = (int)wparam;
    canvas_sync_scrollbars(win, state);
    invalidate_window(win);
    return true;
```

### Getting the current position (`GetScrollPos` equivalent)

```c
int pos = get_scroll_pos(win, SB_VERT);

// Or with the full struct:
scroll_info_t si = { .fMask = SIF_POS };
get_scroll_info(win, SB_VERT, &si);
int pos = si.nPos;
```

### Explicitly showing / hiding a bar

```c
show_scroll_bar(win, SB_VERT, false);   // hide
show_scroll_bar(win, SB_VERT, true);    // show
```

### Explicitly enabling / disabling a bar

`enable_scroll_bar()` controls whether a bar accepts mouse input, independently
of its visibility (which is managed automatically by `set_scroll_info()`).  A
disabled bar is still drawn but ignores clicks; its thumb is rendered in a
darker colour to signal the non-interactive state.

```c
enable_scroll_bar(win, SB_VERT, false);  // disable — bar visible but non-interactive
enable_scroll_bar(win, SB_VERT, true);   // re-enable
```

### Scrollbar interdependence

When both bars are set, adding one bar reduces the viewport in the
perpendicular axis, which can cause the other bar to appear.  Resolve this
before calling `set_scroll_info()`:

```c
static void sync_scrollbars(window_t *win, state_t *state) {
    int content_w = state->content_w;
    int content_h = state->content_h;
    int win_w     = win->frame.w;
    int win_h     = win->frame.h;

    bool need_h = content_w > win_w;
    bool need_v = content_h > win_h;
    // Adding one bar shrinks the viewport on the other axis
    if (need_h && !need_v) need_v = content_h > win_h - SCROLLBAR_WIDTH;
    if (need_v && !need_h) need_h = content_w > win_w - SCROLLBAR_WIDTH;

    int view_w = need_v ? win_w - SCROLLBAR_WIDTH : win_w;
    int view_h = need_h ? win_h - SCROLLBAR_WIDTH : win_h;

    scroll_info_t si = { .fMask = SIF_RANGE | SIF_PAGE | SIF_POS };

    si.nMin = 0; si.nMax = content_w; si.nPage = view_w; si.nPos = state->pan_x;
    set_scroll_info(win, SB_HORZ, &si, false);

    si.nMax = content_h; si.nPage = view_h; si.nPos = state->pan_y;
    set_scroll_info(win, SB_VERT, &si, false);
}
```

---

## API Reference

| Function | Description |
|---|---|
| `set_scroll_info(win, bar, &si, redraw)` | Set scroll range / page / position; auto-shows or hides the bar |
| `get_scroll_info(win, bar, &si)` | Read current scroll range / page / position |
| `get_scroll_pos(win, bar)` | Convenience: return the current scroll position |
| `show_scroll_bar(win, bar, show)` | Explicitly show or hide a bar |
| `enable_scroll_bar(win, bar, enable)` | Enable or disable a bar's interactivity |

`bar` is one of `SB_HORZ` (0), `SB_VERT` (1), or `SB_BOTH` (3).

`fMask` flags in `scroll_info_t`:

| Flag | Meaning |
|---|---|
| `SIF_RANGE` | `nMin` and `nMax` are valid |
| `SIF_PAGE` | `nPage` is valid |
| `SIF_POS` | `nPos` is valid |
| `SIF_ALL` | All fields are valid (`SIF_RANGE \| SIF_PAGE \| SIF_POS`) |

---

## Standalone Scrollbar Control (`win_scrollbar`)

For cases where a scrollbar must exist as an independent child window (e.g.
inside a custom layout), use `win_scrollbar`.  Orientation is set via
`lparam`:

```c
// Horizontal bar
window_t *hsb = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL,
    MAKERECT(0, h - 8, w - 8, 8),
    parent, win_scrollbar, (void *)0 /* SB_HORZ */);

// Vertical bar
window_t *vsb = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL,
    MAKERECT(w - 8, 0, 8, h - 8),
    parent, win_scrollbar, (void *)1 /* SB_VERT */);

// Set info
scrollbar_info_t info = { .min_val = 0, .max_val = 200, .page = 50, .pos = 0 };
send_message(hsb, sbSetInfo, 0, &info);

// Receive notification in parent proc:
case evCommand:
    if (HIWORD(wparam) == sbChanged) {
        int new_pos = (int)(intptr_t)lparam;
        // use new_pos ...
    }
```

> **Note:** `WINDOW_HSCROLL` and `WINDOW_VSCROLL` must **not** be set on a
> `win_scrollbar` window.  Those flags are reserved for parent windows that
> want the built-in framework scrollbars.  Pass `(void *)0` or `(void *)1` as
> `lparam` to select the orientation of `win_scrollbar`.

---

## Common Mistakes

| ❌ Wrong | ✅ Correct |
|---|---|
| `create_window("", WINDOW_HSCROLL, …, win_scrollbar, NULL)` — setting `WINDOW_HSCROLL` on the scrollbar child | `create_window("", 0, …, win_scrollbar, (void *)0)` — pass orientation via `lparam` |
| Creating `win_scrollbar` children when you want built-in scrollbars | Add `WINDOW_HSCROLL \| WINDOW_VSCROLL` to the parent and call `set_scroll_info()` |
| Manually painting scrollbar children from the parent proc | Let the framework draw via `WINDOW_HSCROLL \| WINDOW_VSCROLL`; it paints on top automatically |
| Forwarding mouse events to scrollbar children | Not needed; the framework intercepts clicks in the scrollbar area before calling `win->proc` |
| Handling `sbChanged` for built-in scrollbars | Handle `evHScroll` / `evVScroll` instead |
