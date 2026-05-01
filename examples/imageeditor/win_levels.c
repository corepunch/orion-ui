// Levels dialog
//
// Shader-backed Levels adjustment for the active layer.  The dialog previews
// the edit live using the renderer's levels shader and bakes it into the layer
// texture when confirmed.

#include "imageeditor.h"

// Dialog layout
#define LV_CLIENT_W   280
#define LV_CLIENT_H   184

#define LV_PAD         10
#define LV_GRAPH_X     10
#define LV_GRAPH_Y     21
#define LV_GRAPH_W    260
#define LV_GRAPH_H     84

#define LV_BTN_W       48
#define LV_BTN_H     BUTTON_HEIGHT
#define LV_BTN_GAP      6
#define LV_BTN_Y     160
#define LV_OK_X      116
#define LV_RS_X      168
#define LV_CA_X      220

#define LV_IN_LBL_X    10
#define LV_IN_LBL_Y     4
#define LV_IN_EDIT_Y    0
#define LV_IN_EDIT_H   13
#define LV_IN_BLACK_X   84
#define LV_IN_GAMMA_X  122
#define LV_IN_WHITE_X  160
#define LV_IN_EDIT_W    34

#define LV_OUT_LBL_X    10
#define LV_OUT_LBL_Y   126
#define LV_OUT_EDIT_Y  126
#define LV_OUT_BLACK_X  84
#define LV_OUT_WHITE_X  160
#define LV_OUT_EDIT_W   34

#define LV_STRIP_BAR_Y      4
#define LV_STRIP_HANDLE_Y   8
#define LV_STRIP_BAR_H      8
#define LV_STRIP_H         13

#define LV_TRACK_L         8
#define LV_TRACK_R       (LV_GRAPH_W - 8)

#define LV_ID_GRAPH     1
#define LV_ID_OK        2
#define LV_ID_RESET     3
#define LV_ID_CANCEL    4
#define LV_ID_IN_BLACK   5
#define LV_ID_IN_GAMMA   6
#define LV_ID_IN_WHITE   7
#define LV_ID_OUT_BLACK  8
#define LV_ID_OUT_WHITE  9
#define LV_ID_IN_STRIP  10
#define LV_ID_OUT_STRIP 11

typedef enum {
  LV_HANDLE_IN_BLACK = 0,
  LV_HANDLE_IN_GAMMA = 1,
  LV_HANDLE_IN_WHITE = 2,
  LV_HANDLE_OUT_BLACK = 3,
  LV_HANDLE_OUT_WHITE = 4,
} lv_handle_t;

typedef struct {
  canvas_doc_t *doc;
  int layer_idx;
  uint8_t black;
  uint8_t white;
  float gamma;
  uint8_t out_black;
  uint8_t out_white;
  bool accepted;
  uint32_t hist[256];
  uint32_t hist_max;
  uint32_t graph_tex;
  bool graph_dirty;
  window_t *graph_win;
  window_t *in_strip_win;
  window_t *out_strip_win;
  window_t *in_black_edit;
  window_t *in_gamma_edit;
  window_t *in_white_edit;
  window_t *out_black_edit;
  window_t *out_white_edit;
  lv_handle_t dragging;
} lv_state_t;

static float lv_gamma_from_pos(float t) {
  return 0.1f + CLAMP(t, 0.0f, 1.0f) * 4.9f;
}

static float lv_pos_from_gamma(float gamma) {
  return CLAMP((gamma - 0.1f) / 4.9f, 0.0f, 1.0f);
}

static void lv_rebuild_histogram(lv_state_t *st) {
  memset(st->hist, 0, sizeof(st->hist));
  st->hist_max = 0;
  if (!st->doc || st->layer_idx < 0 || st->layer_idx >= st->doc->layer_count)
    return;
  const layer_t *lay = st->doc->layers[st->layer_idx];
  if (!lay || !lay->pixels) return;
  size_t n = (size_t)st->doc->canvas_w * st->doc->canvas_h;
  for (size_t i = 0; i < n; i++) {
    const uint8_t *px = lay->pixels + i * 4;
    uint8_t v = (uint8_t)((px[0] * 77 + px[1] * 150 + px[2] * 29) >> 8);
    uint32_t c = ++st->hist[v];
    if (c > st->hist_max) st->hist_max = c;
  }
}

