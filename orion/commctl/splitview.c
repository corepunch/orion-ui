// win_splitview — two-pane container with a resizable divider.
//
// Creates two child content windows separated by a draggable splitter bar.
// The split orientation (vertical or horizontal) is set at creation time via
// lparam: (void *)SPLIT_VERT or (void *)SPLIT_HORZ.
//
// SplitView manages the splitter drag loop internally — the parent does not
// need to handle spnDragStart.  The divider position is tracked as a ratio
// (0.0 .. 1.0) of the total content area.
//
// Usage:
//   window_t *sv = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL,
//                                &rect, parent, win_splitview,
//                                hinstance, (void *)SPLIT_VERT);
//   // After creation, get the two panes:
//   window_t *left = splitview_get_left(sv);
//   window_t *right = splitview_get_right(sv);
//   // Or create with form children:
//   // The first two children of the splitview become left/right panes.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/rect.h>
#include <orion/user/theme.h>
#include "commctl.h"
#include "layout_shared.h"

#define SPLITVIEW_MIN_PANE 32

typedef struct {
  int      orientation;   // SPLIT_VERT or SPLIT_HORZ
  double   split_ratio;   // 0.0 .. 1.0 — fraction of space given to left/top pane
  int      pane_margin;   // common leaf margin, shared across a split tree
  bool     dragging;      // true while the user is dragging the divider
  int      drag_start;    // mouse axis position when drag began
  double   drag_ratio;    // split_ratio when drag began
} splitview_state_t;

static bool splitview_is_splitter(window_t *win) {
  return win && win->proc == win_splitter;
}

static splitview_params_t splitview_params(void *lparam) {
  splitview_params_t params = { SPLIT_VERT, 0.5 };
  if (!lparam || (uintptr_t)lparam <= SPLIT_HORZ)
    params.orientation = (int)(intptr_t)lparam;
  else {
    const form_ctrl_def_t *cd = (const form_ctrl_def_t *)lparam;
    if ((uintptr_t)cd->lparam <= SPLIT_HORZ)
      params.orientation = (int)(intptr_t)cd->lparam;
    else params = *(const splitview_params_t *)cd->lparam;
  }
  return params;
}

static void splitview_get_panes(window_t *win, window_t **left, window_t **right, window_t **splitter) {
  if (left) *left = NULL; if (right) *right = NULL; if (splitter) *splitter = NULL;
  int pane = 0;
  for (window_t *c = win ? win->children : NULL; c; c = c->next) {
    if (splitview_is_splitter(c)) { if (splitter) *splitter = c; continue; }
    if (pane == 0 && left) *left = c;
    if (pane == 1 && right) *right = c;
    pane++;
  }
}

static void splitview_arrange(splitview_state_t *st, window_t *win) {
  irect16_t cr = get_client_rect(win);
  int total = (st->orientation == SPLIT_VERT) ? cr.w : cr.h;
  int spacing = MIN(total, get_theme()->splitter.spacing);
  int available = MAX(0, total - spacing);
  int split_px = (int)(available * st->split_ratio + 0.5);
  if (available >= SPLITVIEW_MIN_PANE * 2)
    split_px = MAX(SPLITVIEW_MIN_PANE, MIN(split_px, available - SPLITVIEW_MIN_PANE));
  else split_px = MAX(0, MIN(split_px, available));
  int margin = MIN(st->pane_margin, MIN(split_px, available - split_px));

  irect16_t r_left, r_split, r_right;
  if (st->orientation == SPLIT_VERT) {
    r_left  = rect_split_left(cr, split_px);
    irect16_t rem = rect_trim_left(cr, split_px);
    r_split = rect_inset_xy(rect_split_left(rem, spacing), -margin, 0);
    r_right = rect_trim_left(rem, spacing);
  } else {
    r_left  = rect_split_top(cr, split_px);
    irect16_t rem = rect_trim_top(cr, split_px);
    r_split = rect_inset_xy(rect_split_top(rem, spacing), 0, -margin);
    r_right = rect_trim_top(rem, spacing);
  }

  window_t *left = NULL, *right = NULL, *splitter = NULL;
  splitview_get_panes(win, &left, &right, &splitter);
  bool left_visible = left && window_has_state(left, WINDOW_STATE_VISIBLE);
  bool right_visible = right && window_has_state(right, WINDOW_STATE_VISIBLE);
  if (left_visible != right_visible) {
    window_t *only = left_visible ? left : right;
    if (splitter) window_set_state(splitter, WINDOW_STATE_VISIBLE, false);
    layout_arrange_child(only, cr);
    return;
  }
  if (splitter) window_set_state(splitter, WINDOW_STATE_VISIBLE, r_split.w > 0 && r_split.h > 0);
  layout_arrange_child(left, r_left);
  layout_arrange_child(right, r_right);
  if (splitter) splitter->frame = r_split;
}

