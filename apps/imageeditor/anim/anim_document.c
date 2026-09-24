#include "imageeditor.h"

bool pencil_has_layers(const canvas_doc_t *doc) {
  return IMAGEEDITOR_BW && doc && doc->layer.count == IE_LAYER_COUNT;
}

const uint8_t *pencil_frame_layer(const canvas_doc_t *doc, int frame, int layer) {
  if (!pencil_has_layers(doc) || !doc->anim || frame < 0 || frame >= doc->anim->frame_count ||
      layer < 0 || layer >= IE_LAYER_COUNT) return NULL;
  if (layer == IE_LAYER_BG || frame == doc->anim->active_frame)
    return doc->layer.stack[layer]->pixels;
  const anim_frame_t *f = doc->anim->frames[frame];
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;
  if (f->cels && f->cels_size == 3 * n) return f->cels + (layer - 1) * n;
  if (!f->cels && layer == IE_LAYER_COLOR && f->format == FRAME_FORMAT_INDEXED && f->data_size == n)
    return f->data;
  return NULL;
}

bool pencil_composite_frame(const canvas_doc_t *doc, int frame, uint8_t *pixels) {
#if IMAGEEDITOR_INDEXED
  if (!pencil_has_layers(doc) || !pixels || !doc->anim || frame < 0 || frame >= doc->anim->frame_count)
    return false;
  const anim_frame_t *f = doc->anim->frames[frame];
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;
  if (frame != doc->anim->active_frame &&
      ((f->cels && f->cels_size != 3 * n) || (!f->cels && f->data &&
        (f->format != FRAME_FORMAT_INDEXED || f->data_size != n)))) return false;
  memset(pixels, doc->ipal.transparent, n);
  static const int order[] = {IE_LAYER_BG, IE_LAYER_PENCIL, IE_LAYER_COLOR, IE_LAYER_FX};
  for (int order_i = 0; order_i < IE_LAYER_COUNT; order_i++) {
    int layer = order[order_i];
    if (!doc->layer.stack[layer]->visible) continue;
    const uint8_t *src = pencil_frame_layer(doc, frame, layer);
    if (src) for (size_t p = 0; p < n; p++) {
      if (layer == IE_LAYER_PENCIL) {
        if (src[p]) pixels[p] = (uint8_t)canvas_nearest_palette_index(doc, pencil_configured_color());
      } else if (src[p] != doc->ipal.transparent) pixels[p] = src[p];
    }
  }
  return true;
#else
  (void)doc; (void)frame; (void)pixels;
  return false;
#endif
}

#if IMAGEEDITOR_INDEXED
static void pencil_rgba_blend(uint8_t *dst, uint32_t color, uint8_t alpha) {
  if (!alpha) return;
  uint32_t da = dst[3], inv = 255 - alpha;
  uint32_t out_a = alpha + (da * inv + 127) / 255;
  uint64_t denom = (uint64_t)out_a * 255;
  if (!denom) return;
  dst[0] = (uint8_t)(((uint64_t)COLOR_R(color) * alpha * 255 + (uint64_t)dst[0] * da * inv + denom / 2) / denom);
  dst[1] = (uint8_t)(((uint64_t)COLOR_G(color) * alpha * 255 + (uint64_t)dst[1] * da * inv + denom / 2) / denom);
  dst[2] = (uint8_t)(((uint64_t)COLOR_B(color) * alpha * 255 + (uint64_t)dst[2] * da * inv + denom / 2) / denom);
  dst[3] = (uint8_t)out_a;
}

static bool pencil_composite_frame_rgba(const canvas_doc_t *doc, int frame, uint8_t *rgba) {
  if (!pencil_has_layers(doc) || !rgba || !doc->anim || frame < 0 || frame >= doc->anim->frame_count)
    return false;
  const anim_frame_t *f = doc->anim->frames[frame];
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;
  if (frame != doc->anim->active_frame &&
      ((f->cels && f->cels_size != 3 * n) || (!f->cels && f->data &&
        (f->format != FRAME_FORMAT_INDEXED || f->data_size != n)))) return false;
  memset(rgba, 0, n * 4);
  static const int order[] = {IE_LAYER_BG, IE_LAYER_PENCIL, IE_LAYER_COLOR, IE_LAYER_FX};
  for (int order_i = 0; order_i < IE_LAYER_COUNT; order_i++) {
    int layer = order[order_i];
    const layer_t *lay = doc->layer.stack[layer];
    if (!lay->visible) continue;
    const uint8_t *src = pencil_frame_layer(doc, frame, layer);
    if (!src) continue;
    for (size_t p = 0; p < n; p++) {
      uint32_t color;
      uint32_t alpha;
      if (layer == IE_LAYER_PENCIL) {
        if (!src[p]) continue;
        color = pencil_configured_color();
        alpha = src[p];
      } else {
        uint8_t index = src[p];
        if (index == (uint8_t)doc->ipal.transparent) continue;
        color = doc->ipal.entries[index];
        alpha = COLOR_A(color);
      }
      alpha = alpha * lay->opacity / 255;
      pencil_rgba_blend(rgba + p * 4, color, (uint8_t)alpha);
    }
  }
  return true;
}
#endif