static void lv_sync_preview(lv_state_t *st) {
  if (!st || !st->doc) return;
  ui_render_effect_params_t p = {{0}};
  p.f[0] = (float)st->black / 255.0f;
  p.f[1] = (float)st->white / 255.0f;
  p.f[2] = st->gamma;
  p.f[3] = (float)st->out_black / 255.0f;
  p.f[4] = (float)st->out_white / 255.0f;
  layer_set_preview_effect(st->doc, st->layer_idx, UI_RENDER_EFFECT_LEVELS, &p);
  st->graph_dirty = true;
  if (st->graph_win)
    invalidate_window(st->graph_win);
  if (st->in_strip_win)
    invalidate_window(st->in_strip_win);
  if (st->out_strip_win)
    invalidate_window(st->out_strip_win);
}

static void lv_set_defaults(lv_state_t *st) {
  st->black = 0;
  st->white = 255;
  st->gamma = 1.0f;
  st->out_black = 0;
  st->out_white = 255;
  lv_sync_preview(st);
}

static void lv_set_edit_text(window_t *edit, const char *text) {
  size_t n;
  if (!edit || !text) return;
  n = strlen(text);
  if (n >= sizeof(edit->title)) n = sizeof(edit->title) - 1;
  memcpy(edit->title, text, n);
  edit->title[n] = '\0';
  edit->cursor_pos = (uint32_t)n;
  edit->editing = false;
  invalidate_window(edit);
}

static void lv_sync_edit_fields(lv_state_t *st) {
  char buf[32];
  if (!st) return;
  snprintf(buf, sizeof(buf), "%u", (unsigned)st->black);
  lv_set_edit_text(st->in_black_edit, buf);
  snprintf(buf, sizeof(buf), "%.2f", st->gamma);
  lv_set_edit_text(st->in_gamma_edit, buf);
  snprintf(buf, sizeof(buf), "%u", (unsigned)st->white);
  lv_set_edit_text(st->in_white_edit, buf);
  snprintf(buf, sizeof(buf), "%u", (unsigned)st->out_black);
  lv_set_edit_text(st->out_black_edit, buf);
  snprintf(buf, sizeof(buf), "%u", (unsigned)st->out_white);
  lv_set_edit_text(st->out_white_edit, buf);
}

static bool lv_parse_u8_field(window_t *edit, uint8_t *out) {
  char *end = NULL;
  long v;
  if (!edit || !out) return false;
  v = strtol(edit->title, &end, 10);
  if (end == edit->title) return false;
  *out = (uint8_t)CLAMP(v, 0, 255);
  return true;
}

static bool lv_parse_gamma_field(window_t *edit, float *out) {
  char *end = NULL;
  float v;
  if (!edit || !out) return false;
  v = strtof(edit->title, &end);
  if (end == edit->title) return false;
  *out = CLAMP(v, 0.10f, 5.00f);
  return true;
}

static int lv_track_left(void) { return LV_TRACK_L; }
static int lv_track_right(void) { return LV_TRACK_R; }
static int lv_track_w(void) { return lv_track_right() - lv_track_left(); }

static int lv_black_x(const lv_state_t *st) {
  return lv_track_left() + (int)lroundf((float)st->black * lv_track_w() / 255.0f);
}

static int lv_white_x(const lv_state_t *st) {
  return lv_track_left() + (int)lroundf((float)st->white * lv_track_w() / 255.0f);
}

static int lv_gamma_x(const lv_state_t *st) {
  int bx = lv_black_x(st);
  int wx = lv_white_x(st);
  float t = lv_pos_from_gamma(st->gamma);
  return bx + (int)lroundf((float)(wx - bx) * t);
}

