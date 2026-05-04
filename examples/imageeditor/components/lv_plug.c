#include "../../../ui.h"

#include "lv_cmpn.h"
#include "fg_preview.h"
#include "lv_plug.h"
#include "build/generated/examples/imageeditor/components/imageeditor_component_icons.h"

enum {
  IE_CMP_ICON_HISTOGRAM = 0,
  IE_CMP_ICON_STRIP,
  IE_CMP_ICON_PREVIEW,
};

static const fe_component_desc_t k_levels_components[] = {
  {
    .class_name   = LV_GRAPH_CLASS_NAME,
    .display_name = "Levels Histogram",
    .token        = "lv_histogram",
    .name_prefix  = "LV_HIST",
    .toolbox_ident = 300,
    .toolbox_icon = IE_CMP_ICON_HISTOGRAM,
    .default_size = {260, 84},
    .capabilities = FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBOX,
    .proc         = lv_histogram_component_proc,
  },
  {
    .class_name   = LV_STRIP_CLASS_NAME,
    .display_name = "Levels Strip",
    .token        = "lv_strip",
    .name_prefix  = "LV_STRIP",
    .toolbox_ident = 301,
    .toolbox_icon = IE_CMP_ICON_STRIP,
    .default_size = {260, 13},
    .capabilities = FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBOX,
    .proc         = lv_strip_component_proc,
  },
  {
    .class_name   = FG_PREVIEW_CLASS_NAME,
    .display_name = "Filter Preview",
    .token        = "filter_preview",
    .name_prefix  = "FG_PREV",
    .toolbox_ident = 302,
    .toolbox_icon = IE_CMP_ICON_PREVIEW,
    .default_size = {248, 248},
    .capabilities = FE_COMPONENT_PLACEABLE | FE_COMPONENT_SHOW_TOOLBOX,
    .proc         = fg_preview_component_proc,
  },
};

GEM_CLASSES(k_levels_components, "ImageEditor levels components", FE_PLUGIN_VERSION)

bool fe_plugin_toolbox_icon_strip(fe_icon_strip_t *out) {
  if (!out) return false;
  *out = (fe_icon_strip_t){
    .rgba = imageeditor_component_icons_rgba,
    .width = imageeditor_component_icons_width,
    .height = imageeditor_component_icons_height,
    .icon_size = imageeditor_component_icons_tile,
    .default_icon = IE_CMP_ICON_PREVIEW,
  };
  return true;
}
