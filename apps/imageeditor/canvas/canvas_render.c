// Canvas rendering: compositing and GL texture management

#include "imageeditor.h"

// ============================================================
// Compositing
// ============================================================

void canvas_composite(const canvas_doc_t *doc, uint8_t *dst) {
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;
  memset(dst, 0x00, n * 4);

#if IMAGEEDITOR_INDEXED
  // Indexed mode: one layer, map each pixel index through the palette.
  // The transparent index produces fully transparent pixels.
  if (doc->layer.count > 0 && doc->layer.stack[0]->pixels) {
    const uint8_t *idx_buf = doc->layer.stack[0]->pixels;
    for (size_t i = 0; i < n; i++) {
      uint8_t pidx = idx_buf[i];
      uint8_t *d = dst + i * 4;
      if (pidx == (uint8_t)doc->ipal.transparent) {
        // Transparent: leave as zero (already cleared).
        continue;
      }
      uint32_t c = doc->ipal.entries[pidx];
      d[0] = COLOR_R(c);
      d[1] = COLOR_G(c);
      d[2] = COLOR_B(c);
      d[3] = 255;
    }
  }
#else
  for (int li = 0; li < doc->layer.count; li++) {
    const layer_t *lay = doc->layer.stack[li];
    if (!lay->visible) continue;

    for (size_t i = 0; i < n; i++) {
      const uint8_t *src = lay->pixels + i * 4;
      uint8_t       *d   = dst + i * 4;

      uint32_t sa = (src[3] * lay->opacity + 127) / 255;
      if (sa == 0) continue;

      uint32_t da = d[3];
      uint32_t inv = 255 - sa;
      uint32_t out_a = sa + (da * inv + 127) / 255;
      if (out_a == 0) continue;

      uint8_t br = src[0], bg = src[1], bb = src[2];
      switch (lay->blend_mode) {
        case LAYER_BLEND_MULTIPLY:
          br = (uint8_t)((uint32_t)src[0] * d[0] / 255);
          bg = (uint8_t)((uint32_t)src[1] * d[1] / 255);
          bb = (uint8_t)((uint32_t)src[2] * d[2] / 255);
          break;
        case LAYER_BLEND_SCREEN:
          br = (uint8_t)(255 - ((uint32_t)(255 - src[0]) * (255 - d[0]) / 255));
          bg = (uint8_t)(255 - ((uint32_t)(255 - src[1]) * (255 - d[1]) / 255));
          bb = (uint8_t)(255 - ((uint32_t)(255 - src[2]) * (255 - d[2]) / 255));
          break;
        case LAYER_BLEND_ADD:
          br = (uint8_t)MIN(255, (int)src[0] + (int)d[0]);
          bg = (uint8_t)MIN(255, (int)src[1] + (int)d[1]);
          bb = (uint8_t)MIN(255, (int)src[2] + (int)d[2]);
          break;
        case LAYER_BLEND_NORMAL:
        default:
          break;
      }

      uint64_t out_r = (uint64_t)br * sa * 255 +
                       (uint64_t)d[0] * da * inv;
      uint64_t out_g = (uint64_t)bg * sa * 255 +
                       (uint64_t)d[1] * da * inv;
      uint64_t out_b = (uint64_t)bb * sa * 255 +
                       (uint64_t)d[2] * da * inv;
      uint64_t denom = (uint64_t)out_a * 255;

      d[0] = (uint8_t)((out_r + denom / 2) / denom);
      d[1] = (uint8_t)((out_g + denom / 2) / denom);
      d[2] = (uint8_t)((out_b + denom / 2) / denom);
      d[3] = (uint8_t)out_a;
    }
  }
#endif
}

