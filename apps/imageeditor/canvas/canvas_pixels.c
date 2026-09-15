// Canvas pixel operations: drawing primitives and shape tools

#include "imageeditor.h"

// ============================================================
// Pixel accessors
// ============================================================

#if IMAGEEDITOR_INDEXED
uint32_t canvas_get_pixel_rgba(const canvas_doc_t *doc, int x, int y) {
  if (!canvas_in_bounds(doc, x, y)) return MAKE_COLOR(0,0,0,0);
  uint8_t pidx = doc->pixels[(size_t)y * doc->canvas_w + x];
  if (pidx == (uint8_t)doc->ipal.transparent) return MAKE_COLOR(0,0,0,0);
  return doc->ipal.entries[pidx];
}
#endif

uint32_t canvas_get_pixel(const canvas_doc_t *doc, int x, int y) {
  if (!canvas_in_bounds(doc, x, y)) return MAKE_COLOR(0,0,0,0);
#if IMAGEEDITOR_INDEXED
  return canvas_get_pixel_rgba(doc, x, y);
#else
  const uint8_t *p = doc->pixels + ((size_t)y * doc->canvas_w + x) * 4;
  return MAKE_COLOR(p[0],p[1],p[2],p[3]);
#endif
}

// ============================================================
// Pixel drawing primitives
// ============================================================

// Write a pixel directly (bypasses selection mask – used for paste/move commit).
static void canvas_set_pixel_direct(canvas_doc_t *doc, int x, int y, uint32_t c) {
  if (!canvas_in_bounds(doc, x, y)) return;
#if IMAGEEDITOR_INDEXED
  doc->pixels[(size_t)y * doc->canvas_w + x] =
      (uint8_t)canvas_nearest_palette_index(doc, c);
#else
  uint8_t *p = doc->pixels + ((size_t)y * doc->canvas_w + x) * 4;
  p[0]=COLOR_R(c); p[1]=COLOR_G(c); p[2]=COLOR_B(c); p[3]=COLOR_A(c);
#endif
  doc->canvas_dirty = true;
  doc->modified     = true;
}

void canvas_set_pixel(canvas_doc_t *doc, int x, int y, uint32_t c) {
  if (!canvas_in_bounds(doc, x, y)) return;
  if (!canvas_in_selection(doc, x, y)) return;

#if IMAGEEDITOR_INDEXED
  doc->pixels[(size_t)y * doc->canvas_w + x] =
      (uint8_t)canvas_nearest_palette_index(doc, c);
#else
  uint8_t *p = doc->pixels + ((size_t)y * doc->canvas_w + x) * 4;
  if (doc->layer.editing_mask) {
    // Mask edits affect only alpha, while the RGB content stays intact.
    uint8_t gray = (uint8_t)((COLOR_R(c) * 77 + COLOR_G(c) * 150 + COLOR_B(c) * 29) >> 8);
    p[3] = gray;
  } else {
    p[0]=COLOR_R(c); p[1]=COLOR_G(c); p[2]=COLOR_B(c); p[3]=COLOR_A(c);
  }
#endif
  doc->canvas_dirty = true;
  doc->modified     = true;
}

// Diameter scales with density; positions remain in backing pixels.
void canvas_draw_pen(canvas_doc_t *doc, int x, int y, uint32_t c) {
  int diameter = MAX(1, g_bw_retina_scale);
  int offset = (diameter - 1) / 2;
  for (int dy = 0; dy < diameter; dy++) {
    for (int dx = 0; dx < diameter; dx++) {
      int cx = 2 * dx + 1 - diameter, cy = 2 * dy + 1 - diameter;
      if (cx * cx + cy * cy <= diameter * diameter)
        canvas_set_pixel(doc, x + dx - offset, y + dy - offset, c);
    }
  }
}

void canvas_draw_pen_line(canvas_doc_t *doc, int x0, int y0, int x1, int y1,
                          uint32_t c) {
  int dx = abs(x1 - x0), dy = abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;
  while (true) {
    canvas_draw_pen(doc, x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 > -dy) { err -= dy; x0 += sx; }
    if (e2 <  dx) { err += dx; y0 += sy; }
  }
}

void canvas_clear(canvas_doc_t *doc) {
  memset(doc->pixels, 0x00, (size_t)doc->canvas_w * doc->canvas_h * DOC_BPP);
  doc->canvas_dirty = true;
  doc->modified     = false;
}

