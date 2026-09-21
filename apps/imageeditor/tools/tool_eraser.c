// tools/tool_eraser.c — Eraser tool handler (erases to transparent)

#include "../imageeditor.h"
#include "tools.h"

extern app_state_t *g_app;

static int eraser_brush_radius(void) {
  int idx = g_app ? g_app->brush_size : 0;
  if (idx < 0) idx = 0;
  if (idx >= NUM_BRUSH_SIZES) idx = NUM_BRUSH_SIZES - 1;
  return kBrushSizes[idx];
}

static uint32_t erase_color(canvas_doc_t *doc) {
#if IMAGEEDITOR_INDEXED
  return doc->ipal.entries[doc->ipal.transparent];
#else
  return MAKE_COLOR(0, 0, 0, 0);
#endif
}

static void eraser_begin(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  if (!doc) return;
  if (!ie_doc_begin_op(doc, "Erase")) return;
  canvas_stroke_begin(doc, doc_pt, eraser_brush_radius(), erase_color(doc), false);
  ie_doc_after_pixels_changed(doc);
}

static void eraser_drag(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  (void)view;
  canvas_stroke_drag(doc, doc_pt);
  ie_doc_after_pixels_changed(doc);
}

static void eraser_end(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  if (!doc) return;
  canvas_stroke_end(doc, doc_pt);
  ie_doc_invalidate_canvas(doc);
  ie_doc_commit_op(doc, true);
}

static void eraser_cancel(canvas_doc_t *doc, canvas_win_state_t *view) {
  if (!doc) return;
  canvas_stroke_cancel(doc);
  ie_doc_commit_op(doc, false);
}

static bool eraser_key(canvas_doc_t *doc, canvas_win_state_t *view, uint32_t key, uint32_t mods) {
  return false;
}

const tool_handler_t tool_eraser_handler = {
  .id = ID_TOOL_ERASER,
  .name = "Eraser",
  .begin = eraser_begin,
  .drag = eraser_drag,
  .end = eraser_end,
  .cancel = eraser_cancel,
  .key = eraser_key,
};
