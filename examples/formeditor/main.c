#include "formeditor.h"
#include "../../gem_magic.h"

app_state_t *g_app = NULL;

static const accel_t kAccelEntries[] = {
  { FCONTROL|FVIRTKEY, AX_KEY_N, ID_FILE_NEW  },
  { FCONTROL|FVIRTKEY, AX_KEY_O, ID_FILE_OPEN },
  { FCONTROL|FVIRTKEY, AX_KEY_S, ID_FILE_SAVE },
  { FVIRTKEY,          AX_KEY_DEL,       ID_EDIT_DELETE },
  { FVIRTKEY,          AX_KEY_BACKSPACE, ID_EDIT_DELETE },
};

// frame.y is now the window top (title bar top), not the client area top.
// Place the window so the title bar sits 4px below the menu bar.
static int palette_win_y(void) {
  return MENUBAR_HEIGHT + 4;
}

static int palette_win_h(void) {
  int items = 1;  // Select tool
  for (int i = 0; i < fe_component_count(); i++) {
    const fe_component_desc_t *c = fe_component_at(i);
    if (!c) continue;
    if ((c->capabilities & (FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBOX)) ==
        (FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBOX))
      items++;
  }
  int rows = (items + TOOLBOX_COLS - 1) / TOOLBOX_COLS;
  return TITLEBAR_HEIGHT + rows * FE_TOOLBOX_BTN_SIZE + 4;
}

static bool has_dynlib_ext(const char *path) {
  if (!path) return false;
  size_t n = strlen(path);
  size_t e = strlen(AX_DYNLIB_EXT);
  if (n < e) return false;
  return strcmp(path + n - e, AX_DYNLIB_EXT) == 0;
}

static bool load_default_component_plugin(void) {
  char path[4096];
  int n = snprintf(path, sizeof(path), "%s/../lib/formeditor_components%s",
                   ui_get_exe_dir(), AX_DYNLIB_EXT);
  if (n <= 0 || (size_t)n >= sizeof(path))
    return false;
  return fe_load_component_plugin(path);
}

static void create_app_windows(hinstance_t hinstance) {
  g_app->menubar_win = set_app_menu(editor_menubar_proc, kMenus, kNumMenus,
                                    handle_menu_command, hinstance);

  window_t *tp = create_window(
      "Tools",
      WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
      MAKERECT(PALETTE_WIN_X, palette_win_y(), PALETTE_WIN_W, palette_win_h()),
      NULL, win_tool_palette_proc, hinstance, NULL);
  show_window(tp, true);
  g_app->tool_win = tp;

  g_app->prop_win = property_browser_create(hinstance);
}

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  g_app = calloc(1, sizeof(app_state_t));
  if (!g_app) return false;

  load_default_component_plugin();
  for (int i = 1; i < argc; i++) {
    if (has_dynlib_ext(argv[i]))
      fe_load_component_plugin(argv[i]);
  }
  if (fe_component_count() == 0) {
    free(g_app);
    g_app = NULL;
    return false;
  }

  g_app->current_tool = ID_TOOL_SELECT;
  g_app->hinstance = hinstance;
  create_app_windows(hinstance);

  g_app->accel = load_accelerators(kAccelEntries,
      (int)(sizeof(kAccelEntries)/sizeof(kAccelEntries[0])));
  if (g_app->menubar_win)
    send_message(g_app->menubar_win, kMenuBarMessageSetAccelerators, 0, g_app->accel);

  create_form_doc(FORM_DEFAULT_W, FORM_DEFAULT_H);

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
  free_accelerators(g_app->accel);
  g_app->accel = NULL;
  fe_unload_component_plugins();
  while (g_app->docs)
    close_form_doc(g_app->docs);
  if (g_app->prop_win)
    destroy_window(g_app->prop_win);
  free(g_app);
  g_app = NULL;
}

GEM_DEFINE("Form Editor", "1.0", gem_init, gem_shutdown, NULL)

GEM_STANDALONE_MAIN("Orion Form Editor", UI_INIT_DESKTOP, SCREEN_W, SCREEN_H,
                    g_app->menubar_win, g_app->accel)
