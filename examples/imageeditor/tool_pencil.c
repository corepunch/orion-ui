// Pencil tool – draws single pixels

#include "imageeditor.h"

static void pencil_down(canvas_doc_t *doc, int cx, int cy, rgba_t fg, rgba_t bg) {
  (void)bg;
  canvas_draw_circle(doc, cx, cy, 0, fg);
}

static void pencil_drag(canvas_doc_t *doc, int x0, int y0, int cx, int cy, rgba_t fg, rgba_t bg) {
  (void)bg;
  canvas_draw_line(doc, x0, y0, cx, cy, 0, fg);
}

tool_t tool_pencil = { "Pencil", pencil_down, pencil_drag };
