// tools/tool_fill.c — Fill tool handler (flood fill)

#include "../imageeditor.h"
#include "tools.h"

extern app_state_t *g_app;

static void fill_begin(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  (void)view;
  if (!doc || !g_app) return;
  int gap = g_app->fill.gap * MAX(1, g_bw_retina_scale);
  IE_TRACE("fill begin doc=%p at=(%d,%d) gap=%d scale=%d", (void *)doc,
           doc_pt.x, doc_pt.y, gap, g_bw_retina_scale);
  ie_doc_begin_op(doc, "Fill");
  int stitches = canvas_flood_fill_with_gap(doc, doc_pt.x, doc_pt.y,
                                            g_app->fg_color, gap);
  IE_TRACE("fill end doc=%p stitches=%d", (void *)doc, stitches);
  (void)stitches;
  ie_doc_commit_op(doc, true);
}

static void fill_drag(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  // Fill tool doesn't drag
}

static void fill_end(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  // Fill completes on begin
}

static void fill_cancel(canvas_doc_t *doc, canvas_win_state_t *view) {
  // Fill doesn't need cancel (completes immediately)
}

static bool fill_key(canvas_doc_t *doc, canvas_win_state_t *view, uint32_t key, uint32_t mods) {
  return false;
}

const tool_handler_t tool_fill_handler = {
  .id = ID_TOOL_FILL,
  .name = "Fill",
  .begin = fill_begin,
  .drag = fill_drag,
  .end = fill_end,
  .cancel = fill_cancel,
  .key = fill_key,
};
