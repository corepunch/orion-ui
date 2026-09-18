// canvas/canvas_fill_gap.c — Gap-closing flood fill for the Fill tool.
//
// Implements the Gangnet/Van Thong pipeline in raster form: scan the drawing
// for likely gap points (dangling stroke endpoints and sharp local curvature
// maxima), synthesize the shortest invisible stitch across each candidate's
// stamp window, run the fill against the stitched barrier, then merge away
// tiny slivers the stitching created. Reached stitches receive the fill color
// without propagating across the barrier.

#include "imageeditor.h"

#define GAP_BAR_OPEN    0
#define GAP_BAR_INK     1
#define GAP_BAR_STITCH  2
#define GAP_BAR_FILLED  3
#define GAP_BAR_TEMP    4
#define GAP_BAR_REJECT  5
#define GAP_BAR_ACTIVE  6

typedef struct { int x, y; } gap_pt_t;

static bool gap_is_ink(const canvas_doc_t *doc, int x, int y, uint32_t target) {
  if (x < 0 || x >= doc->canvas_w || y < 0 || y >= doc->canvas_h) return true;
  if (!canvas_in_selection(doc, x, y)) return true;
  return canvas_get_pixel(doc, x, y) != target;
}

static int gap_ink_neighbors(const canvas_doc_t *doc, int x, int y,
                             uint32_t target, gap_pt_t *out) {
  int n = 0;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++) {
      if (!dx && !dy) continue;
      if (gap_is_ink(doc, x + dx, y + dy, target)) {
        if (out) out[n] = (gap_pt_t){dx, dy};
        n++;
      }
    }
  return n;
}

static bool gap_neighbors_one_sided(const gap_pt_t *nb, int n) {
  for (int i = 0; i < n; i++)
    for (int j = i + 1; j < n; j++)
      if (nb[i].x * nb[j].x + nb[i].y * nb[j].y < 0)
        return false;
  return n > 0;
}

int canvas_gap_detect_endpoints(const canvas_doc_t *doc, uint32_t target,
                                ipoint16_t *out, int max_out) {
  if (!doc || !out || max_out <= 0) return 0;
  int found = 0;
  gap_pt_t nb[8];
  for (int y = 0; y < doc->canvas_h; y++)
    for (int x = 0; x < doc->canvas_w; x++) {
      if (!canvas_in_selection(doc, x, y)) continue;
      if (canvas_get_pixel(doc, x, y) == target) continue;
      int n = gap_ink_neighbors(doc, x, y, target, nb);
      // Rounded thick caps have four neighbors in an open half-plane.
      // N==2 is a 1px corner and is handled by canvas_gap_detect_corners.
      if (n != 1 && n != 3 && n != 4) continue;
      if (n == 3 && !gap_neighbors_one_sided(nb, n)) continue;
      if (n == 4) {
        gap_pt_t direction = {0};
        for (int i = 0; i < n; i++) { direction.x += nb[i].x; direction.y += nb[i].y; }
        bool cap = true;
        for (int i = 0; i < n; i++)
          if (direction.x * nb[i].x + direction.y * nb[i].y <= 0) cap = false;
        if (!cap) continue;
      }
      if (found < max_out) out[found] = (ipoint16_t){x, y};
      found++;
    }
  return found;
}

int canvas_gap_detect_corners(const canvas_doc_t *doc, uint32_t target,
                              ipoint16_t *out, int max_out) {
  if (!doc || !out || max_out <= 0) return 0;
  int found = 0;
  gap_pt_t nb[8];
  for (int y = 0; y < doc->canvas_h; y++)
    for (int x = 0; x < doc->canvas_w; x++) {
      if (!canvas_in_selection(doc, x, y)) continue;
      if (canvas_get_pixel(doc, x, y) == target) continue;
      if (gap_ink_neighbors(doc, x, y, target, nb) != 2) continue;
      if (nb[0].x * nb[1].x + nb[0].y * nb[1].y < 0) continue;
      if (found < max_out) out[found] = (ipoint16_t){x, y};
      found++;
    }
  return found;
}