static int lv_out_black_x(const lv_state_t *st) {
  return lv_track_left() + (int)lroundf((float)st->out_black * lv_track_w() / 255.0f);
}

static int lv_out_white_x(const lv_state_t *st) {
  return lv_track_left() + (int)lroundf((float)st->out_white * lv_track_w() / 255.0f);
}

static void lv_draw_handle(int x, int y, uint32_t col, bool active) {
  fill_rect(active ? get_sys_color(brFocusRing) : get_sys_color(brDarkEdge),
            R(x - 2, y - 2, 5, 13));
  fill_rect(col, R(x - 1, y - 1, 3, 11));
}

static void lv_plot_px(uint32_t *pix, int x, int y, uint32_t col);

static void lv_draw_line_px(uint32_t *pix, int x0, int y0, int x1, int y1, uint32_t col) {
  int dx = abs(x1 - x0);
  int sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0);
  int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    lv_plot_px(pix, x0, y0, col);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

static void lv_plot_px(uint32_t *pix, int x, int y, uint32_t col) {
  if (x < 0 || y < 0 || x >= LV_GRAPH_W || y >= LV_GRAPH_H) return;
  pix[(size_t)y * LV_GRAPH_W + x] = col;
}

static void lv_build_graph_texture(lv_state_t *st) {
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

  // Border
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
  int track_l = lv_track_left();
  int track_w = lv_track_w();
  uint32_t maxv = st->hist_max ? st->hist_max : 1;

  // Histogram bars.
  for (int x = 0; x < LV_GRAPH_W - 16; x++) {
    int bin = x * 256 / (LV_GRAPH_W - 16);
    uint32_t v = st->hist[bin];
    int bar_h = (int)lroundf((float)v * (float)hist_h / (float)maxv);
    if (bar_h > hist_h) bar_h = hist_h;
    int px = track_l + x;
    for (int y = hist_bottom - bar_h; y < hist_bottom; y++)
      lv_plot_px(pix, px, y, MAKE_COLOR(0x90, 0x90, 0x90, 0xFF));
  }

  // Levels curve preview.
  int prev_x = track_l;
  int prev_y = hist_bottom;
  for (int x = 0; x < track_w; x++) {
    float t = (float)x / (float)(MAX(1, track_w - 1));
    float black = (float)st->black / 255.0f;
    float white = (float)st->white / 255.0f;
    float range = MAX(white - black, 0.0001f);
    float c = CLAMP((t - black) / range, 0.0f, 1.0f);
    c = powf(c, MAX(st->gamma, 0.0001f));
    int y = hist_bottom - (int)lroundf(c * (float)hist_h);
    if (x > 0)
      lv_draw_line_px(pix, prev_x, prev_y, track_l + x, y, get_sys_color(brFocusRing));
    prev_x = track_l + x;
    prev_y = y;
  }

  st->graph_tex = R_CreateTextureRGBA(LV_GRAPH_W, LV_GRAPH_H, pix,
                                      R_FILTER_NEAREST, R_WRAP_CLAMP);
  free(pix);
  st->graph_dirty = false;
}

static void lv_draw_graph(window_t *win, lv_state_t *st) {
  (void)win;
  if (!st) return;
  if (st->graph_dirty || !st->graph_tex)
    lv_build_graph_texture(st);
  if (st->graph_tex)
    draw_rect(st->graph_tex, R(0, 0, LV_GRAPH_W, LV_GRAPH_H));
}

