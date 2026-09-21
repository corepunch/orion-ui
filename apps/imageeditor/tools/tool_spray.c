// tools/tool_spray.c — Spray tool handler (spray paint pattern)

#include "../imageeditor.h"
#include "tools.h"

extern app_state_t *g_app;

static void spray_begin(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  if (!doc || !g_app) return;
  if (!ie_doc_begin_op(doc, "Spray")) return;
  doc->last = doc_pt;
  canvas_spray(doc, doc_pt.x, doc_pt.y, 8, g_app->fg_color);
  ie_doc_after_pixels_changed(doc);
}

static void spray_drag(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  if (!doc || !g_app) return;
  canvas_spray(doc, doc_pt.x, doc_pt.y, 8, g_app->fg_color);
  doc->last = doc_pt;
  ie_doc_after_pixels_changed(doc);
}

static void spray_end(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  if (!doc) return;
  ie_doc_commit_op(doc, true);
}

static void spray_cancel(canvas_doc_t *doc, canvas_win_state_t *view) {
  if (!doc) return;
  ie_doc_commit_op(doc, false);
}

static bool spray_key(canvas_doc_t *doc, canvas_win_state_t *view, uint32_t key, uint32_t mods) {
  return false;
}

const tool_handler_t tool_spray_handler = {
  .id = ID_TOOL_SPRAY,
  .name = "Spray",
  .begin = spray_begin,
  .drag = spray_drag,
  .end = spray_end,
  .cancel = spray_cancel,
  .key = spray_key,
};
