// Canvas selection operations: rectangular, magic wand, move, expand/contract

#include "imageeditor.h"

// ============================================================
// Selection mask management
// ============================================================

void canvas_clear_selection_mask(canvas_doc_t *doc) {
  if (!doc) return;
  if (doc->sel.mask.tex) {
    R_DeleteTexture(doc->sel.mask.tex);
    doc->sel.mask.tex = 0;
  }
  free(doc->sel.mask.data);
  doc->sel.mask.data = NULL;
  doc->sel.mask.dirty = false;
  doc->sel.mask.offset = (ipoint16_t){0, 0};
}

static bool selection_mask_bounds_for(const canvas_doc_t *doc, const uint8_t *mask,
                                      int *out_x0, int *out_y0,
                                      int *out_x1, int *out_y1) {
  if (!doc || !mask) return false;
  int x0 = doc->canvas_w, y0 = doc->canvas_h, x1 = -1, y1 = -1;
  for (int y = 0; y < doc->canvas_h; y++) {
    for (int x = 0; x < doc->canvas_w; x++) {
      if (mask[(size_t)y * doc->canvas_w + x] == 255) continue;
      if (x < x0) x0 = x;
      if (y < y0) y0 = y;
      if (x > x1) x1 = x;
      if (y > y1) y1 = y;
    }
  }
  if (x1 < x0 || y1 < y0) return false;
  if (out_x0) *out_x0 = x0;
  if (out_y0) *out_y0 = y0;
  if (out_x1) *out_x1 = x1;
  if (out_y1) *out_y1 = y1;
  return true;
}

static bool canvas_apply_selection_mask(canvas_doc_t *doc, uint8_t *mask,
                                        bool add_to_selection) {
  if (!doc || !mask) {
    free(mask);
    return false;
  }

  if (add_to_selection && doc->sel.active) {
    if (doc->sel.mask.data) {
      for (size_t i = 0, count = (size_t)doc->canvas_w * doc->canvas_h; i < count; i++)
        mask[i] = MIN(mask[i], doc->sel.mask.data[i]);
    } else {
      for (int y = 0; y < doc->canvas_h; y++) {
        for (int x = 0; x < doc->canvas_w; x++) {
          if (canvas_in_selection(doc, x, y))
            mask[(size_t)y * doc->canvas_w + x] = 0;
        }
      }
    }
  }

  int x0, y0, x1, y1;
  if (!selection_mask_bounds_for(doc, mask, &x0, &y0, &x1, &y1)) {
    free(mask);
    doc->sel.active = false;
    canvas_clear_selection_mask(doc);
    return false;
  }

  canvas_clear_selection_mask(doc);
  doc->sel.mask.data = mask;
  doc->sel.mask.dirty = true;
  doc->sel.active = true;
  doc->sel.start = (ipoint16_t){x0, y0};
  doc->sel.end = (ipoint16_t){x1, y1};
  return true;
}

// ============================================================
// Rectangular selection
// ============================================================

static bool canvas_select_rect_ex(canvas_doc_t *doc, int x0, int y0, int x1, int y1,
                                  bool add_to_selection) {
  if (!doc) return false;
  int left = MIN(x0, x1);
  int top = MIN(y0, y1);
  int right = MAX(x0, x1);
  int bottom = MAX(y0, y1);

  left = MAX(0, left);
  top = MAX(0, top);
  right = MIN(doc->canvas_w - 1, right);
  bottom = MIN(doc->canvas_h - 1, bottom);

  if (left > right || top > bottom) {
    doc->sel.active = false;
    canvas_clear_selection_mask(doc);
    return false;
  }

  size_t count = (size_t)doc->canvas_w * doc->canvas_h;
  uint8_t *mask = malloc(count);
  if (!mask) return false;
  memset(mask, 255, count);

  for (int y = top; y <= bottom; y++) {
    memset(mask + (size_t)y * doc->canvas_w + left, 0,
           (size_t)(right - left + 1));
  }

  return canvas_apply_selection_mask(doc, mask, add_to_selection);
}

bool canvas_select_rect(canvas_doc_t *doc, int x0, int y0, int x1, int y1) {
  return canvas_select_rect_ex(doc, x0, y0, x1, y1, false);
}

bool canvas_select_rect_add(canvas_doc_t *doc, int x0, int y0, int x1, int y1) {
  return canvas_select_rect_ex(doc, x0, y0, x1, y1, true);
}

