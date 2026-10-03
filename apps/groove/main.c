// Groove — a block-based music sequencer in the spirit of Music 2000 and
// Dance eDJay: drag pre-made musical blocks from the bin onto the sheet.

#include "groove.h"
#include <orion/gem.h>

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  g_app = app_init();
  if (!g_app) return false;
  for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--demo")) app_load_demo();
  g_app->hinstance = hinstance;
  create_menubar();
  int sw = ui_get_system_metrics(kSystemMetricScreenWidth), sh = ui_get_system_metrics(kSystemMetricScreenHeight);
  window_t *win = create_window("Groove", WINDOW_TOOLBAR | WINDOW_STATUSBAR, MAKERECT(0, MENUBAR_HEIGHT, sw, sh - MENUBAR_HEIGHT), NULL, main_win_proc, hinstance, g_app);
  if (!win) { app_shutdown(g_app); return false; }
  show_window(win, true);
  maximize_window(win);
  GR_TRACE("ready bpm=%d clips=%d blocks=%d", g_app->song.bpm, g_app->song.nclips, blocks_count());
  return true;
}

void gem_shutdown(void) {
  if (!g_app) return;
  if (g_app->win && is_window(g_app->win)) destroy_window(g_app->win); // before the state its proc reads goes away
  app_shutdown(g_app);
}

GEM_DEFINE("Groove", "0.1", gem_init, gem_shutdown, NULL)

GEM_STANDALONE_MAIN("Orion Groove", UI_INIT_DESKTOP, SCREEN_W, SCREEN_H, g_app->menubar_win, g_app->accel)
