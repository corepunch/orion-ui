// tools/tool_select.c — Selection tool handler (rect selection)

#include "tools.h"

// ── Selection tool lifecycle ───────────────────────────────────────────────

// Selection tool: Rectangular selection with add-mode support.
// - begin(): Record start point in doc->sel
// - drag():  Update rubber-band rectangle
// - end():   Apply selection (canvas_select_rect or canvas_select_rect_add)
// - cancel(): Clear selection preview

static void select_begin(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  (void)view;
  
  if (!ie_doc_begin_op(doc, doc->sel.add_mode ? "Add to Selection" : "Select Rectangle")) return;
  // Start a new selection; commit any in-progress move first
  if (doc->sel.move.active) 
    canvas_commit_move(doc);
  
  // Note: add_mode should be set by caller based on shift key
  // For now we'll just use whatever's in doc->sel.add_mode
  if (!doc->sel.add_mode) {
    doc->sel.active = false;
    canvas_clear_selection_mask(doc);
  }
  
  doc->sel.start.x = doc_pt.x;
  doc->sel.start.y = doc_pt.y;
  doc->sel.end.x   = doc_pt.x;
  doc->sel.end.y   = doc_pt.y;
}

static void select_drag(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  (void)view;
  doc->sel.end.x = doc_pt.x;
  doc->sel.end.y = doc_pt.y;
  // Invalidate to redraw rubber-band (paint handler reads sel.start/end)
  ie_doc_invalidate_canvas(doc);
}

static void select_end(canvas_doc_t *doc, canvas_win_state_t *view, ipoint16_t doc_pt) {
  (void)view;
  doc->sel.end.x = doc_pt.x;
  doc->sel.end.y = doc_pt.y;
  
  // Normalize coordinates
  int x0 = MIN(doc->sel.start.x, doc->sel.end.x);
  int y0 = MIN(doc->sel.start.y, doc->sel.end.y);
  int x1 = MAX(doc->sel.start.x, doc->sel.end.x);
  int y1 = MAX(doc->sel.start.y, doc->sel.end.y);
  
  // Apply selection
  bool ok = doc->sel.add_mode 
    ? canvas_select_rect_add(doc, x0, y0, x1, y1)
    : canvas_select_rect(doc, x0, y0, x1, y1);
  ie_doc_commit_op(doc, ok);
  

}

static void select_cancel(canvas_doc_t *doc, canvas_win_state_t *view) {
  (void)view;
  ie_doc_commit_op(doc, false);
}

static bool select_key(canvas_doc_t *doc, canvas_win_state_t *view, uint32_t key, uint32_t mods) {
  (void)view; (void)mods;
  
  // ESC cancels selection
  if (key == AX_KEY_ESCAPE) {
    select_cancel(doc, view);
    return true;
  }
  
  return false;
}

// ── Public handler ─────────────────────────────────────────────────────────

const tool_handler_t tool_select_handler = {
  .id     = ID_TOOL_SELECT,
  .name   = "Selection",
  .begin  = select_begin,
  .drag   = select_drag,
  .end    = select_end,
  .cancel = select_cancel,
  .key    = select_key,
};
