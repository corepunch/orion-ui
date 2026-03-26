// Brush tool – draws with a radius-2 circle

static void brush_down(canvas_doc_t *doc, int cx, int cy, rgba_t fg, rgba_t bg) {
  (void)bg;
  canvas_draw_circle(doc, cx, cy, 2, fg);
}

static void brush_drag(canvas_doc_t *doc, int x0, int y0, int cx, int cy, rgba_t fg, rgba_t bg) {
  (void)bg;
  canvas_draw_line(doc, x0, y0, cx, cy, 2, fg);
}

tool_t tool_brush = { "Brush", brush_down, brush_drag };
