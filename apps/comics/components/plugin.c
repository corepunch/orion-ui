#include "comic_panel.h"
#include "comic_stage.h"
#include "comic_shelf.h"

static const fe_component_desc_t comics_components[] = {
  {
    .class_name = COMIC_SHELF_HEADER_CLASS, .name_prefix = "SHELF_HEADER",
    .default_size = {360, 48}, .default_h_align = LAYOUT_ALIGN_STRETCH,
    .default_flags = WINDOW_NOTITLE | WINDOW_LAYOUT_CONTAINER,
    .capabilities = FE_COMPONENT_PLACEABLE, .proc = comic_shelf_header_proc,
  },
  {
    .class_name = COMIC_SHELF_CLASS, .name_prefix = "SHELVES",
    .default_size = {360, 640}, .default_h_align = LAYOUT_ALIGN_STRETCH,
    .default_v_align = LAYOUT_ALIGN_STRETCH, .default_flags = WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_LAYOUT_CONTAINER,
    .capabilities = FE_COMPONENT_PLACEABLE, .proc = comic_shelves_proc,
  },
  {
    .class_name = COMIC_STAGE_CLASS, .name_prefix = "COMIC_STAGE",
    .default_size = {320, 480}, .default_h_align = LAYOUT_ALIGN_STRETCH,
    .default_v_align = LAYOUT_ALIGN_STRETCH, .default_flags = WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOTABSTOP,
    .capabilities = FE_COMPONENT_PLACEABLE, .proc = comic_stage_proc,
  },
  {
    .class_name = COMIC_PANEL_CLASS, .name_prefix = "PANEL",
    .default_size = {280, 240}, .default_h_align = LAYOUT_ALIGN_STRETCH,
    .default_v_align = LAYOUT_ALIGN_STRETCH, .default_flags = WINDOW_NOTITLE | WINDOW_NOTABSTOP | WINDOW_NOACTIVATE,
    .capabilities = FE_COMPONENT_PLACEABLE, .proc = comic_panel_proc,
  },
  {
    .class_name = COMIC_COVER_CLASS, .name_prefix = "COVER",
    .default_size = {224, 336}, .default_flags = WINDOW_NOTITLE,
    .capabilities = FE_COMPONENT_PLACEABLE, .proc = comic_cover_proc,
  },
};

GEM_CLASSES(comics_components, "Comics components", FE_PLUGIN_VERSION)
