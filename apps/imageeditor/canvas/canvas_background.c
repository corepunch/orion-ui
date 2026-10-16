// Background image: an immutable sRGB picture at the bottom of Pencil Test's
// shared Background layer. Palette paint on the Background plane covers it.

#include "imageeditor.h"

bg_image_t *bg_image_new(int w, int h) {
  static uint32_t next_id = 1;
  if (w < 1 || h < 1 || w > 16384 || h > 16384) {
    fprintf(stderr, "[background] image size rejected w=%d h=%d\n", w, h);
    fflush(stderr);
    return NULL;
  }
  bg_image_t *img = malloc(sizeof(*img) + (size_t)w * h * 4);
  if (!img) {
    fprintf(stderr, "[background] image allocation failed w=%d h=%d\n", w, h);
    fflush(stderr);
    return NULL;
  }
  img->refs = 1; img->id = next_id++; img->w = w; img->h = h;
  return img;
}

bg_image_t *bg_image_retain(bg_image_t *img) {
  if (img) img->refs++;
  return img;
}

void bg_image_release(bg_image_t *img) {
  if (img && --img->refs == 0) free(img);
}

bg_image_t *bg_image_cover(const uint8_t *rgba, int src_w, int src_h, int w, int h) {
  int rect[4];
  image_cover_rect(src_w, src_h, w, h, rect);
  uint8_t *pixels = image_resample_srgba8(rgba, src_w, src_h, rect, w, h);
  bg_image_t *img = pixels ? bg_image_new(w, h) : NULL;
  if (img) memcpy(img->pixels, pixels, (size_t)w * h * 4);
  free(pixels);
  return img;
}

bg_image_t *bg_image_load(const char *path, int w, int h) {
  int src_w = 0, src_h = 0;
  uint8_t *rgba = path ? load_image(path, &src_w, &src_h) : NULL;
  if (!rgba) {
    fprintf(stderr, "[background] cannot load image path=%s\n", path ? path : "(null)");
    fflush(stderr);
    return NULL;
  }
  bg_image_t *img = bg_image_cover(rgba, src_w, src_h, w, h);
  image_free(rgba);
  return img;
}

bg_image_t *bg_image_transform(const bg_image_t *img, int x, int y, int w, int h, bool resample) {
  if (!img) return NULL;
  if (resample) {
    int rect[4] = {0, 0, img->w, img->h};
    uint8_t *pixels = image_resample_srgba8(img->pixels, img->w, img->h, rect, w, h);
    bg_image_t *out = pixels ? bg_image_new(w, h) : NULL;
    if (out) memcpy(out->pixels, pixels, (size_t)w * h * 4);
    free(pixels);
    return out;
  }
  bg_image_t *out = bg_image_new(w, h);
  if (!out) return NULL;
  memset(out->pixels, 0, (size_t)w * h * 4);
  irect16_t src = R(0, 0, img->w, img->h), dst = R(x, y, w, h), keep = rect_intersect(src, dst);
  for (int row = 0; row < keep.h; row++)
    memcpy(out->pixels + ((size_t)(keep.y - y + row) * w + (keep.x - x)) * 4,
           img->pixels + ((size_t)(keep.y + row) * img->w + keep.x) * 4, (size_t)keep.w * 4);
  return out;
}

bool doc_set_background_image(canvas_doc_t *doc, bg_image_t *img) {
  if (!pencil_has_layers(doc) || !doc->command.before ||
      (img && (img->w != doc->canvas_w || img->h != doc->canvas_h))) {
    fprintf(stderr, "[background] set image rejected doc=%p command=%d canvas=%dx%d image=%dx%d\n",
            (void *)doc, doc && doc->command.before != NULL, doc ? doc->canvas_w : 0, doc ? doc->canvas_h : 0,
            img ? img->w : 0, img ? img->h : 0);
    fflush(stderr);
    return false;
  }
#if IMAGEEDITOR_INDEXED
  if (img) memset(doc->layer.stack[IE_LAYER_BG]->pixels, doc->ipal.transparent, (size_t)doc->canvas_w * doc->canvas_h);
#endif
  bg_image_release(doc->background.image);
  doc->background.image = bg_image_retain(img);
  doc->canvas_dirty = true;
  doc->modified = true;
  return true;
}

void doc_background_upload(canvas_doc_t *doc) {
  if (!doc) return;
  const bg_image_t *img = doc->background.image;
  if (!img) {
    if (doc->background_cache.tex) R_DeleteTexture(doc->background_cache.tex);
    doc->background_cache.tex = 0; doc->background_cache.id = 0;
    return;
  }
  if (doc->background_cache.tex && doc->background_cache.id == img->id) return;
  if (doc->background_cache.tex) R_DeleteTexture(doc->background_cache.tex);
  doc->background_cache.tex = R_CreateTextureSRGBA8(img->w, img->h, img->pixels, R_FILTER_LINEAR, R_WRAP_CLAMP);
  doc->background_cache.id = doc->background_cache.tex ? img->id : 0;
}

void doc_background_free(canvas_doc_t *doc) {
  if (!doc) return;
  if (doc->background_cache.tex) R_DeleteTexture(doc->background_cache.tex);
  doc->background_cache.tex = 0; doc->background_cache.id = 0;
  bg_image_release(doc->background.image);
  doc->background.image = NULL;
}

void doc_background_composite(const canvas_doc_t *doc, uint8_t *rgba) {
  const bg_image_t *img = doc ? doc->background.image : NULL;
  if (!img || !rgba || !pencil_has_layers(doc) || img->w != doc->canvas_w || img->h != doc->canvas_h) return;
  const layer_t *bg = doc->layer.stack[IE_LAYER_BG];
  if (!bg->visible || !bg->opacity) return;
  float opacity = bg->opacity / 255.0f;
  size_t n = (size_t)img->w * img->h;
  for (size_t i = 0; i < n; i++) ui_composite_srgba8(rgba + i * 4, img->pixels + i * 4, opacity);
}