// ============================================================
// Magic wand selection
// ============================================================

static int color_spread_distance(uint32_t a, uint32_t b) {
  int dr = abs((int)COLOR_R(a) - (int)COLOR_R(b));
  int dg = abs((int)COLOR_G(a) - (int)COLOR_G(b));
  int db = abs((int)COLOR_B(a) - (int)COLOR_B(b));
  int da = abs((int)COLOR_A(a) - (int)COLOR_A(b));
  return MAX(MAX(dr, dg), MAX(db, da));
}

static bool canvas_magic_wand_select_ex(canvas_doc_t *doc, int sx, int sy,
                                        int spread, bool antialias,
                                        bool add_to_selection) {
  if (!doc || !canvas_in_bounds(doc, sx, sy)) return false;

  size_t count = (size_t)doc->canvas_w * (size_t)doc->canvas_h;
  if (count == 0 || count > 64 * 1024 * 1024) return false;

  uint8_t *mask = malloc(count);
  typedef struct { int x, y; } pt_t;
  pt_t *queue = malloc(sizeof(pt_t) * count);
  if (!mask || !queue) {
    free(mask);
    free(queue);
    return false;
  }
  memset(mask, 255, count);

  uint32_t target = canvas_get_pixel(doc, sx, sy);
  spread = CLAMP(spread, 0, 255);
  size_t head = 0, tail = 0;
  mask[(size_t)sy * doc->canvas_w + sx] = 0;
  queue[tail++] = (pt_t){sx, sy};
  int x0 = sx, y0 = sy, x1 = sx, y1 = sy;

  while (head < tail) {
    pt_t cur = queue[head++];
    int nx[4] = {cur.x + 1, cur.x - 1, cur.x,     cur.x};
    int ny[4] = {cur.y,     cur.y,     cur.y + 1, cur.y - 1};
    for (int i = 0; i < 4; i++) {
      if (!canvas_in_bounds(doc, nx[i], ny[i])) continue;
      size_t idx = (size_t)ny[i] * doc->canvas_w + nx[i];
      if (mask[idx] == 0) continue;
      if (color_spread_distance(canvas_get_pixel(doc, nx[i], ny[i]), target) > spread)
        continue;
      mask[idx] = 0;
      queue[tail++] = (pt_t){nx[i], ny[i]};
      if (nx[i] < x0) x0 = nx[i];
      if (ny[i] < y0) y0 = ny[i];
      if (nx[i] > x1) x1 = nx[i];
      if (ny[i] > y1) y1 = ny[i];
    }
  }

  if (antialias) {
    uint8_t *soft = malloc(count);
    if (soft) {
      memcpy(soft, mask, count);
      for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
          size_t idx = (size_t)y * doc->canvas_w + x;
          if (mask[idx] != 0) continue;
          for (int oy = -1; oy <= 1; oy++) {
            for (int ox = -1; ox <= 1; ox++) {
              int xx = x + ox, yy = y + oy;
              if (!canvas_in_bounds(doc, xx, yy)) continue;
              size_t nidx = (size_t)yy * doc->canvas_w + xx;
              int dist = color_spread_distance(canvas_get_pixel(doc, xx, yy), target);
              if (soft[nidx] != 0 && dist <= spread + 12) {
                soft[nidx] = (dist <= spread) ? 0 : 128;
                if (xx < x0) x0 = xx;
                if (yy < y0) y0 = yy;
                if (xx > x1) x1 = xx;
                if (yy > y1) y1 = yy;
              }
            }
          }
        }
      }
      free(mask);
      mask = soft;
    }
  }

  free(queue);
  return canvas_apply_selection_mask(doc, mask, add_to_selection);
}

bool canvas_magic_wand_select(canvas_doc_t *doc, int sx, int sy,
                              int spread, bool antialias) {
  return canvas_magic_wand_select_ex(doc, sx, sy, spread, antialias, false);
}

bool canvas_magic_wand_select_add(canvas_doc_t *doc, int sx, int sy,
                                  int spread, bool antialias) {
  return canvas_magic_wand_select_ex(doc, sx, sy, spread, antialias, true);
}

// ============================================================
// Selection operations
// ============================================================

