// win_splitter — thin draggable divider bar between two panels.
//
// Orientation is fixed at creation time via lparam:
//   (void *)SPLIT_VERT  — vertical bar   (user drags left / right)
//   (void *)SPLIT_HORZ  — horizontal bar (user drags up  / down)
//
// When the user presses the left button the splitter sends evCommand to its
// parent:
//   wparam = MAKEDWORD(win->id, spnDragStart)
//   lparam = (void *)(uintptr_t)MAKEDWORD(parent_local_x, parent_local_y)
//
// The parent is expected to call set_capture(parent_win) and then track
// evMouseMove / evLeftButtonUp to update its layout (see the gitclient's
// gc_main_proc for the canonical usage pattern).  This mirrors the WinAPI
// idiom where a child control notifies the parent, and the parent owns the
// drag loop.
//
// The splitter paints itself with the brBorderFocus colour so it matches the
// rest of the Orion chrome.

#include "../user/user.h"
#include "../user/messages.h"

// Orientation constants (passed as lparam to create_window).
#define SPLIT_VERT 0   // vertical bar — occupies a narrow column
#define SPLIT_HORZ 1   // horizontal bar — occupies a narrow row

typedef struct {
  int orientation;  // SPLIT_VERT or SPLIT_HORZ
} splitter_data_t;

result_t win_splitter(window_t *win, uint32_t msg,
                      uint32_t wparam, void *lparam) {
  (void)wparam;

  switch (msg) {
    case evCreate: {
      splitter_data_t *st =
          (splitter_data_t *)malloc(sizeof(splitter_data_t));
      if (!st) return false;
      st->orientation = (int)(intptr_t)lparam;
      win->userdata = st;
      return true;
    }

    case evDestroy: {
      free(win->userdata);
      win->userdata = NULL;
      return false;
    }

    case evPaint: {
      rect_t cr = get_client_rect(win);
      fill_rect(get_sys_color(brBorderFocus), &cr);
      return true;
    }

    // On left-button press, notify the parent so it can take over the drag.
    // Convert the hit point from splitter-local space to parent-local space
    // so the parent can compute accurate deltas in its own coordinate system.
    case evLeftButtonDown: {
      if (!win->parent) return false;
      // local hit point inside splitter
      int local_x = (int)LOWORD(wparam);
      int local_y = (int)HIWORD(wparam);
      // parent-local coords: add our own frame position
      int px = local_x + win->frame.x;
      int py = local_y + win->frame.y;
      send_message(win->parent, evCommand,
                   MAKEDWORD((uint16_t)win->id, (uint16_t)spnDragStart),
                   (void *)(uintptr_t)MAKEDWORD((uint16_t)px, (uint16_t)py));
      return true;
    }

    default:
      return false;
  }
}

// Getter used by parent windows to query a splitter's orientation without
// accessing internal data directly.
int win_splitter_orientation(window_t *win) {
  if (!win || !win->userdata) return SPLIT_VERT;
  return ((splitter_data_t *)win->userdata)->orientation;
}
