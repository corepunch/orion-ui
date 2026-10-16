#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/theme.h>
#include <orion/user/rect.h>
#include "commctl.h"


typedef struct {
  int min_val;
  int max_val;
  int count;
  int pos[SLIDER_MAX_HANDLES];
  bool dragging;
  int drag_index;
  int grab;  // SLIDER_TROUGH: pointer offset from the thumb centre along the axis
} slider_state_t;

// Positions along the value axis ("axis" coordinates) grow with the value:
// x for horizontal sliders, distance from the bottom edge for vertical ones.
static bool sl_vertical(const window_t *win) { return (win->flags & SLIDER_VERTICAL) != 0; }
static bool sl_trough(const window_t *win)   { return (win->flags & SLIDER_TROUGH) != 0; }

static int sl_length(const window_t *win) {
  irect16_t cr = get_client_rect(win);
  return sl_vertical(win) ? cr.h : cr.w;
}

static int sl_axis(const window_t *win, int mx, int my) {
  return sl_vertical(win) ? get_client_rect(win).h - my : mx;
}

// Range of thumb-centre positions: [start, start + span].
static void sl_travel(const window_t *win, int *start, int *span) {
  int edge = SLIDER_TRACK_PAD;
  const theme_t *theme = get_theme();
  if (sl_trough(win)) edge = theme->slider.trough_inset + theme->slider.thumb_inset + theme->slider.thumb_length / 2;
  *start = edge;
  *span = MAX(1, sl_length(win) - 2 * edge);
}

// Rect covering [from, from + len) along the axis and `across` on the cross axis.
static irect16_t sl_axis_rect(const window_t *win, int from, int len, irect16_t across) {
  if (sl_vertical(win)) return R(across.x, get_client_rect(win).h - from - len, across.w, len);
  return R(from, across.y, len, across.h);
}

static irect16_t sl_trough_rect(const window_t *win) {
  return rect_inset(get_client_rect(win), get_theme()->slider.trough_inset);
}

static int sl_clamp_count(int n) {
  return CLAMP(n, 1, SLIDER_MAX_HANDLES);
}

static int sl_clamp_pos(const slider_state_t *s, int v) {
  return CLAMP(v, s->min_val, s->max_val);
}

static int sl_axis_from_value(const window_t *win, const slider_state_t *s, int v) {
  int start, span, range = MAX(1, s->max_val - s->min_val);
  sl_travel(win, &start, &span);
  float t = (float)(v - s->min_val) / (float)range;
  return start + (int)lroundf(CLAMP(t, 0.0f, 1.0f) * (float)span);
}

static int sl_value_from_axis(const window_t *win, const slider_state_t *s, int a) {
  int start, span, range = MAX(1, s->max_val - s->min_val);
  sl_travel(win, &start, &span);
  int raw = CLAMP(a - start, 0, span);
  return sl_clamp_pos(s, s->min_val + (int)lroundf((float)raw * (float)range / (float)span));
}

static irect16_t sl_knob_rect(const window_t *win, const slider_state_t *s, int i) {
  const theme_t *theme = get_theme();
  irect16_t trough = rect_inset(sl_trough_rect(win), theme->slider.thumb_inset);
  int len = theme->slider.thumb_length;
  return sl_axis_rect(win, sl_axis_from_value(win, s, s->pos[i]) - len / 2, len, trough);
}

static void sl_notify(window_t *win, int handle_index, int value) {
  uint16_t notif = (uint16_t)(sliderValueChanged + CLAMP(handle_index, 0, SLIDER_MAX_HANDLES - 1));
  if (!win->parent) return;
  send_message(win->parent, evCommand,
               MAKEWPARAM(win->id, notif),
               win);
}

static int sl_hit_handle(const window_t *win, const slider_state_t *s, int mx, int my) {
  if (win->flags & SLIDER_VERTICAL) {
    int axis = get_client_rect(win).h - my;
    my = mx - get_client_rect(win).w / 2 + SLIDER_HANDLE_Y;
    mx = axis;
  }
  int y = SLIDER_HANDLE_Y;
  int best_i = -1;
  int best_d = 0x7fffffff;
  if (my < y - 3 || my > y + 10) return -1;
  for (int i = 0; i < s->count; i++) {
    int hx = sl_axis_from_value(win, s, s->pos[i]);
    int d = abs(mx - hx);
    if (d <= 5) return i;
    if (d < best_d) {
      best_d = d;
      best_i = i;
    }
  }
  if (best_d > 8) return -1;
  return best_i;
}

// SLIDER_TROUGH: the whole trough is the target. A press on a thumb keeps its
// grab offset; elsewhere the nearest thumb jumps under the pointer.
static int sl_hit_trough(window_t *win, slider_state_t *s, int mx, int my) {
  ipoint16_t pt = {mx, my};
  if (!rect_contains_point(sl_trough_rect(win), pt)) return -1;
  int a = sl_axis(win, mx, my), best = 0;
  for (int i = 0; i < s->count; i++) {
    if (rect_contains_point(sl_knob_rect(win, s, i), pt)) {
      s->grab = a - sl_axis_from_value(win, s, s->pos[i]);
      return i;
    }
    if (abs(a - sl_axis_from_value(win, s, s->pos[i])) < abs(a - sl_axis_from_value(win, s, s->pos[best]))) best = i;
  }
  s->grab = 0;
  int v = sl_value_from_axis(win, s, a);
  if (v != s->pos[best]) {
    s->pos[best] = v;
    sl_notify(win, best, v);
  }
  return best;
}

