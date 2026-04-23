// commctl/scrollbar.c — scrollbar control
//
// win_scrollbar is a thin, interactive scrollbar that mirrors WinAPI scrollbar
// behaviour.  Orientation is set via the lparam passed to evCreate:
//   (void *)0 → horizontal bar   (SB_HORZ)
//   (void *)1 → vertical bar     (SB_VERT)
//
// Do NOT set WINDOW_HSCROLL or WINDOW_VSCROLL on the scrollbar window itself.
// Those flags are reserved for parent windows that want built-in framework
// scrollbars managed by set_scroll_info() / get_scroll_info().
//
// Coordinate convention
// ---------------------
// Mouse events arriving at the scrollbar proc carry coords in the window's
// own client coordinate system (kernel/event.c LOCAL_X/LOCAL_Y semantics):
//   horiz axis → (int16_t)LOWORD(wparam)  — scrollbar-local x
//   vert  axis → (int16_t)HIWORD(wparam)  — scrollbar-local y
//
// This holds for both events dispatched via handle_mouse and for captured
// events (set_capture), so LOWORD/HIWORD are used directly throughout.
//
// Notification
// ------------
// When the scroll position changes the control sends evCommand to
// its parent window:
//   wparam = MAKEDWORD(win->id, sbChanged)
//   lparam = (void *)(intptr_t)new_pos

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"
#include "commctl.h"

// Internal per-scrollbar state
typedef struct {
  int min_val, max_val; // content range
  int page;             // viewport size (= visible portion of content)
  int pos;              // current scroll position [min_val .. max_val-page]
  bool is_vertical;     // orientation: set from lparam at evCreate
  bool dragging;
  int drag_start_mouse; // scrollbar-local axis coord when drag began
  int drag_start_pos;   // pos value when drag began
} scrollbar_state_t;

// ---- geometry helpers -------------------------------------------------------

static bool sb_vertical(window_t *win) {
  scrollbar_state_t *s = (scrollbar_state_t *)win->userdata;
  return s && s->is_vertical;
}

// Length of the track (the scrollable dimension of the bar)
static int sb_track(window_t *win) {
  return sb_vertical(win) ? win->frame.h : win->frame.w;
}

// Length of the thumb, clamped to a minimum of 8 pixels
static int sb_thumb_len(scrollbar_state_t *s, int track) {
  int range = s->max_val - s->min_val;
  if (range <= 0 || s->page >= range) return track;
  int tl = track * s->page / range;
  return (tl < 8) ? 8 : tl;
}

// Pixel offset of the thumb from the start of the track
static int sb_thumb_off(scrollbar_state_t *s, int track) {
  int travel = s->max_val - s->min_val - s->page;
  if (travel <= 0) return 0;
  int tl = sb_thumb_len(s, track);
  int tt = track - tl;
  if (tt <= 0) return 0;
  return (s->pos - s->min_val) * tt / travel;
}

// Clamp a raw position into the valid range [min_val .. max_val-page]
static int sb_clamp(scrollbar_state_t *s, int pos) {
  int max_pos = s->max_val - s->page;
  if (max_pos < s->min_val) max_pos = s->min_val;
  if (pos < s->min_val) pos = s->min_val;
  if (pos > max_pos)    pos = max_pos;
  return pos;
}

// Notify parent that position changed
static void sb_notify(window_t *win, int pos) {
  if (win->parent) {
    send_message(win->parent, evCommand,
                 MAKEDWORD(win->id, sbChanged),
                 (void *)(intptr_t)pos);
  }
}

// ---- axis coordinate -------------------------------------------------------

// Return the scrollbar-local mouse coordinate on the scroll axis.
// Mouse events are delivered in the window's own client coordinate system
// (see kernel/event.c), so LOWORD/HIWORD are already scrollbar-local.
static int sb_axis(window_t *win, uint32_t wparam) {
  if (sb_vertical(win))
    return (int16_t)HIWORD(wparam);
  else
    return (int16_t)LOWORD(wparam);
}

// ---- window procedure ------------------------------------------------------

result_t win_scrollbar(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  scrollbar_state_t *s = (scrollbar_state_t *)win->userdata;

  switch (msg) {
    case evCreate: {
      scrollbar_state_t *ns = allocate_window_data(win, sizeof(scrollbar_state_t));
      ns->min_val    = 0;
      ns->max_val    = 100;
      ns->page       = 10;
      ns->pos        = 0;
      ns->is_vertical = (bool)(intptr_t)lparam;
      ns->dragging   = false;
      // Scrollbar children are routed through their parent's proc; they must
      // not intercept find_window hit-testing on their own.
      win->notabstop = true;
      return true;
    }

    case evPaint: {
      if (!s) return true;
      bool vert = sb_vertical(win);
      int track = sb_track(win);
      int tl    = sb_thumb_len(s, track);
      int to    = sb_thumb_off(s, track);
      int x = 0, y = 0;
      int w = win->frame.w, h = win->frame.h;
      fill_rect(get_sys_color(brWindowDarkBg), R(x, y, w, h));
      if (vert)
        fill_rect(get_sys_color(brLightEdge), R(x, y + to, w, tl));
      else
        fill_rect(get_sys_color(brLightEdge), R(x + to, y, tl, h));
      return true;
    }

    case sbSetInfo: {
      if (!s || !lparam) return false;
      scrollbar_info_t *info = (scrollbar_info_t *)lparam;
      s->min_val = info->min_val;
      s->max_val = info->max_val;
      s->page    = info->page;
      s->pos     = sb_clamp(s, info->pos);
      invalidate_window(win);
      return true;
    }

    case sbGetPos:
      return s ? (result_t)(uint32_t)s->pos : 0;

    case evLeftButtonDown: {
      if (!s) return false;
      int mouse = sb_axis(win, wparam);
      int track = sb_track(win);
      int tl    = sb_thumb_len(s, track);
      int to    = sb_thumb_off(s, track);
      if (mouse >= to && mouse < to + tl) {
        // Clicked on thumb — begin drag
        s->dragging        = true;
        s->drag_start_mouse = mouse;
        s->drag_start_pos   = s->pos;
        set_capture(win);
      } else {
        // Clicked on track — page-scroll toward the click
        int new_pos = (mouse < to) ? s->pos - s->page : s->pos + s->page;
        new_pos = sb_clamp(s, new_pos);
        if (new_pos != s->pos) {
          s->pos = new_pos;
          invalidate_window(win);
          sb_notify(win, s->pos);
        }
      }
      return true;
    }

    case evMouseMove: {
      if (!s || !s->dragging) return false;
      int mouse = sb_axis(win, wparam);
      int track = sb_track(win);
      int tl    = sb_thumb_len(s, track);
      int travel_px  = track - tl;
      int travel_pos = s->max_val - s->min_val - s->page;
      if (travel_px <= 0 || travel_pos <= 0) return true;
      int delta_px  = mouse - s->drag_start_mouse;
      int delta_pos = delta_px * travel_pos / travel_px;
      int new_pos   = sb_clamp(s, s->drag_start_pos + delta_pos);
      if (new_pos != s->pos) {
        s->pos = new_pos;
        invalidate_window(win);
        sb_notify(win, s->pos);
      }
      return true;
    }

    case evLeftButtonUp:
      if (s && s->dragging) {
        s->dragging = false;
        set_capture(NULL);
      }
      return false;

    default:
      return false;
  }
}