static int gap_draw_stitch(uint8_t *bar, int w, int h,
                           int x0, int y0, int x1, int y1) {
  int dx = abs(x1 - x0), dy = abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = dx - dy, converted = 0;
  while (true) {
    size_t i = (size_t)y0 * w + x0;
    if (bar[i] == GAP_BAR_OPEN) { bar[i] = GAP_BAR_STITCH; converted++; }
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 > -dy) { err -= dy; x0 += sx; }
    if (e2 <  dx) { err += dx; y0 += sy; }
  }
  return converted;
}

static int gap_segment_paper_count(const uint8_t *bar, int w,
                                   int x0, int y0, int x1, int y1) {
  int dx = abs(x1 - x0), dy = abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = dx - dy, paper = 0;
  while (true) {
    // Measure against the original drawing, including already stitched paper.
    uint8_t v = bar[(size_t)y0 * w + x0];
    if (v == GAP_BAR_OPEN || v == GAP_BAR_STITCH) paper++;
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 > -dy) { err -= dy; x0 += sx; }
    if (e2 <  dx) { err += dx; y0 += sy; }
  }
  return paper;
}

static bool gap_try_stitch(uint8_t *bar, int w, int h, int cx, int cy, int gap,
                            uint8_t *connected, gap_pt_t *queue) {
  int reach = gap + 2, side = 2 * reach + 1;
  memset(connected, 0, (size_t)side * side);
  size_t head = 0, tail = 0;
  queue[tail++] = (gap_pt_t){cx, cy};
  connected[reach * side + reach] = 1;
  // Components are local to the stamp: a distant connection must not hide a gap.
  while (head < tail) {
    gap_pt_t cur = queue[head++];
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++) {
        int x = cur.x + dx, y = cur.y + dy;
        int lx = x - cx + reach, ly = y - cy + reach;
        if (x < 0 || x >= w || y < 0 || y >= h || lx < 0 || lx >= side || ly < 0 || ly >= side) continue;
        size_t k = (size_t)ly * side + lx;
        uint8_t v = bar[(size_t)y * w + x];
        if (connected[k] || (v != GAP_BAR_INK && v != GAP_BAR_STITCH)) continue;
        connected[k] = 1;
        queue[tail++] = (gap_pt_t){x, y};
      }
  }
  int bx = -1, by = -1, best = 2 * reach * reach + 1;
  for (int dy = -reach; dy <= reach; dy++)
    for (int dx = -reach; dx <= reach; dx++) {
      if (connected[(dy + reach) * side + dx + reach]) continue;
      if (abs(dx) <= 1 && abs(dy) <= 1) continue;
      int d2 = dx * dx + dy * dy;
      if (d2 >= best) continue;
      int tx = cx + dx, ty = cy + dy;
      if (tx < 0 || tx >= w || ty < 0 || ty >= h) continue;
      if (bar[(size_t)ty * w + tx] != GAP_BAR_INK) continue;
      int paper = gap_segment_paper_count(bar, w, cx, cy, tx, ty);
      if (paper < 1 || paper > gap) continue;
      best = d2; bx = tx; by = ty;
    }
  if (bx < 0) return false;
  int converted = gap_draw_stitch(bar, w, h, cx, cy, bx, by);
  return converted > 0;
}

