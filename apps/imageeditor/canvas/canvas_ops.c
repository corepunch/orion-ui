// canvas_ops.c — Document mutation lifecycle API
// Centralizes the repeated undo/dirty/refresh pattern across ImageEditor

#include "imageeditor.h"

bool ie_doc_begin_op(canvas_doc_t *doc, const char *op_name) {
  bool ok = doc_begin_command(doc, op_name);
  imageeditor_sync_main_toolbar();
  return ok;
}

static void ie_doc_commit_op_impl(canvas_doc_t *doc, bool success, bool active_frame_only) {
  if (!doc || !doc->command.before) return;
  doc_end_command(doc, success);
  ie_doc_update_title(doc);
  imageeditor_sync_tool_palette();
  if (doc->canvas_win && doc->canvas_win->userdata) {
    canvas_win_state_t *view = doc->canvas_win->userdata;
    memset(view->onion_key, 0, sizeof(view->onion_key));
  }
  ie_doc_invalidate_canvas(doc);
  ie_doc_invalidate_layers(doc);
  if (active_frame_only && success && doc->anim) timeline_win_refresh_active_frame();
  else ie_doc_invalidate_timeline(doc);
  imageeditor_sync_main_toolbar();
}

void ie_doc_commit_op(canvas_doc_t *doc, bool success) {
  ie_doc_commit_op_impl(doc, success, false);
}

void ie_doc_commit_frame_op(canvas_doc_t *doc, bool success) {
  ie_doc_commit_op_impl(doc, success, true);
}

// ── Dirty state management ─────────────────────────────────────────────────

void ie_doc_mark_dirty(canvas_doc_t *doc) {
  if (!doc) return;
  doc->modified = true;
}

void ie_doc_update_title(canvas_doc_t *doc) {
  if (!doc) return;
  doc_update_title(doc);
}

// ── Invalidation helpers ───────────────────────────────────────────────────

void ie_doc_invalidate_all(canvas_doc_t *doc) {
  if (!doc) return;
  imageeditor_sync_tool_palette();
  if (doc->canvas_win && doc->canvas_win->userdata) {
    canvas_win_state_t *view = doc->canvas_win->userdata;
    memset(view->onion_key, 0, sizeof(view->onion_key));
  }
  ie_doc_invalidate_canvas(doc);
  ie_doc_invalidate_layers(doc);
  ie_doc_invalidate_timeline(doc);
}

void ie_doc_invalidate_canvas(canvas_doc_t *doc) {
  if (!doc || !doc->canvas_win) return;
  invalidate_window(doc->canvas_win);
}

void ie_doc_invalidate_layers(canvas_doc_t *doc) {
  if (!doc) return;
  layers_win_refresh();
}

void ie_doc_invalidate_timeline(canvas_doc_t *doc) {
  if (!doc) return;
  timeline_win_refresh();
}

// ── Targeted refresh hooks ─────────────────────────────────────────────────

void ie_doc_after_pixels_changed(canvas_doc_t *doc) {
  if (!doc) return;
  ie_doc_invalidate_canvas(doc);
}

void ie_doc_after_layers_changed(canvas_doc_t *doc) {
  if (!doc) return;
  ie_doc_invalidate_canvas(doc);
  ie_doc_invalidate_layers(doc);
}

void ie_doc_after_selection_changed(canvas_doc_t *doc) {
  if (!doc) return;
  ie_doc_invalidate_canvas(doc);
}
