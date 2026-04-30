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
  { FVIRTKEY,          AX_KEY_X, ID_COLOR_SWAP },
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
  // Ctrl+0 — Fit on Screen (Photoshop convention)
  { FCONTROL|FVIRTKEY, AX_KEY_0,      ID_VIEW_ZOOM_FIT },
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

static const toolbar_item_t kMainToolbar[] = {
  { TOOLBAR_ITEM_BUTTON, ID_FILE_NEW,     sysicon_page_add,     0, 0, "New"     },
  { TOOLBAR_ITEM_BUTTON, ID_FILE_OPEN,    sysicon_folder_page,  0, 0, "Open"    },
  { TOOLBAR_ITEM_BUTTON, ID_FILE_SAVE,    sysicon_disk_save,    0, 0, "Save"    },
  { TOOLBAR_ITEM_BUTTON, ID_FILE_SAVEAS,  sysicon_disk_multiple,0, 0, "Save As" },
  { TOOLBAR_ITEM_SPACER, 0, 0, 10, 0, NULL },
  { TOOLBAR_ITEM_BUTTON, ID_VIEW_ZOOM_OUT,  sysicon_magnifier_zoom_out, 0, 0, "Zoom Out" },
  { TOOLBAR_ITEM_BUTTON, ID_VIEW_ZOOM_IN,   sysicon_magnifier_zoom_in,  0, 0, "Zoom In"  },
  { TOOLBAR_ITEM_BUTTON, ID_VIEW_ZOOM_1X,   sysicon_image_dimensions,    0, 0, "1x"       },
  { TOOLBAR_ITEM_BUTTON, ID_VIEW_ZOOM_FIT,  sysicon_expand,              0, 0, "Fit"      },
  { TOOLBAR_ITEM_SPACER, 0, 0, 10, 0, NULL },
  { TOOLBAR_ITEM_BUTTON, ID_VIEW_MASK_ONLY, sysicon_transparency,        0, BUTTON_PUSHLIKE, "Mask" },
};

static result_t main_toolbar_proc(window_t *win, uint32_t msg,
                                  uint32_t wparam, void *lparam) {
  (void)lparam;
  switch (msg) {
    case evCreate:
      send_message(win, tbSetItems,
                   (uint32_t)(sizeof(kMainToolbar) / sizeof(kMainToolbar[0])),
                   (void *)kMainToolbar);
      imageeditor_sync_main_toolbar();
      return true;
    case tbButtonClick:
      handle_menu_command((uint16_t)wparam);
      imageeditor_sync_main_toolbar();
      return true;
    case evDestroy:
      if (g_app && g_app->main_toolbar_win == win) g_app->main_toolbar_win = NULL;
      return false;
    default:
      return false;
  }
}

// Resurrect 64 palette from LoSpec.com
static const uint32_t kDefaultPalette[NUM_COLORS] = {
  0xff2f222e, 0xff46353e, 0xff655562, 0xff624f69, 0xff8a707f, 0xffb2ab9b, 0xffd0dcc7, 0xffffffff,
  0xff27276e, 0xff3423ae, 0xff3138b3, 0xff3b3be8, 0xff364fea, 0xff4a7df5, 0xff1d6bfb, 0xff1796f7,
  0xff45307a, 0xff39459e, 0xff6c6c96, 0xff3d68cd, 0xff4e90e6, 0xff7a94ab, 0xff54b9fb, 0xff2bc2f9,
  0xff243e4c, 0xff336667, 0xff47a9a2, 0xff4be0d5, 0xff86fffb, 0xff6cdfcd, 0xff69db91, 0xff73bc1e,
  0xff4c5a16, 0xff639023, 0xff383631, 0xff4a4e37, 0xff647e54, 0xff84a992, 0xff90bab2, 0xff655e0b,
  0xff8f8a0b, 0xff9baf0e, 0xffb9e130, 0xffe2f88f, 0xff533332, 0xff774a48, 0xffb4654d, 0xffe69b4d,
  0xffffd38f, 0xff3f2945, 0xff753e6b, 0xffa95e90, 0xfff384a8, 0xffedadea, 0xff543c75, 0xff6f4ba2,
  0xff7f65cf, 0xff9980ed, 0xff5d1c83, 0xff5424c3, 0xff784ff0, 0xff8181f6, 0xff90a7fc, 0xffb0cbfd,
};