static void gap_plain_fill(canvas_doc_t *doc, int sx, int sy, uint32_t fill,
                           uint32_t target) {
  size_t capacity = (size_t)doc->canvas_w * (size_t)doc->canvas_h;
  if (capacity > 64 * 1024 * 1024) capacity = 64 * 1024 * 1024;
  gap_pt_t *queue = malloc(sizeof(gap_pt_t) * capacity);
  if (!queue) return;
  size_t head = 0, tail = 0;
  queue[tail++] = (gap_pt_t){sx, sy};
  canvas_set_pixel(doc, sx, sy, fill);
  while (head < tail) {
    gap_pt_t cur = queue[head++];
    int nx[4] = {cur.x+1, cur.x-1, cur.x,   cur.x};
    int ny[4] = {cur.y,   cur.y,   cur.y+1, cur.y-1};
    for (int i = 0; i < 4; i++) {
      if (canvas_in_bounds(doc, nx[i], ny[i]) &&
          canvas_in_selection(doc, nx[i], ny[i]) &&
          canvas_get_pixel(doc, nx[i], ny[i]) == target &&
          tail < capacity) {
        canvas_set_pixel(doc, nx[i], ny[i], fill);
        queue[tail++] = (gap_pt_t){nx[i], ny[i]};
      }
    }
  }
  free(queue);
}

static void gap_activate_stitches(uint8_t *bar, int w, int h, gap_pt_t *queue) {
  size_t head = 0, tail = 0;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      size_t k = (size_t)y * w + x;
      if (bar[k] != GAP_BAR_STITCH) continue;
      if ((x > 0 && bar[k-1] == GAP_BAR_FILLED) ||
          (x+1 < w && bar[k+1] == GAP_BAR_FILLED) ||
          (y > 0 && bar[k-w] == GAP_BAR_FILLED) ||
          (y+1 < h && bar[k+w] == GAP_BAR_FILLED)) {
        bar[k] = GAP_BAR_ACTIVE;
        queue[tail++] = (gap_pt_t){x, y};
      }
    }
  while (head < tail) {
    gap_pt_t cur = queue[head++];
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++) {
        int x = cur.x + dx, y = cur.y + dy;
        if (x < 0 || x >= w || y < 0 || y >= h) continue;
        size_t k = (size_t)y * w + x;
        if (bar[k] != GAP_BAR_STITCH) continue;
        bar[k] = GAP_BAR_ACTIVE;
        queue[tail++] = (gap_pt_t){x, y};
      }
  }
}

