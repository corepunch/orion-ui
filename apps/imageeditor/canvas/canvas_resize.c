// Canvas image operations: flip, invert, resize

#include "imageeditor.h"

// ============================================================
// Image operations: flip, invert
// ============================================================

// Flip canvas pixels horizontally (mirror left-right).
void canvas_flip_h(canvas_doc_t *doc) {
  if (!doc) return;
  int w = doc->canvas_w, h = doc->canvas_h;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w / 2; x++) {
      uint8_t *l = doc->pixels + ((size_t)y * w + x) * DOC_BPP;
      uint8_t *r = doc->pixels + ((size_t)y * w + (w - 1 - x)) * DOC_BPP;
      uint8_t tmp[4];
      memcpy(tmp, l, DOC_BPP);
      memcpy(l, r, DOC_BPP);
      memcpy(r, tmp, DOC_BPP);
    }
  }
  doc->canvas_dirty = true;
  doc->modified     = true;
}

// Flip canvas pixels vertically (mirror top-bottom).
void canvas_flip_v(canvas_doc_t *doc) {
  if (!doc) return;
  int w = doc->canvas_w, h = doc->canvas_h;
  size_t row_bytes = (size_t)w * DOC_BPP;
  uint8_t *tmp = malloc(row_bytes);
  if (!tmp) return;
  for (int y = 0; y < h / 2; y++) {
    uint8_t *top = doc->pixels + (size_t)y * row_bytes;
    uint8_t *bot = doc->pixels + (size_t)(h - 1 - y) * row_bytes;
    memcpy(tmp, top, row_bytes);
    memcpy(top, bot, row_bytes);
    memcpy(bot, tmp, row_bytes);
  }
  free(tmp);
  doc->canvas_dirty = true;
  doc->modified     = true;
}

// Invert all pixel colors (complement R, G, B; leave alpha unchanged).
void canvas_invert_colors(canvas_doc_t *doc) {
  if (!doc) return;
#if IMAGEEDITOR_INDEXED
  // Invert by remapping palette entries — leave pixel indices unchanged.
  for (int i = 0; i < doc->ipal.count && i < 256; i++) {
    if (i == doc->ipal.transparent) continue;
    uint32_t c = doc->ipal.entries[i];
    doc->ipal.entries[i] = MAKE_COLOR((uint8_t)(255 - COLOR_R(c)),
                                      (uint8_t)(255 - COLOR_G(c)),
                                      (uint8_t)(255 - COLOR_B(c)),
                                      COLOR_A(c));
  }
#else
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;
  for (size_t i = 0; i < n; i++) {
    uint8_t *p = doc->pixels + i * 4;
    p[0] = (uint8_t)(255 - p[0]);
    p[1] = (uint8_t)(255 - p[1]);
    p[2] = (uint8_t)(255 - p[2]);
    // alpha unchanged
  }
#endif
  doc->canvas_dirty = true;
  doc->modified     = true;
}

// ============================================================
// Image resize operations
// ============================================================

static inline uint8_t clamp_u8_float(float v) {
  if (v <= 0.0f) return 0;
  if (v >= 255.0f) return 255;
  return (uint8_t)(v + 0.5f);
}

#if !IMAGEEDITOR_INDEXED
static void sample_layer_nearest(const layer_t *lay, int old_w, int old_h,
                                 float sx, float sy, uint8_t out[4]) {
  int ix = (int)floorf(sx + 0.5f);
  int iy = (int)floorf(sy + 0.5f);
  ix = MAX(0, MIN(old_w - 1, ix));
  iy = MAX(0, MIN(old_h - 1, iy));
  const uint8_t *p = lay->pixels + ((size_t)iy * old_w + ix) * 4;
  memcpy(out, p, 4);
}

