#include "imageeditor.h"

bool pencil_has_layers(const canvas_doc_t *doc) {
  return IMAGEEDITOR_BW && doc && doc->layer.count == IE_LAYER_COUNT;
}

static bool pencil_expand_stored_layer(const anim_frame_t *frame, int layer, uint8_t *pixels, int w, int h) {
  if (frame->cels) return anim_frame_expand_cel(frame, layer - 1, pixels, w, h);
  if (layer == IE_LAYER_COLOR && frame->data) return anim_frame_expand_indices(frame, pixels, w, h);
  memset(pixels, layer == IE_LAYER_PENCIL ? 0 : 255, (size_t)w * h);
  return true;
}

bool pencil_read_frame_layer(const canvas_doc_t *doc, int frame, int layer, uint8_t *pixels) {
  if (!pencil_has_layers(doc) || !pixels || !doc->anim || frame < 0 || frame >= doc->anim->frame_count ||
      layer < 0 || layer >= IE_LAYER_COUNT) return false;
  if (layer == IE_LAYER_BG || doc->anim->frames[frame].cel == doc->anim->frames[doc->anim->active_frame].cel) {
    memcpy(pixels, doc->layer.stack[layer]->pixels, (size_t)doc->canvas_w * doc->canvas_h);
    return true;
  }
  return pencil_expand_stored_layer(doc->anim->frames[frame].cel, layer, pixels, doc->canvas_w, doc->canvas_h);
}

bool pencil_composite_frame(const canvas_doc_t *doc, int frame, uint8_t *pixels) {
#if IMAGEEDITOR_INDEXED
  if (!pencil_has_layers(doc) || !pixels || !doc->anim || frame < 0 || frame >= doc->anim->frame_count) return false;
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;
  uint8_t *plane = malloc(n);
  if (!plane) return false;
  memset(pixels, doc->ipal.transparent, n);
  static const int order[] = {IE_LAYER_BG, IE_LAYER_PENCIL, IE_LAYER_COLOR, IE_LAYER_FX};
  bool ok = true;
  int ink = canvas_nearest_palette_index(doc, pencil_configured_color());
  for (int i = 0; ok && i < IE_LAYER_COUNT; i++) {
    int layer = order[i];
    if (!doc->layer.stack[layer]->visible) continue;
    ok = pencil_read_frame_layer(doc, frame, layer, plane);
    if (ok) for (size_t p = 0; p < n; p++) {
      if (layer == IE_LAYER_PENCIL) { if (plane[p]) pixels[p] = ink; }
      else if (plane[p] != doc->ipal.transparent) pixels[p] = plane[p];
    }
  }
  free(plane);
  return ok;
#else
  (void)doc; (void)frame; (void)pixels;
  return false;
#endif
}

#if IMAGEEDITOR_INDEXED
static void pencil_rgba_blend(uint8_t *dst, uint32_t color, uint8_t alpha) {
  uint8_t source[4] = {COLOR_R(color), COLOR_G(color), COLOR_B(color), alpha};
  ui_composite_srgba8(dst, source, 1.0f);
}

static bool pencil_composite_frame_rgba(const canvas_doc_t *doc, int frame, uint8_t *rgba) {
  if (!pencil_has_layers(doc) || !rgba || !doc->anim || frame < 0 || frame >= doc->anim->frame_count) return false;
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;
  uint8_t *plane = malloc(n);
  if (!plane) return false;
  memset(rgba, 0, n * 4);
  static const int order[] = {IE_LAYER_BG, IE_LAYER_PENCIL, IE_LAYER_COLOR, IE_LAYER_FX};
  bool ok = true;
  for (int i = 0; ok && i < IE_LAYER_COUNT; i++) {
    int layer = order[i];
    const layer_t *lay = doc->layer.stack[layer];
    if (!lay->visible) continue;
    if (layer == IE_LAYER_BG) doc_background_composite(doc, rgba);
    ok = pencil_read_frame_layer(doc, frame, layer, plane);
    if (!ok) break;
    for (size_t p = 0; p < n; p++) {
      uint32_t color, alpha;
      if (layer == IE_LAYER_PENCIL) {
        if (!plane[p]) continue;
        color = pencil_configured_color(); alpha = plane[p];
      } else {
        if (plane[p] == (uint8_t)doc->ipal.transparent) continue;
        color = doc->ipal.entries[plane[p]]; alpha = COLOR_A(color);
      }
      pencil_rgba_blend(rgba + p * 4, color, alpha * lay->opacity / 255);
    }
  }
  free(plane);
  return ok;
}
#endif

bool doc_anim_commit(canvas_doc_t *doc) {
  if (!doc || !doc->anim) return false;
  anim_frame_t *frame = doc->anim->frames[doc->anim->active_frame].cel;
  if (!pencil_has_layers(doc)) return anim_frame_compress(frame, doc->pixels, doc->canvas_w, doc->canvas_h, IE_FRAME_FORMAT);
  const uint8_t *planes[3];
  for (int i = 0; i < 3; i++) planes[i] = doc->layer.stack[i + 1]->pixels;
  return anim_frame_pack_cels(frame, planes, doc->canvas_w, doc->canvas_h);
}

bool doc_anim_load(canvas_doc_t *doc, int index) {
  if (!doc || !doc->anim || index < 0 || index >= doc->anim->frame_count) return false;
  anim_frame_t *frame = doc->anim->frames[index].cel;
  size_t n = (size_t)doc->canvas_w * doc->canvas_h;
  if (pencil_has_layers(doc)) {
    uint8_t *decoded = malloc(3 * n);
    if (!decoded) return false;
    bool ok = true;
    for (int i = 0; ok && i < 3; i++)
      ok = pencil_expand_stored_layer(frame, i + 1, decoded + (size_t)i * n, doc->canvas_w, doc->canvas_h);
    if (ok) for (int i = 0; i < 3; i++) memcpy(doc->layer.stack[i + 1]->pixels, decoded + (size_t)i * n, n);
    free(decoded);
    if (!ok) return false;
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
         anim_frame_expand(doc->anim->frames[index].cel, pixels, doc->canvas_w, doc->canvas_h);
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
  return anim_frame_expand(doc->anim->frames[index].cel, rgba, doc->canvas_w, doc->canvas_h);
#endif
}
