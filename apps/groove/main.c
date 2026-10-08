// Groove — a block-based music sequencer in the spirit of Music 2000 and
// Dance eDJay: drag pre-made musical blocks from the bin onto the sheet.

#include "groove.h"
#include <orion/gem.h>
#include <orion/user/svg_icon_loader.h>

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  register_commctl_classes();
  char icons_path[1024];
  int n = snprintf(icons_path, sizeof(icons_path), "%s/../share/groove/icons", ui_get_exe_dir());
  if (n > 0 && (size_t)n < sizeof(icons_path)) svg_add_icons_dir(hinstance, icons_path);
  g_app = app_init();
  if (!g_app) return false;
  for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--demo")) app_load_demo();
  g_app->hinstance = hinstance;
  int sw = ui_get_system_metrics(kSystemMetricScreenWidth), sh = ui_get_system_metrics(kSystemMetricScreenHeight);
  window_t *win = create_window("Groove", 0, MAKERECT(0, 0, sw, sh), NULL, main_win_proc, hinstance, g_app);
  if (!win) { app_shutdown(g_app); return false; }
  show_window(win, true);
  maximize_window(win);
  return true;
}

void gem_shutdown(void) {
  if (!g_app) return;
  if (g_app->win && is_window(g_app->win)) destroy_window(g_app->win); // before the state its proc reads goes away
  app_shutdown(g_app);
}

GEM_DEFINE("Groove", "0.1", gem_init, gem_shutdown, NULL)

GEM_STANDALONE_MAIN("Orion Groove", UI_INIT_DESKTOP, SCREEN_W, SCREEN_H, g_app->menubar_win, g_app->accel)