bool doc_anim_commit(canvas_doc_t *doc) {
  if (!doc || !doc->anim) return false;
  anim_frame_t *frame = doc->anim->frames[doc->anim->active_frame];
  if (!pencil_has_layers(doc))
    return anim_frame_compress(frame, doc->pixels, doc->canvas_w, doc->canvas_h, IE_FRAME_FORMAT);
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;
  uint8_t *cels = malloc(3 * n), *composite = malloc(n);
  bool ok = cels && composite && pencil_composite_frame(doc, doc->anim->active_frame, composite);
  if (ok) {
    for (int i = 1; i < IE_LAYER_COUNT; i++) memcpy(cels + (i - 1) * n, doc->layer.stack[i]->pixels, n);
    ok = anim_frame_compress(frame, composite, doc->canvas_w, doc->canvas_h, FRAME_FORMAT_INDEXED);
  }
  free(composite);
  if (!ok) {
    free(cels);
    IE_TRACE("frame commit failed doc=%p frame=%d", (void *)doc, doc->anim->active_frame);
    return false;
  }
  free(frame->cels);
  frame->cels = cels;
  frame->cels_size = 3 * n;
  return true;
}

bool doc_anim_load(canvas_doc_t *doc, int index) {
  if (!doc || !doc->anim || index < 0 || index >= doc->anim->frame_count) return false;
  anim_frame_t *frame = doc->anim->frames[index];
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;
  if (pencil_has_layers(doc)) {
    if ((frame->cels && frame->cels_size != 3 * n) || (!frame->cels && frame->data &&
        (frame->format != FRAME_FORMAT_INDEXED || frame->data_size != n))) return false;
    for (int i = 1; i < IE_LAYER_COUNT; i++) {
      const uint8_t *src = frame->cels ? frame->cels + (i - 1) * n :
                           i == IE_LAYER_COLOR ? frame->data : NULL;
      if (src) memcpy(doc->layer.stack[i]->pixels, src, n);
      else memset(doc->layer.stack[i]->pixels, i == IE_LAYER_PENCIL ? 0 : 255, n);
    }
  } else if (frame->data && frame->data_size) {
    if (!anim_frame_expand(frame, doc->pixels, doc->canvas_w, doc->canvas_h)) return false;
  } else memset(doc->pixels, 0, n * DOC_BPP);
  doc->anim->active_frame = index;
  doc->canvas_dirty = true;
  return true;
}

bool doc_anim_switch(canvas_doc_t *doc, int index) {
  if (!doc || !doc->anim || index < 0 || index >= doc->anim->frame_count) return false;
  return doc_anim_commit(doc) && doc_anim_load(doc, index);
}

bool doc_anim_rgba(const canvas_doc_t *doc, int index, uint8_t *rgba) {
  if (!doc || !doc->anim || !rgba || index < 0 || index >= doc->anim->frame_count) return false;
#if IMAGEEDITOR_INDEXED
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;
  bool ok;
  if (pencil_has_layers(doc)) ok = pencil_composite_frame_rgba(doc, index, rgba);
  else {
    uint8_t *pixels = malloc(n);
    if (!pixels) return false;
    ok = index == doc->anim->active_frame ? (memcpy(pixels, doc->pixels, n), true) :
         anim_frame_expand(doc->anim->frames[index], pixels, doc->canvas_w, doc->canvas_h);
    if (ok) for (size_t p = 0; p < n; p++) {
      uint32_t c = doc->ipal.entries[pixels[p]];
      rgba[4*p] = COLOR_R(c); rgba[4*p+1] = COLOR_G(c); rgba[4*p+2] = COLOR_B(c);
      rgba[4*p+3] = pixels[p] == doc->ipal.transparent ? 0 : COLOR_A(c);
    }
    free(pixels);
  }
  if (ok) canvas_composite_over_bg(doc, rgba);
  return ok;
#else
  return anim_frame_expand(doc->anim->frames[index], rgba, doc->canvas_w, doc->canvas_h);
#endif
}
