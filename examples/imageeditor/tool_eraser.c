// Eraser tool – paints with the background color using a radius-3 circle

static void eraser_down(canvas_doc_t *doc, int cx, int cy, rgba_t fg, rgba_t bg) {
  (void)fg;
  canvas_draw_circle(doc, cx, cy, 3, bg);
}

static void eraser_drag(canvas_doc_t *doc, int x0, int y0, int cx, int cy, rgba_t fg, rgba_t bg) {
  (void)fg;
  canvas_draw_line(doc, x0, y0, cx, cy, 3, bg);
}

tool_t tool_eraser = { "Eraser", eraser_down, eraser_drag };
