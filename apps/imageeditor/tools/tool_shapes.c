// tools/tool_shapes.c — Shape tool handlers (rect, ellipse, rounded_rect)
// All follow the same rubber-band preview pattern as tool_line.c

#include "../imageeditor.h"
#include "tools.h"

extern app_state_t *g_app;

// ── Rectangle tool ──────────────────────────────────────────────────────────

static void rect_begin(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  if (!doc) return;
  if (!ie_doc_begin_op(doc, "Draw Shape")) return;
  if (!canvas_shape_begin(doc, doc_pt.x, doc_pt.y)) { ie_doc_commit_op(doc, false); return; }
  doc->last = doc_pt;
  doc->shape.start = doc_pt;
}

static void rect_drag(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  if (!doc || !g_app) return;
  canvas_shape_preview(doc, doc->shape.start.x, doc->shape.start.y, doc_pt.x, doc_pt.y,
                       ID_TOOL_RECT, g_app->shape_filled, g_app->fg_color, g_app->fg_color, false);
  ie_doc_invalidate_canvas(doc);
}

static void rect_end(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  if (!doc) return;
  canvas_shape_commit(doc);
  ie_doc_commit_op(doc, true);
}

static void rect_cancel(canvas_doc_t *doc, canvas_win_state_t *view) {
  ie_doc_commit_op(doc, false);
}

static bool rect_key(canvas_doc_t *doc, canvas_win_state_t *view, uint32_t key, uint32_t mods) {
  return false;
}

const tool_handler_t tool_rect_handler = {
  .id = ID_TOOL_RECT,
  .name = "Rectangle",
  .begin = rect_begin,
  .drag = rect_drag,
  .end = rect_end,
  .cancel = rect_cancel,
  .key = rect_key,
};

// ── Ellipse tool ────────────────────────────────────────────────────────────

static void ellipse_begin(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  rect_begin(doc, view, doc_pt);
}

static void ellipse_drag(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  if (!doc || !g_app) return;
  canvas_shape_preview(doc, doc->shape.start.x, doc->shape.start.y, doc_pt.x, doc_pt.y,
                       ID_TOOL_ELLIPSE, g_app->shape_filled, g_app->fg_color, g_app->fg_color, false);
  ie_doc_invalidate_canvas(doc);
}

static void ellipse_end(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  if (!doc) return;
  canvas_shape_commit(doc);
  ie_doc_commit_op(doc, true);
}

static bool ellipse_key(canvas_doc_t *doc, canvas_win_state_t *view, uint32_t key, uint32_t mods) {
  return false;
}

const tool_handler_t tool_ellipse_handler = {
  .id = ID_TOOL_ELLIPSE,
  .name = "Ellipse",
  .begin = ellipse_begin,
  .drag = ellipse_drag,
  .end = ellipse_end,
  .cancel = rect_cancel,
  .key = ellipse_key,
};

// ── Rounded Rectangle tool ──────────────────────────────────────────────────

static void rounded_rect_begin(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  rect_begin(doc, view, doc_pt);
}

static void rounded_rect_drag(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  if (!doc || !g_app) return;
  canvas_shape_preview(doc, doc->shape.start.x, doc->shape.start.y, doc_pt.x, doc_pt.y,
                       ID_TOOL_ROUNDED_RECT, g_app->shape_filled, g_app->fg_color, g_app->fg_color, false);
  ie_doc_invalidate_canvas(doc);
}

static void rounded_rect_end(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  if (!doc) return;
  canvas_shape_commit(doc);
  ie_doc_commit_op(doc, true);
}

static bool rounded_rect_key(canvas_doc_t *doc, canvas_win_state_t *view, uint32_t key, uint32_t mods) {
  return false;
}

const tool_handler_t tool_rounded_rect_handler = {
  .id = ID_TOOL_ROUNDED_RECT,
  .name = "Rounded Rectangle",
  .begin = rounded_rect_begin,
  .drag = rounded_rect_drag,
  .end = rounded_rect_end,
  .cancel = rect_cancel,
  .key = rounded_rect_key,
};