// Returns normalised selection bounds clamped to the canvas.
// Returns false when the clamped region is empty (no-op for callers).
static bool selection_bounds(const canvas_doc_t *doc,
                             int *x0, int *y0, int *x1, int *y1) {
  if (doc->sel.mask.data) {
    bool any = false;
    *x0 = doc->canvas_w;
    *y0 = doc->canvas_h;
    *x1 = -1;
    *y1 = -1;
    for (int y = 0; y < doc->canvas_h; y++) {
      for (int x = 0; x < doc->canvas_w; x++) {
        if (doc->sel.mask.data[(size_t)y * doc->canvas_w + x] == 255) continue;
        int sx = x + doc->sel.mask.offset.x;
        int sy = y + doc->sel.mask.offset.y;
        if (!canvas_in_bounds(doc, sx, sy)) continue;
        if (sx < *x0) *x0 = sx;
        if (sy < *y0) *y0 = sy;
        if (sx > *x1) *x1 = sx;
        if (sy > *y1) *y1 = sy;
        any = true;
      }
    }
    return any;
  }
  *x0 = MIN(doc->sel.start.x, doc->sel.end.x);
  *y0 = MIN(doc->sel.start.y, doc->sel.end.y);
  *x1 = MAX(doc->sel.start.x, doc->sel.end.x);
  *y1 = MAX(doc->sel.start.y, doc->sel.end.y);
  // Clamp to canvas bounds so callers are safe against out-of-range coords.
  if (*x0 < 0) *x0 = 0;
  if (*y0 < 0) *y0 = 0;
  if (*x1 >= doc->canvas_w) *x1 = doc->canvas_w - 1;
  if (*y1 >= doc->canvas_h) *y1 = doc->canvas_h - 1;
  return (*x0 <= *x1 && *y0 <= *y1);
}

// Copy the selected region into the app clipboard.
void canvas_copy_selection(canvas_doc_t *doc) {
  if (!doc || !doc->sel.active || !g_app) return;
  int x0, y0, x1, y1;
  if (!selection_bounds(doc, &x0, &y0, &x1, &y1)) return;
  int w = x1 - x0 + 1;
  int h = y1 - y0 + 1;
  uint8_t *buf = malloc((size_t)w * h * 4);
  if (!buf) return;
  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      uint32_t c = canvas_get_pixel(doc, x0 + col, y0 + row);
      uint8_t *p = buf + ((size_t)row * w + col) * 4;
      if (canvas_in_selection(doc, x0 + col, y0 + row)) {
        p[0]=COLOR_R(c); p[1]=COLOR_G(c); p[2]=COLOR_B(c); p[3]=COLOR_A(c);
      } else {
        memset(p, 0, 4);
      }
    }
  }
  free(g_app->clipboard);
  g_app->clipboard   = buf;
  g_app->clipboard_size.w = w;
  g_app->clipboard_size.h = h;
}

// Fill the selected region with fill_color.
void canvas_clear_selection(canvas_doc_t *doc, uint32_t fill) {
  if (!doc || !doc->sel.active) return;
  int x0, y0, x1, y1;
  if (!selection_bounds(doc, &x0, &y0, &x1, &y1)) return;
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++)
      canvas_set_pixel(doc, x, y, fill);
}

// Copy selection to clipboard, then clear the selection region.
void canvas_cut_selection(canvas_doc_t *doc, uint32_t fill) {
  if (!doc) return;
  canvas_copy_selection(doc);
  canvas_clear_selection(doc, fill);
}

// Paste clipboard pixels at (0, 0), bypassing the selection mask.
// The pasted region becomes the new selection.
void canvas_paste_clipboard(canvas_doc_t *doc) {
  if (!doc || !g_app || !g_app->clipboard) return;
  int w = g_app->clipboard_size.w;
  int h = g_app->clipboard_size.h;
  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      const uint8_t *p = g_app->clipboard + ((size_t)row * w + col) * 4;
      canvas_set_pixel_direct(doc, col, row, MAKE_COLOR(p[0], p[1], p[2], p[3]));
    }
  }
  // Select the pasted region (clamped to canvas bounds)
  int sel_x1 = w - 1;
  int sel_y1 = h - 1;
  if (sel_x1 >= doc->canvas_w) sel_x1 = doc->canvas_w - 1;
  if (sel_y1 >= doc->canvas_h) sel_y1 = doc->canvas_h - 1;
  canvas_select_rect(doc, 0, 0, sel_x1, sel_y1);
}

// Select the entire canvas.
void canvas_select_all(canvas_doc_t *doc) {
  if (!doc) return;
  canvas_select_rect(doc, 0, 0, doc->canvas_w - 1, doc->canvas_h - 1);
}