static void sample_layer_bilinear(const layer_t *lay, int old_w, int old_h,
                                  float sx, float sy, uint8_t out[4]) {
  int x0 = (int)floorf(sx);
  int y0 = (int)floorf(sy);
  float tx = sx - (float)x0;
  float ty = sy - (float)y0;
  int x1 = x0 + 1;
  int y1 = y0 + 1;
  x0 = MAX(0, MIN(old_w - 1, x0));
  y0 = MAX(0, MIN(old_h - 1, y0));
  x1 = MAX(0, MIN(old_w - 1, x1));
  y1 = MAX(0, MIN(old_h - 1, y1));

  float acc_r = 0.0f, acc_g = 0.0f, acc_b = 0.0f, acc_a = 0.0f;
  const int xs[2] = { x0, x1 };
  const int ys[2] = { y0, y1 };
  const float wx[2] = { 1.0f - tx, tx };
  const float wy[2] = { 1.0f - ty, ty };
  for (int yy = 0; yy < 2; yy++) {
    for (int xx = 0; xx < 2; xx++) {
      float w = wx[xx] * wy[yy];
      const uint8_t *p = lay->pixels + ((size_t)ys[yy] * old_w + xs[xx]) * 4;
      float a = (float)p[3];
      acc_r += (float)p[0] * a * w;
      acc_g += (float)p[1] * a * w;
      acc_b += (float)p[2] * a * w;
      acc_a += a * w;
    }
  }

  if (acc_a > 0.0f) {
    out[0] = clamp_u8_float(acc_r / acc_a);
    out[1] = clamp_u8_float(acc_g / acc_a);
    out[2] = clamp_u8_float(acc_b / acc_a);
  } else {
    out[0] = out[1] = out[2] = 0;
  }
  out[3] = clamp_u8_float(acc_a);
}
#endif

static uint8_t *layer_resample_pixels(const layer_t *lay, int old_w, int old_h,
                                      int new_w, int new_h,
                                      image_resize_filter_t filter) {
#if IMAGEEDITOR_INDEXED
  // Indexed mode: use nearest-neighbor only (palette indices can't be interpolated).
  uint8_t *buf = malloc((size_t)new_w * new_h);
  if (!buf) return NULL;
  float sx_scale = (float)old_w / (float)new_w;
  float sy_scale = (float)old_h / (float)new_h;
  for (int y = 0; y < new_h; y++) {
    for (int x = 0; x < new_w; x++) {
      int sx = (int)(((float)x + 0.5f) * sx_scale);
      int sy = (int)(((float)y + 0.5f) * sy_scale);
      sx = MAX(0, MIN(old_w - 1, sx));
      sy = MAX(0, MIN(old_h - 1, sy));
      buf[(size_t)y * new_w + x] = lay->pixels[(size_t)sy * old_w + sx];
    }
  }
  return buf;
#else
  uint8_t *buf = malloc((size_t)new_w * new_h * 4);
  if (!buf) return NULL;
  float sx_scale = (float)old_w / (float)new_w;
  float sy_scale = (float)old_h / (float)new_h;
  for (int y = 0; y < new_h; y++) {
    for (int x = 0; x < new_w; x++) {
      float sx = ((float)x + 0.5f) * sx_scale - 0.5f;
      float sy = ((float)y + 0.5f) * sy_scale - 0.5f;
      uint8_t *dst = buf + ((size_t)y * new_w + x) * 4;
      if (filter == IMAGE_RESIZE_NEAREST)
        sample_layer_nearest(lay, old_w, old_h, sx, sy, dst);
      else
        sample_layer_bilinear(lay, old_w, old_h, sx, sy, dst);
    }
  }
  return buf;
#endif
}

static void layer_replace_pixels(layer_t *lay, uint8_t *pixels) {
  if (!lay || !pixels) return;
  free(lay->pixels);
  lay->pixels = pixels;
  if (lay->tex) {
    R_DeleteTexture(lay->tex);
    lay->tex = 0;
  }
  lay->preview.active = false;
}

