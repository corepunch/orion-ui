// tools/tool_pencil.c — Pencil tool handler (simple drawing)

#include "tools.h"

// Forward declaration for global app state
extern app_state_t *g_app;

// ── Pencil tool lifecycle ──────────────────────────────────────────────────

// Pencil tool:  Simple pixel-by-pixel drawing.
// - begin(): Push undo, start stroke
// - drag():  Draw line from last position to current
// - end():   Finish stroke
// - cancel(): Handled by default undo discard

static void pencil_begin(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  (void)view;
  ie_doc_begin_op(doc, "Pencil Stroke");
  doc->last.x = doc_pt.x;
  doc->last.y = doc_pt.y;
  canvas_draw_pen(doc, doc_pt.x, doc_pt.y, g_app->fg_color);
  ie_doc_invalidate_canvas(doc);
}

static void pencil_drag(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  (void)view;
  // Draw line from last position to current
  canvas_draw_pen_line(doc, doc->last.x, doc->last.y, doc_pt.x, doc_pt.y, g_app->fg_color);
  doc->last.x = doc_pt.x;
  doc->last.y = doc_pt.y;
  ie_doc_invalidate_canvas(doc);
}

static void pencil_end(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  (void)view; (void)doc_pt;
  ie_doc_commit_op(doc, true);
}

static void pencil_cancel(canvas_doc_t *doc, canvas_win_state_t *view) {
  (void)view;
  ie_doc_commit_op(doc, false);  // Discard undo
}

static bool pencil_key(canvas_doc_t *doc, canvas_win_state_t *view, uint32_t key, uint32_t mods) {
  (void)doc; (void)view; (void)key; (void)mods;
  return false;  // No tool-specific shortcuts
}

// ── Public handler ─────────────────────────────────────────────────────────

const tool_handler_t tool_pencil_handler = {
  .id     = ID_TOOL_PENCIL,
  .name   = "Pencil",
  .begin  = pencil_begin,
  .drag   = pencil_drag,
  .end    = pencil_end,
  .cancel = pencil_cancel,
  .key    = pencil_key,
};
