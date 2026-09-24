// Canvas rendering: compositing and GL texture management

#include "imageeditor.h"

// ============================================================
// Compositing
// ============================================================

void canvas_composite(const canvas_doc_t *doc, uint8_t *dst) {
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;
  memset(dst, 0x00, n * 4);

#if IMAGEEDITOR_INDEXED
  // Indexed layers are opaque palette pixels, except Pencil Test's pencil
  // layer, whose byte is per-pixel coverage in the configured pencil color.
  static const int pencil_order[] = {IE_LAYER_BG, IE_LAYER_PENCIL, IE_LAYER_COLOR, IE_LAYER_FX};
  for (int order_i = 0; order_i < doc->layer.count; order_i++) {
    int li = pencil_has_layers(doc) ? pencil_order[order_i] : order_i;
    const layer_t *lay = doc->layer.stack[li];
    if (!lay->visible) continue;
    const uint8_t *idx_buf = lay->pixels;
    for (size_t i = 0; i < n; i++) {
      uint8_t pidx = idx_buf[i];
      uint32_t c;
      uint32_t sa;
      if (pencil_has_layers(doc) && li == IE_LAYER_PENCIL) {
        if (!pidx) continue;
        c = pencil_configured_color();
        sa = (uint32_t)pidx * lay->opacity / 255;
      } else {
        if (pidx == (uint8_t)doc->ipal.transparent) continue;
        c = doc->ipal.entries[pidx];
        sa = (uint32_t)COLOR_A(c) * lay->opacity / 255;
      }
      if (!sa) continue;
      uint8_t *d = dst + i * 4;
      uint8_t source[4] = {COLOR_R(c), COLOR_G(c), COLOR_B(c), (uint8_t)sa};
      ui_composite_srgba8(d, source, 1.0f);
    }
  }
#else
  for (int li = 0; li < doc->layer.count; li++) {
    const layer_t *lay = doc->layer.stack[li];
    if (!lay->visible) continue;

    for (size_t i = 0; i < n; i++) {
      const uint8_t *src = lay->pixels + i * 4;
      uint8_t       *d   = dst + i * 4;

      uint8_t sa8 = (uint8_t)((src[3] * lay->opacity + 127) / 255);
      if (sa8 == 0) continue;
      float sa = (float)sa8 / 255.0f;
      float da = (float)d[3] / 255.0f;
      float inv = 1.0f - sa;
      float source[3] = {ui_srgb8_to_linear(src[0]),
                         ui_srgb8_to_linear(src[1]),
                         ui_srgb8_to_linear(src[2])};
      float dest[3] = {ui_srgb8_to_linear(d[0]),
                       ui_srgb8_to_linear(d[1]),
                       ui_srgb8_to_linear(d[2])};
      switch (lay->blend_mode) {
        case LAYER_BLEND_MULTIPLY:
          for (int c = 0; c < 3; c++) source[c] *= dest[c];
          break;
        case LAYER_BLEND_SCREEN:
          for (int c = 0; c < 3; c++) source[c] = source[c] + dest[c] - source[c] * dest[c];
          break;
        case LAYER_BLEND_ADD:
          for (int c = 0; c < 3; c++) source[c] = MIN(1.0f, source[c] + dest[c]);
          break;
        case LAYER_BLEND_NORMAL:
        default:
          break;
      }
      float out_a = sa + da * inv;
      if (out_a <= 0.0f) continue;
      for (int c = 0; c < 3; c++)
        d[c] = ui_linear_to_srgb8((source[c] * sa + dest[c] * da * inv) / out_a);
      d[3] = (uint8_t)(out_a * 255.0f + 0.5f);
    }
  }
#endif
}

void canvas_composite_over_bg(const canvas_doc_t *doc, uint8_t *rgba) {
  if (!doc || !rgba) return;
  if (!doc->background.show) return;

  uint8_t background[4] = {COLOR_R(doc->background.color),
                           COLOR_G(doc->background.color),
                           COLOR_B(doc->background.color), 255};
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;

  for (size_t i = 0; i < n; i++) {
    uint8_t *p = rgba + i * 4;
    uint8_t foreground[4];
    memcpy(foreground, p, sizeof(foreground));
    memcpy(p, background, sizeof(background));
    ui_composite_srgba8(p, foreground, 1.0f);
  }
}

// ============================================================
// GL texture management
// ============================================================

static bool layer_upload_texture(canvas_doc_t *doc, layer_t *lay, irect16_t r) {
#if IMAGEEDITOR_BW
  if (pencil_has_layers(doc)) {
    const uint8_t *pixels = lay->pixels + (size_t)r.y * doc->canvas_w;
    if (r.x != 0 || r.w != doc->canvas_w) {
      if (!doc->layer.composite_buf)
        doc->layer.composite_buf = malloc((size_t)doc->canvas_w * doc->canvas_h * 4);
      if (!doc->layer.composite_buf) {
        IE_TRACE("R8 upload allocation failed doc=%p", (void *)doc);
        return false;
      }
      for (int y = 0; y < r.h; y++)
        memcpy(doc->layer.composite_buf + (size_t)y * r.w,
               pixels + (size_t)y * doc->canvas_w + r.x, r.w);
      pixels = doc->layer.composite_buf;
    }
    if (!lay->tex) {
      lay->tex = R_CreateTextureR8(doc->canvas_w, doc->canvas_h, pixels, R_FILTER_NEAREST, R_WRAP_CLAMP);
      return lay->tex != 0;
    }
    return R_UpdateTextureR8(lay->tex, r.x, r.y, r.w, r.h, pixels);
  }
#endif
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
        uint32_t c = pencil_has_layers(doc) && lay == doc->layer.stack[IE_LAYER_PENCIL]
                   ? pencil_configured_color() : doc->ipal.entries[idx];
        if (pencil_has_layers(doc) && lay == doc->layer.stack[IE_LAYER_PENCIL]) {
          dst[0] = COLOR_R(c); dst[1] = COLOR_G(c); dst[2] = COLOR_B(c); dst[3] = idx;
        } else if (idx == (uint8_t)doc->ipal.transparent) {
          memset(dst, 0, 4);
        } else {
          dst[0] = COLOR_R(c); dst[1] = COLOR_G(c);
          dst[2] = COLOR_B(c); dst[3] = COLOR_A(c);
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
#if IMAGEEDITOR_INDEXED
    lay->tex = R_CreateTextureRGBA(doc->canvas_w, doc->canvas_h, rgba,
                                   R_FILTER_NEAREST, R_WRAP_CLAMP);
#else
    lay->tex = R_CreateTextureSRGBA8(doc->canvas_w, doc->canvas_h, rgba,
                                     R_FILTER_NEAREST, R_WRAP_CLAMP);
#endif
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