result_t win_slider(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  slider_state_t *s = (slider_state_t *)win->userdata;
  switch (msg) {
    case evQueryDrag: return DRAG_NOW;
    case evCreate: {
      slider_state_t *ns = allocate_window_data(win, sizeof(slider_state_t));
      if (!ns) {
        fprintf(stderr, "[sl] allocation failed win=%u\n", win->id);
        fflush(stderr);
        return false;
      }
      ns->min_val = 0;
      ns->max_val = 255;
      ns->count = 1;
      for (int i = 0; i < SLIDER_MAX_HANDLES; i++)
        ns->pos[i] = ns->min_val;
      ns->dragging = false;
      ns->drag_index = -1;
      win->flags |= WINDOW_NOTABSTOP;
      return true;
    }

    case evPaint:
      if (!s) return true;
      {
      irect16_t cr = get_client_rect(win);
      if (!(win->flags & WINDOW_NOFILL)) theme_draw(THEME_PART_SURFACE, cr, CTRL_NORMAL);
      if (sl_trough(win)) {
        ctrl_state_t base = window_has_state(win, WINDOW_STATE_DISABLED) ? CTRL_DISABLED : CTRL_NORMAL;
        theme_draw(THEME_PART_SLIDER_TROUGH, sl_trough_rect(win), base);
        for (int i = 0; i < s->count; i++) {
          bool active = s->dragging && s->drag_index == i;
          theme_draw(THEME_PART_SLIDER_KNOB, sl_knob_rect(win, s, i), base | (active ? CTRL_PRESSED : 0));
        }
        return true;
      }
      bool vertical = sl_vertical(win);
      int start, span;
      sl_travel(win, &start, &span);
      irect16_t track = R(start, SLIDER_BAR_Y + SLIDER_BAR_H / 2 - 1, span, 2);
      if (vertical) track = R(cr.w / 2 - 1, start, 2, span);
      theme_draw(THEME_PART_SLIDER_TRACK, track, CTRL_NORMAL);
      for (int i = 0; i < s->count; i++) {
        bool active = s->dragging && s->drag_index == i;
        int tx = sl_axis_from_value(win, s, s->pos[i]);
        irect16_t thumb = R(tx - 3, SLIDER_HANDLE_Y - 2, SLIDER_MIN_THUMB_W, 11);
        if (vertical) thumb = R(cr.w / 2 - 5, cr.h - tx - 3, 11, SLIDER_MIN_THUMB_W);
        theme_draw(THEME_PART_SLIDER_THUMB, thumb, active ? CTRL_PRESSED : CTRL_NORMAL);
      }
      }
      return true;


    case slSetRange:
      if (!s || !lparam) return false;
      {
        const slider_range_t *r = (const slider_range_t *)lparam;
        int min_v = r->min_val;
        int max_v = r->max_val;
        if (max_v < min_v) {
          int t = min_v;
          min_v = max_v;
          max_v = t;
        }
        s->min_val = min_v;
        s->max_val = max_v;
        for (int i = 0; i < s->count; i++)
          s->pos[i] = sl_clamp_pos(s, s->pos[i]);
        invalidate_window(win);
        return true;
      }

    case slGetRange:
      if (!s) return false;
      if (lparam) {
        slider_range_t *r = (slider_range_t *)lparam;
        r->min_val = s->min_val;
        r->max_val = s->max_val;
      }
      return true;

    case slSetCount:
      if (!s) return false;
      s->count = sl_clamp_count((int)wparam);
      if (s->drag_index >= s->count) {
        s->dragging = false;
        s->drag_index = -1;
      }
      invalidate_window(win);
      return true;

    case slSetPos:
      if (!s) return false;
      {
        int idx = (int)wparam;
        if (idx < 0 || idx >= s->count) return false;
        s->pos[idx] = sl_clamp_pos(s, (int)(intptr_t)lparam);
        invalidate_window(win);
        return true;
      }

    case slGetPos:
      if (!s) return (result_t)0xFFFFFFFFu;
      {
        int idx = (int)wparam;
        int v;
        if (idx < 0 || idx >= s->count) return (result_t)0xFFFFFFFFu;
        v = s->pos[idx];
        if (lparam) *(int *)lparam = v;
        return (result_t)(uint32_t)v;
      }

    case evLeftButtonDown: {
      int mx, my, h;
      if (!s) return false;
      mx = (int16_t)LOWORD(wparam);
      my = (int16_t)HIWORD(wparam);
      s->grab = 0;
      h = sl_trough(win) ? sl_hit_trough(win, s, mx, my) : sl_hit_handle(win, s, mx, my);
      if (h < 0) return false;
      s->dragging = true;
      s->drag_index = h;
      set_capture(win);
      invalidate_window(win);
      return true;
    }

    case evMouseMove:
      if (!s || !s->dragging || s->drag_index < 0) return false;
      {
        int a = sl_axis(win, (int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)) - s->grab;
        int v = sl_value_from_axis(win, s, a);
        if (v != s->pos[s->drag_index]) {
          s->pos[s->drag_index] = v;
          sl_notify(win, s->drag_index, v);
          invalidate_window(win);
        }
      }
      return true;

    case evLeftButtonUp:
      if (s && s->dragging) {
        s->dragging = false;
        s->drag_index = -1;
        set_capture(NULL);
        invalidate_window(win);
        return true;
      }
      return false;

    case evDestroy:
      if (s && s->dragging) set_capture(NULL);
      free(s);
      win->userdata = NULL;
      return true;
    default:
      return false;
  }
}