// Clear selection (no-op on pixels).
void canvas_deselect(canvas_doc_t *doc) {
  if (!doc) return;
  // Commit any in-progress move before deselecting.
  if (doc->sel.move.active) canvas_commit_move(doc);
  doc->sel.active = false;
  canvas_clear_selection_mask(doc);
}

// ============================================================
// Selection modification (expand/contract)
// ============================================================

static bool selection_modify_mask_gpu(canvas_doc_t *doc, int amount, bool expand) {
  if (!g_ui_runtime.running || !doc || !doc->sel.active || amount <= 0)
    return false;

  size_t count = (size_t)doc->canvas_w * doc->canvas_h;
  uint8_t *src = doc->sel.mask.data;
  uint8_t *owned_src = NULL;
  if (!src) {
    int x0, y0, x1, y1;
    if (!selection_bounds(doc, &x0, &y0, &x1, &y1)) return false;
    owned_src = malloc(count);
    if (!owned_src) return false;
    memset(owned_src, 255, count);
    for (int y = y0; y <= y1; y++) {
      memset(owned_src + (size_t)y * doc->canvas_w + x0, 0,
             (size_t)(x1 - x0 + 1));
    }
    src = owned_src;
  }

  uint8_t *rgba = malloc(count * 4);
  if (!rgba) {
    free(owned_src);
    return false;
  }
  for (size_t i = 0; i < count; i++) {
    rgba[i * 4 + 0] = 255;
    rgba[i * 4 + 1] = 255;
    rgba[i * 4 + 2] = 255;
    rgba[i * 4 + 3] = 255 - src[i];
  }

  uint32_t src_tex = R_CreateTextureRGBA(doc->canvas_w, doc->canvas_h, rgba,
                                         R_FILTER_LINEAR, R_WRAP_CLAMP);
  free(rgba);
  free(owned_src);
  if (!src_tex) return false;

  uint32_t blur_tex = 0;
  if (!imageeditor_bake_texture_blur((int)src_tex, doc->canvas_w, doc->canvas_h,
                                     amount, &blur_tex)) {
    R_DeleteTexture(src_tex);
    return false;
  }
  R_DeleteTexture(src_tex);

  ui_render_effect_params_t p = {{0}};
  p.f[0] = expand ? 0.02f : 0.98f;
  p.f[1] = 0.08f;
  uint32_t thresh_tex = 0;
  if (!imageeditor_bake_texture_effect((int)blur_tex, doc->canvas_w, doc->canvas_h,
                                       IE_RENDER_EFFECT_ALPHA_THRESHOLD, &p, &thresh_tex)) {
    R_DeleteTexture(blur_tex);
    return false;
  }
  R_DeleteTexture(blur_tex);

  uint8_t *out = malloc(count * 4);
  if (!out) {
    R_DeleteTexture(thresh_tex);
    return false;
  }
  bool read_ok = read_texture_rgba((int)thresh_tex, doc->canvas_w, doc->canvas_h, out);
  R_DeleteTexture(thresh_tex);
  if (!read_ok) {
    free(out);
    return false;
  }

  uint8_t *dst = malloc(count);
  if (!dst) {
    free(out);
    return false;
  }
  memset(dst, 255, count);
  bool any = false;
  for (size_t i = 0; i < count; i++) {
    uint8_t selected = MAX(out[i * 4 + 3], out[i * 4]);
    if (selected > 0) {
      dst[i] = 255 - selected;
      any = true;
    }
  }
  free(out);

  canvas_clear_selection_mask(doc);
  if (!any) {
    free(dst);
    doc->sel.active = false;
    return true;
  }

  doc->sel.mask.data = dst;
  doc->sel.mask.dirty = true;
  int x0, y0, x1, y1;
  selection_bounds(doc, &x0, &y0, &x1, &y1);
  doc->sel.start = (ipoint16_t){x0, y0};
  doc->sel.end = (ipoint16_t){x1, y1};
  doc->sel.active = true;
  return true;
}

