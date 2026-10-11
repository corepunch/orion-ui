#include <orion/ui.h>

static result_t terminal_preview_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)wparam; (void)lparam;
  switch (msg) {
    case evCreate: return true;
    case evPaint: {
      irect16_t r = get_client_rect(win);
      fill_rect(get_sys_color(brWindowDarkBg), r);
      draw_text(FONT_SMALL, "Terminal session", r.x + 8, r.y + 8, get_sys_color(brTextNormal));
      return true;
    }
    case evDestroy: return true;
  }
  return false;
}

static const fe_component_desc_t k_terminal_components[] = {
  { .class_name = "TerminalView", .name_prefix = "IDC_TERM", .toolbar_icon = "terminal",
    .default_size = {480, 400}, .capabilities = FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBAR,
    .proc = terminal_preview_proc, .default_layout_size = {0, 0},
    .default_flags = WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL | WINDOW_FLEXSPACE,
    .default_h_align = LAYOUT_ALIGN_STRETCH, .default_v_align = LAYOUT_ALIGN_STRETCH },
};

GEM_CLASSES(k_terminal_components, "Terminal components", FE_PLUGIN_VERSION)
