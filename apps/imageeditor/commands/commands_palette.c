#include "imageeditor.h"
#include "../palette.h"

const uint32_t *pencil_default_colors(void) {
  static uint32_t colors[256];
  static bool loaded;
  if (!loaded) {
    uint8_t rgba[256][4]; int count;
    if (!pencil_palette_load_default(ui_get_exe_dir(), rgba, &count)) return NULL;
    for (int i = 0; i < 256; i++)
      colors[i] = MAKE_COLOR(rgba[i][0], rgba[i][1], rgba[i][2], rgba[i][3]);
    loaded = true;
  }
  return colors;
}

uint32_t pencil_configured_color(void) {
#if IMAGEEDITOR_BW
  const uint32_t *colors = pencil_default_colors();
  return colors ? colors[IE_PENCIL_COLOR_SWATCH] : IE_INK_COLOR;
#else
  return IE_INK_COLOR;
#endif
}

bool cmd_pencil_color(canvas_doc_t *doc, int index) {
#if IMAGEEDITOR_BW
  if (!g_app || !doc || index < 0 || index >= doc->ipal.count || index >= 256 ||
      index == doc->ipal.transparent || !COLOR_A(doc->ipal.entries[index])) return false;
  g_app->fg_palette_idx = index;
  g_app->fg_color = doc->ipal.entries[index];
  doc->pencil_color_index[doc->layer.active] = index;
  if (g_app->tool_win) {
    if (doc->layer.active != IE_LAYER_PENCIL && g_app->tool_win->value == (uint32_t)(doc->layer.active + 1))
      send_message(g_app->tool_win, tbSetItemColor, ID_TOOL_BRUSH, &g_app->fg_color);
    invalidate_window(g_app->tool_win);
  }
  if (g_app->color_win) invalidate_window(g_app->color_win);
  return true;
#else
  (void)doc; (void)index;
  return false;
#endif
}