static bool selection_modify_mask(canvas_doc_t *doc, int amount, bool expand) {
  if (amount <= 16 && selection_modify_mask_gpu(doc, amount, expand))
    return true;

  size_t count = (size_t)doc->canvas_w * doc->canvas_h;
  uint8_t *src = doc->sel.mask.data;
  uint8_t *owned_src = NULL;
  uint8_t *dst = malloc(count);
  if (!dst) return false;
  memset(dst, 255, count);

  if (!src) {
    int x0, y0, x1, y1;
    if (!selection_bounds(doc, &x0, &y0, &x1, &y1)) {
      free(dst);
      return false;
    }
    owned_src = malloc(count);
    if (!owned_src) {
      free(dst);
      return false;
    }
    memset(owned_src, 255, count);
    for (int y = y0; y <= y1; y++) {
      memset(owned_src + (size_t)y * doc->canvas_w + x0, 0,
             (size_t)(x1 - x0 + 1));
    }
    src = owned_src;
  }

  bool any = false;
  for (int y = 0; y < doc->canvas_h; y++) {
    for (int x = 0; x < doc->canvas_w; x++) {
      uint8_t v = expand ? 255 : 0;
      for (int yy = y - amount; yy <= y + amount; yy++) {
        for (int xx = x - amount; xx <= x + amount; xx++) {
          uint8_t sample = 255;
          if (xx >= 0 && xx < doc->canvas_w &&
              yy >= 0 && yy < doc->canvas_h)
            sample = src[(size_t)yy * doc->canvas_w + xx];
          v = expand ? MIN(v, sample) : MAX(v, sample);
        }
      }
      if (v < 255) {
        dst[(size_t)y * doc->canvas_w + x] = v;
        any = true;
      }
    }
  }

  free(owned_src);
  canvas_clear_selection_mask(doc);
  if (!any) {
    free(dst);
    doc->sel.active = false;
    return true;
  }
  doc->sel.mask.data = dst;
  doc->sel.mask.dirty = true;
  int x0, y0, x1, y1;
  selection_bounds(doc, &x0, &y0, &x1, &y1);
  doc->sel.start = (ipoint16_t){x0, y0};
  doc->sel.end = (ipoint16_t){x1, y1};
  doc->sel.active = true;
  return true;
}

bool canvas_expand_selection(canvas_doc_t *doc, int amount) {
  if (!doc || !doc->sel.active || amount <= 0) return false;
  if (doc->sel.move.active) canvas_commit_move(doc);
  return selection_modify_mask(doc, amount, true);
}

bool canvas_contract_selection(canvas_doc_t *doc, int amount) {
  if (!doc || !doc->sel.active || amount <= 0) return false;
  if (doc->sel.move.active) canvas_commit_move(doc);
  return selection_modify_mask(doc, amount, false);
}

// ============================================================
// Crop operations
// ============================================================

// Crop the canvas to the active selection: copy the selected pixels, clear the
// entire canvas, then stamp the copied pixels at the top-left
// corner (0,0).  The selection is cleared afterwards.
void canvas_crop_to_selection(canvas_doc_t *doc) {
  if (!doc || !doc->sel.active) return;
  int x0, y0, x1, y1;
  if (!selection_bounds(doc, &x0, &y0, &x1, &y1)) return;
  int w = x1 - x0 + 1;
  int h = y1 - y0 + 1;
  uint8_t *buf = malloc((size_t)w * h * 4);
  if (!buf) return;
  // Copy the selected region into the temporary buffer.
  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      uint32_t c = canvas_get_pixel(doc, x0 + col, y0 + row);
      uint8_t *p = buf + ((size_t)row * w + col) * 4;
      p[0] = COLOR_R(c); p[1] = COLOR_G(c); p[2] = COLOR_B(c); p[3] = COLOR_A(c);
    }
  }
  // Clear the entire canvas.
  canvas_clear(doc);
  // Stamp the copied region at (0,0), clipping to the canvas dimensions.
  for (int row = 0; row < h && row < doc->canvas_h; row++) {
    for (int col = 0; col < w && col < doc->canvas_w; col++) {
      const uint8_t *p = buf + ((size_t)row * w + col) * 4;
      uint32_t c = MAKE_COLOR(p[0], p[1], p[2], p[3]);
      canvas_set_pixel_direct(doc, col, row, c);
    }
  }
  free(buf);
  doc->sel.active = false;
  canvas_clear_selection_mask(doc);
  doc->canvas_dirty = true;
}

