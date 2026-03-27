// Canvas operations: pixel drawing, PNG I/O, GL texture management

#include "imageeditor.h"

// ============================================================
// Canvas pixel operations
// ============================================================

// Write a pixel directly (bypasses selection mask – used for paste/move commit).
static void canvas_set_pixel_direct(canvas_doc_t *doc, int x, int y, rgba_t c) {
  if (!canvas_in_bounds(x, y)) return;
  uint8_t *p = doc->pixels + ((size_t)y * CANVAS_W + x) * 4;
  p[0]=c.r; p[1]=c.g; p[2]=c.b; p[3]=c.a;
  doc->canvas_dirty = true;
  doc->modified     = true;
}

void canvas_set_pixel(canvas_doc_t *doc, int x, int y, rgba_t c) {
  if (!canvas_in_bounds(x, y)) return;
  if (!canvas_in_selection(doc, x, y)) return;
  uint8_t *p = doc->pixels + ((size_t)y * CANVAS_W + x) * 4;
  p[0]=c.r; p[1]=c.g; p[2]=c.b; p[3]=c.a;
  doc->canvas_dirty = true;
  doc->modified     = true;
}

rgba_t canvas_get_pixel(const canvas_doc_t *doc, int x, int y) {
  if (!canvas_in_bounds(x, y)) return (rgba_t){0,0,0,0};
  const uint8_t *p = doc->pixels + ((size_t)y * CANVAS_W + x) * 4;
  return (rgba_t){p[0],p[1],p[2],p[3]};
}

void canvas_clear(canvas_doc_t *doc) {
  memset(doc->pixels, 0xFF, sizeof(doc->pixels));
  doc->canvas_dirty = true;
  doc->modified     = false;
}

void canvas_draw_circle(canvas_doc_t *doc, int cx, int cy, int r, rgba_t c) {
  for (int dy = -r; dy <= r; dy++)
    for (int dx = -r; dx <= r; dx++)
      if (dx*dx + dy*dy <= r*r)
        canvas_set_pixel(doc, cx+dx, cy+dy, c);
}

void canvas_draw_line(canvas_doc_t *doc, int x0, int y0, int x1, int y1,
                      int radius, rgba_t c) {
  int dx = abs(x1-x0), dy = abs(y1-y0);
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;
  while (true) {
    canvas_draw_circle(doc, x0, y0, radius, c);
    if (x0==x1 && y0==y1) break;
    int e2 = 2*err;
    if (e2 > -dy) { err -= dy; x0 += sx; }
    if (e2 <  dx) { err += dx; y0 += sy; }
  }
}

void canvas_flood_fill(canvas_doc_t *doc, int sx, int sy, rgba_t fill) {
  if (!canvas_in_selection(doc, sx, sy)) return;
  rgba_t target = canvas_get_pixel(doc, sx, sy);
  if (rgba_eq(target, fill)) return;

  typedef struct { int16_t x, y; } pt_t;
  int capacity = CANVAS_W * CANVAS_H;
  pt_t *queue = malloc(sizeof(pt_t) * capacity);
  if (!queue) return;

  int head = 0, tail = 0;
  queue[tail++] = (pt_t){(int16_t)sx, (int16_t)sy};
  canvas_set_pixel(doc, sx, sy, fill);

  while (head < tail) {
    pt_t cur = queue[head++];
    int nx[4] = {cur.x+1, cur.x-1, cur.x,   cur.x};
    int ny[4] = {cur.y,   cur.y,   cur.y+1, cur.y-1};
    for (int i = 0; i < 4; i++) {
      if (canvas_in_bounds(nx[i], ny[i]) &&
          canvas_in_selection(doc, nx[i], ny[i]) &&
          rgba_eq(canvas_get_pixel(doc, nx[i], ny[i]), target) &&
          tail < capacity) {
        canvas_set_pixel(doc, nx[i], ny[i], fill);
        queue[tail++] = (pt_t){(int16_t)nx[i], (int16_t)ny[i]};
      }
    }
  }
  free(queue);
}

// ============================================================
// Shape drawing functions
// ============================================================

void canvas_draw_rect_outline(canvas_doc_t *doc, int x, int y, int w, int h, rgba_t c) {
  if (w <= 0 || h <= 0) return;
  canvas_draw_line(doc, x,     y,     x+w-1, y,     0, c);
  canvas_draw_line(doc, x,     y+h-1, x+w-1, y+h-1, 0, c);
  canvas_draw_line(doc, x,     y,     x,     y+h-1, 0, c);
  canvas_draw_line(doc, x+w-1, y,     x+w-1, y+h-1, 0, c);
}

