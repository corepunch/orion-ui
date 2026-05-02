#include "../../../ui.h"

#include "lv_cmpn.h"
#include "lv_plug.h"

static const fe_component_desc_t k_levels_components[] = {
  {
    .class_name   = LV_GRAPH_CLASS_NAME,
    .display_name = "Levels Histogram",
    .token        = "lv_histogram",
    .name_prefix  = "",
    .capabilities = 0,
    .proc         = lv_histogram_component_proc,
  },
  {
    .class_name   = LV_STRIP_CLASS_NAME,
    .display_name = "Levels Strip",
    .token        = "lv_strip",
    .name_prefix  = "",
    .capabilities = 0,
    .proc         = lv_strip_component_proc,
  },
};

GEM_CLASSES(k_levels_components, "ImageEditor levels components", FE_PLUGIN_VERSION)
