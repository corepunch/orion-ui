#include "vgat.h"
#include "build/generated/apps/terminal/terminal.h"
#include <orion/gem.h>
#include <orion/commctl/commctl.h>
#include <orion/user/dock.h>
#include <orion/user/svg_icon_loader.h>

typedef struct {
  window_t *host;
  window_t *activity;
  window_t *page;
  window_t *left;
  window_t *right;
} terminal_app_t;

static terminal_app_t g_terminal;
static const char *terminal_file_types[] = { ".lua", NULL };

static result_t terminal_page_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)wparam; (void)lparam;
  switch (msg) {
    case evCreate:  return true;
    case evPaint:   return false;
    case evDestroy: return true;
  }
  return false;
}

static result_t terminal_host_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)lparam;
  switch (msg) {
    case evCreate:
      g_terminal.host = win;
      g_terminal.activity = get_window_item(win, ID_MAIN_WINDOW_ACTIVITY);
      if (!g_terminal.activity || !dock_window(g_terminal.activity, DOCK_LEFT,
            DOCK_EDGE(DOCK_LEFT), DOCK_TOOLBAR | DOCK_NOFLOAT, 0, 0)) return false;

      g_terminal.page = create_window_from_form(&terminal_workspace_page_form, 0, 0,
                                                win, terminal_page_proc, win->hinstance, NULL);
      if (!g_terminal.page || !dock_window(g_terminal.page, DOCK_FILL, 0, DOCK_NOFLOAT, 0, 0)) return false;
      g_terminal.left  = get_window_item(g_terminal.page, ID_WORKSPACE_PAGE_LEFT_TERMINAL);
      g_terminal.right = get_window_item(g_terminal.page, ID_WORKSPACE_PAGE_RIGHT_TERMINAL);
      if (!g_terminal.left || !g_terminal.right) return false;
      set_host_page(win, g_terminal.page);
      show_window(g_terminal.page, true);
      if (g_terminal.activity)
        send_message(g_terminal.activity, tbCheckButton, ID_VIEW_WORKSPACE, (void *)(intptr_t)1);
      return true;

    case evCommand:
      if (LOWORD(wparam) == ID_VIEW_WORKSPACE) {
        if (g_terminal.left) set_focus(g_terminal.left);
        if (g_terminal.activity)
          send_message(g_terminal.activity, tbCheckButton, ID_VIEW_WORKSPACE, (void *)(intptr_t)1);
        return true;
      }
      return false;

    case evPaint: return false;
    case evDestroy:
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

  g_terminal.host = create_window_from_form(&terminal_main_window_form, 0, 0,
                                             NULL, terminal_host_proc, hinstance, NULL);
  if (!g_terminal.host || !g_terminal.left || !g_terminal.right) {
    gem_shutdown();
    return false;
  }
  if (argc > 1 && argv[1]) {
    terminal_start_script(g_terminal.left, argv[1]);
    terminal_start_script(g_terminal.right, argv[1]);
  }
  maximize_window(g_terminal.host);
  show_window(g_terminal.host, true);
  set_focus(g_terminal.left);
  return true;
}

void gem_shutdown(void) {
  if (g_terminal.host && is_window(g_terminal.host)) destroy_window(g_terminal.host);
  memset(&g_terminal, 0, sizeof(g_terminal));
  vga_font_shutdown();
}

GEM_DEFINE("Terminal", "1.0", gem_init, gem_shutdown, terminal_file_types)

#ifndef BUILD_AS_GEM
GEM_STANDALONE_MAIN(VGAT_WINDOW_TITLE, UI_INIT_DESKTOP, 1024, 768, NULL, NULL)
#endif
