// Fill tool – flood fills a region with the foreground color

static void fill_down(canvas_doc_t *doc, int cx, int cy, rgba_t fg, rgba_t bg) {
  (void)bg;
  canvas_flood_fill(doc, cx, cy, fg);
}

static void fill_drag(canvas_doc_t *doc, int x0, int y0, int cx, int cy, rgba_t fg, rgba_t bg) {
  (void)doc; (void)x0; (void)y0; (void)cx; (void)cy; (void)fg; (void)bg;
}

tool_t tool_fill = { "Fill", fill_down, fill_drag };