// Resize stored frames alongside the working layers, inside the same command.
bool canvas_resize_frames(canvas_doc_t *doc, int x, int y, int w, int h,
                           bool resample, image_resize_filter_t filter) {
  if (!doc->anim) return true;
  for (int i = 0; i < doc->anim->frame_count; i++) {
    if (i == doc->anim->active_frame) continue;
    anim_frame_t *frame = doc->anim->frames[i];
    if (pencil_has_layers(doc) && frame->cels) {
      size_t old_n = (size_t)doc->canvas_w * doc->canvas_h, new_n = (size_t)w * h;
      if (frame->cels_size != 3 * old_n) return false;
      uint8_t *cels = malloc(3 * new_n);
      if (!cels) return false;
      for (int li = 0; li < 3; li++) {
        layer_t cel = {0};
        cel.pixels = malloc(old_n);
        if (!cel.pixels) { free(cels); return false; }
        memcpy(cel.pixels, frame->cels + li * old_n, old_n);
        bool ok = true;
        if (resample) {
          uint8_t *pixels = layer_resample_pixels(&cel, doc->canvas_w, doc->canvas_h, w, h, filter);
          free(cel.pixels); cel.pixels = pixels; ok = pixels != NULL;
        } else ok = layer_crop_expand(&cel, doc->canvas_w, doc->canvas_h, x, y, w, h, layer_empty_value(doc, li + 1));
        if (ok) memcpy(cels + li * new_n, cel.pixels, new_n);
        free(cel.pixels);
        if (!ok) { free(cels); return false; }
      }
      free(frame->cels); frame->cels = cels; frame->cels_size = 3 * new_n;
    }
    if (!frame->data || !frame->data_size) continue;
    layer_t layer = {0};
    layer.pixels = malloc((size_t)doc->canvas_w * doc->canvas_h * DOC_BPP);
    if (!layer.pixels) return false;
    bool ok = anim_frame_expand(frame, layer.pixels, doc->canvas_w, doc->canvas_h);
    if (ok && resample) {
      uint8_t *pixels = layer_resample_pixels(&layer, doc->canvas_w, doc->canvas_h, w, h, filter);
      free(layer.pixels);
      layer.pixels = pixels;
      ok = pixels != NULL;
    } else if (ok) {
      ok = layer_crop_expand(&layer, doc->canvas_w, doc->canvas_h, x, y, w, h, layer_empty_value(doc, -1));
    }
    if (ok) ok = anim_frame_compress(frame, layer.pixels, w, h, IE_FRAME_FORMAT);
    free(layer.pixels);
    if (!ok) return false;
  }
  return true;
}

// Resize the image contents to new_w x new_h.
// All layer pixel buffers are resampled with the requested filter.
bool canvas_resize_image(canvas_doc_t *doc, int new_w, int new_h,
                         image_resize_filter_t filter) {
  if (!doc || new_w <= 0 || new_h <= 0) return false;
  if (new_w == doc->canvas_w && new_h == doc->canvas_h) return true;
  if ((size_t)new_w > 16384 || (size_t)new_h > 16384) return false;
  if (filter < 0 || filter >= IMAGE_RESIZE_FILTER_COUNT)
    filter = IMAGE_RESIZE_BILINEAR;

  uint8_t **new_pixels = calloc((size_t)doc->layer.count, sizeof(uint8_t *));
  if (!new_pixels) return false;
  for (int i = 0; i < doc->layer.count; i++) {
    new_pixels[i] = layer_resample_pixels(doc->layer.stack[i],
                                          doc->canvas_w, doc->canvas_h,
                                          new_w, new_h, filter);
    if (!new_pixels[i]) {
      for (int j = 0; j < i; j++) free(new_pixels[j]);
      free(new_pixels);
      return false;
    }
  }

  if (!canvas_resize_frames(doc, 0, 0, new_w, new_h, true, filter)) {
    for (int i = 0; i < doc->layer.count; i++) free(new_pixels[i]);
    free(new_pixels);
    return false;
  }
  for (int i = 0; i < doc->layer.count; i++)
    layer_replace_pixels(doc->layer.stack[i], new_pixels[i]);
  free(new_pixels);

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

// Resize the canvas to new_w x new_h.
// All layer pixel buffers are resized. Existing pixels are preserved
// at the top-left corner; any new area is filled with transparent pixels.
// The GL texture is invalidated so it will be re-created on the next paint.
// Returns true on success, false if an allocation fails (canvas may be partially
// resized in that case; callers should treat it as a fatal document error).
bool canvas_resize(canvas_doc_t *doc, int new_w, int new_h) {
  if (!doc || new_w <= 0 || new_h <= 0) return false;
  if (new_w == doc->canvas_w && new_h == doc->canvas_h) return true;
  if ((size_t)new_w > 16384 || (size_t)new_h > 16384) return false;

  if (!canvas_resize_frames(doc, 0, 0, new_w, new_h, false, IMAGE_RESIZE_NEAREST)) return false;

  for (int i = 0; i < doc->layer.count; i++) {
    if (!layer_crop_expand(doc->layer.stack[i], doc->canvas_w, doc->canvas_h,
                           0, 0, new_w, new_h, layer_empty_value(doc, i)))
      return false;
  }

  free(doc->layer.composite_buf);
  doc->layer.composite_buf = malloc((size_t)new_w * new_h * 4);
  // If this allocation fails, canvas_upload will retry and skip rendering
  // until it succeeds.  The document's layer data is already valid.

  doc->canvas_w     = new_w;
  doc->canvas_h     = new_h;
  doc->pixels       = doc->layer.stack[doc->layer.active]->pixels;
  doc->canvas_dirty = true;
  doc->modified     = true;
  doc->sel.active   = false;
  canvas_clear_selection_mask(doc);
  return true;
}
