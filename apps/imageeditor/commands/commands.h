// commands/commands.h — Command module interface for ImageEditor
// Replaces handle_menu_command's giant switch with focused command modules

#ifndef __IMAGEEDITOR_COMMANDS_H__
#define __IMAGEEDITOR_COMMANDS_H__

#include "../imageeditor.h"

// ── Command categories ─────────────────────────────────────────────────────

// Edit commands (Undo, Redo, Cut, Copy, Paste, etc.)
void cmd_undo(canvas_doc_t *doc);
void cmd_redo(canvas_doc_t *doc);
void cmd_cut(canvas_doc_t *doc);
void cmd_copy(canvas_doc_t *doc);
void cmd_paste(canvas_doc_t *doc);

// Selection commands (Select All, Deselect, Expand, Contract, Crop)
void cmd_select_all(canvas_doc_t *doc);
void cmd_deselect(canvas_doc_t *doc);
void cmd_select_clear(canvas_doc_t *doc);
void cmd_select_expand(canvas_doc_t *doc, int amount);
void cmd_select_contract(canvas_doc_t *doc, int amount);
void cmd_crop_to_selection(canvas_doc_t *doc);

// Image commands (Flip H/V, Invert, Resize, Canvas Size)
void cmd_flip_horizontal(canvas_doc_t *doc);
void cmd_flip_vertical(canvas_doc_t *doc);
void cmd_invert_colors(canvas_doc_t *doc);
void cmd_resize_image(canvas_doc_t *doc, int new_w, int new_h, image_resize_filter_t filter);
void cmd_resize_canvas(canvas_doc_t *doc, int new_w, int new_h);

// Layer commands (New, Delete, Duplicate, Move, Merge, Flatten, Fill, Mask)
void cmd_layer_new(canvas_doc_t *doc, uint32_t fill_color);
void cmd_layer_delete(canvas_doc_t *doc);
void cmd_layer_duplicate(canvas_doc_t *doc);
void cmd_layer_move_up(canvas_doc_t *doc);
void cmd_layer_move_down(canvas_doc_t *doc);
void cmd_layer_merge_down(canvas_doc_t *doc);
void cmd_layer_flatten(canvas_doc_t *doc);
void cmd_layer_fill(canvas_doc_t *doc, uint32_t color);
void cmd_layer_set_visibility(canvas_doc_t *doc, int layer_idx, bool visible);
void cmd_layer_set_blend_mode(canvas_doc_t *doc, int layer_idx, layer_blend_mode_t mode);
void cmd_layer_add_mask(canvas_doc_t *doc, int fill_mode);
void cmd_layer_apply_mask(canvas_doc_t *doc);
void cmd_layer_remove_mask(canvas_doc_t *doc);
canvas_doc_t *cmd_layer_extract_mask(canvas_doc_t *doc);
void cmd_layer_edit_mask(canvas_doc_t *doc, bool enable);

// Pencil Test background image (picture under the Background layer's paint)
bool cmd_background_set_image(canvas_doc_t *doc, bg_image_t *img);
bool cmd_background_load(canvas_doc_t *doc, const char *path);
void cmd_background_remove(canvas_doc_t *doc);

#endif // __IMAGEEDITOR_COMMANDS_H__
