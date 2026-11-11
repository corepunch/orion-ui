// terminal_components: TerminalView for the Terminal app, Git Client and FormEditor.
#include "vgat.h"

static const fe_component_desc_t k_terminal_components[] = {
  { .class_name = TERMINAL_VIEW_CLASS, .name_prefix = "IDC_TERM", .toolbar_icon = "terminal",
    .default_size = {480, 400}, .capabilities = FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBAR,
    .proc = terminal_proc, .default_layout_size = {0, 0},
    .default_flags = WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL | WINDOW_FLEXSPACE,
    .default_h_align = LAYOUT_ALIGN_STRETCH, .default_v_align = LAYOUT_ALIGN_STRETCH },
};

// The emulator's xterm palette must exist before a view paints.
GEM_EXPORT bool fe_plugin_init(void) { ansi_init_palette256(); return true; }

GEM_CLASSES(k_terminal_components, "Terminal components", FE_PLUGIN_VERSION)