static void lv_draw_strip_window(window_t *win, lv_state_t *st, bool output) {
  if (!st) return;
  (void)win;
  fill_rect(get_sys_color(brWindowBg), R(0, 0, LV_GRAPH_W, LV_STRIP_H));
  int track_l = lv_track_left();
  int track_w = lv_track_w();
  draw_gradient_rect(R(track_l, LV_STRIP_BAR_Y, track_w, LV_STRIP_BAR_H),
                     MAKE_COLOR(0x00, 0x00, 0x00, 0xFF),
                     MAKE_COLOR(0xFF, 0xFF, 0xFF, 0xFF));
  if (!output) {
    lv_draw_handle(lv_black_x(st), LV_STRIP_HANDLE_Y, 0xFF000000, st->dragging == LV_HANDLE_IN_BLACK);
    lv_draw_handle(lv_gamma_x(st), LV_STRIP_HANDLE_Y, 0xFF7F7F7F, st->dragging == LV_HANDLE_IN_GAMMA);
    lv_draw_handle(lv_white_x(st), LV_STRIP_HANDLE_Y, 0xFFFFFFFF, st->dragging == LV_HANDLE_IN_WHITE);
  } else {
    lv_draw_handle(lv_out_black_x(st), LV_STRIP_HANDLE_Y, 0xFF000000, st->dragging == LV_HANDLE_OUT_BLACK);
    lv_draw_handle(lv_out_white_x(st), LV_STRIP_HANDLE_Y, 0xFFFFFFFF, st->dragging == LV_HANDLE_OUT_WHITE);
  }
}

static lv_handle_t lv_hit_input_handle(const lv_state_t *st, int mx, int my) {
  int bx = lv_black_x(st);
  int gx = lv_gamma_x(st);
  int wx = lv_white_x(st);
  if (my < LV_STRIP_HANDLE_Y - 2 || my > LV_STRIP_HANDLE_Y + 12) return -1;
  if (abs(mx - bx) <= 4) return LV_HANDLE_IN_BLACK;
  if (abs(mx - gx) <= 4) return LV_HANDLE_IN_GAMMA;
  if (abs(mx - wx) <= 4) return LV_HANDLE_IN_WHITE;
  if (mx < gx)
    return (abs(mx - bx) <= abs(mx - gx)) ? LV_HANDLE_IN_BLACK : LV_HANDLE_IN_GAMMA;
  return (abs(mx - gx) <= abs(mx - wx)) ? LV_HANDLE_IN_GAMMA : LV_HANDLE_IN_WHITE;
}

static lv_handle_t lv_hit_output_handle(const lv_state_t *st, int mx, int my) {
  int bx = lv_out_black_x(st);
  int wx = lv_out_white_x(st);
  if (my < LV_STRIP_HANDLE_Y - 2 || my > LV_STRIP_HANDLE_Y + 12) return -1;
  if (abs(mx - bx) <= 4) return LV_HANDLE_OUT_BLACK;
  if (abs(mx - wx) <= 4) return LV_HANDLE_OUT_WHITE;
  return (abs(mx - bx) <= abs(mx - wx)) ? LV_HANDLE_OUT_BLACK : LV_HANDLE_OUT_WHITE;
}

static void lv_apply_input_drag(lv_state_t *st, lv_handle_t h, int mx) {
  int track_l = lv_track_left();
  int track_w = lv_track_w();
  int raw = CLAMP(mx - track_l, 0, track_w);
  int val = (int)lroundf((float)raw * 255.0f / (float)track_w);
  switch (h) {
    case LV_HANDLE_IN_BLACK:
      if (val >= st->white) val = st->white - 1;
      st->black = (uint8_t)CLAMP(val, 0, 254);
      break;
    case LV_HANDLE_IN_WHITE:
      if (val <= st->black) val = st->black + 1;
      st->white = (uint8_t)CLAMP(val, 1, 255);
      break;
    case LV_HANDLE_IN_GAMMA: {
      int bx = st->black;
      int wx = st->white;
      int range = MAX(1, wx - bx);
      float t = CLAMP((float)(val - bx) / (float)range, 0.0f, 1.0f);
      st->gamma = lv_gamma_from_pos(t);
      break;
    }
    case LV_HANDLE_OUT_BLACK:
    case LV_HANDLE_OUT_WHITE:
    default:
      break;
  }
  lv_sync_preview(st);
  lv_sync_edit_fields(st);
}