#ifndef BUILD_AS_GEM
static bool image_editor_open_file_handler(const char *path) {
  return imageeditor_open_file_path(path);
}
#endif

// ============================================================
// Application init
// ============================================================

static void create_app_windows(hinstance_t hinstance) {
  g_app->menubar_win = set_app_menu(editor_menubar_proc, kMenus, kNumMenus,
                                    handle_menu_command, hinstance);
  create_main_toolbar_window();

  create_tool_palette_window();
  create_tool_options_window();
  create_color_palette_window();
#if !IMAGEEDITOR_SINGLE_LAYER
  create_layers_window();
#endif
}

window_t *create_main_toolbar_window(void) {
  if (!g_app) return NULL;
  int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
  window_t *win = create_window(
      "Toolbar",
      WINDOW_TOOLBAR | WINDOW_NOTITLE | WINDOW_ALWAYSONTOP |
      WINDOW_NORESIZE | WINDOW_NOTRAYBUTTON,
      MAKERECT(0, APP_TOOLBAR_Y, sw, APP_TOOLBAR_H),
      NULL, main_toolbar_proc,
      g_app->hinstance, NULL);
  if (!win) return NULL;
  show_window(win, true);
  g_app->main_toolbar_win = win;
  imageeditor_sync_main_toolbar();
  return win;
}

void imageeditor_sync_main_toolbar(void) {
  if (!g_app || !g_app->main_toolbar_win) return;
  window_t *btn = get_window_item(g_app->main_toolbar_win, ID_VIEW_MASK_ONLY);
  if (!btn) return;
  bool checked = g_app->active_doc && g_app->active_doc->mask_only_view;
  send_message(btn, btnSetCheck, checked ? btnStateChecked : btnStateUnchecked, NULL);
}

// ============================================================
// .gem entry points
// ============================================================

static const char *image_editor_types[] = { ".png", ".bmp", ".jpg", ".jpeg", NULL };

static bool has_ext(const char *path, const char *ext) {
  if (!path || !ext) return false;
  size_t path_len = strlen(path);
  size_t ext_len = strlen(ext);
  if (path_len < ext_len) return false;
  return strcmp(path + path_len - ext_len, ext) == 0;
}

static bool is_gem_module_path(const char *path) {
  return has_ext(path, ".gem");
}

static int open_startup_documents(int argc, char *argv[]) {
  int opened = 0;
  for (int i = 1; i < argc; i++) {
    const char *path = argv[i];
    if (!path || !path[0]) continue;
    // In gem mode argv[0] is the gem itself and argv[1..] are payload files.
    // In standalone mode argv[0] is the executable, but callers may still
    // pass image paths on the command line. Skip any .gem module paths.
    if (is_gem_module_path(path)) continue;
    if (imageeditor_open_file_path(path))
      opened++;
  }
  return opened;
}

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
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

#ifndef BUILD_AS_GEM
  ui_register_open_file_handler(image_editor_open_file_handler);
#endif

  srand((unsigned int)time(NULL));

  create_app_windows(hinstance);
  imageeditor_load_filters();

  g_app->accel = load_accelerators(kAccelEntries,
                                   (int)(sizeof(kAccelEntries)/sizeof(kAccelEntries[0])));
  if (g_app->menubar_win)
    send_message(g_app->menubar_win, kMenuBarMessageSetAccelerators, 0, g_app->accel);

  if (open_startup_documents(argc, argv) == 0)
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

  imageeditor_free_filters();

  while (g_app->docs) {
    canvas_doc_t *next = g_app->docs->next;
    doc_free_undo(g_app->docs);
    if (g_app->docs->float_tex)
      glDeleteTextures(1, &g_app->docs->float_tex);
    image_free(g_app->docs->float_pixels);
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