void canvas_draw_rect_filled(canvas_doc_t *doc, int x, int y, int w, int h, rgba_t outline, rgba_t fill) {
  if (w <= 0 || h <= 0) return;
  for (int dy = 1; dy < h - 1; dy++)
    canvas_draw_line(doc, x+1, y+dy, x+w-2, y+dy, 0, fill);
  canvas_draw_rect_outline(doc, x, y, w, h, outline);
}

// Midpoint ellipse algorithm (Bresenham's)
void canvas_draw_ellipse_outline(canvas_doc_t *doc, int cx, int cy, int rx, int ry, rgba_t c) {
  if (rx <= 0 || ry <= 0) return;
  long rx2 = (long)rx * rx, ry2 = (long)ry * ry;
  long x = 0, y = ry;
  long dx = 2 * ry2 * x, dy = 2 * rx2 * y;
  long p = (long)(ry2 - rx2 * ry + 0.25f * rx2);

  while (dx < dy) {
    canvas_set_pixel(doc, (int)(cx+x), (int)(cy+y), c);
    canvas_set_pixel(doc, (int)(cx-x), (int)(cy+y), c);
    canvas_set_pixel(doc, (int)(cx+x), (int)(cy-y), c);
    canvas_set_pixel(doc, (int)(cx-x), (int)(cy-y), c);
    x++;
    dx += 2 * ry2;
    if (p < 0) {
      p += ry2 + dx;
    } else {
      y--; dy -= 2 * rx2; p += ry2 + dx - dy;
    }
  }
  p = (long)(ry2 * (x + 0.5f) * (x + 0.5f) + rx2 * (y-1) * (y-1) - rx2 * ry2);
  while (y >= 0) {
    canvas_set_pixel(doc, (int)(cx+x), (int)(cy+y), c);
    canvas_set_pixel(doc, (int)(cx-x), (int)(cy+y), c);
    canvas_set_pixel(doc, (int)(cx+x), (int)(cy-y), c);
    canvas_set_pixel(doc, (int)(cx-x), (int)(cy-y), c);
    y--;
    dy -= 2 * rx2;
    if (p > 0) {
      p += rx2 - dy;
    } else {
      x++; dx += 2 * ry2; p += rx2 - dy + dx;
    }
  }
}

void canvas_draw_ellipse_filled(canvas_doc_t *doc, int cx, int cy, int rx, int ry, rgba_t outline, rgba_t fill) {
  if (rx <= 0 || ry <= 0) return;
  double rx2 = (double)rx * (double)rx;
  double ry2 = (double)ry * (double)ry;
  for (int py = cy - ry; py <= cy + ry; py++) {
    if (!canvas_in_bounds(cx, py)) continue;
    double dy = (double)(py - cy);
    double t = 1.0 - (dy * dy) / ry2;
    if (t <= 0.0) continue;
    int dx = (int)(sqrt(rx2 * t) + 0.5);
    canvas_draw_line(doc, cx - dx + 1, py, cx + dx - 1, py, 0, fill);
  }
  canvas_draw_ellipse_outline(doc, cx, cy, rx, ry, outline);
}

// Rounded rectangle using arc + straight edges
void canvas_draw_rounded_rect_outline(canvas_doc_t *doc, int x, int y, int w, int h, int r, rgba_t c) {
  if (w <= 0 || h <= 0) return;
  if (r < 0) r = 0;
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;
  // Straight edges
  canvas_draw_line(doc, x+r,   y,     x+w-r-1, y,     0, c);
  canvas_draw_line(doc, x+r,   y+h-1, x+w-r-1, y+h-1, 0, c);
  canvas_draw_line(doc, x,     y+r,   x,        y+h-r-1, 0, c);
  canvas_draw_line(doc, x+w-1, y+r,   x+w-1,   y+h-r-1, 0, c);
  // Four quarter arcs using midpoint circle algorithm
  int px = 0, py = r, d = 3 - 2*r;
  while (px <= py) {
    canvas_set_pixel(doc, x+r-px,   y+r-py,   c);
    canvas_set_pixel(doc, x+w-r+px-1, y+r-py, c);
    canvas_set_pixel(doc, x+r-py,   y+r-px,   c);
    canvas_set_pixel(doc, x+w-r+py-1, y+r-px, c);
    canvas_set_pixel(doc, x+r-px,   y+h-r+py-1, c);
    canvas_set_pixel(doc, x+w-r+px-1, y+h-r+py-1, c);
    canvas_set_pixel(doc, x+r-py,   y+h-r+px-1, c);
    canvas_set_pixel(doc, x+w-r+py-1, y+h-r+px-1, c);
    if (d < 0) { d += 4*px + 6; }
    else       { d += 4*(px-py) + 10; py--; }
    px++;
  }
}

