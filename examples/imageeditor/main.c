// Image Editor – MacPaint-inspired with color support
// MDI architecture: floating tool palette, floating color palette,
// menu bar, and multiple document windows.
// PNG open/save via libpng.

#include "imageeditor.h"

extern bool running;

// Global application state
app_state_t *g_app = NULL;

// ============================================================
// Keyboard accelerators
// ============================================================

static const accel_t kAccelEntries[] = {
  { FCONTROL|FVIRTKEY, 'z', ID_EDIT_UNDO },
  { FCONTROL|FVIRTKEY, 'y', ID_EDIT_REDO },
  { FCONTROL|FVIRTKEY, 'x', ID_EDIT_CUT  },
  { FCONTROL|FVIRTKEY, 'c', ID_EDIT_COPY },
  { FCONTROL|FVIRTKEY, 'v', ID_EDIT_PASTE},
  { FCONTROL|FVIRTKEY, 'a', ID_EDIT_SELECT_ALL},
  { FVIRTKEY,          AX_KEY_ESCAPE, ID_EDIT_DESELECT},
  { FCONTROL|FVIRTKEY, 'n', ID_FILE_NEW  },
  { FCONTROL|FVIRTKEY, 'o', ID_FILE_OPEN },
  { FCONTROL|FVIRTKEY, 's', ID_FILE_SAVE },
  { FCONTROL|FVIRTKEY, 'w', ID_FILE_CLOSE},
  // Zoom shortcuts: Ctrl+= (Ctrl++) and Ctrl+-
  { FCONTROL|FVIRTKEY, '=',  ID_VIEW_ZOOM_IN  },
  { FCONTROL|FSHIFT|FVIRTKEY, '=',  ID_VIEW_ZOOM_IN  },
  { FCONTROL|FVIRTKEY, '-',  ID_VIEW_ZOOM_OUT },
  // Tool hotkeys – same as MS Paint
  { FVIRTKEY,          'p', ID_TOOL_PENCIL },
  { FVIRTKEY,          'b', ID_TOOL_BRUSH  },
  { FVIRTKEY,          'e', ID_TOOL_ERASER },
  { FVIRTKEY,          'k', ID_TOOL_FILL   },
  { FVIRTKEY,          's', ID_TOOL_SELECT },
  { FVIRTKEY,          'a', ID_TOOL_SPRAY       },
  { FVIRTKEY,          'i', ID_TOOL_EYEDROPPER  },
  { FVIRTKEY,          'g', ID_TOOL_MAGNIFIER   },
  { FVIRTKEY,          't', ID_TOOL_TEXT   },
  // Allow tool hotkeys to work even when Shift is held
  { FSHIFT|FVIRTKEY,   'p', ID_TOOL_PENCIL },
  { FSHIFT|FVIRTKEY,   'b', ID_TOOL_BRUSH  },
  { FSHIFT|FVIRTKEY,   'e', ID_TOOL_ERASER },
  { FSHIFT|FVIRTKEY,   'k', ID_TOOL_FILL   },
  { FSHIFT|FVIRTKEY,   's', ID_TOOL_SELECT },
  { FSHIFT|FVIRTKEY,   'a', ID_TOOL_SPRAY       },
  { FSHIFT|FVIRTKEY,   'i', ID_TOOL_EYEDROPPER  },
  { FSHIFT|FVIRTKEY,   'g', ID_TOOL_MAGNIFIER   },
  { FSHIFT|FVIRTKEY,   't', ID_TOOL_TEXT   },
};

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
      WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE | WINDOW_TOOLBAR,
      MAKERECT(PALETTE_WIN_X, PALETTE_WIN_Y, PALETTE_WIN_W, TOOL_WIN_H),
      NULL, win_tool_palette_proc, NULL);
  show_window(tp, true);
  g_app->tool_win = tp;

  window_t *cp = create_window(
      "Colors",
      WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
      MAKERECT(COLOR_WIN_X, COLOR_WIN_Y, COLOR_WIN_W, COLOR_WIN_H),
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

  g_app->current_tool = ID_TOOL_SELECT;
  g_app->fg_color = kPalette[4]; // black
  g_app->bg_color = kPalette[0]; // white
  g_app->next_x   = DOC_START_X;
  g_app->next_y   = DOC_START_Y;
  g_app->text_font_size = 16;
  g_app->text_antialias = true;

  srand((unsigned int)time(NULL));

  if (!ui_init_graphics(UI_INIT_DESKTOP, "Orion Image Editor", SCREEN_W, SCREEN_H)) {
    free(g_app);
    return 1;
  }

  create_app_windows();

  g_app->accel = load_accelerators(kAccelEntries,
                                   (int)(sizeof(kAccelEntries)/sizeof(kAccelEntries[0])));
  send_message(g_app->menubar_win, kMenuBarMessageSetAccelerators, 0, g_app->accel);
  create_document(NULL, CANVAS_W, CANVAS_H);

  while (running) {
    ui_event_t e;
    while (get_message(&e)) {
      if (!translate_accelerator(g_app->menubar_win, &e, g_app->accel))
        dispatch_message(&e);
    }
    repost_messages();
  }

  free_accelerators(g_app->accel);
  g_app->accel = NULL;

  free(g_app->clipboard);
  g_app->clipboard = NULL;

  while (g_app->docs) {
    canvas_doc_t *next = g_app->docs->next;
    doc_free_undo(g_app->docs);
    if (g_app->docs->float_tex)
      glDeleteTextures(1, &g_app->docs->float_tex);
    image_free(g_app->docs->float_pixels);
    if (g_app->docs->canvas_tex)
      glDeleteTextures(1, &g_app->docs->canvas_tex);
    image_free(g_app->docs->pixels);
    free(g_app->docs);
    g_app->docs = next;
  }
  free(g_app);
  g_app = NULL;

  ui_shutdown_graphics();
  return 0;
}
