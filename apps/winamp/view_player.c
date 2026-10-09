// VIEW: the main window — transport, time, visualizer, marquee and sliders,
// drawn from MAIN.BMP and its companion sheets at Winamp 2 coordinates.

#include "winamp.h"

enum { SL_VOLUME = 1, SL_BALANCE, SL_POSITION, HIT_TIME };

static const wa_region_t kRegions[] = {
  { ID_PLAYBACK_PREV,     {  16,  88,  23, 18 } },
  { ID_PLAYBACK_PLAY,     {  39,  88,  23, 18 } },
  { ID_PLAYBACK_PAUSE,    {  62,  88,  23, 18 } },
  { ID_PLAYBACK_STOP,     {  85,  88,  23, 18 } },
  { ID_PLAYBACK_NEXT,     { 108,  88,  22, 18 } },
  { ID_PLAYBACK_OPEN,     { 136,  89,  22, 16 } },
  { ID_OPTIONS_SHUFFLE,   { 164,  89,  47, 15 } },
  { ID_OPTIONS_REPEAT,    { 210,  89,  28, 15 } },
  { ID_WINDOW_EQUALIZER,  { 219,  58,  23, 12 } },
  { ID_WINDOW_PLAYLIST,   { 242,  58,  23, 12 } },
  { SL_VOLUME,            { 107,  57,  68, 13 } },
  { SL_BALANCE,           { 177,  57,  38, 13 } },
  { SL_POSITION,          {  16,  72, 248, 10 } },
  { HIT_TIME,             {  36,  26,  63, 13 } },
};

// Transport sprites in CBUTTONS.BMP, in kRegions order.
static const irect16_t kTransport[] = {
  { 0, 0, 23, 18 }, { 23, 0, 23, 18 }, { 46, 0, 23, 18 }, { 69, 0, 23, 18 }, { 92, 0, 22, 18 }, { 114, 0, 22, 16 },
};

typedef struct {
  wa_canvas_t canvas;
  uint16_t pressed;     // region id under the finger
  bool inside;          // finger still over the pressed region
  float drag;           // slider value while dragging
} player_view_t;

static int player_hit(ipoint16_t p) {
  for (int i = 0; i < (int)ARRAY_LEN(kRegions); i++) if (rect_contains_point(kRegions[i].r, p)) return kRegions[i].id;
  return 0;
}

static const wa_region_t *region(uint16_t id) {
  for (int i = 0; i < (int)ARRAY_LEN(kRegions); i++) if (kRegions[i].id == id) return &kRegions[i];
  return NULL;
}

