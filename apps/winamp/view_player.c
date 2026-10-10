// VIEW: the main window — MAIN.BMP artwork, time, visualizer and marquee painted here; the
// transport, toggles and sliders are SpriteButton / SpriteSlider children laid out at Winamp 2 coordinates.

#include "winamp.h"

enum { SL_VOLUME = 1, SL_BALANCE, SL_POSITION };
#define VOL_STEPS 100                      // slider range: volume 0..100, balance -100..100, position 0..1000
#define POS_STEPS 1000

// Transport buttons (CBUTTONS.BMP: up sprite on row 0, pressed on row 1) and toggles, in skin pixels.
static const wa_region_t kPlayerButtons[] = {
  { ID_PLAYBACK_PREV,    {  16,  88, 23, 18 } }, { ID_PLAYBACK_PLAY,   {  39,  88, 23, 18 } },
  { ID_PLAYBACK_PAUSE,   {  62,  88, 23, 18 } }, { ID_PLAYBACK_STOP,   {  85,  88, 23, 18 } },
  { ID_PLAYBACK_NEXT,    { 108,  88, 22, 18 } }, { ID_PLAYBACK_OPEN,   { 136,  89, 22, 16 } },
  { ID_OPTIONS_SHUFFLE,  { 164,  89, 47, 15 } }, { ID_OPTIONS_REPEAT,  { 210,  89, 28, 15 } },
  { ID_WINDOW_EQUALIZER, { 219,  58, 23, 12 } }, { ID_WINDOW_PLAYLIST, { 242,  58, 23, 12 } },
};
static const wa_region_t kPlayerSliders[] = {
  { SL_VOLUME, { 107, 57, 68, 13 } }, { SL_BALANCE, { 177, 57, 38, 13 } }, { SL_POSITION, { 16, 72, 248, 10 } },
};
static const irect16_t kTimeRect = { 36, 26, 63, 13 };   // click toggles elapsed / remaining

typedef struct {
  wa_canvas_t canvas;
  bool time_down;
} player_view_t;

static void player_button_sprites(uint16_t id, sprite_button_t *d) {
  irect16_t transport[] = { {0, 0, 23, 18}, {23, 0, 23, 18}, {46, 0, 23, 18}, {69, 0, 23, 18}, {92, 0, 22, 18}, {114, 0, 22, 16} };
  for (int i = 0; i < 6; i++) {
    if (kPlayerButtons[i].id != id) continue;
    irect16_t down = transport[i];
    down.y += down.h;
    skin_button_sprites(SKIN_CBUTTONS, transport[i], down, d);
    return;
  }
  switch (id) {
    case ID_OPTIONS_SHUFFLE:  skin_toggle_sprites(SKIN_SHUFREP, R(28, 0, 47, 15), R(28, 15, 47, 15), R(28, 30, 47, 15), R(28, 45, 47, 15), d); break;
    case ID_OPTIONS_REPEAT:   skin_toggle_sprites(SKIN_SHUFREP, R(0, 0, 28, 15),  R(0, 15, 28, 15),  R(0, 30, 28, 15),  R(0, 45, 28, 15),  d); break;
    case ID_WINDOW_EQUALIZER: skin_toggle_sprites(SKIN_SHUFREP, R(0, 61, 23, 12), R(46, 61, 23, 12), R(0, 73, 23, 12),  R(46, 73, 23, 12), d); break;
    default:                  skin_toggle_sprites(SKIN_SHUFREP, R(23, 61, 23, 12), R(69, 61, 23, 12), R(23, 73, 23, 12), R(69, 73, 23, 12), d); break;
  }
}

