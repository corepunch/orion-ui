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

// Resurrect 64 palette from LoSpec.com
static const uint32_t kDefaultPalette[NUM_COLORS] = {
  MAKE_COLOR(0x2e,0x22,0x2f,0xFF), MAKE_COLOR(0x3e,0x35,0x46,0xFF),
  MAKE_COLOR(0x62,0x55,0x65,0xFF), MAKE_COLOR(0x96,0x6c,0x6c,0xFF),
  MAKE_COLOR(0xab,0x94,0x7a,0xFF), MAKE_COLOR(0x69,0x4f,0x62,0xFF),
  MAKE_COLOR(0x7f,0x70,0x8a,0xFF), MAKE_COLOR(0x9b,0xab,0xb2,0xFF),
  MAKE_COLOR(0xc7,0xdc,0xd0,0xFF), MAKE_COLOR(0xff,0xff,0xff,0xFF),
  MAKE_COLOR(0x6e,0x27,0x27,0xFF), MAKE_COLOR(0xb3,0x38,0x31,0xFF),
  MAKE_COLOR(0xea,0x4f,0x36,0xFF), MAKE_COLOR(0xf5,0x7d,0x4a,0xFF),
  MAKE_COLOR(0xae,0x23,0x34,0xFF), MAKE_COLOR(0xe8,0x3b,0x3b,0xFF),
  MAKE_COLOR(0xfb,0x6b,0x1d,0xFF), MAKE_COLOR(0xf7,0x96,0x17,0xFF),
  MAKE_COLOR(0xf9,0xc2,0x2b,0xFF), MAKE_COLOR(0x7a,0x30,0x45,0xFF),
  MAKE_COLOR(0x9e,0x45,0x39,0xFF), MAKE_COLOR(0xcd,0x68,0x3d,0xFF),
  MAKE_COLOR(0xe6,0x90,0x4e,0xFF), MAKE_COLOR(0xfb,0xb9,0x54,0xFF),
  MAKE_COLOR(0x4c,0x3e,0x24,0xFF), MAKE_COLOR(0x67,0x66,0x33,0xFF),
  MAKE_COLOR(0xa2,0xa9,0x47,0xFF), MAKE_COLOR(0xd5,0xe0,0x4b,0xFF),
  MAKE_COLOR(0xfb,0xff,0x86,0xFF), MAKE_COLOR(0x16,0x5a,0x4c,0xFF),
  MAKE_COLOR(0x23,0x90,0x63,0xFF), MAKE_COLOR(0x1e,0xbc,0x73,0xFF),
  MAKE_COLOR(0x91,0xdb,0x69,0xFF), MAKE_COLOR(0xcd,0xdf,0x6c,0xFF),
  MAKE_COLOR(0x31,0x36,0x38,0xFF), MAKE_COLOR(0x37,0x4e,0x4a,0xFF),
  MAKE_COLOR(0x54,0x7e,0x64,0xFF), MAKE_COLOR(0x92,0xa9,0x84,0xFF),
  MAKE_COLOR(0xb2,0xba,0x90,0xFF), MAKE_COLOR(0x0b,0x5e,0x65,0xFF),
  MAKE_COLOR(0x0b,0x8a,0x8f,0xFF), MAKE_COLOR(0x0e,0xaf,0x9b,0xFF),
  MAKE_COLOR(0x30,0xe1,0xb9,0xFF), MAKE_COLOR(0x8f,0xf8,0xe2,0xFF),
  MAKE_COLOR(0x32,0x33,0x53,0xFF), MAKE_COLOR(0x48,0x4a,0x77,0xFF),
  MAKE_COLOR(0x4d,0x65,0xb4,0xFF), MAKE_COLOR(0x4d,0x9b,0xe6,0xFF),
  MAKE_COLOR(0x8f,0xd3,0xff,0xFF), MAKE_COLOR(0x45,0x29,0x3f,0xFF),
  MAKE_COLOR(0x6b,0x3e,0x75,0xFF), MAKE_COLOR(0x90,0x5e,0xa9,0xFF),
  MAKE_COLOR(0xa8,0x84,0xf3,0xFF), MAKE_COLOR(0xea,0xad,0xed,0xFF),
  MAKE_COLOR(0x75,0x3c,0x54,0xFF), MAKE_COLOR(0xa2,0x4b,0x6f,0xFF),
  MAKE_COLOR(0xcf,0x65,0x7f,0xFF), MAKE_COLOR(0xed,0x80,0x99,0xFF),
  MAKE_COLOR(0x83,0x1c,0x5d,0xFF), MAKE_COLOR(0xc3,0x24,0x54,0xFF),
  MAKE_COLOR(0xf0,0x4f,0x78,0xFF), MAKE_COLOR(0xf6,0x81,0x81,0xFF),
  MAKE_COLOR(0xfc,0xa7,0x90,0xFF), MAKE_COLOR(0xfd,0xcb,0xb0,0xFF),
};

// ============================================================
// Application init
// ============================================================

static void create_app_windows(hinstance_t hinstance) {
  g_app->menubar_win = set_app_menu(editor_menubar_proc, kMenus, kNumMenus,
                                    handle_menu_command, hinstance);

  create_tool_palette_window();
  create_tool_options_window();
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
  memcpy(g_app->palette, kDefaultPalette, sizeof(kDefaultPalette));
  g_app->fg_color = g_app->palette[4];
  g_app->bg_color = g_app->palette[0];
  g_app->next_x   = DOC_START_X;
  g_app->next_y   = DOC_START_Y;
  g_app->brush_size = 1;  // default: radius 1 (3px diameter)
  g_app->text_font_size = 16;
  g_app->text_antialias = true;
  g_app->grid_spacing_x = 16;
  g_app->grid_spacing_y = 16;

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
