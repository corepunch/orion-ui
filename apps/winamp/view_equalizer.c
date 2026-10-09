// VIEW: the equalizer window — ON/AUTO/PRESETS, the response graph, the
// preamp and ten band sliders, drawn from EQMAIN.BMP.

#include "winamp.h"

enum { SL_PREAMP = 1, SL_BAND0 = 2 };   // SL_BAND0 + band

static const wa_region_t kButtons[] = {
  { ID_EQ_TOGGLE, {  14, 18, 26, 12 } },
  { ID_EQ_AUTO,   {  40, 18, 32, 12 } },
  { ID_EQ_PRESET, { 217, 18, 44, 12 } },
};

typedef struct {
  wa_canvas_t canvas;
  uint16_t pressed;
  bool inside;
} eq_view_t;

static irect16_t slider_rect(int id) {
  return id == SL_PREAMP ? R(21, 38, 14, 63) : R(78 + 18 * (id - SL_BAND0), 38, 14, 63);
}

static float slider_db(int id) { return id == SL_PREAMP ? g_app->preamp_db : g_app->eq_db[id - SL_BAND0]; }

static int eq_hit(ipoint16_t p) {
  for (int i = 0; i < (int)ARRAY_LEN(kButtons); i++) if (rect_contains_point(kButtons[i].r, p)) return kButtons[i].id;
  // Sliders take the whole column below the graph so narrow thumbs are easy to grab.
  for (int id = SL_PREAMP; id < SL_BAND0 + WA_BANDS; id++) {
    irect16_t r = slider_rect(id);
    if (rect_contains_point(R(r.x - 2, r.y - 4, r.w + 4, r.h + 8), p)) return id;
  }
  return 0;
}

static void draw_slider(wa_canvas_t *c, int id, bool pressed) {
  irect16_t r = slider_rect(id);
  float t = (slider_db(id) + 12.0f) / 24.0f;
  int frame = (int)lroundf(t * 27);
  canvas_blit(c, SKIN_EQMAIN, R(13 + (frame % 14) * 15, 164 + (frame / 14) * 65, 14, 63), r.x, r.y);
  canvas_blit(c, SKIN_EQMAIN, R(0, pressed ? 176 : 164, 11, 11), r.x + 1, r.y + (int)lroundf((1 - t) * 51));
}

// Response curve: linear between band points across the 113-px graph.
static void draw_graph(wa_canvas_t *c) {
  canvas_blit(c, SKIN_EQMAIN, R(0, 294, 113, 19), 86, 17);
  int pre = (int)lroundf((1 - (g_app->preamp_db + 12) / 24) * 18);
  canvas_blit(c, SKIN_EQMAIN, R(0, 314, 113, 1), 86, 17 + pre);
  int last = -1;
  for (int x = 0; x < 109; x++) {
    float at = x * (WA_BANDS - 1) / 108.0f;
    int b = MIN(WA_BANDS - 2, (int)at);
    float db = g_app->eq_db[b] + (g_app->eq_db[b + 1] - g_app->eq_db[b]) * (at - b);
    int y = (int)lroundf((1 - (db + 12) / 24) * 18);
    int y0 = last < 0 ? y : last;
    for (int yy = MIN(y, y0); yy <= MAX(y, y0); yy++)
      canvas_blit(c, SKIN_EQMAIN, R(115, 294 + yy, 1, 1), 88 + x, 17 + yy);
    last = y;
  }
}

