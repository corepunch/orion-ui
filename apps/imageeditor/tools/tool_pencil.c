// tools/tool_pencil.c — Pencil tool handler (simple drawing)

#include "tools.h"

// Forward declaration for global app state
extern app_state_t *g_app;

static float pencil_radius(void) {
  int idx = g_app ? g_app->brush_size : 0;
  if (idx < 0) idx = 0;
  if (idx >= NUM_BRUSH_SIZES) idx = NUM_BRUSH_SIZES - 1;
  return canvas_pointer_radius((float)kBrushSizes[idx]);
}

// ── Pencil tool lifecycle ──────────────────────────────────────────────────

// Pencil tool: rounded freehand strokes.
// - begin(): Push undo, start stroke
// - drag():  Extend the smoothed stroke
// - end():   Finish stroke
// - cancel(): Handled by default undo discard

static void pencil_begin(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  (void)view;
  ie_doc_begin_op(doc, "Pencil Stroke");
  canvas_stroke_begin(doc, doc_pt, pencil_radius(), g_app->fg_color, false);
  ie_doc_invalidate_canvas(doc);
}

static void pencil_drag(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  (void)view;
  canvas_stroke_set_radius(doc, pencil_radius());
  canvas_stroke_drag(doc, doc_pt);
  ie_doc_invalidate_canvas(doc);
}

static void pencil_end(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  (void)view;
  canvas_stroke_set_radius(doc, pencil_radius());
  canvas_stroke_end(doc, doc_pt);
  ie_doc_invalidate_canvas(doc);
  ie_doc_commit_op(doc, true);
}

static void pencil_cancel(canvas_doc_t *doc, canvas_win_state_t *view) {
  (void)view;
  canvas_stroke_cancel(doc);
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
