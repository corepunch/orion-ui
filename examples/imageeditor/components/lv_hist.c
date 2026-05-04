#include <math.h>
#include <stdlib.h>

#include "../../../ui.h"

#include "lv_cmpn.h"
#include "lv_plug.h"

typedef struct {
  lv_graph_data_t data;
  uint32_t graph_tex;
  int tex_w;
  int tex_h;
  bool graph_dirty;
} lv_hist_state_t;

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

static uint32_t lv_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  return ((uint32_t)a << 24) | ((uint32_t)b << 16) |
         ((uint32_t)g << 8) | (uint32_t)r;
}

static void lv_plot_px(uint32_t *pix, int w, int h,
                       int x, int y, uint32_t col) {
  if (x < 0 || y < 0 || x >= w || y >= h) return;
  pix[(size_t)y * w + x] = col;
}

static void lv_draw_line_px(uint32_t *pix, int w, int h,
                            int x0, int y0, int x1, int y1, uint32_t col) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    lv_plot_px(pix, w, h, x0, y0, col);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

static uint32_t lv_default_hist_bin(int bin) {
  float t = (float)bin / 255.0f;
  float mountain = sinf(t * 3.1415927f);
  float ripple = 0.92f + 0.08f * sinf(t * 6.2831853f * 5.0f);
  float v = mountain * mountain * ripple;
  v = CLAMP(v, 0.0f, 1.0f);
  return (uint32_t)lroundf(v * 1000.0f);
}

static uint32_t lv_hist_value(const lv_hist_state_t *st, int bin,
                              bool use_default) {
  return use_default ? lv_default_hist_bin(bin) : st->data.hist[bin];
}

static uint32_t lv_hist_max(const lv_hist_state_t *st, bool use_default) {
  if (!use_default) return st->data.hist_max ? st->data.hist_max : 1;
  uint32_t maxv = 1;
  for (int i = 0; i < 256; i++) {
    uint32_t v = lv_default_hist_bin(i);
    if (v > maxv) maxv = v;
  }
  return maxv;
}

static void lv_build_graph_texture(lv_hist_state_t *st, int w, int h) {
  if (!st) return;
  if (w < 1 || h < 1) return;
  if (st->graph_tex) {
    R_DeleteTexture(st->graph_tex);
    st->graph_tex = 0;
  }

  size_t count = (size_t)w * h;
  uint32_t *pix = malloc(count * sizeof(uint32_t));
  if (!pix) return;

  uint32_t bg = get_sys_color(brWindowBg);
  for (size_t i = 0; i < count; i++)
    pix[i] = bg;

  int hist_top = 1;
  int hist_bottom = MAX(hist_top + 1, h - 1);
  int hist_h = MAX(1, hist_bottom - hist_top);
  int track_l = lv_track_l(w);
  int track_w = lv_track_w(w);
  bool use_default = (st->data.hist_max == 0);
  uint32_t maxv = lv_hist_max(st, use_default);

  for (int x = 0; x < track_w; x++) {
    int bin = x * 256 / track_w;
    uint32_t v = lv_hist_value(st, bin, use_default);
    int bar_h = (int)lroundf((float)v * (float)hist_h / (float)maxv);
    if (bar_h > hist_h) bar_h = hist_h;
    int px = track_l + x;
    for (int y = hist_bottom - bar_h; y < hist_bottom; y++)
      lv_plot_px(pix, w, h, px, y, lv_rgba(0x90, 0x90, 0x90, 0xFF));
  }

  int prev_x = track_l;
  int prev_y = hist_bottom;
  uint8_t black_u8 = st->data.black;
  uint8_t white_u8 = st->data.white ? st->data.white : 255;
  float gamma = st->data.gamma > 0.0f ? st->data.gamma : 1.0f;
  for (int x = 0; x < track_w; x++) {
    float t = (float)x / (float)(MAX(1, track_w - 1));
    float black = (float)black_u8 / 255.0f;
    float white = (float)white_u8 / 255.0f;
    float range = MAX(white - black, 0.0001f);
    float c = CLAMP((t - black) / range, 0.0f, 1.0f);
    c = powf(c, MAX(gamma, 0.0001f));
    int y = hist_bottom - (int)lroundf(c * (float)hist_h);
    if (x > 0)
      lv_draw_line_px(pix, w, h, prev_x, prev_y, track_l + x, y,
                      get_sys_color(brFocusRing));
    prev_x = track_l + x;
    prev_y = y;
  }

  uint32_t edge = get_sys_color(brDarkEdge);
  for (int x = 0; x < w; x++) {
    lv_plot_px(pix, w, h, x, 0, edge);
    lv_plot_px(pix, w, h, x, h - 1, edge);
  }
  for (int y = 0; y < h; y++) {
    lv_plot_px(pix, w, h, 0, y, edge);
    lv_plot_px(pix, w, h, w - 1, y, edge);
  }

  st->graph_tex = R_CreateTextureRGBA(w, h, pix,
                                      R_FILTER_NEAREST, R_WRAP_CLAMP);
  free(pix);
  st->tex_w = w;
  st->tex_h = h;
  st->graph_dirty = false;
}

result_t lv_histogram_component_proc(window_t *win, uint32_t msg,
                                     uint32_t wparam, void *lparam) {
  lv_hist_state_t *st = (lv_hist_state_t *)win->userdata;
  (void)wparam;
  switch (msg) {
    case evCreate: {
      lv_hist_state_t *ns = allocate_window_data(win, sizeof(lv_hist_state_t));
      win->flags |= WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOTABSTOP;
      ns->data.black = 0;
      ns->data.white = 255;
      ns->data.gamma = 1.0f;
      ns->graph_dirty = true;
      return true;
    }
    case evDestroy:
      if (st && st->graph_tex) {
        R_DeleteTexture(st->graph_tex);
        st->graph_tex = 0;
      }
      return false;
    case lvGraphSetData:
      if (!st || !lparam) return false;
      st->data = *(const lv_graph_data_t *)lparam;
      st->graph_dirty = true;
      invalidate_window(win);
      return true;
    case evPaint:
      if (st) {
        irect16_t cr = get_client_rect(win);
        if (st->graph_dirty || !st->graph_tex ||
            st->tex_w != cr.w || st->tex_h != cr.h)
          lv_build_graph_texture(st, cr.w, cr.h);
        if (st->graph_tex)
          draw_rect(st->graph_tex, cr);
      }
      return true;
    default:
      return false;
  }
}
