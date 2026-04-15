// Image Editor – MacPaint-inspired with color support
// MDI architecture: floating tool palette, floating color palette,
// menu bar, and multiple document windows.
// PNG open/save via libpng.

#include "imageeditor.h"
#include "../../gem_magic.h"

// Global application state
app_state_t *g_app = NULL;

// ============================================================
// Keyboard accelerators
// ============================================================

static const accel_t kAccelEntries[] = {
  { FCONTROL|FVIRTKEY, AX_KEY_Z, ID_EDIT_UNDO },
  { FCONTROL|FVIRTKEY, AX_KEY_Y, ID_EDIT_REDO },
  { FCONTROL|FVIRTKEY, AX_KEY_X, ID_EDIT_CUT  },
  { FCONTROL|FVIRTKEY, AX_KEY_C, ID_EDIT_COPY },
  { FCONTROL|FVIRTKEY, AX_KEY_V, ID_EDIT_PASTE},
  { FCONTROL|FVIRTKEY, AX_KEY_A, ID_EDIT_SELECT_ALL},
  { FVIRTKEY,          AX_KEY_ESCAPE, ID_EDIT_DESELECT},
  // Delete / Backspace clears the active selection (fill with bg color)
  { FVIRTKEY,          AX_KEY_DEL,       ID_EDIT_CLEAR_SEL },
  { FVIRTKEY,          AX_KEY_BACKSPACE, ID_EDIT_CLEAR_SEL },
  { FCONTROL|FVIRTKEY, AX_KEY_N, ID_FILE_NEW  },
  { FCONTROL|FVIRTKEY, AX_KEY_O, ID_FILE_OPEN },
  { FCONTROL|FVIRTKEY, AX_KEY_S, ID_FILE_SAVE },
  { FCONTROL|FVIRTKEY, AX_KEY_W, ID_FILE_CLOSE},
  // Zoom shortcuts: Ctrl+= (Ctrl++) and Ctrl+-
  { FCONTROL|FVIRTKEY, AX_KEY_EQUALS,  ID_VIEW_ZOOM_IN  },
  { FCONTROL|FSHIFT|FVIRTKEY, AX_KEY_EQUALS,  ID_VIEW_ZOOM_IN  },
  { FCONTROL|FVIRTKEY, AX_KEY_MINUS,  ID_VIEW_ZOOM_OUT },
  // Tool hotkeys – same as MS Paint
  { FVIRTKEY,          AX_KEY_P, ID_TOOL_PENCIL },
  { FVIRTKEY,          AX_KEY_B, ID_TOOL_BRUSH  },
  { FVIRTKEY,          AX_KEY_E, ID_TOOL_ERASER },
  { FVIRTKEY,          AX_KEY_K, ID_TOOL_FILL   },
  { FVIRTKEY,          AX_KEY_S, ID_TOOL_SELECT },
  { FVIRTKEY,          AX_KEY_A, ID_TOOL_SPRAY       },
  { FVIRTKEY,          AX_KEY_I, ID_TOOL_EYEDROPPER  },
  { FVIRTKEY,          AX_KEY_G, ID_TOOL_MAGNIFIER   },
  { FVIRTKEY,          AX_KEY_T, ID_TOOL_TEXT   },
  // Allow tool hotkeys to work even when Shift is held
  { FSHIFT|FVIRTKEY,   AX_KEY_P, ID_TOOL_PENCIL },
  { FSHIFT|FVIRTKEY,   AX_KEY_B, ID_TOOL_BRUSH  },
  { FSHIFT|FVIRTKEY,   AX_KEY_E, ID_TOOL_ERASER },
  { FSHIFT|FVIRTKEY,   AX_KEY_K, ID_TOOL_FILL   },
  { FSHIFT|FVIRTKEY,   AX_KEY_S, ID_TOOL_SELECT },
  { FSHIFT|FVIRTKEY,   AX_KEY_A, ID_TOOL_SPRAY       },
  { FSHIFT|FVIRTKEY,   AX_KEY_I, ID_TOOL_EYEDROPPER  },
  { FSHIFT|FVIRTKEY,   AX_KEY_G, ID_TOOL_MAGNIFIER   },
  { FSHIFT|FVIRTKEY,   AX_KEY_T, ID_TOOL_TEXT   },
};

// ============================================================
// Application init
// ============================================================

static void create_app_windows(hinstance_t hinstance) {
  g_app->menubar_win = set_app_menu(editor_menubar_proc, kMenus, kNumMenus,
                                    handle_menu_command, hinstance);

  window_t *tp = create_window(
      "Tools",
      WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE | WINDOW_TOOLBAR,
      MAKERECT(PALETTE_WIN_X, PALETTE_WIN_Y, PALETTE_WIN_W, TOOL_WIN_H),
      NULL, win_tool_palette_proc, hinstance, NULL);
  show_window(tp, true);
  g_app->tool_win = tp;

  window_t *cp = create_window(
      "Colors",
      WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
      MAKERECT(COLOR_WIN_X, COLOR_WIN_Y, COLOR_WIN_W, COLOR_WIN_H),
      NULL, win_color_palette_proc, hinstance, NULL);
  show_window(cp, true);
  g_app->color_win = cp;
}

// ============================================================
// .gem entry points
// ============================================================

static const char *image_editor_types[] = { ".png", ".bmp", ".jpg", ".jpeg", NULL };

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  (void)argc; (void)argv;
  g_app = calloc(1, sizeof(app_state_t));
  if (!g_app) return false;

  g_app->current_tool = ID_TOOL_SELECT;
  g_app->hinstance    = hinstance;
  g_app->fg_color = kPalette[4];
  g_app->bg_color = kPalette[0];
  g_app->next_x   = DOC_START_X;
  g_app->next_y   = DOC_START_Y;
  g_app->text_font_size = 16;
  g_app->text_antialias = true;

  srand((unsigned int)time(NULL));

  create_app_windows(hinstance);

  g_app->accel = load_accelerators(kAccelEntries,
                                   (int)(sizeof(kAccelEntries)/sizeof(kAccelEntries[0])));
  if (g_app->menubar_win)
    send_message(g_app->menubar_win, kMenuBarMessageSetAccelerators, 0, g_app->accel);

  create_document(NULL, CANVAS_W, CANVAS_H);

  // Show splash screen if the image is available.
#ifdef SHAREDIR
  {
    char splash_path[512];
    snprintf(splash_path, sizeof(splash_path), "%s/" SHAREDIR "/splash.jpg",
             ui_get_exe_dir());
    show_splash_screen(splash_path, hinstance);
  }
#endif

  return true;
}

void gem_shutdown(void) {
  if (!g_app) return;

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
}

GEM_DEFINE("Image Editor", "1.0", gem_init, gem_shutdown, image_editor_types)

// ============================================================
// Standalone entry point
// ============================================================

#ifndef BUILD_AS_GEM
int main(int argc, char *argv[]) {
  if (!ui_init_graphics(UI_INIT_DESKTOP, "Orion Image Editor", SCREEN_W, SCREEN_H))
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
#endif /* BUILD_AS_GEM */
