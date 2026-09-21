// tools/tool_line.c — Line tool handler (shape preview pattern)

#include "tools.h"

// Forward declaration for global app state
extern app_state_t *g_app;

// ── Line tool lifecycle ────────────────────────────────────────────────────

// Line tool: Rubber-band preview + commit on mouse up.
// - begin(): Begin a command before previewing
// - drag():  Restore snapshot and draw preview line
// - end():   Commit the command
// - cancel(): Roll back the command

static void line_begin(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  (void)view;
  if (!ie_doc_begin_op(doc, "Draw Shape")) return;
  if (!canvas_shape_begin(doc, doc_pt.x, doc_pt.y)) { ie_doc_commit_op(doc, false); return; }
  doc->last.x = doc_pt.x;
  doc->last.y = doc_pt.y;
}

static void line_drag(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  (void)view;
  // Restore snapshot and preview line with constraints
  canvas_shape_preview(doc, doc->last.x, doc->last.y, doc_pt.x, doc_pt.y,
                      ID_TOOL_LINE, false, g_app->fg_color, 0, false);
  ie_doc_invalidate_canvas(doc);
}

static void line_end(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  (void)view; (void)doc_pt;
  canvas_shape_commit(doc);
  ie_doc_commit_op(doc, true);
  doc->last.x = -1;
  doc->last.y = -1;
}

static void line_cancel(canvas_doc_t *doc, canvas_win_state_t *view) {
  (void)view;
  ie_doc_commit_op(doc, false);
  doc->last.x = -1;
  doc->last.y = -1;
  ie_doc_invalidate_canvas(doc);
}

static bool line_key(canvas_doc_t *doc, canvas_win_state_t *view, uint32_t key, uint32_t mods) {
  (void)view; (void)mods;
  
  // ESC cancels line
  if (key == AX_KEY_ESCAPE) {
    line_cancel(doc, view);
    return true;
  }
  
  return false;
}

// ── Public handler ─────────────────────────────────────────────────────────

const tool_handler_t tool_line_handler = {
  .id     = ID_TOOL_LINE,
  .name   = "Line",
  .begin  = line_begin,
  .drag   = line_drag,
  .end    = line_end,
  .cancel = line_cancel,
  .key    = line_key,
};