void canvas_composite_over_bg(const canvas_doc_t *doc, uint8_t *rgba) {
  if (!doc || !rgba) return;
  if (!doc->background.show) return;

  uint8_t bg_r = COLOR_R(doc->background.color);
  uint8_t bg_g = COLOR_G(doc->background.color);
  uint8_t bg_b = COLOR_B(doc->background.color);
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;

  for (size_t i = 0; i < n; i++) {
    uint8_t *p = rgba + i * 4;
    uint32_t sa = p[3];
    if (sa == 0) {
      p[0] = bg_r;
      p[1] = bg_g;
      p[2] = bg_b;
      p[3] = 255;
      continue;
    }
    if (sa == 255) {
      continue;
    }

    uint32_t inv = 255 - sa;
    p[0] = (uint8_t)((p[0] * sa + bg_r * inv + 127) / 255);
    p[1] = (uint8_t)((p[1] * sa + bg_g * inv + 127) / 255);
    p[2] = (uint8_t)((p[2] * sa + bg_b * inv + 127) / 255);
    p[3] = 255;
  }
}

// ============================================================
// GL texture management
// ============================================================

static bool layer_upload_texture(canvas_doc_t *doc, layer_t *lay, irect16_t r) {
  const uint8_t *rgba = lay->pixels;
#if IMAGEEDITOR_INDEXED
  bool pack = true;
#else
  bool pack = r.x != 0 || r.w != doc->canvas_w;
  rgba += (size_t)r.y * doc->canvas_w * 4;
#endif
  if (pack) {
    if (!doc->layer.composite_buf)
      doc->layer.composite_buf = malloc((size_t)doc->canvas_w * doc->canvas_h * 4);
    if (!doc->layer.composite_buf) {
      IE_TRACE("texture scratch allocation failed doc=%p size=%dx%d",
               (void *)doc, doc->canvas_w, doc->canvas_h);
      return false;
    }
    uint8_t *dst = doc->layer.composite_buf;
    for (int y = r.y; y < r.y + r.h; y++) {
#if IMAGEEDITOR_INDEXED
      const uint8_t *src = lay->pixels + (size_t)y * doc->canvas_w + r.x;
      for (int x = 0; x < r.w; x++, dst += 4) {
        uint8_t idx = src[x];
        uint32_t c = doc->ipal.entries[idx];
        if (idx == (uint8_t)doc->ipal.transparent) {
          memset(dst, 0, 4);
        } else {
          dst[0] = COLOR_R(c); dst[1] = COLOR_G(c);
          dst[2] = COLOR_B(c); dst[3] = 255;
        }
      }
#else
      memcpy(dst, lay->pixels + ((size_t)y * doc->canvas_w + r.x) * 4, (size_t)r.w * 4);
      dst += (size_t)r.w * 4;
#endif
    }
    rgba = doc->layer.composite_buf;
  }
  if (!lay->tex) {
    lay->tex = R_CreateTextureRGBA(doc->canvas_w, doc->canvas_h, rgba,
                                   R_FILTER_NEAREST, R_WRAP_CLAMP);
    return lay->tex != 0;
  }
  return R_UpdateTextureRGBA(lay->tex, r.x, r.y, r.w, r.h, rgba);
}

void canvas_upload(canvas_doc_t *doc) {
  if (!doc) return;
  bool complete = true;
  for (int i = 0; i < doc->layer.count; i++) {
    layer_t *lay = doc->layer.stack[i];
    if (!lay || !lay->pixels) {
      IE_TRACE("texture pixels unavailable doc=%p layer=%d", (void *)doc, i);
      complete = false;
      continue;
    }
    irect16_t r = (doc->canvas_dirty || !lay->tex) ?
                  R(0, 0, doc->canvas_w, doc->canvas_h) : lay->dirty_rect;
    if (r.w <= 0 || r.h <= 0) continue;
    if (layer_upload_texture(doc, lay, r)) {
      lay->dirty_rect = R(0, 0, 0, 0);
    } else {
      IE_TRACE("texture upload failed doc=%p layer=%d rect=%d,%d,%d,%d",
               (void *)doc, i, r.x, r.y, r.w, r.h);
      complete = false;
    }
  }
  if (complete) doc->canvas_dirty = false;
}
