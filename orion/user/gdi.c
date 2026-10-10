#include "gdi.h"
#include "draw.h"
#include "image.h"
#include <orion/kernel/renderer.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct bitmap_s { uint8_t *px; int w, h; uint32_t tex; bool keyed; };

static void gdi_reject(const char *what, const char *detail) {
  fprintf(stderr, "[gdi] %s %s\n", what, detail ? detail : "");
  fflush(stderr);
}

bitmap_t *bitmap_create(int w, int h, const uint8_t *rgba) {
  if (w <= 0 || h <= 0) { fprintf(stderr, "[gdi] bitmap_create rejected %dx%d\n", w, h); fflush(stderr); return NULL; }
  bitmap_t *bm = calloc(1, sizeof(*bm));
  uint8_t *px = calloc((size_t)w * h, 4);
  if (!bm || !px) { free(bm); free(px); fprintf(stderr, "[gdi] bitmap allocation failed %dx%d\n", w, h); fflush(stderr); return NULL; }
  if (rgba) memcpy(px, rgba, (size_t)w * h * 4);
  bm->px = px; bm->w = w; bm->h = h;
  return bm;
}

bitmap_t *bitmap_load_memory(const void *data, size_t size) {
  int w = 0, h = 0;
  uint8_t *px = load_image_memory(data, size, &w, &h);
  if (!px) { gdi_reject("bitmap_load_memory: unreadable image", NULL); return NULL; }
  bitmap_t *bm = bitmap_create(w, h, px);
  image_free(px);
  return bm;
}

bitmap_t *bitmap_load(const char *path) {
  int w = 0, h = 0;
  uint8_t *px = path ? load_image(path, &w, &h) : NULL;
  if (!px) { gdi_reject("bitmap_load: unreadable image", path); return NULL; }
  bitmap_t *bm = bitmap_create(w, h, px);
  image_free(px);
  return bm;
}

void bitmap_free(bitmap_t *bm) {
  if (!bm) return;
  R_DeleteTexture(bm->tex);
  free(bm->px);
  free(bm);
}

isize16_t bitmap_size(const bitmap_t *bm) { return bm ? (isize16_t){ (int16_t)bm->w, (int16_t)bm->h } : (isize16_t){0}; }

uint32_t bitmap_pixel(const bitmap_t *bm, int x, int y) {
  if (!bm || x < 0 || y < 0 || x >= bm->w || y >= bm->h) return 0;
  uint32_t c;
  memcpy(&c, bm->px + ((size_t)y * bm->w + x) * 4, 4);
  return c;
}

void bitmap_set_color_key(bitmap_t *bm, uint32_t rgb) {
  if (!bm) return;
  for (size_t i = 0; i < (size_t)bm->w * bm->h; i++) {
    uint8_t *p = bm->px + i * 4;
    if ((uint32_t)(p[0] | p[1] << 8 | p[2] << 16) == (rgb & 0xFFFFFFu)) p[3] = 0;
  }
  bm->keyed = true;
  R_DeleteTexture(bm->tex);
  bm->tex = 0;
}

static bool bitmap_texture(bitmap_t *bm) {
  if (bm->tex) return true;
  bm->tex = R_CreateTextureSRGBA8(bm->w, bm->h, bm->px, R_FILTER_NEAREST, R_WRAP_CLAMP);
  if (!bm->tex) gdi_reject("texture creation failed", NULL);
  return bm->tex != 0;
}

// Clips `src` to the bitmap and shrinks `dst` by the same fraction.
static bool gdi_clip(const bitmap_t *bm, irect16_t *dst, irect16_t *src) {
  if (!bm || src->w <= 0 || src->h <= 0 || dst->w <= 0 || dst->h <= 0) return false;
  irect16_t s = *src, d = *dst;
  int dx0 = d.w * (MAX(0, -s.x)) / s.w, dy0 = d.h * (MAX(0, -s.y)) / s.h;
  int ow = MAX(0, s.x + s.w - bm->w), oh = MAX(0, s.y + s.h - bm->h);
  int dx1 = d.w * ow / s.w, dy1 = d.h * oh / s.h;
  int cx = MAX(0, s.x), cy = MAX(0, s.y), cw = s.x + s.w - ow - cx, ch = s.y + s.h - oh - cy;
  if (cw <= 0 || ch <= 0) return false;
  *src = R(cx, cy, cw, ch);
  *dst = R(d.x + dx0, d.y + dy0, d.w - dx0 - dx1, d.h - dy0 - dy1);
  return dst->w > 0 && dst->h > 0;
}

void stretch_blt(irect16_t dst, const bitmap_t *cbm, irect16_t src) {
  bitmap_t *bm = (bitmap_t *)cbm;
  if (!gdi_clip(bm, &dst, &src) || !bitmap_texture(bm)) return;
  float u0 = (float)src.x / bm->w, v0 = (float)src.y / bm->h, u1 = (float)(src.x + src.w) / bm->w, v1 = (float)(src.y + src.h) / bm->h;
  draw_sprite_region((int)bm->tex, dst, UV_RECT(u0, v0, u1, v1), 0xFFFFFFFFu, 0);
}

void bit_blt(irect16_t dst, const bitmap_t *bm, ipoint16_t src) {
  stretch_blt(dst, bm, R(src.x, src.y, dst.w, dst.h));
}

void tile_blt(irect16_t dst, const bitmap_t *bm, irect16_t src) {
  if (src.w <= 0 || src.h <= 0) { fprintf(stderr, "[gdi] tile_blt rejected source %dx%d\n", src.w, src.h); fflush(stderr); return; }
  for (int y = 0; y < dst.h; y += src.h)
    for (int x = 0; x < dst.w; x += src.w) {
      int w = MIN(src.w, dst.w - x), h = MIN(src.h, dst.h - y);
      bit_blt(R(dst.x + x, dst.y + y, w, h), bm, (ipoint16_t){ src.x, src.y });
    }
}
