#include <orion/user/icons.h>
#include <orion/ui.h>

#include "lv_cmpn.h"
#include "fg_preview.h"
#include "lv_plug.h"

static const fe_component_desc_t k_levels_components[] = {
  {
    .class_name   = LV_GRAPH_CLASS_NAME,
    .name_prefix  = "LV_HIST",
    .toolbar_icon = "database-check",
    .default_size = {260, 84},
    .default_layout_size = {0, 84},
    .capabilities = FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBAR,
    .proc         = lv_histogram_component_proc,
  },
  {
    .class_name   = LV_STRIP_CLASS_NAME,
    .name_prefix  = "LV_STRIP",
    .toolbar_icon = "palette",
    .default_size = {260, 13},
    .default_layout_size = {0, 13},
    .capabilities = FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBAR,
    .proc         = lv_strip_component_proc,
  },
  {
    .class_name   = FG_PREVIEW_CLASS_NAME,
    .name_prefix  = "FG_PREV",
    .toolbar_icon = "media-image",
    .default_size = {248, 248},
    .default_layout_size = {0, 248},
    .capabilities = FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBAR,
    .proc         = fg_preview_component_proc,
  },
};

GEM_CLASSES(k_levels_components, "ImageEditor levels components", FE_PLUGIN_VERSION)