void canvas_draw_rounded_rect_filled(canvas_doc_t *doc, int x, int y, int w, int h, int r, rgba_t outline, rgba_t fill) {
  if (w <= 0 || h <= 0) return;
  if (r < 0) r = 0;
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;
  // Fill rows from y+r to y+h-r (full width interior)
  for (int dy = r; dy < h - r; dy++)
    canvas_draw_line(doc, x+1, y+dy, x+w-2, y+dy, 0, fill);
  // Fill corner arcs using circle scan-line fill
  int px = 0, py = r, d = 3 - 2*r;
  while (px <= py) {
    // Fill horizontal spans for corner arcs
    canvas_draw_line(doc, x+r-py+1, y+r-px, x+w-r+py-2, y+r-px, 0, fill);
    canvas_draw_line(doc, x+r-px+1, y+r-py, x+w-r+px-2, y+r-py, 0, fill);
    canvas_draw_line(doc, x+r-py+1, y+h-r+px-1, x+w-r+py-2, y+h-r+px-1, 0, fill);
    canvas_draw_line(doc, x+r-px+1, y+h-r+py-1, x+w-r+px-2, y+h-r+py-1, 0, fill);
    if (d < 0) { d += 4*px + 6; }
    else       { d += 4*(px-py) + 10; py--; }
    px++;
  }
  canvas_draw_rounded_rect_outline(doc, x, y, w, h, r, outline);
}

// Polygon: draw edges between consecutive vertices and close the last to first
void canvas_draw_polygon_outline(canvas_doc_t *doc, const point_t *pts, int count, rgba_t c) {
  if (count < 2) return;
  for (int i = 0; i < count - 1; i++)
    canvas_draw_line(doc, pts[i].x, pts[i].y, pts[i+1].x, pts[i+1].y, 0, c);
  canvas_draw_line(doc, pts[count-1].x, pts[count-1].y, pts[0].x, pts[0].y, 0, c);
}

// Scanline fill for a closed polygon using the ray-casting / edge table approach
void canvas_draw_polygon_filled(canvas_doc_t *doc, const point_t *pts, int count, rgba_t outline, rgba_t fill) {
  if (count < 3) { canvas_draw_polygon_outline(doc, pts, count, outline); return; }
  // Find bounding box
  int y_min = pts[0].y, y_max = pts[0].y;
  for (int i = 1; i < count; i++) {
    if (pts[i].y < y_min) y_min = pts[i].y;
    if (pts[i].y > y_max) y_max = pts[i].y;
  }
  y_min = MAX(y_min, 0); y_max = MIN(y_max, CANVAS_H - 1);
  int *xs = malloc(sizeof(int) * count * 2);
  if (!xs) { canvas_draw_polygon_outline(doc, pts, count, outline); return; }
  for (int y = y_min; y <= y_max; y++) {
    int n = 0;
    for (int i = 0, j = count - 1; i < count; j = i++) {
      int yi = pts[i].y, yj = pts[j].y;
      if ((yi <= y && yj > y) || (yj <= y && yi > y)) {
        xs[n++] = pts[i].x + (y - yi) * (pts[j].x - pts[i].x) / (yj - yi);
      }
    }
    // Sort intersections
    for (int a = 0; a < n - 1; a++)
      for (int b = a + 1; b < n; b++)
        if (xs[a] > xs[b]) { int t = xs[a]; xs[a] = xs[b]; xs[b] = t; }
    for (int a = 0; a + 1 < n; a += 2)
      canvas_draw_line(doc, xs[a], y, xs[a+1], y, 0, fill);
  }
  free(xs);
  canvas_draw_polygon_outline(doc, pts, count, outline);
}

// Returns true if tool is a shape tool that uses rubber-band dragging
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

// Save pixel snapshot before starting a shape drag (no undo push yet)
void canvas_shape_begin(canvas_doc_t *doc, int cx, int cy) {
  if (!doc->shape_snapshot) {
    doc->shape_snapshot = malloc(CANVAS_H * CANVAS_W * 4);
  }
  if (doc->shape_snapshot) {
    memcpy(doc->shape_snapshot, doc->pixels, CANVAS_H * CANVAS_W * 4);
  }
  doc->shape_start.x = cx;
  doc->shape_start.y = cy;
}

