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

  create_tool_palette_window();
  create_color_palette_window();
}

// ============================================================
// .gem entry points
// ============================================================

static const char *image_editor_types[] = { ".png", ".bmp", ".jpg", ".jpeg", NULL };

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  (void)argc; (void)argv;
  g_app = calloc(1, sizeof(app_state_t));
  if (!g_app) return false;

#if IMAGEEDITOR_DEBUG
  {
    char log_path[1024];
    int path_len = snprintf(log_path, sizeof(log_path), "%s/imageeditor.log",
                            axSettingsDirectory());
    if (path_len >= 0 && (size_t)path_len < sizeof(log_path)) {
      if (axSetLogFile(log_path))
        axLog("[imageeditor] logging initialized: %s", axGetLogFile());
    }
  }
#endif

  g_app->current_tool = ID_TOOL_SELECT;
  g_app->hinstance    = hinstance;
  g_app->fg_color = kPalette[4];
  g_app->bg_color = kPalette[0];
  g_app->next_x   = DOC_START_X;
  g_app->next_y   = DOC_START_Y;
  g_app->text_font_size = 16;
  g_app->text_antialias = true;
  g_app->grid_spacing_x = 8;
  g_app->grid_spacing_y = 8;

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
    char splash_path[4096];
    int path_len = snprintf(splash_path, sizeof(splash_path), "%s/" SHAREDIR "/splash.jpg",
             ui_get_exe_dir());
    if (path_len >= 0 && (size_t)path_len < sizeof(splash_path))
      show_splash_screen(splash_path, hinstance);
  }
#endif

  return true;
}

void gem_shutdown(void) {
  if (!g_app) return;

#if IMAGEEDITOR_DEBUG
  if (axGetLogFile()[0])
    axLog("[imageeditor] shutting down");
  axSetLogFile(NULL);
#endif

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

GEM_STANDALONE_MAIN("Orion Image Editor", UI_INIT_DESKTOP, SCREEN_W, SCREEN_H,
                    g_app->menubar_win, g_app->accel)
