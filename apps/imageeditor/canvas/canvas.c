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
  IE_TRACE("shape_begin win=%p doc=%p start=(%d,%d)", (void *)doc->canvas_win, (void *)doc, cx, cy);
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

static void canvas_shape_rotated(canvas_doc_t *doc, int x0, int y0, int x1, int y1,
                                 int tool, bool filled, uint32_t fg, uint32_t bg,
                                 bool shift_held, float c, float s) {
  // Work in screen-aligned axes at document scale, then rotate the contour back.
  int dx = (int)lroundf(c * (x1 - x0) - s * (y1 - y0));
  int dy = (int)lroundf(s * (x1 - x0) + c * (y1 - y0));
  canvas_constrain_tool_drag(tool, shift_held ? AX_MOD_SHIFT : 0, 0, 0, &dx, &dy);
  float hw = abs(dx), hh = abs(dy);
  float radius = tool == ID_TOOL_ROUNDED_RECT ? MIN(8 * MAX(1, g_bw_retina_scale),
                                                  MIN((2 * abs(dx) + 1) / 4, (2 * abs(dy) + 1) / 4)) : 0;
  int steps = tool == ID_TOOL_ELLIPSE ? (int)ceilf(sqrtf(MAX(hw, hh)) * 2) :
              radius > 0 ? (int)ceilf(sqrtf(radius) * 2) : 0;
  steps = CLAMP(steps, 1, 255);
  ipoint16_t points[1024];
  int count = 0;
  for (int quadrant = 0; quadrant < 4; quadrant++) {
    float sx = quadrant == 0 || quadrant == 3 ? 1 : -1;
    float sy = quadrant < 2 ? 1 : -1;
    for (int i = 0; i <= steps; i++) {
      float angle = (quadrant + (float)i / steps) * 1.57079632679f;
      float x = tool == ID_TOOL_ELLIPSE ? hw * cosf(angle) : sx * (hw - radius) + radius * cosf(angle);
      float y = tool == ID_TOOL_ELLIPSE ? hh * sinf(angle) : sy * (hh - radius) + radius * sinf(angle);
      points[count++] = (ipoint16_t){CLAMP(lroundf(x0 + c * x + s * y), INT16_MIN, INT16_MAX),
                                    CLAMP(lroundf(y0 - s * x + c * y), INT16_MIN, INT16_MAX)};
    }
  }
  canvas_draw_polygon_scaled(doc, points, count, filled, fg, bg);
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
  window_t *win = doc->canvas_win;
  if (tool != ID_TOOL_LINE && win && win->view.enabled) {
    float a = win->view.matrix.a, b = win->view.matrix.b;
    float scale = hypotf(a, b);
    if (scale > 0 && (fabsf(b) > scale * 0.00001f || a < 0)) {
      IE_TRACE("shape_preview win=%p tool=%d start=(%d,%d) end=(%d,%d) rotation=%f",
               (void *)win, tool, x0, y0, x1, y1, atan2f(b, a));
      canvas_shape_rotated(doc, x0, y0, x1, y1, tool, filled, fg, bg, shift_held, a / scale, b / scale);
      return;
    }
  }
  int step = MAX(1, g_bw_retina_scale);
  canvas_constrain_tool_drag(tool, shift_held ? AX_MOD_SHIFT : 0, x0, y0, &x1, &y1);
  int half_w = abs(x1 - x0), half_h = abs(y1 - y0);
  int lx = x0 - half_w, rx = x0 + half_w;
  int ty = y0 - half_h, by = y0 + half_h;
  int w = rx - lx + 1, h = by - ty + 1;
  int rxa = half_w, rya = half_h;
  int corner_r = MIN(8 * step, MIN(w / 4, h / 4));

  switch (tool) {
    case ID_TOOL_LINE:
      canvas_draw_pen_line(doc, x0, y0, x1, y1, fg);
      break;
    case ID_TOOL_RECT:
      canvas_draw_rect_scaled(doc, lx, ty, w, h, filled, fg, bg);
      break;
    case ID_TOOL_ELLIPSE:
      canvas_draw_ellipse_scaled(doc, x0, y0, rxa, rya, filled, fg, bg);
      break;
    case ID_TOOL_ROUNDED_RECT:
      canvas_draw_rounded_rect_scaled(doc, lx, ty, w, h, corner_r, filled, fg, bg);
      break;
  }
}

// No-op: snapshot is kept until next shape begins or doc is freed
void canvas_shape_commit(canvas_doc_t *doc) {
  (void)doc;
}