static void player_slider_sprites(uint16_t id, sprite_slider_t *d) {
  bool pos = id == SL_POSITION;
  int sheet = pos ? SKIN_POSBAR : id == SL_VOLUME ? SKIN_VOLUME : SKIN_BALANCE;
  *d = (sprite_slider_t){
    .bm = g_app->skin.bmp[sheet], .native = { kPlayerSliders[id - 1].r.w, kPlayerSliders[id - 1].r.h }, .columns = 1,
    .track = pos ? R(0, 0, 248, 10) : id == SL_VOLUME ? R(0, 0, 68, 13) : R(9, 0, 38, 13),
    .frame_step = { 0, 15 }, .frames = pos ? 1 : 28,
    .thumb = pos ? R(248, 0, 29, 10) : R(15, 422, 14, 11), .thumb_down = pos ? R(278, 0, 29, 10) : R(0, 422, 14, 11),
    .travel = pos ? R(0, 0, 219, 0) : R(0, 1, id == SL_VOLUME ? 54 : 24, 0), .by_magnitude = id == SL_BALANCE,
  };
}

static void player_apply_skin(window_t *win) {
  for (int i = 0; i < (int)ARRAY_LEN(kPlayerButtons); i++) {
    window_t *c = get_window_item(win, kPlayerButtons[i].id);
    sprite_button_t d;
    player_button_sprites(kPlayerButtons[i].id, &d);
    if (c) send_message(c, spbSetSprites, 0, &d);
  }
  for (int i = 0; i < (int)ARRAY_LEN(kPlayerSliders); i++) {
    window_t *c = get_window_item(win, kPlayerSliders[i].id);
    sprite_slider_t d;
    player_slider_sprites(kPlayerSliders[i].id, &d);
    if (c) send_message(c, spsSetSprites, 0, &d);
  }
}

static void player_place(window_t *win) {
  for (int i = 0; i < (int)ARRAY_LEN(kPlayerButtons); i++) { window_t *c = get_window_item(win, kPlayerButtons[i].id); if (c) skin_place(c, kPlayerButtons[i].r); }
  for (int i = 0; i < (int)ARRAY_LEN(kPlayerSliders); i++) { window_t *c = get_window_item(win, kPlayerSliders[i].id); if (c) skin_place(c, kPlayerSliders[i].r); }
}

// Brings the controls in line with the app state; a held slider keeps its own value. Changes only,
// because each setter repaints.
static void player_sync(window_t *win) {
  wa_engine_t *e = &g_app->engine;
  bool loaded = e->dec != NULL;
  struct { uint16_t id; int want; } sliders[] = {
    { SL_VOLUME, (int)lroundf(e->volume * VOL_STEPS) }, { SL_BALANCE, (int)lroundf(e->balance * VOL_STEPS) },
    { SL_POSITION, (int)lroundf(app_position() * POS_STEPS) },
  };
  for (int i = 0; i < 3; i++) {
    window_t *c = get_window_item(win, sliders[i].id);
    if (!c || skin_slider_dragging(win, sliders[i].id) || skin_slider_pos(win, sliders[i].id) == sliders[i].want) continue;
    send_message(c, slSetPos, 0, (void *)(intptr_t)sliders[i].want);
  }
  struct { uint16_t id; bool want; } toggles[] = {
    { ID_OPTIONS_SHUFFLE, g_app->shuffle }, { ID_OPTIONS_REPEAT, g_app->repeat },
    { ID_WINDOW_EQUALIZER, g_app->show_eq }, { ID_WINDOW_PLAYLIST, g_app->show_pl },
  };
  for (int i = 0; i < 4; i++) {
    window_t *c = get_window_item(win, toggles[i].id);
    if (c && (send_message(c, btnGetCheck, 0, NULL) != 0) != toggles[i].want) send_message(c, btnSetCheck, toggles[i].want, NULL);
  }
  window_t *pos = get_window_item(win, SL_POSITION);
  if (pos) send_message(pos, spsSetThumbVisible, loaded && e->state != WA_STOPPED, NULL);
}