// Crop or expand the canvas to the active selection rectangle.
// Unlike canvas_crop_to_selection(), the selection may extend outside the
// current canvas bounds — in that case the canvas grows to fit, with the new
// areas filled with transparent pixels.  If the selection is entirely inside the
// canvas the canvas shrinks (crop).  The existing pixels within the
// intersection of old and new bounds are preserved in place.
// Returns true on success, false if the state is invalid, the requested size
// exceeds the maximum, or memory allocation fails (canvas is unchanged).
bool canvas_crop_or_expand_to_selection(canvas_doc_t *doc) {
  if (!doc || !doc->sel.active) return false;
  int x0 = MIN(doc->sel.start.x, doc->sel.end.x);
  int y0 = MIN(doc->sel.start.y, doc->sel.end.y);
  int x1 = MAX(doc->sel.start.x, doc->sel.end.x);
  int y1 = MAX(doc->sel.start.y, doc->sel.end.y);
  int new_w = x1 - x0 + 1;
  int new_h = y1 - y0 + 1;

  if (new_w <= 0 || new_h <= 0) return false;
  if ((size_t)new_w > 16384 || (size_t)new_h > 16384) return false;

  if (!canvas_resize_frames(doc, x0, y0, new_w, new_h, false, IMAGE_RESIZE_NEAREST)) return false;

  for (int i = 0; i < doc->layer.count; i++) {
    if (!layer_crop_expand(doc->layer.stack[i], doc->canvas_w, doc->canvas_h,
                           x0, y0, new_w, new_h))
      return false;
  }

  free(doc->layer.composite_buf);
  doc->layer.composite_buf = malloc((size_t)new_w * new_h * 4);

  doc->canvas_w     = new_w;
  doc->canvas_h     = new_h;
  doc->pixels       = doc->layer.stack[doc->layer.active]->pixels;
  doc->canvas_dirty = true;
  doc->modified     = true;
  doc->sel.active   = false;
  canvas_clear_selection_mask(doc);
  return true;
}

// ============================================================
// Selection move operations
// ============================================================

// Extract the current selection into a float buffer and clear that region.
// Enters "move mode": the caller should track float_pos deltas and call
// canvas_commit_move() when the drag ends.
void canvas_begin_move(canvas_doc_t *doc, uint32_t bg) {
  if (!doc || !doc->sel.active || doc->sel.move.active) return;
  int x0, y0, x1, y1;
  if (!selection_bounds(doc, &x0, &y0, &x1, &y1)) return;
  int w = x1 - x0 + 1;
  int h = y1 - y0 + 1;
  uint8_t *buf = malloc((size_t)w * h * 4);
  uint8_t *mask = malloc((size_t)w * h);
  if (!buf || !mask) {
    free(buf);
    free(mask);
    return;
  }
  // Extract pixels
  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      uint32_t c = canvas_get_pixel(doc, x0 + col, y0 + row);
      uint8_t *p = buf + ((size_t)row * w + col) * 4;
      uint8_t *m = mask + (size_t)row * w + col;
      if (canvas_in_selection(doc, x0 + col, y0 + row)) {
        p[0]=COLOR_R(c); p[1]=COLOR_G(c); p[2]=COLOR_B(c); p[3]=COLOR_A(c);
        *m = 0;
      } else {
        memset(p, 0, 4);
        *m = 255;
      }
    }
  }
  // Clear the region from canvas
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++)
      if (canvas_in_selection(doc, x, y))
        canvas_set_pixel_direct(doc, x, y, bg);
  doc->sel.floating.pixels  = buf;
  doc->sel.floating.mask    = mask;
  doc->sel.floating.rect    = (irect16_t){x0, y0, w, h};
  doc->sel.move.active      = true;
  canvas_clear_selection_mask(doc);
}

// Paste float_pixels back at float_pos, update selection bounds, end move.
void canvas_commit_move(canvas_doc_t *doc) {
  if (!doc || !doc->sel.move.active) return;
  int dx = doc->sel.floating.rect.x;
  int dy = doc->sel.floating.rect.y;
  int w  = doc->sel.floating.rect.w;
  int h  = doc->sel.floating.rect.h;
  size_t count = (size_t)doc->canvas_w * doc->canvas_h;
  uint8_t *new_mask = malloc(count);
  if (new_mask) memset(new_mask, 255, count);
  bool any = false;
  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      size_t local_idx = (size_t)row * w + col;
      if (doc->sel.floating.mask && doc->sel.floating.mask[local_idx] != 0)
        continue;
      int x = dx + col;
      int y = dy + row;
      if (!canvas_in_bounds(doc, x, y))
        continue;
      const uint8_t *p = doc->sel.floating.pixels + local_idx * 4;
      canvas_set_pixel_direct(doc, x, y, MAKE_COLOR(p[0], p[1], p[2], p[3]));
      if (new_mask) {
        new_mask[(size_t)y * doc->canvas_w + x] = 0;
        any = true;
      }
    }
  }
  // Update selection to the new position
  canvas_clear_selection_mask(doc);
  if (new_mask && any) {
    doc->sel.mask.data = new_mask;
    doc->sel.mask.dirty = true;
    int x0, y0, x1, y1;
    selection_bounds(doc, &x0, &y0, &x1, &y1);
    doc->sel.start = (ipoint16_t){x0, y0};
    doc->sel.end = (ipoint16_t){x1, y1};
    doc->sel.active = true;
  } else {
    free(new_mask);
    doc->sel.active = false;
  }
  canvas_discard_move(doc);
}