result_t win_splitview(window_t *win, uint32_t msg,
                       uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      splitview_state_t *st = allocate_window_data(win, sizeof(splitview_state_t));
      if (!st) return false;
      splitview_params_t params = splitview_params(lparam);
      st->orientation = params.orientation;
      st->split_ratio = params.split_ratio;
      if (!(st->split_ratio >= 0.0 && st->split_ratio <= 1.0)) {
        fprintf(stderr, "[splitview] invalid split ratio win=%u ratio=%g; using 0.5\n", (unsigned)win->id, st->split_ratio);
        fflush(stderr);
        st->split_ratio = 0.5;
      }
      st->dragging = false;

      // Create the splitter child.
      irect16_t r0 = {0, 0, 0, 0};
      create_window("", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOTABSTOP,
                    &r0, win, "Splitter",
                    win->hinstance, (void *)(intptr_t)st->orientation);
      return true;
    }

    case evArrange: {
      splitview_state_t *st = (splitview_state_t *)win->userdata;
      layout_arrange_t *a = (layout_arrange_t *)lparam;
      if (a) win->frame = a->rect;
      if (st) splitview_arrange(st, win);
      return true;
    }

    case svGetPane:
      if (wparam <= 1) return (result_t)(wparam ? splitview_get_right(win) : splitview_get_left(win));
      fprintf(stderr, "[splitview] get_pane rejected win=%u index=%u\n", win->id, wparam);
      fflush(stderr); return 0;
    case svSetPaneMargin: {
      splitview_state_t *st = win->userdata;
      if (!st || wparam > INT16_MAX) {
        fprintf(stderr, "[splitview] pane margin rejected win=%u margin=%u state=%p\n", win->id, wparam, (void *)st);
        fflush(stderr); return false;
      }
      if (st->pane_margin != (int)wparam) {
        st->pane_margin = (int)wparam;
        splitview_arrange(st, win);
        invalidate_window(win);
      }
      return true;
    }
    case evChildHitTest: {
      window_t *splitter = NULL;
      splitview_get_panes(win, NULL, NULL, &splitter);
      ipoint16_t point = {(int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)};
      return splitter && window_has_state(splitter, WINDOW_STATE_VISIBLE) && rect_contains_point(splitter->frame, point)
        ? (result_t)splitter : 0;
    }
    case evThemeChanged: case evResize: {
      splitview_state_t *st = (splitview_state_t *)win->userdata;
      if (st) splitview_arrange(st, win);
      return true;
    }

    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (m) {
        m->desired_w = 200;
        m->desired_h = 200;
      }
      return true;
    }

    case evCommand: {
      splitview_state_t *st = (splitview_state_t *)win->userdata;
      if (!st) return false;
      uint16_t code = HIWORD(wparam);
      if (code == tcnBeginDrag || code == tcnSelChange || code == tcnAction)
        return win->parent ? send_message(win->parent, msg, wparam, lparam) : false;
      if (code == spnDragStart) {
        st->dragging = true;
        uint32_t packed = (uint32_t)(uintptr_t)lparam;
        st->drag_start = st->orientation == SPLIT_VERT
                           ? (int16_t)LOWORD(packed)
                           : (int16_t)HIWORD(packed);
        st->drag_ratio = st->split_ratio;
        set_capture(win);
        return true;
      }
      return false;
    }

    case evMouseMove: {
      splitview_state_t *st = (splitview_state_t *)win->userdata;
      if (!st || !st->dragging) return false;
      int pos = st->orientation == SPLIT_VERT
                  ? (int16_t)LOWORD(wparam)
                  : (int16_t)HIWORD(wparam);
      int delta = pos - st->drag_start;
      irect16_t cr = get_client_rect(win);
      int total = MAX(0, (st->orientation == SPLIT_VERT ? cr.w : cr.h) - get_theme()->splitter.spacing);
      if (total > 0)
        st->split_ratio = st->drag_ratio + (double)delta / total;
      if (st->split_ratio < 0.0) st->split_ratio = 0.0;
      if (st->split_ratio > 1.0) st->split_ratio = 1.0;
      splitview_arrange(st, win);
      invalidate_window(win);
      return true;
    }

    case evPointerCancel: case evLeftButtonUp: {
      splitview_state_t *st = (splitview_state_t *)win->userdata;
      if (st && st->dragging) {
        st->dragging = false;
        set_capture(NULL);
        return true;
      }
      return false;
    }

    case evPaint: return false;
    case evDestroy: free(win->userdata); win->userdata = NULL; return true;
    case evGetCursor:
      return curArrow;

    default:
      return false;
  }
}

window_t *splitview_get_left(window_t *win) {
  window_t *left = NULL;
  splitview_get_panes(win, &left, NULL, NULL);
  return left;
}

window_t *splitview_get_right(window_t *win) {
  window_t *right = NULL;
  splitview_get_panes(win, NULL, &right, NULL);
  return right;
}
