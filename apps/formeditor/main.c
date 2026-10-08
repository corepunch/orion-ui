#include "formeditor.h"
#include <orion/gem.h>
#include <orion/user/svg_icon_loader.h>

app_state_t *g_app = NULL;

#ifdef BUILD_AS_GEM
#define FE_QUIT_AFTER_SCREENSHOT false
#else
#define FE_QUIT_AFTER_SCREENSHOT true
#endif

static const accel_t kAccelEntries[] = {
  { FCONTROL|FVIRTKEY, AX_KEY_N, ID_FILE_NEW  },
  { FCONTROL|FVIRTKEY, AX_KEY_O, ID_FILE_OPEN },
  { FCONTROL|FVIRTKEY, AX_KEY_S, ID_FILE_SAVE },
  { FVIRTKEY,          AX_KEY_DEL,       ID_EDIT_DELETE },
  { FVIRTKEY,          AX_KEY_BACKSPACE, ID_EDIT_DELETE },
};

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
  g_app->windows[FE_WIN_MENUBAR] = set_app_menu(editor_menubar_proc, kMenus, kNumMenus,
                                                 handle_menu_command, hinstance);

  formeditor_rebuild_tool_palette();

  g_app->windows[FE_WIN_PROP] = property_browser_create(hinstance);
  g_app->windows[FE_WIN_FORMS] = forms_browser_create(hinstance);
  g_app->windows[FE_WIN_PLUGINS] = plugins_browser_create(hinstance);
}

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  g_app = calloc(1, sizeof(app_state_t));
  if (!g_app) return false;
  g_app->grid_size = 8;
  g_app->show_grid = true;
  g_app->snap_to_grid = true;

#ifdef SHAREDIR
  {
    char icons_dir[512];
    int n = snprintf(icons_dir, sizeof(icons_dir), "%s/" SHAREDIR "/icons", ui_get_exe_dir());
    if (n > 0 && (size_t)n < sizeof(icons_dir))
      svg_add_icons_dir(hinstance, icons_dir);
  }
#endif
  register_commctl_classes();
  load_default_component_plugin();
  const char *project_path = NULL;
  for (int i = 1; i < argc; i++) {
    if (i + 1 < argc && strcmp(argv[i], "--screenshot") == 0) {
      ui_request_screenshot(argv[i + 1], 90, FE_QUIT_AFTER_SCREENSHOT);
      i++;
    } else if (has_dynlib_ext(argv[i]))
      fe_load_component_plugin(argv[i]);
    else {
      size_t n = strlen(argv[i]);
      if (n >= 6 && strcmp(argv[i] + n - 6, ".orion") == 0)
        project_path = argv[i];
    }
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
  if (g_app->windows[FE_WIN_MENUBAR])
    send_message(g_app->windows[FE_WIN_MENUBAR], kMenuBarMessageSetAccelerators, 0, g_app->accel);

  bool loaded = false;
  if (project_path)
    loaded = fe_project_load(project_path);

  if (!loaded)
    create_form_doc(FORM_DEFAULT_W, FORM_DEFAULT_H);

  /* Splash screen disabled for form editor startup.
#ifdef SHAREDIR
  {
    char splash_path[4096];
    int path_len = snprintf(splash_path, sizeof(splash_path), "%s/" SHAREDIR "/splash.jpg",
             ui_get_exe_dir());
    if (path_len >= 0 && (size_t)path_len < sizeof(splash_path))
      show_splash_screen(splash_path, hinstance);
  }
#endif
  */

  return true;
}

void gem_shutdown(void) {
  if (!g_app) return;
  free_accelerators(g_app->accel);
  g_app->accel = NULL;

  // Shutdown can trigger document-close notifications that refresh the
  // Objects browser. Clear DB rows first to avoid dereferencing plugin-owned
  // database pointers during teardown.
  memset(g_app->project.databases, 0, sizeof(g_app->project.databases));
  g_app->project.database_count = 0;

  while (g_app->form_count > 0 && g_app->forms[0]) {
    close_form_doc(g_app->forms[0]);
  }
  fe_project_clear_xml();
  fe_unload_component_plugins();
  for (int i = 0; i < FE_NUM_WINDOWS; i++) {
    if (g_app->windows[i])
      destroy_window(g_app->windows[i]);
  }
  free(g_app);
  g_app = NULL;
}

GEM_DEFINE("Form Editor", "1.0", gem_init, gem_shutdown, NULL)

GEM_STANDALONE_MAIN("Orion Form Editor", UI_INIT_DESKTOP, SCREEN_W, SCREEN_H,
                    g_app->windows[FE_WIN_MENUBAR], g_app->accel)
