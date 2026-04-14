// Orion Task Manager — entry point and application lifecycle.
//
// Demonstrates MVC-style architecture with:
//   - MODEL      : model_task.c   — task_t CRUD
//   - CONTROLLER : controller_app.c — app_state_t, command routing
//   - VIEW       : view_main.c / view_menubar.c / view_tasklist.c /
//                  view_dlg_task.c / view_dlg_about.c

#include "taskmanager.h"
#include "../../gem_magic.h"

// ============================================================
// gem_init / gem_shutdown (works standalone and as a .gem)
// ============================================================

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  (void)argc; (void)argv;

  g_app = app_init();
  if (!g_app) return false;

  g_app->hinstance = hinstance;

  // Build the menu bar.
  create_menubar();

  // Create the main document window.
  int sw = MIN(320, ui_get_system_metrics(kSystemMetricScreenWidth));
  int sh = MIN(240, ui_get_system_metrics(kSystemMetricScreenHeight));
  window_t *mw = create_window(
      "Task Manager",
      WINDOW_STATUSBAR,
      MAKERECT(MAIN_WIN_X, MAIN_WIN_Y,
               sw - MAIN_WIN_X - 4,
               sh - MAIN_WIN_Y - 4),
      NULL, main_win_proc, hinstance, NULL);
  // Note: g_app->main_win is set inside main_win_proc's kWindowMessageCreate
  // so that app_update_status() works correctly during initial window setup.
  show_window(mw, true);

  // Attach accelerators to the menu bar for hotkey hints in menus.
  if (g_app->menubar_win && g_app->accel)
    send_message(g_app->menubar_win, kMenuBarMessageSetAccelerators,
                 0, g_app->accel);

#ifdef BUILD_AS_GEM
  gem_interface_t *iface = gem_get_interface();
  iface->menus          = kMenus;
  iface->menu_count     = kNumMenus;
  iface->handle_command = (void (*)(uint16_t))handle_menu_command;
#endif

  return true;
}

void gem_shutdown(void) {
  if (!g_app) return;
  app_shutdown(g_app);
  g_app = NULL;
}

GEM_DEFINE("Task Manager", "1.0", gem_init, gem_shutdown, NULL)

// ============================================================
// Standalone main
// ============================================================

#ifndef BUILD_AS_GEM
int main(int argc, char *argv[]) {
  if (!ui_init_graphics(UI_INIT_DESKTOP, "Orion Task Manager",
                        SCREEN_W, SCREEN_H))
    return 1;

  if (!gem_init(argc, argv, 0)) {
    ui_shutdown_graphics();
    return 1;
  }

  while (ui_is_running()) {
    ui_event_t e;
    while (get_message(&e)) {
      if (!translate_accelerator(g_app->menubar_win, &e, g_app->accel))
        dispatch_message(&e);
    }
    repost_messages();
  }

  gem_shutdown();
  ui_shutdown_graphics();
  return 0;
}
#endif