// Restore snapshot and draw a preview of the current shape without pushing undo.
// shift_held constrains the shape (45° line, square, circle).
void canvas_shape_preview(canvas_doc_t *doc, int x0, int y0, int x1, int y1,
                          int tool, bool filled, rgba_t fg, rgba_t bg, bool shift_held) {
  // Restore snapshot
  if (doc->shape_snapshot) {
    memcpy(doc->pixels, doc->shape_snapshot, CANVAS_H * CANVAS_W * 4);
    doc->canvas_dirty = true;
  }
  if (shift_held) {
    int dx = x1 - x0, dy = y1 - y0;
    switch (tool) {
      case ID_TOOL_LINE: {
        // Snap to nearest 45° increment
        if (abs(dx) > abs(dy) * 2) { dy = 0; }
        else if (abs(dy) > abs(dx) * 2) { dx = 0; }
        else { int s = MAX(abs(dx), abs(dy)); dx = (dx<0?-s:s); dy = (dy<0?-s:s); }
        x1 = x0 + dx; y1 = y0 + dy;
        break;
      }
      case ID_TOOL_RECT:
      case ID_TOOL_ROUNDED_RECT: {
        // Make square: use shorter dimension
        int s = MIN(abs(dx), abs(dy));
        x1 = x0 + (dx < 0 ? -s : s);
        y1 = y0 + (dy < 0 ? -s : s);
        break;
      }
      case ID_TOOL_ELLIPSE: {
        // Make circle
        int s = MIN(abs(dx), abs(dy));
        x1 = x0 + (dx < 0 ? -s : s);
        y1 = y0 + (dy < 0 ? -s : s);
        break;
      }
    }
  }
  int lx = MIN(x0, x1), rx = MAX(x0, x1);
  int ty = MIN(y0, y1), by = MAX(y0, y1);
  int w = rx - lx + 1, h = by - ty + 1;
  int cx2 = (lx + rx) / 2, cy2 = (ty + by) / 2;
  int rxa = (rx - lx + 1) / 2, rya = (by - ty + 1) / 2;
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
      if (filled) canvas_draw_ellipse_filled(doc, cx2, cy2, rxa, rya, fg, bg);
      else        canvas_draw_ellipse_outline(doc, cx2, cy2, rxa, rya, fg);
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

// ============================================================
// Selection operations
// ============================================================

// Returns normalised selection bounds clamped to the canvas.
// Returns false when the clamped region is empty (no-op for callers).
static bool selection_bounds(const canvas_doc_t *doc,
                             int *x0, int *y0, int *x1, int *y1) {
  *x0 = MIN(doc->sel_start.x, doc->sel_end.x);
  *y0 = MIN(doc->sel_start.y, doc->sel_end.y);
  *x1 = MAX(doc->sel_start.x, doc->sel_end.x);
  *y1 = MAX(doc->sel_start.y, doc->sel_end.y);
  // Clamp to canvas bounds so callers are safe against out-of-range coords.
  if (*x0 < 0) *x0 = 0;
  if (*y0 < 0) *y0 = 0;
  if (*x1 >= CANVAS_W) *x1 = CANVAS_W - 1;
  if (*y1 >= CANVAS_H) *y1 = CANVAS_H - 1;
  return (*x0 <= *x1 && *y0 <= *y1);
}

// Copy the selected region into the app clipboard.
void canvas_copy_selection(canvas_doc_t *doc) {
  if (!doc || !doc->sel_active || !g_app) return;
  int x0, y0, x1, y1;
  if (!selection_bounds(doc, &x0, &y0, &x1, &y1)) return;
  int w = x1 - x0 + 1;
  int h = y1 - y0 + 1;
  uint8_t *buf = malloc((size_t)w * h * 4);
  if (!buf) return;
  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      rgba_t c = canvas_get_pixel(doc, x0 + col, y0 + row);
      uint8_t *p = buf + ((size_t)row * w + col) * 4;
      p[0]=c.r; p[1]=c.g; p[2]=c.b; p[3]=c.a;
    }
  }
  free(g_app->clipboard);
  g_app->clipboard   = buf;
  g_app->clipboard_w = w;
  g_app->clipboard_h = h;
}

