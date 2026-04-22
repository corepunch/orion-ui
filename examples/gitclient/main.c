// Git Client — entry point.
//
// This is a standalone Orion example application.  The GEM_STANDALONE_MAIN
// macro (from gem_magic.h) generates a platform-agnostic main() that
// initialises the framework, runs the event loop, and shuts down cleanly.

#include "gitclient.h"
#include "../../gem_magic.h"

// ============================================================
// Module-level application state
// ============================================================

static gc_state_t g_gc_state;
gc_state_t *g_gc = NULL;

// ============================================================
// gem_init / gem_shutdown — required signatures for GEM_STANDALONE_MAIN
// ============================================================

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  memset(&g_gc_state, 0, sizeof(g_gc_state));
  g_gc = &g_gc_state;

  g_gc->hinstance     = hinstance;
  g_gc->selected_commit = -1;
  g_gc->selected_file   = -1;
  g_gc->left_w          = PANEL_LEFT_W_DEFAULT;
  g_gc->right_w         = PANEL_RIGHT_W_DEFAULT;

  // Menubar + accelerators must be wired before the main window is shown.
  gc_create_menubar();

  // Calculate initial vsplit_y from screen height.
  int sh = ui_get_system_metrics(kSystemMetricScreenHeight);
  int mh = sh - MENUBAR_HEIGHT;
  g_gc->vsplit_y = (int)(mh * PANEL_VSPLIT_FRAC / 100);
  if (g_gc->vsplit_y < 60)  g_gc->vsplit_y = 60;

  // Create the main application window.
  g_gc->main_win = create_window("Git Client",
      WINDOW_TOOLBAR | WINDOW_STATUSBAR,
      MAKERECT(0, 0, SCREEN_W, SCREEN_H),
      NULL,   // no parent → root window
      gc_main_proc, hinstance, NULL);

  if (!g_gc->main_win) return false;

  show_window(g_gc->main_win, true);

  // If a path was passed on the command line, open it immediately.
  if (argc > 1 && argv[1] && argv[1][0])
    gc_open_repo(argv[1]);

  return true;
}

void gem_shutdown(void) {
  if (g_gc) {
    if (g_gc->accel)
      free_accelerators(g_gc->accel);
    g_gc = NULL;
  }
}

// ============================================================
// Standalone entry point (no-op when built as a .gem)
// ============================================================

GEM_STANDALONE_MAIN("Git Client", UI_INIT_DESKTOP, SCREEN_W, SCREEN_H,
                    g_gc->menubar_win, g_gc->accel)
