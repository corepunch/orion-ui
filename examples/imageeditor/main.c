// Image Editor – MacPaint-inspired with color support
// MDI architecture: floating tool palette, floating color palette,
// menu bar, and multiple document windows.
// PNG open/save via libpng.

#include "imageeditor.h"

extern bool running;

// Global application state
app_state_t *g_app = NULL;

// ============================================================
// Application init
// ============================================================

static void create_app_windows(void) {
  int sw = ui_get_system_metrics(kSystemMetricScreenWidth);

  window_t *mb = create_window(
      "menubar",
      WINDOW_NOTITLE | WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
      MAKERECT(0, 0, sw, MENUBAR_HEIGHT),
      NULL, editor_menubar_proc, NULL);
  send_message(mb, kMenuBarMessageSetMenus,
               sizeof(kMenus)/sizeof(kMenus[0]), (void *)kMenus);
  show_window(mb, true);
  g_app->menubar_win = mb;

  window_t *tp = create_window(
      "Tools",
      WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
      MAKERECT(PALETTE_WIN_X, PALETTE_WIN_Y, PALETTE_WIN_W, TOOL_WIN_H),
      NULL, win_tool_palette_proc, NULL);
  show_window(tp, true);
  g_app->tool_win = tp;

  window_t *cp = create_window(
      "Colors",
      WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
      MAKERECT(PALETTE_WIN_X, COLOR_WIN_Y, PALETTE_WIN_W, COLOR_WIN_H),
      NULL, win_color_palette_proc, NULL);
  show_window(cp, true);
  g_app->color_win = cp;
}

// ============================================================
// main
// ============================================================

int main(int argc, char *argv[]) {
  (void)argc; (void)argv;
  g_app = calloc(1, sizeof(app_state_t));
  if (!g_app) return 1;

  g_app->current_tool = 0; // pencil
  g_app->fg_color = kPalette[4]; // black
  g_app->bg_color = kPalette[0]; // white
  g_app->next_x   = DOC_START_X;
  g_app->next_y   = DOC_START_Y;

  if (!ui_init_graphics(UI_INIT_DESKTOP, "Orion Image Editor", SCREEN_W, SCREEN_H)) {
    free(g_app);
    return 1;
  }

  create_app_windows();
  create_document(NULL);

  while (running) {
    ui_event_t e;
    while (get_message(&e)) {
      dispatch_message(&e);
    }
    repost_messages();
  }

  while (g_app->docs) {
    canvas_doc_t *next = g_app->docs->next;
    if (g_app->docs->canvas_tex)
      glDeleteTextures(1, &g_app->docs->canvas_tex);
    free(g_app->docs);
    g_app->docs = next;
  }
  free(g_app);
  g_app = NULL;

  ui_shutdown_graphics();
  return 0;
}
