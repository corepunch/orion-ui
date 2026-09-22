#include "imageeditor.h"

bool cmd_pencil_layer(canvas_doc_t *doc, int layer) {
  if (!pencil_has_layers(doc) || layer < 0 || layer >= IE_LAYER_COUNT || doc->command.before) return false;
  anim_stop_playback(doc);
  IE_TRACE("layer select win=%p from=%d to=%d frame=%d", (void *)doc->canvas_win,
           doc->layer.active, layer, doc->anim->active_frame);
  if (doc->sel.move.active) return false;
  if (g_app) {
    g_app->pencil_layer_tool[doc->layer.active] = g_app->current_tool;
    g_app->pencil_layer_color[doc->layer.active] = g_app->fg_color;
  }
  doc_set_active_layer(doc, layer);
  canvas_clear_selection_mask(doc);
  doc->sel.active = false;
  if (g_app) {
    g_app->fg_color = layer == IE_LAYER_PENCIL ? IE_INK_COLOR :
                      g_app->pencil_layer_color[layer] ? g_app->pencil_layer_color[layer] : k_pencil_palette[4];
    g_app->bg_color = IE_PAPER_COLOR;
    int tool = g_app->pencil_layer_tool[layer];
    handle_menu_command(tool ? tool : layer == IE_LAYER_COLOR ? ID_TOOL_FILL : ID_TOOL_PENCIL);
  }
  imageeditor_sync_tool_palette();
  ie_doc_invalidate_all(doc);
  return true;
}

bool pencil_color_fill(canvas_doc_t *doc, int x, int y, uint32_t color, int gap) {
#if IMAGEEDITOR_BW
  if (!pencil_has_layers(doc) || doc->layer.active != IE_LAYER_COLOR || !canvas_in_bounds(doc, x, y)) return false;
  size_t n = (size_t)doc->canvas_w * doc->canvas_h, at = (size_t)y * doc->canvas_w + x;
  const uint8_t *ink = doc->layer.stack[IE_LAYER_PENCIL]->pixels;
  if (ink[at] != doc->ipal.transparent) return true;
  uint8_t target = doc->pixels[at];
  uint8_t *mask = malloc(n);
  if (!mask) return false;
  for (size_t p = 0; p < n; p++) mask[p] = ink[p] != doc->ipal.transparent || doc->pixels[p] != target;
  canvas_doc_t scratch = *doc;
  layer_t layer = {.pixels = mask};
  layer_t *stack[] = {&layer};
  scratch.pixels = mask; scratch.layer.stack = stack; scratch.layer.count = 1; scratch.layer.active = 0;
  scratch.ipal.count = 3; scratch.ipal.transparent = 0;
  scratch.ipal.entries[0] = 0; scratch.ipal.entries[1] = MAKE_COLOR(0,0,0,255);
  scratch.ipal.entries[2] = MAKE_COLOR(255,0,0,255);
  canvas_flood_fill_with_gap(&scratch, x, y, scratch.ipal.entries[2], gap);
  int fill = canvas_nearest_palette_index(doc, color);
  for (size_t p = 0; p < n; p++) if (mask[p] == 2) doc->pixels[p] = fill;
  free(mask);
  doc->canvas_dirty = true;
  return true;
#else
  (void)doc; (void)x; (void)y; (void)color; (void)gap;
  return false;
#endif
}