static void draw_time(wa_canvas_t *c, wa_engine_t *e) {
  if (e->state == WA_STOPPED || !e->dec) return;
  if (e->state == WA_PAUSED && (g_app->tick / 15) % 2) return;     // Winamp blinks a paused clock
  int s = app_elapsed_seconds();
  if (g_app->time_remaining) { s = MAX(0, app_track_seconds() - s); canvas_blit(c, SKIN_NUMBERS, R(20, 6, 5, 1), 36, 32); }
  int m = MIN(99, s / 60);
  canvas_digit(c, m / 10, 48, 26);
  canvas_digit(c, m % 10, 60, 26);
  canvas_digit(c, s % 60 / 10, 78, 26);
  canvas_digit(c, s % 10, 90, 26);
}

static void draw_visualizer(wa_canvas_t *c, wa_engine_t *e) {
  const uint32_t *vis = g_app->skin.vis;
  canvas_fill(c, R(24, 43, 76, 16), vis[0]);
  for (int y = 1; y < 16; y += 2)
    for (int x = 1; x < 76; x += 2) canvas_fill(c, R(24 + x, 43 + y, 1, 1), vis[1]);
  if (e->state == WA_STOPPED) return;
  for (int b = 0; b < WA_VIS_BARS; b++) {
    int x = 24 + b * 4;
    for (int r = 0; r < g_app->vis_bars[b]; r++) canvas_fill(c, R(x, 58 - r, 3, 1), vis[17 - r]);
    if (g_app->vis_peaks[b] > 0) canvas_fill(c, R(x, 58 - MIN(15, g_app->vis_peaks[b]), 3, 1), vis[23]);
  }
}

static void marquee(window_t *win, char *buf, size_t n, int *scroll) {
  *scroll = g_app->marquee_px / 2;
  if (skin_slider_dragging(win, SL_VOLUME)) { snprintf(buf, n, "VOLUME: %d%%", skin_slider_pos(win, SL_VOLUME)); *scroll = 0; return; }
  if (skin_slider_dragging(win, SL_BALANCE)) {
    int bal = skin_slider_pos(win, SL_BALANCE), pct = abs(bal);
    if (g_app->engine.balance == 0) snprintf(buf, n, "BALANCE: CENTER"); else snprintf(buf, n, "BALANCE: %d%% %s", pct, bal < 0 ? "LEFT" : "RIGHT");
    *scroll = 0;
    return;
  }
  if (skin_slider_dragging(win, SL_POSITION)) {
    float f = skin_slider_pos(win, SL_POSITION) / (float)POS_STEPS;
    int total = app_track_seconds(), at = (int)(f * total);
    snprintf(buf, n, "SEEK TO: %d:%02d/%d:%02d (%d%%)", at / 60, at % 60, total / 60, total % 60, (int)(f * 100));
    *scroll = 0;
    return;
  }
  app_marquee_text(buf, n);
}

// The static artwork and readouts; the buttons and sliders are child controls painted on top.
static void player_paint(window_t *win, player_view_t *v) {
  wa_canvas_t *c = &v->canvas;
  wa_engine_t *e = &g_app->engine;
  canvas_blit(c, SKIN_MAIN, R(0, 0, WA_W, WA_MAIN_H), 0, 0);
  canvas_blit(c, SKIN_TITLEBAR, R(27, 0, WA_W, 14), 0, 0);
  canvas_blit(c, SKIN_TITLEBAR, R(0, 0, 9, 9), 6, 3);
  canvas_blit(c, SKIN_TITLEBAR, R(9, 0, 9, 9), 244, 3);
  canvas_blit(c, SKIN_TITLEBAR, R(0, 18, 9, 9), 254, 3);
  canvas_blit(c, SKIN_TITLEBAR, R(18, 0, 9, 9), 264, 3);
  canvas_blit(c, SKIN_TITLEBAR, R(304, 0, 8, 43), 10, 22);
  // Play state and the "working" indicator.
  int status = e->state == WA_PLAYING ? 0 : e->state == WA_PAUSED ? 9 : 18;
  canvas_blit(c, SKIN_PLAYPAUS, R(status, 0, 9, 9), 26, 28);
  if (e->state == WA_PLAYING) canvas_blit(c, SKIN_PLAYPAUS, R(39, 0, 3, 9), 24, 28);
  draw_time(c, e);
  draw_visualizer(c, e);
  char text[512];
  int scroll = 0;
  marquee(win, text, sizeof(text), &scroll);
  canvas_text(c, text, 111, 27, 154, scroll);
  char num[8] = "";
  bool loaded = e->dec != NULL;
  if (loaded) snprintf(num, sizeof(num), "%3d", MIN(999, g_app->kbps));
  canvas_text(c, num, 111, 43, 15, 0);
  if (loaded) snprintf(num, sizeof(num), "%2d", MIN(99, g_app->khz));
  canvas_text(c, num, 156, 43, 10, 0);
  canvas_blit(c, SKIN_MONOSTER, R(29, loaded && g_app->channels == 1 ? 0 : 12, 27, 12), 212, 41);
  canvas_blit(c, SKIN_MONOSTER, R(0, loaded && g_app->channels == 2 ? 0 : 12, 29, 12), 239, 41);
}

