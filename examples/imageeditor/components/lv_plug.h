#ifndef __IMAGEEDITOR_LEVELS_COMPONENTS_PLUGIN_H__
#define __IMAGEEDITOR_LEVELS_COMPONENTS_PLUGIN_H__

#include "../../../ui.h"

result_t lv_histogram_component_proc(window_t *win, uint32_t msg,
                                     uint32_t wparam, void *lparam);
result_t lv_strip_component_proc(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam);

#endif /* __IMAGEEDITOR_LEVELS_COMPONENTS_PLUGIN_H__ */