void canvas_discard_move(canvas_doc_t *doc) {
  if (!doc) return;
  R_DeleteTexture(doc->sel.floating.tex);
  doc->sel.floating.tex = 0;
  free(doc->sel.floating.pixels);
  free(doc->sel.floating.mask);
  doc->sel.floating.pixels = NULL;
  doc->sel.floating.mask   = NULL;
  doc->sel.floating.rect.w      = 0;
  doc->sel.floating.rect.h      = 0;
  doc->sel.move.active   = false;
}

// ============================================================
// Selection mask translation
// ============================================================

bool canvas_translate_selection_mask(canvas_doc_t *doc, int dx, int dy) {
  if (!doc || !doc->sel.active) return false;
  dx += doc->sel.mask.offset.x;
  dy += doc->sel.mask.offset.y;
  doc->sel.mask.offset = (ipoint16_t){0, 0};
  if (dx == 0 && dy == 0) return true;

  size_t count = (size_t)doc->canvas_w * doc->canvas_h;
  uint8_t *src = doc->sel.mask.data;
  uint8_t *owned_src = NULL;
  if (!src) {
    int x0, y0, x1, y1;
    if (!selection_bounds(doc, &x0, &y0, &x1, &y1)) return false;
    owned_src = malloc(count);
    if (!owned_src) return false;
    memset(owned_src, 255, count);
    for (int y = y0; y <= y1; y++)
      memset(owned_src + (size_t)y * doc->canvas_w + x0, 0,
             (size_t)(x1 - x0 + 1));
    src = owned_src;
  }

  uint8_t *dst = malloc(count);
  if (!dst) {
    free(owned_src);
    return false;
  }
  memset(dst, 255, count);

  bool any = false;
  for (int y = 0; y < doc->canvas_h; y++) {
    for (int x = 0; x < doc->canvas_w; x++) {
      uint8_t v = src[(size_t)y * doc->canvas_w + x];
      if (v == 255) continue;
      int nx = x + dx;
      int ny = y + dy;
      if (!canvas_in_bounds(doc, nx, ny)) continue;
      uint8_t *d = dst + (size_t)ny * doc->canvas_w + nx;
      *d = MIN(*d, v);
      any = true;
    }
  }

  free(owned_src);
  canvas_clear_selection_mask(doc);
  if (!any) {
    free(dst);
    doc->sel.active = false;
    return true;
  }

  doc->sel.mask.data = dst;
  doc->sel.mask.dirty = true;
  int x0, y0, x1, y1;
  selection_bounds(doc, &x0, &y0, &x1, &y1);
  doc->sel.start = (ipoint16_t){x0, y0};
  doc->sel.end = (ipoint16_t){x1, y1};
  doc->sel.active = true;
  return true;
}

void canvas_set_selection_mask_offset(canvas_doc_t *doc, int dx, int dy) {
  if (!doc) return;
  doc->sel.mask.offset = (ipoint16_t){dx, dy};
  if (doc->sel.active && doc->sel.mask.data) {
    int x0, y0, x1, y1;
    if (selection_bounds(doc, &x0, &y0, &x1, &y1)) {
      doc->sel.start = (ipoint16_t){x0, y0};
      doc->sel.end = (ipoint16_t){x1, y1};
    }
  }
}

bool canvas_commit_selection_mask_offset(canvas_doc_t *doc) {
  if (!doc || !doc->sel.active) return false;
  int dx = doc->sel.mask.offset.x;
  int dy = doc->sel.mask.offset.y;
  if (dx == 0 && dy == 0) return true;
  return canvas_translate_selection_mask(doc, 0, 0);
}
