// FormEditor design-time stand-ins for Groove's runtime-only controls.
#include <orion/ui.h>
#include <orion/commctl/commctl.h>

static const fe_component_desc_t kGrooveComponents[] = {
  { .class_name = "GrooveArrangement", .name_prefix = "IDC_GSH", .toolbar_icon = "FlowView",
    .default_size = { 640, 420 }, .capabilities = FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBAR,
    .proc = win_stack, .default_layout_size = { 0, 0 },
    .default_flags = WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_HSCROLL | WINDOW_FLEXSPACE,
    .default_h_align = LAYOUT_ALIGN_STRETCH, .default_v_align = LAYOUT_ALIGN_STRETCH },
  { .class_name = "GrooveBlockBin", .name_prefix = "IDC_GBN", .toolbar_icon = "FlowView",
    .default_size = { 800, 220 }, .capabilities = FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBAR,
    .proc = win_flowview, .default_layout_size = { 0, -1 },
    .default_flags = WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL | WINDOW_NOACTIVATE,
    .default_h_align = LAYOUT_ALIGN_STRETCH, .default_v_align = LAYOUT_ALIGN_STRETCH },
  { .class_name = "GrooveBlockCard", .name_prefix = "IDC_GBC", .toolbar_icon = "Card",
    .default_size = { 128, 44 }, .capabilities = FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBAR,
    .proc = win_card, .default_layout_size = { 0, 0 },
    .default_flags = WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NODRAG | WINDOW_NOACTIVATE | WINDOW_NOTABSTOP,
    .default_h_align = LAYOUT_ALIGN_STRETCH, .default_v_align = LAYOUT_ALIGN_STRETCH },
};

GEM_CLASSES(kGrooveComponents, "Groove component previews", FE_PLUGIN_VERSION)
