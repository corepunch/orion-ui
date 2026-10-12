#include "vgat.h"
#include "build/generated/apps/terminal/terminal.h"
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

static const fe_component_desc_t k_terminal_view = {
  .class_name = "TerminalView", .name_prefix = "IDC_TERM", .toolbar_icon = "terminal",
  .default_size = {480, 400}, .capabilities = FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBAR,
  .proc = terminal_proc, .default_layout_size = {0, 0},
  .default_flags = WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL | WINDOW_FLEXSPACE,
  .default_h_align = LAYOUT_ALIGN_STRETCH, .default_v_align = LAYOUT_ALIGN_STRETCH,
};

void gem_shutdown(void);

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  register_commctl_classes();
  if (!register_window_class(&k_terminal_view)) return false;

  char icons_path[1024];
  int n = snprintf(icons_path, sizeof(icons_path), "%s/../share/terminal/icons", ui_get_exe_dir());
  if (n > 0 && (size_t)n < sizeof(icons_path)) svg_add_icons_dir(hinstance, icons_path);

  char font_path[1024];
  n = snprintf(font_path, sizeof(font_path), "%s/../share/orion/fonts/monoid.ttf", ui_get_exe_dir());
  if (n <= 0 || (size_t)n >= sizeof(font_path) || !vga_font_init(font_path, 12.0f)) return false;
  ansi_init_palette256();

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
    terminal_start_script(left, argv[1]);
    terminal_start_script(right, argv[1]);
  }
  maximize_window(g_terminal.host);
  show_window(g_terminal.host, true);
  set_focus(left);
  return true;
}

void gem_shutdown(void) {
  if (g_terminal.host && is_window(g_terminal.host)) destroy_window(g_terminal.host);
  free_accelerators(g_terminal.accel);
  memset(&g_terminal, 0, sizeof(g_terminal));
  vga_font_shutdown();
}

GEM_DEFINE("Terminal", "1.0", gem_init, gem_shutdown, terminal_file_types)

#ifndef BUILD_AS_GEM
GEM_STANDALONE_MAIN(VGAT_WINDOW_TITLE, UI_INIT_DESKTOP, 1024, 768, g_terminal.host, g_terminal.accel)
#endif
