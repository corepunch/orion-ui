#include "terminal_app.h"
#include <orion/gem.h>
#include <orion/commctl/commctl.h>
#include <orion/user/svg_icon_loader.h>

typedef struct {
  window_t *host;
  window_t *activity;
  window_t *page;
  accel_table_t *accel;
} terminal_app_t;

static terminal_app_t g_terminal;
static const char *terminal_file_types[] = { ".lua", NULL };

static result_t terminal_host_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)wparam; (void)lparam;
  switch (msg) {
    case evCreate: {
      static const page_handler_t pages[] = {{"workspace_page", terminal_workspace_proc}};
      g_terminal.host = win;
      if (!page_host_init(win, &terminal_main_window_form, pages, ARRAY_LEN(pages))) return false;
      g_terminal.activity = get_window_item(win, ID_MAIN_WINDOW_ACTIVITY);
      g_terminal.page = page_host_page(win, ID_VIEW_WORKSPACE);
      return terminal_workspace_terminal(g_terminal.page, 0) && terminal_workspace_terminal(g_terminal.page, 1);
    }
    case evPaint: return false;
    case evDestroy:
      free_accelerators(g_terminal.accel);
      memset(&g_terminal, 0, sizeof(g_terminal));
      return true;
  }
  return false;
}

void gem_shutdown(void);

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  register_commctl_classes();
  char plugin[1024];
  int p = snprintf(plugin, sizeof(plugin), "%s/../lib/terminal_components%s", ui_get_exe_dir(), AX_DYNLIB_EXT);
  if (p <= 0 || (size_t)p >= sizeof(plugin) || !fe_load_component_plugin(plugin)) {
    fprintf(stderr, "[terminal] cannot load %s\n", plugin); fflush(stderr); return false;
  }

  char icons_path[1024];
  int n = snprintf(icons_path, sizeof(icons_path), "%s/../share/terminal/icons", ui_get_exe_dir());
  if (n > 0 && (size_t)n < sizeof(icons_path)) svg_add_icons_dir(hinstance, icons_path);

  g_terminal.accel = load_accelerators(terminal_default_accels, terminal_default_accel_count);
  if (!g_terminal.accel) { gem_shutdown(); return false; }
  g_terminal.host = create_window_from_form(&terminal_main_window_form, 0, 0,
                                             NULL, terminal_host_proc, hinstance, NULL);
  if (!g_terminal.host || !page_host_selection(g_terminal.host) || (!terminal_workspace_terminal(g_terminal.page, 0) || !terminal_workspace_terminal(g_terminal.page, 1))) {
    gem_shutdown();
    return false;
  }
  window_t *left = terminal_workspace_terminal(g_terminal.page, 0);
  window_t *right = terminal_workspace_terminal(g_terminal.page, 1);
  if (argc > 1 && argv[1]) {
    send_message(left, termRunScript, 0, argv[1]);
    send_message(right, termRunScript, 0, argv[1]);
  }
  maximize_window(g_terminal.host);
  show_window(g_terminal.host, true);
  set_focus(left);
  return true;
}

void gem_shutdown(void) {
  if (g_terminal.host && is_window(g_terminal.host)) destroy_window(g_terminal.host);
  fe_unload_component_plugins();   // after the views: their procedure lives in the plugin
  free_accelerators(g_terminal.accel);
  memset(&g_terminal, 0, sizeof(g_terminal));
}

GEM_DEFINE("Terminal", "1.0", gem_init, gem_shutdown, terminal_file_types)

#ifndef BUILD_AS_GEM
GEM_STANDALONE_MAIN(VGAT_WINDOW_TITLE, UI_INIT_DESKTOP, 1024, 768, g_terminal.host, g_terminal.accel)
#endif
