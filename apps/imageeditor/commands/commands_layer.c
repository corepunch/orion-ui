// commands/commands_layer.c — Layer command implementations

#include "commands.h"

// Forward declaration for global app state
extern app_state_t *g_app;

// ── Layer commands ─────────────────────────────────────────────────────────

void cmd_layer_new(canvas_doc_t *doc, uint32_t fill_color) {
  if (!doc) return;
  
  if (!ie_doc_begin_op(doc, "New Layer")) return;
  bool ok = doc_add_layer_filled(doc, fill_color);
  ie_doc_commit_op(doc, ok);
  
  if (ok)
    ie_doc_after_layers_changed(doc);
}

void cmd_layer_delete(canvas_doc_t *doc) {
  if (!doc) return;
  
  if (!ie_doc_begin_op(doc, "Delete Layer")) return;
  bool ok = doc_delete_layer(doc);
  ie_doc_commit_op(doc, ok);
  
  if (ok)
    ie_doc_after_layers_changed(doc);
}

void cmd_layer_duplicate(canvas_doc_t *doc) {
  if (!doc) return;
  
  if (!ie_doc_begin_op(doc, "Duplicate Layer")) return;
  bool ok = doc_duplicate_layer(doc);
  ie_doc_commit_op(doc, ok);
  
  if (ok)
    ie_doc_after_layers_changed(doc);
}

void cmd_layer_move_up(canvas_doc_t *doc) {
  if (!doc) return;
  
  if (!ie_doc_begin_op(doc, "Move Layer Up")) return;
  doc_move_layer_up(doc);
  ie_doc_commit_op(doc, true);
  
  ie_doc_after_layers_changed(doc);
}

void cmd_layer_move_down(canvas_doc_t *doc) {
  if (!doc) return;
  
  if (!ie_doc_begin_op(doc, "Move Layer Down")) return;
  doc_move_layer_down(doc);
  ie_doc_commit_op(doc, true);
  
  ie_doc_after_layers_changed(doc);
}

void cmd_layer_merge_down(canvas_doc_t *doc) {
  if (!doc) return;
  
  if (!ie_doc_begin_op(doc, "Merge Down")) return;
  doc_merge_down(doc);
  ie_doc_commit_op(doc, true);
  
  ie_doc_after_layers_changed(doc);
}

void cmd_layer_flatten(canvas_doc_t *doc) {
  if (!doc) return;
  
  if (!ie_doc_begin_op(doc, "Flatten")) return;
  doc_flatten(doc);
  ie_doc_commit_op(doc, true);
  
  ie_doc_after_layers_changed(doc);
}

void cmd_layer_fill(canvas_doc_t *doc, uint32_t color) {
  if (!doc) return;
  
  if (!ie_doc_begin_op(doc, "Fill Layer")) return;
  bool ok = canvas_fill_active_layer(doc, color);
  ie_doc_commit_op(doc, ok);
}

void cmd_layer_add_mask(canvas_doc_t *doc, int fill_mode) {
  if (!doc || doc->layer.active < 0 || doc->layer.active >= doc->layer.count)
    return;
  
  // Check if already in mask editing mode
  if (doc->layer.editing_mask) return;
  
  if (!ie_doc_begin_op(doc, "Add Layer Mask")) return;
  bool ok = layer_add_mask_ex(doc, doc->layer.active, fill_mode);
  ie_doc_commit_op(doc, ok);
  
  if (ok)
    ie_doc_after_layers_changed(doc);
}

void cmd_layer_apply_mask(canvas_doc_t *doc) {
  if (!doc || doc->layer.active < 0 || doc->layer.active >= doc->layer.count)
    return;
  
  // Check if in mask editing mode
  if (!doc->layer.editing_mask) return;
  
  if (!ie_doc_begin_op(doc, "Apply Layer Mask")) return;
  layer_apply_mask(doc, doc->layer.active);
  ie_doc_commit_op(doc, true);
  
  ie_doc_after_layers_changed(doc);
}

void cmd_layer_remove_mask(canvas_doc_t *doc) {
  if (!doc || doc->layer.active < 0 || doc->layer.active >= doc->layer.count)
    return;
  
  // Check if in mask editing mode
  if (!doc->layer.editing_mask) return;
  
  if (!ie_doc_begin_op(doc, "Remove Layer Mask")) return;
  layer_remove_mask(doc, doc->layer.active);
  ie_doc_commit_op(doc, true);
  
  ie_doc_after_layers_changed(doc);
}

canvas_doc_t *cmd_layer_extract_mask(canvas_doc_t *doc) {
  if (!doc) return NULL;
  return canvas_extract_mask(doc);
}

void cmd_layer_edit_mask(canvas_doc_t *doc, bool enable) {
  if (!doc || doc->layer.active < 0 || doc->layer.active >= doc->layer.count)
    return;
  
  layer_t *lay = doc->layer.stack[doc->layer.active];
  
  doc->layer.editing_mask = enable;
  doc->pixels = lay->pixels;  // In both cases, pixels point to the same buffer
  
  ie_doc_invalidate_all(doc);
}

void cmd_layer_set_visibility(canvas_doc_t *doc, int layer_idx, bool visible) {
  if (!doc || layer_idx < 0 || layer_idx >= doc->layer.count)
    return;
  
  if (!ie_doc_begin_op(doc, "Toggle Layer Visibility")) return;
  doc->layer.stack[layer_idx]->visible = visible;
  doc->canvas_dirty = true;
  ie_doc_commit_op(doc, true);
  
  ie_doc_after_layers_changed(doc);
}

void cmd_layer_set_blend_mode(canvas_doc_t *doc, int layer_idx, layer_blend_mode_t mode) {
  if (!doc || layer_idx < 0 || layer_idx >= doc->layer.count)
    return;
  
  if (!ie_doc_begin_op(doc, "Set Layer Blend Mode")) return;
  doc_set_layer_blend_mode(doc, layer_idx, mode);
  ie_doc_commit_op(doc, true);
  
  ie_doc_after_layers_changed(doc);
}
