#include <math.h>

#include "../../../ui.h"

#include "lv_cmpn.h"
#include "lv_plug.h"

typedef struct {
  lv_graph_data_t data;
  uint32_t graph_tex;
  bool graph_dirty;
} lv_hist_state_t;

static int lv_track_w(void) {
  return LV_TRACK_R - LV_TRACK_L;
}

static uint32_t lv_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  return ((uint32_t)a << 24) | ((uint32_t)b << 16) |
         ((uint32_t)g << 8) | (uint32_t)r;
}

static void lv_plot_px(uint32_t *pix, int x, int y, uint32_t col) {
  if (x < 0 || y < 0 || x >= LV_GRAPH_W || y >= LV_GRAPH_H) return;
  pix[(size_t)y * LV_GRAPH_W + x] = col;
}

static void lv_draw_line_px(uint32_t *pix, int x0, int y0, int x1, int y1, uint32_t col) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    lv_plot_px(pix, x0, y0, col);
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

static void lv_build_graph_texture(lv_hist_state_t *st) {
  if (!st) return;
  if (st->graph_tex) {
    R_DeleteTexture(st->graph_tex);
    st->graph_tex = 0;
  }

  size_t count = (size_t)LV_GRAPH_W * LV_GRAPH_H;
  uint32_t *pix = malloc(count * sizeof(uint32_t));
  if (!pix) return;

  uint32_t bg = get_sys_color(brWindowBg);
  for (size_t i = 0; i < count; i++)
    pix[i] = bg;

  uint32_t edge = get_sys_color(brDarkEdge);
  for (int x = 0; x < LV_GRAPH_W; x++) {
    lv_plot_px(pix, x, 0, edge);
    lv_plot_px(pix, x, LV_GRAPH_H - 1, edge);
  }
  for (int y = 0; y < LV_GRAPH_H; y++) {
    lv_plot_px(pix, 0, y, edge);
    lv_plot_px(pix, LV_GRAPH_W - 1, y, edge);
  }

  int hist_top = 8;
  int hist_bottom = LV_GRAPH_H - 4;
  int hist_h = hist_bottom - hist_top;
  int track_w = lv_track_w();
  uint32_t maxv = st->data.hist_max ? st->data.hist_max : 1;

  for (int x = 0; x < LV_GRAPH_W - 16; x++) {
    int bin = x * 256 / (LV_GRAPH_W - 16);
    uint32_t v = st->data.hist[bin];
    int bar_h = (int)lroundf((float)v * (float)hist_h / (float)maxv);
    if (bar_h > hist_h) bar_h = hist_h;
    int px = LV_TRACK_L + x;
    for (int y = hist_bottom - bar_h; y < hist_bottom; y++)
      lv_plot_px(pix, px, y, lv_rgba(0x90, 0x90, 0x90, 0xFF));
  }

  int prev_x = LV_TRACK_L;
  int prev_y = hist_bottom;
  for (int x = 0; x < track_w; x++) {
    float t = (float)x / (float)(MAX(1, track_w - 1));
    float black = (float)st->data.black / 255.0f;
    float white = (float)st->data.white / 255.0f;
    float range = MAX(white - black, 0.0001f);
    float c = CLAMP((t - black) / range, 0.0f, 1.0f);
    c = powf(c, MAX(st->data.gamma, 0.0001f));
    int y = hist_bottom - (int)lroundf(c * (float)hist_h);
    if (x > 0)
      lv_draw_line_px(pix, prev_x, prev_y, LV_TRACK_L + x, y, get_sys_color(brFocusRing));
    prev_x = LV_TRACK_L + x;
    prev_y = y;
  }

  st->graph_tex = R_CreateTextureRGBA(LV_GRAPH_W, LV_GRAPH_H, pix,
                                      R_FILTER_NEAREST, R_WRAP_CLAMP);
  free(pix);
  st->graph_dirty = false;
}

result_t lv_histogram_component_proc(window_t *win, uint32_t msg,
                                     uint32_t wparam, void *lparam) {
  lv_hist_state_t *st = (lv_hist_state_t *)win->userdata;
  (void)wparam;
  switch (msg) {
    case evCreate: {
      lv_hist_state_t *ns = allocate_window_data(win, sizeof(lv_hist_state_t));
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
        if (st->graph_dirty || !st->graph_tex)
          lv_build_graph_texture(st);
        if (st->graph_tex)
          draw_rect(st->graph_tex, R(0, 0, LV_GRAPH_W, LV_GRAPH_H));
      }
      return true;
    default:
      return false;
  }
}