void canvas_draw_circle(canvas_doc_t *doc, int cx, int cy, int r, uint32_t c) {
  for (int dy = -r; dy <= r; dy++)
    for (int dx = -r; dx <= r; dx++)
      if (dx*dx + dy*dy <= r*r)
        canvas_set_pixel(doc, cx+dx, cy+dy, c);
}

void canvas_draw_line(canvas_doc_t *doc, int x0, int y0, int x1, int y1,
                      int radius, uint32_t c) {
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

void canvas_flood_fill(canvas_doc_t *doc, int sx, int sy, uint32_t fill) {
  if (!canvas_in_selection(doc, sx, sy)) return;
  uint32_t target = canvas_get_pixel(doc, sx, sy);
  if (target == fill) return;

  typedef struct { int x, y; } pt_t;
  // Use size_t arithmetic to avoid overflow for large canvases.
  size_t capacity = (size_t)doc->canvas_w * (size_t)doc->canvas_h;
  // Sanity-cap the queue at 64 M entries (~512 MB) to avoid OOM on huge images.
  if (capacity > 64 * 1024 * 1024) capacity = 64 * 1024 * 1024;
  pt_t *queue = malloc(sizeof(pt_t) * capacity);
  if (!queue) return;

  size_t head = 0, tail = 0;
  queue[tail++] = (pt_t){sx, sy};
  canvas_set_pixel(doc, sx, sy, fill);

  while (head < tail) {
    pt_t cur = queue[head++];
    int nx[4] = {cur.x+1, cur.x-1, cur.x,   cur.x};
    int ny[4] = {cur.y,   cur.y,   cur.y+1, cur.y-1};
    for (int i = 0; i < 4; i++) {
      if (canvas_in_bounds(doc, nx[i], ny[i]) &&
          canvas_in_selection(doc, nx[i], ny[i]) &&
          canvas_get_pixel(doc, nx[i], ny[i]) == target &&
          tail < capacity) {
        canvas_set_pixel(doc, nx[i], ny[i], fill);
        queue[tail++] = (pt_t){nx[i], ny[i]};
      }
    }
  }
  free(queue);
}

// Airbrush/spray: scatter random pixels within radius around (cx, cy).
// Approximately 20 dots per call using Cartesian rejection sampling
// (naturally higher density toward the center, mimicking a real airbrush).
void canvas_spray(canvas_doc_t *doc, int cx, int cy, int radius, uint32_t c) {
  int r2 = radius * radius;
  for (int i = 0; i < 20; i++) {
    int dx = (rand() % (2 * radius + 1)) - radius;
    int dy = (rand() % (2 * radius + 1)) - radius;
    if (dx * dx + dy * dy <= r2)
      canvas_set_pixel(doc, cx + dx, cy + dy, c);
  }
}

// ============================================================
// Shape drawing functions
// ============================================================

void canvas_draw_rect_outline(canvas_doc_t *doc, int x, int y, int w, int h, uint32_t c) {
  if (w <= 0 || h <= 0) return;
  canvas_draw_line(doc, x,     y,     x+w-1, y,     0, c);
  canvas_draw_line(doc, x,     y+h-1, x+w-1, y+h-1, 0, c);
  canvas_draw_line(doc, x,     y,     x,     y+h-1, 0, c);
  canvas_draw_line(doc, x+w-1, y,     x+w-1, y+h-1, 0, c);
}

void canvas_draw_rect_filled(canvas_doc_t *doc, int x, int y, int w, int h, uint32_t outline, uint32_t fill) {
  if (w <= 0 || h <= 0) return;
  for (int dy = 1; dy < h - 1; dy++)
    canvas_draw_line(doc, x+1, y+dy, x+w-2, y+dy, 0, fill);
  canvas_draw_rect_outline(doc, x, y, w, h, outline);
}

// Midpoint ellipse algorithm (Bresenham's)
void canvas_draw_ellipse_outline(canvas_doc_t *doc, int cx, int cy, int rx, int ry, uint32_t c) {
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

void canvas_draw_ellipse_filled(canvas_doc_t *doc, int cx, int cy, int rx, int ry, uint32_t outline, uint32_t fill) {
  if (rx <= 0 || ry <= 0) return;
  double rx2 = (double)rx * (double)rx;
  double ry2 = (double)ry * (double)ry;
  for (int py = cy - ry; py <= cy + ry; py++) {
    if (!canvas_in_bounds(doc, cx, py)) continue;
    double dy = (double)(py - cy);
    double t = 1.0 - (dy * dy) / ry2;
    if (t <= 0.0) continue;
    int dx = (int)(sqrt(rx2 * t) + 0.5);
    canvas_draw_line(doc, cx - dx + 1, py, cx + dx - 1, py, 0, fill);
  }
  canvas_draw_ellipse_outline(doc, cx, cy, rx, ry, outline);
}

// Rounded rectangle using arc + straight edges
void canvas_draw_rounded_rect_outline(canvas_doc_t *doc, int x, int y, int w, int h, int r, uint32_t c) {
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

void canvas_draw_rounded_rect_filled(canvas_doc_t *doc, int x, int y, int w, int h, int r, uint32_t outline, uint32_t fill) {
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
void canvas_draw_polygon_outline(canvas_doc_t *doc, const ipoint16_t *pts, int count, uint32_t c) {
  if (count < 2) return;
  for (int i = 0; i < count - 1; i++)
    canvas_draw_line(doc, pts[i].x, pts[i].y, pts[i+1].x, pts[i+1].y, 0, c);
  canvas_draw_line(doc, pts[count-1].x, pts[count-1].y, pts[0].x, pts[0].y, 0, c);
}

// Scanline fill for a closed polygon using the ray-casting / edge table approach
void canvas_draw_polygon_filled(canvas_doc_t *doc, const ipoint16_t *pts, int count, uint32_t outline, uint32_t fill) {
  if (count < 3) { canvas_draw_polygon_outline(doc, pts, count, outline); return; }
  // Find bounding box
  int y_min = pts[0].y, y_max = pts[0].y;
  for (int i = 1; i < count; i++) {
    if (pts[i].y < y_min) y_min = pts[i].y;
    if (pts[i].y > y_max) y_max = pts[i].y;
  }
  y_min = MAX(y_min, 0); y_max = MIN(y_max, doc->canvas_h - 1);
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

// Shape geometry and scan conversion use backing-pixel coordinates.
static int shape_scale(void) { return MAX(1, g_bw_retina_scale); }
static void shape_line(canvas_doc_t *doc, int x0, int y0, int x1, int y1, uint32_t c) {
  canvas_draw_pen_line(doc, x0, y0, x1, y1, c);
}
static void shape_pixel(canvas_doc_t *doc, int x, int y, uint32_t c) {
  canvas_draw_pen(doc, x, y, c);
}

void canvas_draw_rect_scaled(canvas_doc_t *doc, int x, int y, int w, int h,
                             bool filled, uint32_t outline, uint32_t fill) {
  if (shape_scale() == 1) {
    if (filled) canvas_draw_rect_filled(doc, x, y, w, h, outline, fill);
    else        canvas_draw_rect_outline(doc, x, y, w, h, outline);
    return;
  }
  if (w <= 0 || h <= 0) return;
  if (filled) canvas_draw_rect_filled(doc, x, y, w, h, fill, fill);
  shape_line(doc, x, y, x + w - 1, y, outline);
  shape_line(doc, x, y + h - 1, x + w - 1, y + h - 1, outline);
  shape_line(doc, x, y, x, y + h - 1, outline);
  shape_line(doc, x + w - 1, y, x + w - 1, y + h - 1, outline);
}

void canvas_draw_ellipse_scaled(canvas_doc_t *doc, int cx, int cy, int rx, int ry,
                                bool filled, uint32_t outline, uint32_t fill) {
  if (shape_scale() == 1) {
    if (filled) canvas_draw_ellipse_filled(doc, cx, cy, rx, ry, outline, fill);
    else        canvas_draw_ellipse_outline(doc, cx, cy, rx, ry, outline);
    return;
  }
  if (rx <= 0 || ry <= 0) return;
  if (filled) canvas_draw_ellipse_filled(doc, cx, cy, rx, ry, fill, fill);
  long rx2 = (long)rx * rx, ry2 = (long)ry * ry;
  long x = 0, y = ry, dx = 2 * ry2 * x, dy = 2 * rx2 * y;
  long p = (long)(ry2 - rx2 * ry + 0.25f * rx2);
  while (dx < dy) {
    shape_pixel(doc, cx + x, cy + y, outline); shape_pixel(doc, cx - x, cy + y, outline);
    shape_pixel(doc, cx + x, cy - y, outline); shape_pixel(doc, cx - x, cy - y, outline);
    x++; dx += 2 * ry2;
    if (p < 0) p += ry2 + dx;
    else { y--; dy -= 2 * rx2; p += ry2 + dx - dy; }
  }
  p = (long)(ry2 * (x + 0.5f) * (x + 0.5f) + rx2 * (y - 1) * (y - 1) - rx2 * ry2);
  while (y >= 0) {
    shape_pixel(doc, cx + x, cy + y, outline); shape_pixel(doc, cx - x, cy + y, outline);
    shape_pixel(doc, cx + x, cy - y, outline); shape_pixel(doc, cx - x, cy - y, outline);
    y--; dy -= 2 * rx2;
    if (p > 0) p += rx2 - dy;
    else { x++; dx += 2 * ry2; p += rx2 - dy + dx; }
  }
}

void canvas_draw_rounded_rect_scaled(canvas_doc_t *doc, int x, int y, int w, int h, int r,
                                     bool filled, uint32_t outline, uint32_t fill) {
  if (shape_scale() == 1) {
    if (filled) canvas_draw_rounded_rect_filled(doc, x, y, w, h, r, outline, fill);
    else        canvas_draw_rounded_rect_outline(doc, x, y, w, h, r, outline);
    return;
  }
  if (w <= 0 || h <= 0) return;
  r = MIN(MAX(r, 0), MIN(w / 2, h / 2));
  if (filled) canvas_draw_rounded_rect_filled(doc, x, y, w, h, r, fill, fill);
  shape_line(doc, x + r, y, x + w - r - 1, y, outline);
  shape_line(doc, x + r, y + h - 1, x + w - r - 1, y + h - 1, outline);
  shape_line(doc, x, y + r, x, y + h - r - 1, outline);
  shape_line(doc, x + w - 1, y + r, x + w - 1, y + h - r - 1, outline);
  int px = 0, py = r, d = 3 - 2 * r;
  while (px <= py) {
    shape_pixel(doc, x + r - px, y + r - py, outline); shape_pixel(doc, x + w - r + px - 1, y + r - py, outline);
    shape_pixel(doc, x + r - py, y + r - px, outline); shape_pixel(doc, x + w - r + py - 1, y + r - px, outline);
    shape_pixel(doc, x + r - px, y + h - r + py - 1, outline); shape_pixel(doc, x + w - r + px - 1, y + h - r + py - 1, outline);
    shape_pixel(doc, x + r - py, y + h - r + px - 1, outline); shape_pixel(doc, x + w - r + py - 1, y + h - r + px - 1, outline);
    if (d < 0) d += 4 * px + 6; else { d += 4 * (px - py) + 10; py--; }
    px++;
  }
}

void canvas_draw_polygon_scaled(canvas_doc_t *doc, const ipoint16_t *pts, int count,
                                bool filled, uint32_t outline, uint32_t fill) {
  if (shape_scale() == 1) {
    if (filled) canvas_draw_polygon_filled(doc, pts, count, outline, fill);
    else        canvas_draw_polygon_outline(doc, pts, count, outline);
    return;
  }
  if (count < 3) { for (int i = 0; i + 1 < count; i++) shape_line(doc, pts[i].x, pts[i].y, pts[i+1].x, pts[i+1].y, outline); return; }
  if (filled) {
    int y_min = pts[0].y, y_max = pts[0].y;
    for (int i = 1; i < count; i++) { y_min = MIN(y_min, pts[i].y); y_max = MAX(y_max, pts[i].y); }
    int *xs = malloc(sizeof(int) * count * 2);
    if (xs) {
      for (int y = MAX(y_min, 0); y <= MIN(y_max, doc->canvas_h - 1); y++) {
        int n = 0;
        for (int i = 0, j = count - 1; i < count; j = i++) {
          if ((pts[i].y <= y && pts[j].y > y) || (pts[j].y <= y && pts[i].y > y))
            xs[n++] = pts[i].x + (y - pts[i].y) * (pts[j].x - pts[i].x) / (pts[j].y - pts[i].y);
        }
        for (int a = 0; a < n - 1; a++) for (int b = a + 1; b < n; b++) if (xs[a] > xs[b]) { int t = xs[a]; xs[a] = xs[b]; xs[b] = t; }
        for (int a = 0; a + 1 < n; a += 2) canvas_draw_line(doc, xs[a], y, xs[a+1], y, 0, fill);
      }
      free(xs);
    }
  }
  for (int i = 0; i < count - 1; i++) shape_line(doc, pts[i].x, pts[i].y, pts[i+1].x, pts[i+1].y, outline);
  shape_line(doc, pts[count-1].x, pts[count-1].y, pts[0].x, pts[0].y, outline);
}