static void lv_apply_output_drag(lv_state_t *st, lv_handle_t h, int mx) {
  int track_l = lv_track_left();
  int track_w = lv_track_w();
  int raw = CLAMP(mx - track_l, 0, track_w);
  int val = (int)lroundf((float)raw * 255.0f / (float)track_w);
  switch (h) {
    case LV_HANDLE_OUT_BLACK:
      if (val >= st->out_white) val = st->out_white - 1;
      st->out_black = (uint8_t)CLAMP(val, 0, 254);
      break;
    case LV_HANDLE_OUT_WHITE:
      if (val <= st->out_black) val = st->out_black + 1;
      st->out_white = (uint8_t)CLAMP(val, 1, 255);
      break;
    default:
      break;
  }
  lv_sync_preview(st);
  lv_sync_edit_fields(st);
}

static void lv_apply_drag(lv_state_t *st, lv_handle_t h, int mx) {
  if (h == LV_HANDLE_OUT_BLACK || h == LV_HANDLE_OUT_WHITE)
    lv_apply_output_drag(st, h, mx);
  else
    lv_apply_input_drag(st, h, mx);
}

static result_t lv_graph_proc(window_t *win, uint32_t msg,
                              uint32_t wparam, void *lparam) {
  lv_state_t *st = (lv_state_t *)win->userdata;
  switch (msg) {
    case evCreate:
      win->userdata = lparam;
      return true;
    case evPaint:
      if (st) lv_draw_graph(win, st);
      return true;
    default:
      return false;
  }
}

static lv_handle_t lv_hit_strip_input(const lv_state_t *st, int mx, int my) {
  return lv_hit_input_handle(st, mx, my);
}

static lv_handle_t lv_hit_strip_output(const lv_state_t *st, int mx, int my) {
  return lv_hit_output_handle(st, mx, my);
}

static result_t lv_strip_proc(window_t *win, uint32_t msg,
                              uint32_t wparam, void *lparam) {
  lv_state_t *st = (lv_state_t *)win->userdata;
  bool output = (win->id == LV_ID_OUT_STRIP);
  switch (msg) {
    case evCreate:
      win->userdata = lparam;
      return true;
    case evPaint:
      if (st) lv_draw_strip_window(win, st, output);
      return true;
    case evLeftButtonDown: {
      if (!st) return false;
      int mx = (int16_t)LOWORD(wparam);
      int my = (int16_t)HIWORD(wparam);
      lv_handle_t h = output ? lv_hit_strip_output(st, mx, my) : lv_hit_strip_input(st, mx, my);
      if (h >= 0) {
        st->dragging = h;
        set_capture(win);
        lv_apply_drag(st, h, mx);
        invalidate_window(win);
        return true;
      }
      return false;
    }
    case evMouseMove: {
      if (!st || st->dragging < 0) return false;
      int mx = (int16_t)LOWORD(wparam);
      lv_apply_drag(st, st->dragging, mx);
      invalidate_window(win);
      return true;
    }
    case evLeftButtonUp:
      if (st && st->dragging >= 0) {
        st->dragging = -1;
        set_capture(NULL);
        return true;
      }
      return false;
    default:
      return false;
  }
}

