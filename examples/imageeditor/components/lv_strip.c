#include <math.h>
#include <stdlib.h>

#include "../../../ui.h"

#include "lv_cmpn.h"
#include "lv_plug.h"

typedef struct {
  lv_strip_data_t data;
  int8_t dragging_index;
  bool dragging;
} lv_strip_state_t;

#define LV_SLOT_MIN  0
#define LV_SLOT_MAX  1

static int lv_track_margin(int w) {
  return CLAMP(w / 32, 4, LV_TRACK_L);
}

static int lv_track_l(int w) {
  return lv_track_margin(w);
}

static int lv_track_w(int w) {
  int margin = lv_track_margin(w);
  return MAX(1, w - 2 * margin);
}

static int lv_strip_bar_h(int h) {
  return MAX(1, h);
}

static int lv_strip_bar_y(int h) {
  (void)h;
  return 0;
}

static int lv_strip_handle_y(int h) {
  int handle_h = MIN(13, MAX(5, h));
  return MAX(0, (h - handle_h) / 2);
}

static int lv_strip_handle_h(int h) {
  return MIN(13, MAX(5, h));
}

static uint32_t lv_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  return ((uint32_t)a << 24) | ((uint32_t)b << 16) |
         ((uint32_t)g << 8) | (uint32_t)r;
}

static int lv_clamp_slider(int v) {
  return CLAMP(v, 0, 255);
}

static int lv_handle_x_from_norm(float t, int w) {
  int raw = (int)lroundf(CLAMP(t, 0.0f, 1.0f) * (float)lv_track_w(w));
  return lv_track_l(w) + raw;
}

static void lv_get_handle_visuals(const lv_strip_state_t *st,
                                  float pos[3], uint32_t col[3], int *count) {
  int s0 = lv_clamp_slider(st->data.sliders[0]);
  int s1 = lv_clamp_slider(st->data.sliders[1]);
  if (count) *count = 2;
  pos[0] = (float)s0 / 255.0f;
  pos[1] = (float)s1 / 255.0f;
  col[0] = 0xFF000000;
  col[1] = 0xFFFFFFFF;
}

static void lv_draw_handle(int x, int y, int h, uint32_t col, bool active) {
  int outer_h = lv_strip_handle_h(h);
  int inner_h = MAX(1, outer_h - 2);
  fill_rect(active ? get_sys_color(brFocusRing) : get_sys_color(brDarkEdge),
            R(x - 2, y, 5, outer_h));
  fill_rect(col, R(x - 1, y + 1, 3, inner_h));
}

static void lv_draw_strip_window(window_t *win, lv_strip_state_t *st) {
  irect16_t cr = get_client_rect(win);
  int track_l = lv_track_l(cr.w);
  int track_w = lv_track_w(cr.w);
  int bar_y = lv_strip_bar_y(cr.h);
  int bar_h = lv_strip_bar_h(cr.h);
  int handle_y = lv_strip_handle_y(cr.h);
  float pos[3] = {0};
  uint32_t col[3] = {0};
  int count = 0;

  lv_get_handle_visuals(st, pos, col, &count);

  fill_rect(get_sys_color(brWindowBg), cr);
  draw_gradient_rect(R(track_l, bar_y, track_w, bar_h),
                     lv_rgba(0x00, 0x00, 0x00, 0xFF),
                     lv_rgba(0xFF, 0xFF, 0xFF, 0xFF));
  for (int i = 0; i < count; i++) {
    bool active = (st->dragging && st->dragging_index == i);
    lv_draw_handle(lv_handle_x_from_norm(pos[i], cr.w), handle_y,
                   cr.h, col[i], active);
  }
}

static int lv_hit_handle(const lv_strip_state_t *st,
                         int mx, int my, int w, int h) {
  float pos[3] = {0};
  uint32_t col[3] = {0};
  int count = 0;
  int best_i = -1;
  int best_d = 0x7fffffff;
  int handle_y = lv_strip_handle_y(h);
  int handle_h = lv_strip_handle_h(h);

  (void)col;
  if (my < handle_y || my >= handle_y + handle_h) return -1;

  lv_get_handle_visuals(st, pos, col, &count);
  for (int i = 0; i < count; i++) {
    int hx = lv_handle_x_from_norm(pos[i], w);
    int d = abs(mx - hx);
    if (d <= 4) return i;
    if (d < best_d) {
      best_d = d;
      best_i = i;
    }
  }
  return best_i;
}

