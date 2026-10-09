// VIEW: the root window. It stacks the three skinned windows (a vertical
// auto-layout from winamp.orion) and picks one whole-device-pixel scale for
// all of them: landscape fits the main window alone, portrait fits the width.

#include "winamp.h"

void skin_view_measure(int skin_h, layout_measure_t *m) {
  float pt = g_app ? g_app->pt_per_px : 1.0f;
  m->desired_w = (int)lroundf(WA_W * pt);
  m->desired_h = skin_h > 0 ? (int)lroundf(skin_h * pt) : 1;
}

void app_relayout(void) {
  window_t *win = g_app ? g_app->win : NULL;
  if (!win) return;
  irect16_t cr = get_client_rect(win);
  if (cr.w <= 0 || cr.h <= 0) return;
  float s = ui_surface_scale();
  g_app->landscape = cr.w > cr.h;
  float fit = g_app->landscape ? MIN(cr.w * s / WA_W, cr.h * s / WA_MAIN_H) : cr.w * s / WA_W;
  int k = (int)floorf(fit);
  g_app->pt_per_px = (k >= 1 ? (float)k : fit) / s;
  bool eq = !g_app->landscape && g_app->show_eq, pl = !g_app->landscape && g_app->show_pl;
  if (g_app->equalizer) show_window(g_app->equalizer, eq);
  if (g_app->playlist) show_window(g_app->playlist, pl);
  int total = (int)lroundf((WA_MAIN_H + (eq ? WA_EQ_H : 0)) * g_app->pt_per_px);
  int pad = pl ? 0 : MAX(0, (cr.h - total) / 2);
  window_set_layout(win, WINDOW_STACK_VERTICAL, 0, R(0, pad, 0, 0));
  WA_DEBUG("layout %dx%d scale=%.2f k=%d pt=%.3f landscape=%d", cr.w, cr.h, s, k, g_app->pt_per_px, g_app->landscape);
  for (window_t *c = win->children; c; c = c->next)
    WA_DEBUG("child %s frame=%d,%d,%d,%d visible=%d", c->title, c->frame.x, c->frame.y, c->frame.w, c->frame.h, window_has_state(c, WINDOW_STATE_VISIBLE));
  WA_DEBUG("player=%p eq=%p pl=%p", (void *)g_app->player, (void *)g_app->equalizer, (void *)g_app->playlist);
  invalidate_window(win);
  app_invalidate_all();
}

result_t win_winamp_main(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)lparam;
  if (!g_app) return false;
  switch (msg) {
    case evCreate:
      g_app->win = win;
      g_app->accel = load_accelerators(winamp_default_accels, winamp_default_accel_count);
      g_app->timer = axSetTimer(win, WA_TICK_MS, NULL, true);
      return true;
    case evPaint:
      fill_rect(0xFF000000u, get_client_rect(win));
      return false;                              // let the framework paint the children
    case evResize:
      app_relayout();
      return false;
    case evTimer:
      app_tick();
      return true;
    case evCommand:
      if (HIWORD(wparam) != kAcceleratorNotification) return false;
      app_command(LOWORD(wparam));
      return true;
    case evClose:
      ui_request_quit();
      return true;
    case evDestroy:
      if (g_app->timer) axCancelTimer(g_app->timer);
      g_app->timer = 0;
      if (g_app->accel) free_accelerators(g_app->accel);
      g_app->accel = NULL;
      g_app->win = NULL;
      return true;
    default:
      return false;
  }
}