result_t win_winamp_player(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  player_view_t *v = win->userdata;
  switch (msg) {
    case evCreate:
      v = allocate_window_data(win, sizeof(player_view_t));
      if (!v || !canvas_resize(&v->canvas, WA_W, WA_MAIN_H)) return false;
      if (g_app) g_app->player = win;
      for (int i = 0; i < (int)ARRAY_LEN(kPlayerButtons); i++) skin_add_control(win, "SpriteButton", kPlayerButtons[i].id);
      for (int i = 0; i < (int)ARRAY_LEN(kPlayerSliders); i++) {
        window_t *c = skin_add_control(win, "SpriteSlider", kPlayerSliders[i].id);
        if (!c) continue;
        slider_range_t r = { kPlayerSliders[i].id == SL_BALANCE ? -VOL_STEPS : 0, kPlayerSliders[i].id == SL_POSITION ? POS_STEPS : VOL_STEPS };
        send_message(c, slSetRange, 0, &r);
      }
      player_apply_skin(win);
      return true;
    case evMeasure:
      skin_view_measure(WA_MAIN_H, lparam);
      return true;
    case evResize:
      player_place(win);
      return false;
    case evPaint:
      if (!g_app) return true;
      player_sync(win);
      player_paint(win, v);
      return false;                              // let the framework paint the child controls
    case evQueryDrag:
      return DRAG_NOW;
    case evCommand:
      switch (HIWORD(wparam)) {
        case btnClicked:         app_command(LOWORD(wparam)); return true;
        case sliderValueChanged:
          if (LOWORD(wparam) == SL_VOLUME)  app_set_volume(skin_slider_pos(win, SL_VOLUME) / (float)VOL_STEPS);
          if (LOWORD(wparam) == SL_BALANCE) app_set_balance(skin_slider_pos(win, SL_BALANCE) / (float)VOL_STEPS);
          invalidate_window(win);
          return true;
        case spsnReleased:
          if (LOWORD(wparam) == SL_POSITION) app_seek(skin_slider_pos(win, SL_POSITION) / (float)POS_STEPS);
          invalidate_window(win);
          return true;
        default: return false;
      }
    case evLeftButtonDown: {
      ipoint16_t p = skin_point(win, &v->canvas, wparam);
      if (!rect_contains_point(kTimeRect, p)) return true;
      v->time_down = true;
      set_capture(win);
      return true;
    }
    case evLeftButtonUp: {
      if (!v->time_down) return false;
      v->time_down = false;
      set_capture(NULL);
      if (rect_contains_point(kTimeRect, skin_point(win, &v->canvas, wparam))) app_command(ID_OPTIONS_TIME);
      invalidate_window(win);
      return true;
    }
    case evPointerCancel:
      if (v && v->time_down) { v->time_down = false; set_capture(NULL); }
      return true;
    case evDestroy:
      if (v) { canvas_free(&v->canvas); free(v); win->userdata = NULL; }
      if (g_app && g_app->player == win) g_app->player = NULL;
      return true;
    default:
      return false;
  }
}
