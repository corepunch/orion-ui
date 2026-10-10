// VIEW: the equalizer window — EQMAIN.BMP artwork and the response graph painted here; ON/AUTO/PRESETS
// and the preamp and ten band sliders are SpriteButton / SpriteSlider children.

#include "winamp.h"

enum { SL_PREAMP = 1, SL_BAND0 = 2 };   // SL_BAND0 + band
#define EQ_STEPS 10                      // slider units per dB; range -12..+12 dB

static const wa_region_t kEqButtons[] = {
  { ID_EQ_TOGGLE, {  14, 18, 26, 12 } },
  { ID_EQ_AUTO,   {  40, 18, 32, 12 } },
  { ID_EQ_PRESET, { 217, 18, 44, 12 } },
};

typedef struct { wa_canvas_t canvas; } eq_view_t;

static irect16_t slider_rect(int id) {
  return id == SL_PREAMP ? R(21, 38, 14, 63) : R(78 + 18 * (id - SL_BAND0), 38, 14, 63);
}

static float slider_db(int id) { return id == SL_PREAMP ? g_app->preamp_db : g_app->eq_db[id - SL_BAND0]; }

static void eq_button_sprites(uint16_t id, sprite_button_t *d) {
  switch (id) {
    case ID_EQ_TOGGLE: skin_toggle_sprites(SKIN_EQMAIN, R(10, 119, 26, 12), R(128, 119, 26, 12), R(69, 119, 26, 12), R(187, 119, 26, 12), d); break;
    case ID_EQ_AUTO:   skin_toggle_sprites(SKIN_EQMAIN, R(36, 119, 32, 12), R(155, 119, 32, 12), R(95, 119, 32, 12), R(214, 119, 32, 12), d); break;
    default:           skin_button_sprites(SKIN_EQMAIN, R(224, 164, 44, 12), R(224, 176, 44, 12), d); break;
  }
}

// 28 track frames laid out 14 per row in EQMAIN.BMP; the frame follows the gain.
static void eq_slider_sprites(sprite_slider_t *d) {
  *d = (sprite_slider_t){
    .bm = g_app->skin.bmp[SKIN_EQMAIN], .native = { 14, 63 }, .track = R(13, 164, 14, 63), .frame_step = { 15, 65 },
    .frames = 28, .columns = 14, .thumb = R(0, 164, 11, 11), .thumb_down = R(0, 176, 11, 11),
    .travel = R(1, 0, 0, 51), .vertical = true,
  };
}

void eq_apply_skin(window_t *win) {
  for (int i = 0; i < (int)ARRAY_LEN(kEqButtons); i++) {
    window_t *c = get_window_item(win, kEqButtons[i].id);
    sprite_button_t d;
    eq_button_sprites(kEqButtons[i].id, &d);
    if (c) send_message(c, spbSetSprites, 0, &d);
  }
  sprite_slider_t d;
  eq_slider_sprites(&d);
  for (int id = SL_PREAMP; id < SL_BAND0 + WA_BANDS; id++) {
    window_t *c = get_window_item(win, id);
    if (c) send_message(c, spsSetSprites, 0, &d);
  }
}

static void eq_place(window_t *win) {
  for (int i = 0; i < (int)ARRAY_LEN(kEqButtons); i++) { window_t *c = get_window_item(win, kEqButtons[i].id); if (c) skin_place(c, kEqButtons[i].r); }
  for (int id = SL_PREAMP; id < SL_BAND0 + WA_BANDS; id++) { window_t *c = get_window_item(win, id); if (c) skin_place(c, slider_rect(id)); }
}

// A held slider keeps its own value; the rest follow the app. Changes only, because each setter repaints.
static void eq_sync(window_t *win) {
  for (int id = SL_PREAMP; id < SL_BAND0 + WA_BANDS; id++) {
    window_t *c = get_window_item(win, id);
    int want = (int)lroundf(slider_db(id) * EQ_STEPS);
    if (c && !send_message(c, spsIsDragging, 0, NULL) && skin_slider_pos(win, id) != want) send_message(c, slSetPos, 0, (void *)(intptr_t)want);
  }
  struct { uint16_t id; bool want; } toggles[] = { { ID_EQ_TOGGLE, g_app->engine.eq_on }, { ID_EQ_AUTO, g_app->eq_auto } };
  for (int i = 0; i < 2; i++) {
    window_t *c = get_window_item(win, toggles[i].id);
    if (c && (send_message(c, btnGetCheck, 0, NULL) != 0) != toggles[i].want) send_message(c, btnSetCheck, toggles[i].want, NULL);
  }
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
  canvas_blit(c, SKIN_EQMAIN, R(0, 0, WA_W, WA_EQ_H), 0, 0);
  canvas_blit(c, SKIN_EQMAIN, R(0, 134, WA_W, 14), 0, 0);
  canvas_blit(c, SKIN_EQMAIN, R(0, 116, 9, 9), 264, 3);
  draw_graph(c);
}

result_t win_winamp_equalizer(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  eq_view_t *v = win->userdata;
  switch (msg) {
    case evCreate:
      v = allocate_window_data(win, sizeof(eq_view_t));
      if (!v || !canvas_resize(&v->canvas, WA_W, WA_EQ_H)) return false;
      if (g_app) g_app->equalizer = win;
      for (int i = 0; i < (int)ARRAY_LEN(kEqButtons); i++) skin_add_control(win, "SpriteButton", kEqButtons[i].id);
      for (int id = SL_PREAMP; id < SL_BAND0 + WA_BANDS; id++) {
        window_t *c = skin_add_control(win, "SpriteSlider", (uint16_t)id);
        slider_range_t r = { -12 * EQ_STEPS, 12 * EQ_STEPS };
        if (c) send_message(c, slSetRange, 0, &r);
      }
      eq_apply_skin(win);
      return true;
    case evMeasure:
      skin_view_measure(g_app && g_app->show_eq && !g_app->landscape ? WA_EQ_H : 0, lparam);
      return true;
    case evResize:
      eq_place(win);
      return false;
    case evPaint:
      if (!g_app) return true;
      eq_sync(win);
      eq_paint(v);
      return false;                              // let the framework paint the child controls
    case evQueryDrag:
      return DRAG_NOW;
    case evCommand: {
      uint16_t id = LOWORD(wparam);
      if (HIWORD(wparam) == btnClicked) { app_command(id); return true; }
      if (HIWORD(wparam) != sliderValueChanged) return false;
      float db = skin_slider_pos(win, id) / (float)EQ_STEPS;
      if (fabsf(db) < 0.8f) {                    // detent at 0 dB
        db = 0;
        window_t *c = get_window_item(win, id);
        if (c) send_message(c, slSetPos, 0, (void *)(intptr_t)0);
      }
      app_set_eq(id == SL_PREAMP ? -1 : id - SL_BAND0, db);
      invalidate_window(win);
      return true;
    }
    case evDestroy:
      if (v) { canvas_free(&v->canvas); free(v); win->userdata = NULL; }
      if (g_app && g_app->equalizer == win) g_app->equalizer = NULL;
      return true;
    default:
      return false;
  }
}
