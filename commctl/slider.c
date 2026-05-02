#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"
#include "commctl.h"

#define SLIDER_MAX_HANDLES 4
#define SLIDER_MIN_THUMB_W 7
#define SLIDER_TRACK_PAD   8
#define SLIDER_BAR_Y       4
#define SLIDER_BAR_H       8
#define SLIDER_HANDLE_Y    8

typedef struct {
  int min_val;
  int max_val;
  int count;
  int pos[SLIDER_MAX_HANDLES];
  bool dragging;
  int drag_index;
} slider_state_t;

static int sl_track_w(const window_t *win) {
  return MAX(1, get_client_rect(win).w - 2 * SLIDER_TRACK_PAD);
}

static int sl_clamp_count(int n) {
  return CLAMP(n, 1, SLIDER_MAX_HANDLES);
}

static int sl_clamp_pos(const slider_state_t *s, int v) {
  return CLAMP(v, s->min_val, s->max_val);
}

static int sl_thumb_x_from_value(const window_t *win, const slider_state_t *s, int v) {
  int range = MAX(1, s->max_val - s->min_val);
  float t = (float)(v - s->min_val) / (float)range;
  int raw = (int)lroundf(CLAMP(t, 0.0f, 1.0f) * (float)sl_track_w(win));
  return SLIDER_TRACK_PAD + raw;
}

static int sl_value_from_mouse_x(const window_t *win, const slider_state_t *s, int mx) {
  int raw = CLAMP(mx - SLIDER_TRACK_PAD, 0, sl_track_w(win));
  int range = MAX(1, s->max_val - s->min_val);
  int v = s->min_val + (int)lroundf((float)raw * (float)range / (float)sl_track_w(win));
  return sl_clamp_pos(s, v);
}

static void sl_draw_thumb(int x, int y, bool active) {
  fill_rect(active ? get_sys_color(brFocusRing) : get_sys_color(brDarkEdge),
            R(x - 3, y - 2, SLIDER_MIN_THUMB_W, 11));
  fill_rect(get_sys_color(brTextNormal), R(x - 2, y - 1, SLIDER_MIN_THUMB_W - 2, 9));
}

static void sl_notify(window_t *win, int handle_index, int value) {
  uint16_t notif = (uint16_t)(sliderValueChanged + CLAMP(handle_index, 0, SLIDER_MAX_HANDLES - 1));
  (void)value; /* value readable via slGetPos; lparam must be the source window */
  if (!win->parent) return;
  send_message(win->parent, evCommand,
               MAKEWPARAM(win->id, notif),
               win);
}

static int sl_hit_handle(const window_t *win, const slider_state_t *s, int mx, int my) {
  int y = SLIDER_HANDLE_Y;
  int best_i = -1;
  int best_d = 0x7fffffff;
  if (my < y - 3 || my > y + 10) return -1;
  for (int i = 0; i < s->count; i++) {
    int hx = sl_thumb_x_from_value(win, s, s->pos[i]);
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

result_t win_slider(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  slider_state_t *s = (slider_state_t *)win->userdata;
  switch (msg) {
    case evCreate: {
      slider_state_t *ns = allocate_window_data(win, sizeof(slider_state_t));
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
      fill_rect(get_sys_color(brWindowBg), cr);
      fill_rect(get_sys_color(brDarkEdge),
                R(SLIDER_TRACK_PAD, SLIDER_BAR_Y + SLIDER_BAR_H / 2 - 1,
                  sl_track_w(win), 2));
      for (int i = 0; i < s->count; i++) {
        bool active = s->dragging && s->drag_index == i;
        sl_draw_thumb(sl_thumb_x_from_value(win, s, s->pos[i]), SLIDER_HANDLE_Y, active);
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
      h = sl_hit_handle(win, s, mx, my);
      if (h < 0) return false;
      s->dragging = true;
      s->drag_index = h;
      set_capture(win);
      return true;
    }

    case evMouseMove:
      if (!s || !s->dragging || s->drag_index < 0) return false;
      {
        int mx = (int16_t)LOWORD(wparam);
        int v = sl_value_from_mouse_x(win, s, mx);
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

    default:
      return false;
  }
}
