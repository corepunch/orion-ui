// commands/commands_edit.c — Edit command implementations

#include "commands.h"

// ── Edit commands ──────────────────────────────────────────────────────────

void cmd_undo(canvas_doc_t *doc) {
  if (!doc) return;
  if (doc->command.before) return;
  anim_stop_playback(doc);
  if (doc_undo(doc)) {
    ie_doc_update_title(doc);
    if (doc->canvas_win) canvas_win_sync_scrollbars(doc->canvas_win);
    ie_doc_invalidate_all(doc);
    imageeditor_sync_main_toolbar();
  }
}

void cmd_redo(canvas_doc_t *doc) {
  if (!doc) return;
  if (doc->command.before) return;
  anim_stop_playback(doc);
  if (doc_redo(doc)) {
    ie_doc_update_title(doc);
    if (doc->canvas_win) canvas_win_sync_scrollbars(doc->canvas_win);
    ie_doc_invalidate_all(doc);
    imageeditor_sync_main_toolbar();
  }
}

void cmd_cut(canvas_doc_t *doc) {
  if (!doc || !doc->sel.active) return;
  
  if (!ie_doc_begin_op(doc, "Cut")) return;
  canvas_cut_selection(doc, MAKE_COLOR(0, 0, 0, 0));
  ie_doc_commit_op(doc, true);
}

void cmd_copy(canvas_doc_t *doc) {
  if (!doc || !doc->sel.active) return;
  canvas_copy_selection(doc);
}

void cmd_paste(canvas_doc_t *doc) {
  if (!doc) return;
  
  if (!ie_doc_begin_op(doc, "Paste")) return;
  // Commit any in-progress selection move within the paste command
  if (doc->sel.move.active)
    canvas_commit_move(doc);
  
  canvas_paste_clipboard(doc);
  ie_doc_commit_op(doc, true);
}