static result_t levels_dlg_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam) {
  lv_state_t *st = (lv_state_t *)win->userdata;
  switch (msg) {
    case evCreate: {
      st = (lv_state_t *)lparam;
      win->userdata = st;

      irect16_t in_lbl_r = { LV_IN_LBL_X, LV_IN_LBL_Y, 0, LV_IN_EDIT_H };
      irect16_t in_black_r = { LV_IN_BLACK_X, LV_IN_EDIT_Y, LV_IN_EDIT_W, LV_IN_EDIT_H };
      irect16_t in_gamma_r = { LV_IN_GAMMA_X, LV_IN_EDIT_Y, LV_IN_EDIT_W, LV_IN_EDIT_H };
      irect16_t in_white_r = { LV_IN_WHITE_X, LV_IN_EDIT_Y, LV_IN_EDIT_W, LV_IN_EDIT_H };
      irect16_t out_lbl_r = { LV_OUT_LBL_X, LV_OUT_LBL_Y, 0, LV_IN_EDIT_H };
      irect16_t out_black_r = { LV_OUT_BLACK_X, LV_OUT_EDIT_Y, LV_OUT_EDIT_W, LV_IN_EDIT_H };
      irect16_t out_white_r = { LV_OUT_WHITE_X, LV_OUT_EDIT_Y, LV_OUT_EDIT_W, LV_IN_EDIT_H };

      create_window("Input Levels:", WINDOW_NOTITLE | WINDOW_NOFILL,
                    &in_lbl_r, win, win_label, 0, NULL);

      st->in_black_edit = create_window("0", 0,
                                        &in_black_r,
                                        win, win_textedit, 0, NULL);
      if (st->in_black_edit) st->in_black_edit->id = LV_ID_IN_BLACK;
      st->in_gamma_edit = create_window("1.00", 0,
                                        &in_gamma_r,
                                        win, win_textedit, 0, NULL);
      if (st->in_gamma_edit) st->in_gamma_edit->id = LV_ID_IN_GAMMA;
      st->in_white_edit = create_window("255", 0,
                                        &in_white_r,
                                        win, win_textedit, 0, NULL);
      if (st->in_white_edit) st->in_white_edit->id = LV_ID_IN_WHITE;

      irect16_t graph_r = R(LV_GRAPH_X, LV_GRAPH_Y, LV_GRAPH_W, LV_GRAPH_H);
      window_t *graph = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL,
                                      &graph_r, win, lv_graph_proc, 0, st);
      if (graph) {
        graph->id = LV_ID_GRAPH;
        graph->notabstop = true;
        st->graph_win = graph;
      }

      irect16_t in_strip_r = { LV_GRAPH_X, LV_GRAPH_Y + LV_GRAPH_H + 4, LV_GRAPH_W, LV_STRIP_H };
      window_t *in_strip = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL,
                                         &in_strip_r, win, lv_strip_proc, 0, st);
      if (in_strip) {
        in_strip->id = LV_ID_IN_STRIP;
        in_strip->notabstop = true;
        st->in_strip_win = in_strip;
      }

      create_window("Output Levels:", WINDOW_NOTITLE | WINDOW_NOFILL,
                    &out_lbl_r, win, win_label, 0, NULL);

      st->out_black_edit = create_window("0", 0,
                                         &out_black_r,
                                         win, win_textedit, 0, NULL);
      if (st->out_black_edit) st->out_black_edit->id = LV_ID_OUT_BLACK;
      st->out_white_edit = create_window("255", 0,
                                         &out_white_r,
                                         win, win_textedit, 0, NULL);
      if (st->out_white_edit) st->out_white_edit->id = LV_ID_OUT_WHITE;

      irect16_t out_strip_r = { LV_GRAPH_X, LV_OUT_EDIT_Y + LV_IN_EDIT_H + 4, LV_GRAPH_W, LV_STRIP_H };
      window_t *out_strip = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL,
                                          &out_strip_r, win, lv_strip_proc, 0, st);
      if (out_strip) {
        out_strip->id = LV_ID_OUT_STRIP;
        out_strip->notabstop = true;
        st->out_strip_win = out_strip;
      }

      window_t *ok = create_window("OK", 0, MAKERECT(LV_OK_X, LV_BTN_Y, LV_BTN_W, LV_BTN_H),
                                   win, win_button, 0, NULL);
      if (ok) ok->id = LV_ID_OK;
      window_t *rs = create_window("Reset", 0, MAKERECT(LV_RS_X, LV_BTN_Y, LV_BTN_W, LV_BTN_H),
                                   win, win_button, 0, NULL);
      if (rs) rs->id = LV_ID_RESET;
      window_t *ca = create_window("Cancel", 0, MAKERECT(LV_CA_X, LV_BTN_Y, LV_BTN_W, LV_BTN_H),
                                   win, win_button, 0, NULL);
      if (ca) ca->id = LV_ID_CANCEL;
      lv_rebuild_histogram(st);
      lv_sync_edit_fields(st);
      lv_sync_preview(st);
      return true;
    }

    case evCommand:
      if (!st) return false;
      {
        window_t *src = (window_t *)lparam;
        if (!src) return false;
        if (HIWORD(wparam) == edUpdate) {
          if (src == st->in_black_edit) {
            lv_parse_u8_field(src, &st->black);
            if (st->black >= st->white) st->black = (uint8_t)(st->white ? st->white - 1 : 0);
            lv_sync_edit_fields(st);
            lv_sync_preview(st);
            return true;
          }
          if (src == st->in_gamma_edit) {
            lv_parse_gamma_field(src, &st->gamma);
            lv_sync_edit_fields(st);
            lv_sync_preview(st);
            return true;
          }
          if (src == st->in_white_edit) {
            lv_parse_u8_field(src, &st->white);
            if (st->white <= st->black) st->white = (uint8_t)(st->black + 1);
            lv_sync_edit_fields(st);
            lv_sync_preview(st);
            return true;
          }
          if (src == st->out_black_edit) {
            lv_parse_u8_field(src, &st->out_black);
            if (st->out_black >= st->out_white) st->out_black = (uint8_t)(st->out_white ? st->out_white - 1 : 0);
            lv_sync_edit_fields(st);
            lv_sync_preview(st);
            return true;
          }
          if (src == st->out_white_edit) {
            lv_parse_u8_field(src, &st->out_white);
            if (st->out_white <= st->out_black) st->out_white = (uint8_t)(st->out_black + 1);
            lv_sync_edit_fields(st);
            lv_sync_preview(st);
            return true;
          }
          return false;
        }
        if (HIWORD(wparam) != btnClicked) return false;
        if (src->id == LV_ID_OK) {
          st->accepted = true;
          end_dialog(win, 1);
          return true;
        }
        if (src->id == LV_ID_CANCEL) {
          layer_clear_preview_effect(st->doc, st->layer_idx);
          end_dialog(win, 0);
          return true;
        }
        if (src->id == LV_ID_RESET) {
          lv_set_defaults(st);
          lv_sync_edit_fields(st);
          if (st->graph_win) invalidate_window(st->graph_win);
          return true;
        }
      }
      return false;

    case evDestroy:
      if (st && st->graph_tex) {
        R_DeleteTexture(st->graph_tex);
        st->graph_tex = 0;
      }
      if (st && !st->accepted)
        layer_clear_preview_effect(st->doc, st->layer_idx);
      return false;

    default:
      return false;
  }
}

bool show_levels_dialog(window_t *parent) {
  if (!g_app || !g_app->active_doc) return false;
  canvas_doc_t *doc = g_app->active_doc;
  if (doc->active_layer < 0 || doc->active_layer >= doc->layer_count) return false;

  lv_state_t st;
  memset(&st, 0, sizeof(st));
  st.doc = doc;
  st.layer_idx = doc->active_layer;
  st.gamma = 1.0f;
  st.black = 0;
  st.white = 255;
  st.out_black = 0;
  st.out_white = 255;
  st.dragging = -1;
  lv_rebuild_histogram(&st);

  irect16_t wr = {0, 0, LV_CLIENT_W, LV_CLIENT_H};
  adjust_window_rect(&wr, WINDOW_DIALOG | WINDOW_NOTRAYBUTTON);
  uint32_t res = show_dialog_ex("Levels", wr.w, wr.h, parent,
                                WINDOW_DIALOG | WINDOW_NOTRAYBUTTON,
                                levels_dlg_proc, &st);
  if (!res || !st.accepted) return false;
  if (!layer_commit_preview_effect(doc, st.layer_idx)) {
    layer_clear_preview_effect(doc, st.layer_idx);
    return false;
  }
  return true;
}