static void lv_apply_drag(lv_strip_state_t *st, int handle_index, int mx,
                          int w) {
  int track_w = lv_track_w(w);
  int raw = CLAMP(mx - lv_track_l(w), 0, track_w);
  int val = (int)lroundf((float)raw * 255.0f / (float)track_w);
  switch (handle_index) {
    case LV_SLOT_MIN:
      if (val >= st->data.sliders[1]) val = st->data.sliders[1] - 1;
      st->data.sliders[0] = CLAMP(val, 0, 254);
      break;
    case LV_SLOT_MAX:
      if (val <= st->data.sliders[0]) val = st->data.sliders[0] + 1;
      st->data.sliders[1] = CLAMP(val, 1, 255);
      break;
    default:
      break;
  }
}

static int lv_strip_value_at(const lv_strip_state_t *st,
                             uint32_t index) {
  if (!st) return -1;
  if (index == LV_STRIP_INDEX_MIN) return lv_clamp_slider(st->data.sliders[0]);
  if (index == LV_STRIP_INDEX_MID || index == LV_STRIP_INDEX_MAX)
    return lv_clamp_slider(st->data.sliders[1]);
  return -1;
}

result_t lv_strip_component_proc(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam) {
  lv_strip_state_t *st = (lv_strip_state_t *)win->userdata;
  switch (msg) {
    case evCreate: {
      lv_strip_state_t *ns = allocate_window_data(win, sizeof(lv_strip_state_t));
      win->flags |= WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOTABSTOP;
      ns->data.sliders[0] = 0;
      ns->data.sliders[1] = 255;
      ns->dragging_index = -1;
      ns->dragging = false;
      return true;
    }
    case evDestroy:
      return false;
    case lvStripSetData:
      if (!st || !lparam) return false;
      st->data = *(const lv_strip_data_t *)lparam;
      st->data.sliders[0] = lv_clamp_slider(st->data.sliders[0]);
      st->data.sliders[1] = lv_clamp_slider(st->data.sliders[1]);
      if (st->data.sliders[1] <= st->data.sliders[0]) st->data.sliders[1] = st->data.sliders[0] + 1;
      if (st->data.sliders[1] > 255) st->data.sliders[1] = 255;
      invalidate_window(win);
      return true;
    case lvStripGetValue: {
      int v = lv_strip_value_at(st, wparam);
      if (lparam) *(int *)lparam = v;
      return (result_t)(uint32_t)((v < 0) ? 0xFFFFFFFFu : (uint32_t)v);
    }
    case evPaint:
      if (st) lv_draw_strip_window(win, st);
      return true;
    case evLeftButtonDown: {
      if (!st) return false;
      int mx = (int16_t)LOWORD(wparam);
      int my = (int16_t)HIWORD(wparam);
      irect16_t cr = get_client_rect(win);
      int h = lv_hit_handle(st, mx, my, cr.w, cr.h);
      if (h >= 0) {
        st->dragging_index = (int8_t)h;
        st->dragging = true;
        set_capture(win);
        lv_apply_drag(st, h, mx, cr.w);
        send_message(win->parent, evCommand,
                     MAKEWPARAM(win->id, lvStripChanged), win);
        invalidate_window(win);
        return true;
      }
      return false;
    }
    case evMouseMove: {
      if (!st || !st->dragging || st->dragging_index < 0) return false;
      int mx = (int16_t)LOWORD(wparam);
      irect16_t cr = get_client_rect(win);
      lv_apply_drag(st, st->dragging_index, mx, cr.w);
      send_message(win->parent, evCommand,
                   MAKEWPARAM(win->id, lvStripChanged), win);
      invalidate_window(win);
      return true;
    }
    case evLeftButtonUp:
      if (st && st->dragging && st->dragging_index >= 0) {
        st->dragging_index = -1;
        st->dragging = false;
        set_capture(NULL);
        invalidate_window(win);
        return true;
      }
      return false;
    default:
      return false;
  }
}
