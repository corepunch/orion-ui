// Canvas operations: shape tool helpers (main logic split into specialized modules)
// See: canvas_pixels.c, canvas_layers.c, canvas_selection.c, canvas_resize.c, canvas_render.c

#include "imageeditor.h"
#include <limits.h>

// ============================================================
// Shape tool helpers
// ============================================================

bool canvas_is_shape_tool(int tool_id) {
  switch (tool_id) {
    case ID_TOOL_LINE:
    case ID_TOOL_RECT:
    case ID_TOOL_ELLIPSE:
    case ID_TOOL_ROUNDED_RECT:
      return true;
    default:
      return false;
  }
}

typedef enum {
  DRAG_ALIAS_NONE = 0,
  DRAG_ALIAS_45_DEGREES,
  DRAG_ALIAS_SQUARE,
} drag_alias_t;

typedef struct {
  int          tool_id;
  uint32_t     mods;
  drag_alias_t alias;
} tool_drag_alias_t;

static const tool_drag_alias_t kToolDragAliases[] = {
  { ID_TOOL_LINE,         AX_MOD_SHIFT, DRAG_ALIAS_45_DEGREES },
  { ID_TOOL_RECT,         AX_MOD_SHIFT, DRAG_ALIAS_SQUARE },
  { ID_TOOL_ELLIPSE,      AX_MOD_SHIFT, DRAG_ALIAS_SQUARE },
  { ID_TOOL_ROUNDED_RECT, AX_MOD_SHIFT, DRAG_ALIAS_SQUARE },
  { ID_TOOL_SELECT,       AX_MOD_SHIFT, DRAG_ALIAS_SQUARE },
};

static drag_alias_t tool_drag_alias_for(int tool_id, uint32_t mods) {
  for (size_t i = 0; i < sizeof(kToolDragAliases) / sizeof(kToolDragAliases[0]); i++) {
    const tool_drag_alias_t *a = &kToolDragAliases[i];
    if (a->tool_id == tool_id && (mods & a->mods) == a->mods)
      return a->alias;
  }
  return DRAG_ALIAS_NONE;
}

void canvas_constrain_tool_drag(int tool_id, uint32_t mods,
                                int x0, int y0, int *x1, int *y1) {
  if (!x1 || !y1) return;
  int dx = *x1 - x0;
  int dy = *y1 - y0;

  switch (tool_drag_alias_for(tool_id, mods)) {
    case DRAG_ALIAS_45_DEGREES:
      if (abs(dx) > abs(dy) * 2) {
        dy = 0;
      } else if (abs(dy) > abs(dx) * 2) {
        dx = 0;
      } else {
        int s = MAX(abs(dx), abs(dy));
        dx = (dx < 0) ? -s : s;
        dy = (dy < 0) ? -s : s;
      }
      *x1 = x0 + dx;
      *y1 = y0 + dy;
      break;
    case DRAG_ALIAS_SQUARE: {
      int s = MIN(abs(dx), abs(dy));
      *x1 = x0 + ((dx < 0) ? -s : s);
      *y1 = y0 + ((dy < 0) ? -s : s);
      break;
    }
    case DRAG_ALIAS_NONE:
    default:
      break;
  }
}

// Save pixel snapshot before starting a shape drag (no undo push yet)
void canvas_shape_begin(canvas_doc_t *doc, int cx, int cy) {
  size_t sz = (size_t)doc->canvas_w * doc->canvas_h * DOC_BPP;
  if (!doc->shape.snapshot) {
    doc->shape.snapshot = malloc(sz);
  }
  if (doc->shape.snapshot) {
    memcpy(doc->shape.snapshot, doc->pixels, sz);
  }
  doc->shape.start.x = cx;
  doc->shape.start.y = cy;
}

// Restore snapshot and draw a preview of the current shape without pushing undo.
// The initial point is the center for area shapes and the first endpoint for lines.
// shift_held constrains the shape (45° line, square, circle).
void canvas_shape_preview(canvas_doc_t *doc, int x0, int y0, int x1, int y1,
                          int tool, bool filled, uint32_t fg, uint32_t bg, bool shift_held) {
  // Restore snapshot
  if (doc->shape.snapshot) {
    memcpy(doc->pixels, doc->shape.snapshot, (size_t)doc->canvas_w * doc->canvas_h * DOC_BPP);
    doc->canvas_dirty = true;
  }
  canvas_constrain_tool_drag(tool, shift_held ? AX_MOD_SHIFT : 0, x0, y0, &x1, &y1);
  int half_w = abs(x1 - x0), half_h = abs(y1 - y0);
  int lx = x0 - half_w, rx = x0 + half_w;
  int ty = y0 - half_h, by = y0 + half_h;
  int w = rx - lx + 1, h = by - ty + 1;
  int rxa = half_w, rya = half_h;
  int corner_r = MIN(8, MIN(w / 4, h / 4));

  switch (tool) {
    case ID_TOOL_LINE:
      canvas_draw_line(doc, x0, y0, x1, y1, 0, fg);
      break;
    case ID_TOOL_RECT:
      if (filled) canvas_draw_rect_filled(doc, lx, ty, w, h, fg, bg);
      else        canvas_draw_rect_outline(doc, lx, ty, w, h, fg);
      break;
    case ID_TOOL_ELLIPSE:
      if (filled) canvas_draw_ellipse_filled(doc, x0, y0, rxa, rya, fg, bg);
      else        canvas_draw_ellipse_outline(doc, x0, y0, rxa, rya, fg);
      break;
    case ID_TOOL_ROUNDED_RECT:
      if (filled) canvas_draw_rounded_rect_filled(doc, lx, ty, w, h, corner_r, fg, bg);
      else        canvas_draw_rounded_rect_outline(doc, lx, ty, w, h, corner_r, fg);
      break;
  }
}

// No-op: snapshot is kept until next shape begins or doc is freed
void canvas_shape_commit(canvas_doc_t *doc) {
  (void)doc;
}