// Fill the selected region with fill_color.
void canvas_clear_selection(canvas_doc_t *doc, rgba_t fill) {
  if (!doc || !doc->sel_active) return;
  int x0, y0, x1, y1;
  if (!selection_bounds(doc, &x0, &y0, &x1, &y1)) return;
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++)
      canvas_set_pixel(doc, x, y, fill);
}

// Copy selection to clipboard, then clear the selection region.
void canvas_cut_selection(canvas_doc_t *doc, rgba_t fill) {
  if (!doc) return;
  canvas_copy_selection(doc);
  canvas_clear_selection(doc, fill);
}

// Paste clipboard pixels at (0, 0), bypassing the selection mask.
// The pasted region becomes the new selection.
void canvas_paste_clipboard(canvas_doc_t *doc) {
  if (!doc || !g_app || !g_app->clipboard) return;
  doc_push_undo(doc);
  int w = g_app->clipboard_w;
  int h = g_app->clipboard_h;
  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      const uint8_t *p = g_app->clipboard + ((size_t)row * w + col) * 4;
      rgba_t c = {p[0], p[1], p[2], p[3]};
      canvas_set_pixel_direct(doc, col, row, c);
    }
  }
  // Select the pasted region (clamped to canvas bounds)
  doc->sel_active   = true;
  doc->sel_start    = (point_t){0, 0};
  int sel_x1 = w - 1;
  int sel_y1 = h - 1;
  if (sel_x1 >= CANVAS_W) sel_x1 = CANVAS_W - 1;
  if (sel_y1 >= CANVAS_H) sel_y1 = CANVAS_H - 1;
  doc->sel_end      = (point_t){sel_x1, sel_y1};
}

// Select the entire canvas.
void canvas_select_all(canvas_doc_t *doc) {
  if (!doc) return;
  doc->sel_active   = true;
  doc->sel_start    = (point_t){0, 0};
  doc->sel_end      = (point_t){CANVAS_W - 1, CANVAS_H - 1};
}

// Clear selection (no-op on pixels).
void canvas_deselect(canvas_doc_t *doc) {
  if (!doc) return;
  // Commit any in-progress move before deselecting.
  if (doc->sel_moving) canvas_commit_move(doc);
  doc->sel_active = false;
}

// Extract the current selection into a float buffer and clear that region.
// Enters "move mode": the caller should track float_pos deltas and call
// canvas_commit_move() when the drag ends.
void canvas_begin_move(canvas_doc_t *doc, rgba_t bg) {
  if (!doc || !doc->sel_active || doc->sel_moving) return;
  int x0, y0, x1, y1;
  if (!selection_bounds(doc, &x0, &y0, &x1, &y1)) return;
  int w = x1 - x0 + 1;
  int h = y1 - y0 + 1;
  uint8_t *buf = malloc((size_t)w * h * 4);
  if (!buf) return;
  // Extract pixels
  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      rgba_t c = canvas_get_pixel(doc, x0 + col, y0 + row);
      uint8_t *p = buf + ((size_t)row * w + col) * 4;
      p[0]=c.r; p[1]=c.g; p[2]=c.b; p[3]=c.a;
    }
  }
  // Clear the region from canvas
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++)
      canvas_set_pixel_direct(doc, x, y, bg);
  doc->float_pixels  = buf;
  doc->float_w       = w;
  doc->float_h       = h;
  doc->float_pos     = (point_t){x0, y0};
  doc->sel_moving    = true;
}

// Paste float_pixels back at float_pos, update selection bounds, end move.
void canvas_commit_move(canvas_doc_t *doc) {
  if (!doc || !doc->sel_moving) return;
  int dx = doc->float_pos.x;
  int dy = doc->float_pos.y;
  int w  = doc->float_w;
  int h  = doc->float_h;
  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      const uint8_t *p = doc->float_pixels + ((size_t)row * w + col) * 4;
      rgba_t c = {p[0], p[1], p[2], p[3]};
      canvas_set_pixel_direct(doc, dx + col, dy + row, c);
    }
  }
  // Update selection to the new position
  doc->sel_start  = (point_t){dx, dy};
  doc->sel_end    = (point_t){dx + w - 1, dy + h - 1};
  // Release float resources including the GL texture overlay.
  if (doc->float_tex) {
    glDeleteTextures(1, &doc->float_tex);
    doc->float_tex = 0;
  }
  free(doc->float_pixels);
  doc->float_pixels = NULL;
  doc->float_w      = 0;
  doc->float_h      = 0;
  doc->sel_moving   = false;
}