int canvas_flood_fill_with_gap(canvas_doc_t *doc, int sx, int sy,
                               uint32_t fill, int gap_px) {
  if (!doc || !canvas_in_bounds(doc, sx, sy)) return 0;
  if (!canvas_in_selection(doc, sx, sy)) return 0;
  uint32_t target = canvas_get_pixel(doc, sx, sy);
  if (target == fill) return 0;
  if (gap_px <= 0) { gap_plain_fill(doc, sx, sy, fill, target); return 0; }
  int max_gap = IE_FILL_GAP_MAX * MAX(1, g_bw_retina_scale);
  if (gap_px > max_gap) gap_px = max_gap;

  int w = doc->canvas_w, h = doc->canvas_h;
  size_t n = (size_t)w * (size_t)h;
  uint8_t *bar = malloc(n);
  gap_pt_t *queue = malloc(sizeof(gap_pt_t) * (n ? n : 1));
  if (!bar || !queue) {
    free(bar); free(queue);
    gap_plain_fill(doc, sx, sy, fill, target);
    return 0;
  }
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      bar[(size_t)y * w + x] =
          (!canvas_in_selection(doc, x, y) ||
           canvas_get_pixel(doc, x, y) != target) ? GAP_BAR_INK : GAP_BAR_OPEN;

  ipoint16_t first;
  int nend = canvas_gap_detect_endpoints(doc, target, &first, 1);
  int ncor = canvas_gap_detect_corners(doc, target, &first, 1);
  ipoint16_t *cand = malloc(sizeof(*cand) * (size_t)MAX(1, nend + ncor));
  if (!cand) {
    IE_TRACE("fill_gap candidate allocation failed count=%d", nend + ncor);
    free(bar); free(queue);
    return 0;
  }
  if (nend) canvas_gap_detect_endpoints(doc, target, cand, nend);
  if (ncor) canvas_gap_detect_corners(doc, target, cand + nend, ncor);
  int side = 2 * (gap_px + 2) + 1;
  uint8_t *connected = malloc((size_t)side * side);
  if (!connected) {
    IE_TRACE("fill_gap component allocation failed side=%d", side);
    free(cand); free(bar); free(queue);
    return 0;
  }
  int stitches = 0;
  for (int i = 0; i < nend + ncor; i++)
    if (gap_try_stitch(bar, w, h, cand[i].x, cand[i].y, gap_px, connected, queue)) stitches++;

  free(connected);
  free(cand);

  size_t head = 0, tail = 0;
  queue[tail++] = (gap_pt_t){sx, sy};
  bar[(size_t)sy * w + sx] = GAP_BAR_FILLED;
  canvas_set_pixel(doc, sx, sy, fill);
  int filled = 1;
  while (head < tail) {
    gap_pt_t cur = queue[head++];
    int nx[4] = {cur.x+1, cur.x-1, cur.x,   cur.x};
    int ny[4] = {cur.y,   cur.y,   cur.y+1, cur.y-1};
    for (int i = 0; i < 4; i++) {
      if (nx[i] < 0 || nx[i] >= w || ny[i] < 0 || ny[i] >= h) continue;
      if (bar[(size_t)ny[i] * w + nx[i]] != GAP_BAR_OPEN) continue;
      bar[(size_t)ny[i] * w + nx[i]] = GAP_BAR_FILLED;
      canvas_set_pixel(doc, nx[i], ny[i], fill);
      queue[tail++] = (gap_pt_t){nx[i], ny[i]};
      filled++;
    }
  }

  gap_activate_stitches(bar, w, h, queue);
  int sliver_max = gap_px * gap_px * 2, slivers = 0;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      if (bar[(size_t)y * w + x] != GAP_BAR_OPEN) continue;
      head = tail = 0;
      queue[tail++] = (gap_pt_t){x, y};
      bar[(size_t)y * w + x] = GAP_BAR_TEMP;
      int area = 1, touch = 0;
      while (head < tail) {
        gap_pt_t cur = queue[head++];
        int nx[4] = {cur.x+1, cur.x-1, cur.x,   cur.x};
        int ny[4] = {cur.y,   cur.y,   cur.y+1, cur.y-1};
        for (int i = 0; i < 4; i++) {
          if (nx[i] < 0 || nx[i] >= w || ny[i] < 0 || ny[i] >= h) continue;
          uint8_t v = bar[(size_t)ny[i] * w + nx[i]];
          if (v == GAP_BAR_ACTIVE) touch = 1;
          else if (v == GAP_BAR_OPEN) {
            bar[(size_t)ny[i] * w + nx[i]] = GAP_BAR_TEMP;
            queue[tail++] = (gap_pt_t){nx[i], ny[i]};
            area++;
          }
        }
      }
      bool merge = touch && area <= sliver_max;
      head = 0;
      while (head < tail) {
        gap_pt_t cur = queue[head++];
        size_t k = (size_t)cur.y * w + cur.x;
        if (bar[k] != GAP_BAR_TEMP) continue;
        bar[k] = merge ? GAP_BAR_FILLED : GAP_BAR_REJECT;
        if (merge) canvas_set_pixel(doc, cur.x, cur.y, fill);
      }
      if (merge) slivers++;
    }

  // Paint reached barriers, but never propagate through them into the other region.
  gap_activate_stitches(bar, w, h, queue);
  int bridge_pixels = 0;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++)
      if (bar[(size_t)y * w + x] == GAP_BAR_ACTIVE) {
        canvas_set_pixel(doc, x, y, fill);
        bridge_pixels++;
      }

  IE_TRACE("fill_gap at=(%d,%d) gap=%d endpoints=%d corners=%d stitches=%d filled=%d slivers=%d bridge_pixels=%d",
           sx, sy, gap_px, nend, ncor, stitches, filled, slivers, bridge_pixels);
  (void)filled; (void)slivers; (void)bridge_pixels;
  free(bar);
  free(queue);
  return stitches;
}
