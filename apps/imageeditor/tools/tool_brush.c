// tools/tool_brush.c — Brush tool handler (similar to pencil, with radius)

#include "../imageeditor.h"
#include "tools.h"

extern app_state_t *g_app;

// Helper to get brush radius from app state
static float tool_brush_get_radius(void) {
  int idx = g_app ? g_app->brush_size : 0;
  if (idx < 0) idx = 0;
  if (idx >= NUM_BRUSH_SIZES) idx = NUM_BRUSH_SIZES - 1;
  return canvas_pointer_radius((float)kBrushSizes[idx]);
}

static void brush_begin(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  if (!doc || !g_app) return;
  ie_doc_begin_op(doc, "Brush Stroke");
  canvas_stroke_begin(doc, doc_pt, tool_brush_get_radius(), g_app->fg_color, true);
  ie_doc_after_pixels_changed(doc);
}

static void brush_drag(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  (void)view;
  canvas_stroke_set_radius(doc, tool_brush_get_radius());
  canvas_stroke_drag(doc, doc_pt);
  ie_doc_after_pixels_changed(doc);
}

static void brush_end(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  if (!doc) return;
  canvas_stroke_set_radius(doc, tool_brush_get_radius());
  canvas_stroke_end(doc, doc_pt);
  ie_doc_invalidate_canvas(doc);
  ie_doc_commit_op(doc, true);
}

static void brush_cancel(canvas_doc_t *doc, canvas_win_state_t *view) {
  if (!doc) return;
  canvas_stroke_cancel(doc);
  ie_doc_commit_op(doc, false);
}

static bool brush_key(canvas_doc_t *doc, canvas_win_state_t *view, uint32_t key, uint32_t mods) {
  return false;
}

const tool_handler_t tool_brush_handler = {
  .id = ID_TOOL_BRUSH,
  .name = "Brush",
  .begin = brush_begin,
  .drag = brush_drag,
  .end = brush_end,
  .cancel = brush_cancel,
  .key = brush_key,
};