// ============================================================
// PNG I/O (libpng)
// ============================================================

static bool is_png(const char *path) {
  if (!path) return false;
  size_t n = strlen(path);
  if (n < 5) return false;
  const char *ext = path + n - 4;
  return (ext[0]=='.' &&
          (ext[1]=='p'||ext[1]=='P') &&
          (ext[2]=='n'||ext[2]=='N') &&
          (ext[3]=='g'||ext[3]=='G'));
}

bool png_load(const char *path, uint8_t *out_pixels);
bool png_save(const char *path, const uint8_t *pixels);

bool png_load(const char *path, uint8_t *out_pixels) {
  FILE *fp = fopen(path, "rb");
  if (!fp) return false;

  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png) { fclose(fp); return false; }

  png_infop info = png_create_info_struct(png);
  if (!info) { png_destroy_read_struct(&png, NULL, NULL); fclose(fp); return false; }

  if (setjmp(png_jmpbuf(png))) {
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    return false;
  }

  png_init_io(png, fp);
  png_read_info(png, info);

  int w = (int)png_get_image_width(png, info);
  int h = (int)png_get_image_height(png, info);
  png_byte ct  = png_get_color_type(png, info);
  png_byte bd  = png_get_bit_depth(png, info);

  if (bd == 16) png_set_strip_16(png);
  if (ct == PNG_COLOR_TYPE_PALETTE)       png_set_palette_to_rgb(png);
  if (ct == PNG_COLOR_TYPE_GRAY && bd<8)  png_set_expand_gray_1_2_4_to_8(png);
  if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
  if (ct == PNG_COLOR_TYPE_RGB  ||
      ct == PNG_COLOR_TYPE_GRAY ||
      ct == PNG_COLOR_TYPE_PALETTE) png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
  if (ct == PNG_COLOR_TYPE_GRAY || ct == PNG_COLOR_TYPE_GRAY_ALPHA)
    png_set_gray_to_rgb(png);
  png_read_update_info(png, info);

  png_bytep *rows = malloc(sizeof(png_bytep) * h);
  if (!rows) { png_destroy_read_struct(&png, &info, NULL); fclose(fp); return false; }

  png_size_t rowbytes = png_get_rowbytes(png, info);
  for (int r = 0; r < h; r++) {
    rows[r] = malloc(rowbytes);
    if (!rows[r]) {
      for (int i = 0; i < r; i++) free(rows[i]);
      free(rows);
      png_destroy_read_struct(&png, &info, NULL);
      fclose(fp);
      return false;
    }
  }

  png_read_image(png, rows);

  memset(out_pixels, 0xFF, CANVAS_H * CANVAS_W * 4);
  int copy_w = w < CANVAS_W ? w : CANVAS_W;
  int copy_h = h < CANVAS_H ? h : CANVAS_H;
  for (int row = 0; row < copy_h; row++)
    memcpy(out_pixels + row * CANVAS_W * 4, rows[row], (size_t)copy_w * 4);

  for (int r = 0; r < h; r++) free(rows[r]);
  free(rows);
  png_destroy_read_struct(&png, &info, NULL);
  fclose(fp);
  return true;
}

bool png_save(const char *path, const uint8_t *pixels) {
  FILE *fp = fopen(path, "wb");
  if (!fp) return false;

  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png) { fclose(fp); return false; }

  png_infop info = png_create_info_struct(png);
  if (!info) { png_destroy_write_struct(&png, NULL); fclose(fp); return false; }

  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    return false;
  }

  png_init_io(png, fp);
  png_set_IHDR(png, info, CANVAS_W, CANVAS_H, 8,
               PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);

  for (int r = 0; r < CANVAS_H; r++)
    png_write_row(png, (png_bytep)(pixels + r * CANVAS_W * 4));

  png_write_end(png, NULL);
  png_destroy_write_struct(&png, &info);
  fclose(fp);
  return true;
}

// ============================================================
// Canvas GL texture
// ============================================================

static void canvas_upload(canvas_doc_t *doc) {
  if (!doc->canvas_tex) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, CANVAS_W, CANVAS_H, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, doc->pixels);
    doc->canvas_tex = tex;
  } else if (doc->canvas_dirty) {
    glBindTexture(GL_TEXTURE_2D, doc->canvas_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, CANVAS_W, CANVAS_H,
                    GL_RGBA, GL_UNSIGNED_BYTE, doc->pixels);
  }
  doc->canvas_dirty = false;
}