static float slider_value(uint16_t id, ipoint16_t p) {
  const wa_region_t *r = region(id);
  int thumb = id == SL_POSITION ? 29 : 14;
  float t = (float)(p.x - r->r.x - thumb / 2) / (r->r.w - thumb);
  t = MAX(0.0f, MIN(1.0f, t));
  return id == SL_BALANCE ? t * 2 - 1 : t;
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

static void marquee(player_view_t *v, char *buf, size_t n, int *scroll) {
  *scroll = g_app->marquee_px / 2;
  if (v->pressed == SL_VOLUME && v->inside) { snprintf(buf, n, "VOLUME: %d%%", (int)lroundf(v->drag * 100)); *scroll = 0; return; }
  if (v->pressed == SL_BALANCE && v->inside) {
    int pct = (int)lroundf(fabsf(v->drag) * 100);
    if (!pct) snprintf(buf, n, "BALANCE: CENTER"); else snprintf(buf, n, "BALANCE: %d%% %s", pct, v->drag < 0 ? "LEFT" : "RIGHT");
    *scroll = 0;
    return;
  }
  if (v->pressed == SL_POSITION && v->inside) {
    int total = app_track_seconds(), at = (int)(v->drag * total);
    snprintf(buf, n, "SEEK TO: %d:%02d/%d:%02d (%d%%)", at / 60, at % 60, total / 60, total % 60, (int)(v->drag * 100));
    *scroll = 0;
    return;
  }
  app_marquee_text(buf, n);
}

static void player_paint(player_view_t *v) {
  wa_canvas_t *c = &v->canvas;
  wa_engine_t *e = &g_app->engine;
  bool down = v->inside;
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
  marquee(v, text, sizeof(text), &scroll);
  canvas_text(c, text, 111, 27, 154, scroll);
  char num[8] = "";
  bool loaded = e->dec != NULL;
  if (loaded) snprintf(num, sizeof(num), "%3d", MIN(999, g_app->kbps));
  canvas_text(c, num, 111, 43, 15, 0);
  if (loaded) snprintf(num, sizeof(num), "%2d", MIN(99, g_app->khz));
  canvas_text(c, num, 156, 43, 10, 0);
  canvas_blit(c, SKIN_MONOSTER, R(29, loaded && g_app->channels == 1 ? 0 : 12, 27, 12), 212, 41);
  canvas_blit(c, SKIN_MONOSTER, R(0, loaded && g_app->channels == 2 ? 0 : 12, 29, 12), 239, 41);
  // Volume and balance: the strip frame follows the value, then the thumb.
  float vol = v->pressed == SL_VOLUME ? v->drag : e->volume;
  float bal = v->pressed == SL_BALANCE ? v->drag : e->balance;
  canvas_blit(c, SKIN_VOLUME, R(0, (int)lroundf(vol * 27) * 15, 68, 13), 107, 57);
  canvas_blit(c, SKIN_VOLUME, R(v->pressed == SL_VOLUME ? 0 : 15, 422, 14, 11), 107 + (int)lroundf(vol * 54), 58);
  canvas_blit(c, SKIN_BALANCE, R(9, (int)lroundf(fabsf(bal) * 27) * 15, 38, 13), 177, 57);
  canvas_blit(c, SKIN_BALANCE, R(v->pressed == SL_BALANCE ? 0 : 15, 422, 14, 11), 177 + (int)lroundf((bal + 1) * 12), 58);
  // EQ / PL toggles.
  bool eq_down = down && v->pressed == ID_WINDOW_EQUALIZER, pl_down = down && v->pressed == ID_WINDOW_PLAYLIST;
  canvas_blit(c, SKIN_SHUFREP, R(eq_down ? 46 : 0, g_app->show_eq ? 73 : 61, 23, 12), 219, 58);
  canvas_blit(c, SKIN_SHUFREP, R(pl_down ? 69 : 23, g_app->show_pl ? 73 : 61, 23, 12), 242, 58);
  // Position bar; the thumb only shows while a track is loaded.
  canvas_blit(c, SKIN_POSBAR, R(0, 0, 248, 10), 16, 72);
  if (loaded && e->state != WA_STOPPED) {
    float pos = v->pressed == SL_POSITION ? v->drag : app_position();
    canvas_blit(c, SKIN_POSBAR, R(v->pressed == SL_POSITION ? 278 : 248, 0, 29, 10), 16 + (int)lroundf(pos * 219), 72);
  }
  for (int i = 0; i < (int)ARRAY_LEN(kTransport); i++) {
    irect16_t src = kTransport[i];
    if (down && v->pressed == kRegions[i].id) src.y += src.h;
    canvas_blit(c, SKIN_CBUTTONS, src, kRegions[i].r.x, kRegions[i].r.y);
  }
  bool sh_down = down && v->pressed == ID_OPTIONS_SHUFFLE, rp_down = down && v->pressed == ID_OPTIONS_REPEAT;
  canvas_blit(c, SKIN_SHUFREP, R(28, (g_app->shuffle ? 30 : 0) + (sh_down ? 15 : 0), 47, 15), 164, 89);
  canvas_blit(c, SKIN_SHUFREP, R(0, (g_app->repeat ? 30 : 0) + (rp_down ? 15 : 0), 28, 15), 210, 89);
}

static bool player_is_slider(uint16_t id) { return id == SL_VOLUME || id == SL_BALANCE || id == SL_POSITION; }

static void player_slider_move(player_view_t *v, ipoint16_t p) {
  v->drag = slider_value(v->pressed, p);
  if (v->pressed == SL_VOLUME) app_set_volume(v->drag);
  if (v->pressed == SL_BALANCE) app_set_balance(v->drag);
}

result_t win_winamp_player(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  player_view_t *v = win->userdata;
  switch (msg) {
    case evCreate:
      v = allocate_window_data(win, sizeof(player_view_t));
      if (!v || !canvas_resize(&v->canvas, WA_W, WA_MAIN_H)) return false;
      if (g_app) g_app->player = win;
      return true;
    case evMeasure:
      skin_view_measure(WA_MAIN_H, lparam);
      return true;
    case evPaint:
      if (!g_app) return true;
      player_paint(v);
      canvas_present(&v->canvas, R(0, 0, (int)lroundf(WA_W * g_app->pt_per_px), (int)lroundf(WA_MAIN_H * g_app->pt_per_px)));
      return true;
    case evQueryDrag:
      return DRAG_NOW;
    case evLeftButtonDown: {
      ipoint16_t p = skin_point(win, &v->canvas, wparam);
      v->pressed = (uint16_t)player_hit(p);
      v->inside = v->pressed != 0;
      if (!v->pressed) return true;
      set_capture(win);
      if (player_is_slider(v->pressed)) player_slider_move(v, p);
      invalidate_window(win);
      return true;
    }
    case evMouseMove: {
      if (!v->pressed) return false;
      ipoint16_t p = skin_point(win, &v->canvas, wparam);
      if (player_is_slider(v->pressed)) player_slider_move(v, p);
      else v->inside = rect_contains_point(region(v->pressed)->r, p);
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
      if (id == SL_POSITION) app_seek(v->drag);
      else if (id == HIT_TIME && inside) app_command(ID_OPTIONS_TIME);
      else if (!player_is_slider(id) && inside) app_command(id);
      invalidate_window(win);
      return true;
    }
    case evPointerCancel:
      if (v && v->pressed) { v->pressed = 0; v->inside = false; set_capture(NULL); invalidate_window(win); }
      return true;
    case evDestroy:
      if (v) { canvas_free(&v->canvas); free(v); win->userdata = NULL; }
      if (g_app && g_app->player == win) g_app->player = NULL;
      return true;
    default:
      return false;
  }
}