static void eq_paint(eq_view_t *v) {
  wa_canvas_t *c = &v->canvas;
  bool down = v->inside;
  canvas_blit(c, SKIN_EQMAIN, R(0, 0, WA_W, WA_EQ_H), 0, 0);
  canvas_blit(c, SKIN_EQMAIN, R(0, 134, WA_W, 14), 0, 0);
  canvas_blit(c, SKIN_EQMAIN, R(0, 116, 9, 9), 264, 3);
  bool on = g_app->engine.eq_on;
  bool on_down = down && v->pressed == ID_EQ_TOGGLE, auto_down = down && v->pressed == ID_EQ_AUTO;
  canvas_blit(c, SKIN_EQMAIN, R(on ? (on_down ? 187 : 69) : (on_down ? 128 : 10), 119, 26, 12), 14, 18);
  canvas_blit(c, SKIN_EQMAIN, R(g_app->eq_auto ? (auto_down ? 214 : 95) : (auto_down ? 155 : 36), 119, 32, 12), 40, 18);
  canvas_blit(c, SKIN_EQMAIN, R(224, down && v->pressed == ID_EQ_PRESET ? 176 : 164, 44, 12), 217, 18);
  draw_graph(c);
  for (int id = SL_PREAMP; id < SL_BAND0 + WA_BANDS; id++) draw_slider(c, id, v->pressed == id);
}

static void eq_slider_move(eq_view_t *v, ipoint16_t p) {
  irect16_t r = slider_rect(v->pressed);
  float t = 1 - (float)(p.y - r.y - 5) / 51;
  float db = MAX(0.0f, MIN(1.0f, t)) * 24 - 12;
  if (fabsf(db) < 0.8f) db = 0;
  app_set_eq(v->pressed == SL_PREAMP ? -1 : v->pressed - SL_BAND0, db);
}

static bool eq_is_slider(uint16_t id) { return id >= SL_PREAMP && id < SL_BAND0 + WA_BANDS; }

result_t win_winamp_equalizer(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  eq_view_t *v = win->userdata;
  switch (msg) {
    case evCreate:
      v = allocate_window_data(win, sizeof(eq_view_t));
      if (!v || !canvas_resize(&v->canvas, WA_W, WA_EQ_H)) return false;
      if (g_app) g_app->equalizer = win;
      return true;
    case evMeasure:
      skin_view_measure(g_app && g_app->show_eq && !g_app->landscape ? WA_EQ_H : 0, lparam);
      return true;
    case evPaint:
      if (!g_app) return true;
      eq_paint(v);
      canvas_present(&v->canvas, R(0, 0, (int)lroundf(WA_W * g_app->pt_per_px), (int)lroundf(WA_EQ_H * g_app->pt_per_px)));
      return true;
    case evQueryDrag:
      return DRAG_NOW;
    case evLeftButtonDown: {
      ipoint16_t p = skin_point(win, &v->canvas, wparam);
      v->pressed = (uint16_t)eq_hit(p);
      v->inside = v->pressed != 0;
      if (!v->pressed) return true;
      set_capture(win);
      if (eq_is_slider(v->pressed)) eq_slider_move(v, p);
      invalidate_window(win);
      return true;
    }
    case evMouseMove: {
      if (!v->pressed) return false;
      ipoint16_t p = skin_point(win, &v->canvas, wparam);
      if (eq_is_slider(v->pressed)) eq_slider_move(v, p);
      else {
        for (int i = 0; i < (int)ARRAY_LEN(kButtons); i++)
          if (kButtons[i].id == v->pressed) v->inside = rect_contains_point(kButtons[i].r, p);
      }
      invalidate_window(win);
      return true;
    }
    case evLeftButtonUp: {
      if (!v->pressed) return false;
      uint16_t id = v->pressed;
      bool inside = v->inside;
      v->pressed = 0;
      v->inside = false;
      set_capture(NULL);
      if (!eq_is_slider(id) && inside) app_command(id);
      invalidate_window(win);
      return true;
    }
    case evPointerCancel:
      if (v && v->pressed) { v->pressed = 0; v->inside = false; set_capture(NULL); invalidate_window(win); }
      return true;
    case evDestroy:
      if (v) { canvas_free(&v->canvas); free(v); win->userdata = NULL; }
      if (g_app && g_app->equalizer == win) g_app->equalizer = NULL;
      return true;
    default:
      return false;
  }
}
