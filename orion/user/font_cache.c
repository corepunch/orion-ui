#include "font_cache.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <orion/kernel/renderer.h>
#include <platform/platform.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#define FONT_ATLAS_COLS 64
#define FONT_ATLAS_ROWS 64
#define FONT_ATLAS_CAPACITY (FONT_ATLAS_COLS * FONT_ATLAS_ROWS)
#define FONT_CODEPOINT_LIMIT 0x10000

struct font_cache_s {
  FT_Library library;
  FT_Face font;
  float scale;
  float raster_scale;
  float bitmap_scale;
  int baseline;
  int line_height;
  int cell_w, cell_h, columns, capacity;
  int texture_w, texture_h;
  uint32_t texture;
  uint16_t next_slot;
  uint16_t slots[FONT_CODEPOINT_LIMIT];
  font_cache_glyph_t glyphs[FONT_ATLAS_CAPACITY];
};

font_cache_t *font_cache_create(const char *path, float em_size) {
  if (!path || em_size <= 0.0f) {
    fprintf(stderr, "[font] invalid path=%s size=%g\n", path ? path : "(null)", em_size); fflush(stderr); return NULL;
  }
  font_cache_t *cache = (font_cache_t *)calloc(1, sizeof(*cache));
  if (!cache) { fprintf(stderr, "[font] cache allocation failed\n"); fflush(stderr); return NULL; }
  if (FT_Init_FreeType(&cache->library) || FT_New_Face(cache->library, path, 0, &cache->font) || !cache->font->units_per_EM) {
    fprintf(stderr, "[font] failed to load %s\n", path); fflush(stderr);
    font_cache_destroy(cache); return NULL;
  }
  cache->bitmap_scale = axGetScaling();
  if (cache->bitmap_scale < 1.0f) cache->bitmap_scale = 1.0f;
  cache->scale = em_size / cache->font->units_per_EM;
  cache->raster_scale = cache->scale * cache->bitmap_scale;
  if (FT_Set_Char_Size(cache->font, 0, (FT_F26Dot6)lroundf(em_size * cache->bitmap_scale * 64), 72, 72)) {
    fprintf(stderr, "[font] failed to set size %g for %s\n", em_size, path); fflush(stderr);
    font_cache_destroy(cache); return NULL;
  }
  // Line metrics as CSS takes them from the face's hhea table: ascent, descent and line gap
  // each rounded to a whole logical pixel, and the gap split above and below the glyphs
  // (floor above). A line of line-height: normal is their sum and the baseline sits
  // ascent + gap/2 below its top, as in a browser.
  int ascent = (int)lroundf(cache->font->ascender * cache->scale);
  int descent = (int)lroundf(-cache->font->descender * cache->scale);
  int gap = (int)lroundf((cache->font->height - (cache->font->ascender - cache->font->descender)) * cache->scale);
  if (gap < 0) gap = 0;
  cache->baseline = (int)lroundf((ascent + gap / 2) * cache->bitmap_scale);
  cache->line_height = ascent + descent + gap;
  cache->cell_w = (int)ceilf((cache->font->bbox.xMax - cache->font->bbox.xMin) * cache->raster_scale) + 4;
  cache->cell_h = (int)ceilf((cache->font->bbox.yMax - cache->font->bbox.yMin) * cache->raster_scale) + 4;
  if (cache->line_height < 1) cache->line_height = 1;
  if (cache->cell_w < 1) cache->cell_w = 1;
  if (cache->cell_h < 1) cache->cell_h = 1;
  int limit = R_GetMaxTextureSize();
  cache->columns = MIN(FONT_ATLAS_COLS, limit / cache->cell_w);
  int rows = MIN(FONT_ATLAS_ROWS, limit / cache->cell_h);
  if (!cache->columns || !rows) {
    fprintf(stderr, "[font] glyph cell %dx%d exceeds texture limit=%d\n", cache->cell_w, cache->cell_h, limit);
    fflush(stderr);
    font_cache_destroy(cache);
    return NULL;
  }
  cache->capacity = cache->columns * rows;
  cache->texture_w = cache->columns * cache->cell_w;
  cache->texture_h = rows * cache->cell_h;

  size_t atlas_size = (size_t)cache->texture_w * (size_t)cache->texture_h;
  uint8_t *empty_atlas = (uint8_t *)calloc(atlas_size, 1);
  if (!empty_atlas) {
    font_cache_destroy(cache);
    return NULL;
  }
  cache->texture = R_CreateTextureR8(cache->texture_w, cache->texture_h,
                                     empty_atlas, R_FILTER_LINEAR, R_WRAP_CLAMP);
  free(empty_atlas);
  if (!cache->texture) {
    fprintf(stderr, "[font] failed to allocate atlas for %s\n", path);
    font_cache_destroy(cache);
    return NULL;
  }
  return cache;
}

void font_cache_destroy(font_cache_t *cache) {
  if (!cache) return;
  R_DeleteTexture(cache->texture);
  if (cache->font) FT_Done_Face(cache->font);
  if (cache->library) FT_Done_FreeType(cache->library);
  free(cache);
}

static uint32_t supported_codepoint(font_cache_t *cache, uint32_t codepoint) {
  if (codepoint >= FONT_CODEPOINT_LIMIT ||
      FT_Get_Char_Index(cache->font, codepoint) == 0) {
    if (FT_Get_Char_Index(cache->font, 0xFFFD) != 0) return 0xFFFD;
    return '?';
  }
  return codepoint;
}

const font_cache_glyph_t *font_cache_get_glyph(font_cache_t *cache,
                                                uint32_t codepoint) {
  if (!cache) return NULL;
  codepoint = supported_codepoint(cache, codepoint);
  uint16_t mapped = cache->slots[codepoint];
  if (mapped) return &cache->glyphs[mapped - 1];
  if (cache->next_slot >= cache->capacity) {
    fprintf(stderr, "[font] atlas capacity exhausted at U+%04X\n", codepoint);
    fflush(stderr);
    return codepoint == '?' ? NULL : font_cache_get_glyph(cache, '?');
  }

  uint16_t slot = cache->next_slot;
  font_cache_glyph_t *glyph = &cache->glyphs[slot];
  glyph->atlas_x = (slot % cache->columns) * cache->cell_w;
  glyph->atlas_y = (slot / cache->columns) * cache->cell_h;
  FT_UInt index = FT_Get_Char_Index(cache->font, codepoint);
  if (FT_Load_Glyph(cache->font, index, FT_LOAD_NO_SCALE)) {
    fprintf(stderr, "[font] failed to load metrics U+%04X\n", codepoint); fflush(stderr); return NULL;
  }
  glyph->advance = (int16_t)lroundf(cache->font->glyph->advance.x * cache->scale);
  glyph->advance64 = (int32_t)lroundf(cache->font->glyph->advance.x * cache->scale * 64.0f);
  if (FT_Load_Glyph(cache->font, index, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP | FT_LOAD_RENDER)) {
    fprintf(stderr, "[font] failed to render U+%04X\n", codepoint); fflush(stderr); return NULL;
  }
  FT_GlyphSlot image = cache->font->glyph;
  glyph->x_offset = (int16_t)image->bitmap_left;
  glyph->y_offset = (int16_t)(cache->baseline - image->bitmap_top);
  glyph->width = (uint16_t)image->bitmap.width;
  glyph->height = (uint16_t)image->bitmap.rows;
  if (glyph->width > cache->cell_w || glyph->height > cache->cell_h) {
    fprintf(stderr, "[font] glyph U+%04X exceeds cell %dx%d\n", codepoint, cache->cell_w, cache->cell_h); fflush(stderr); return NULL;
  }
  if (glyph->width && glyph->height) {
    uint8_t *pixels = malloc((size_t)glyph->width * glyph->height);
    if (!pixels) { fprintf(stderr, "[font] bitmap allocation failed U+%04X\n", codepoint); fflush(stderr); return NULL; }
    for (int y = 0; y < glyph->height; y++)
      memcpy(pixels + (size_t)y * glyph->width, image->bitmap.buffer + (size_t)y * image->bitmap.pitch, glyph->width);
    bool uploaded = R_UpdateTextureR8(cache->texture, glyph->atlas_x, glyph->atlas_y, glyph->width, glyph->height, pixels);
    free(pixels);
    if (!uploaded) { fprintf(stderr, "[font] upload failed U+%04X\n", codepoint); fflush(stderr); return NULL; }
  }
  cache->slots[codepoint] = slot + 1;
  cache->next_slot++;
  return glyph;
}

uint32_t font_cache_texture(const font_cache_t *cache) { return cache ? cache->texture : 0; }
int font_cache_texture_width(const font_cache_t *cache) { return cache ? cache->texture_w : 0; }
int font_cache_texture_height(const font_cache_t *cache) { return cache ? cache->texture_h : 0; }
int font_cache_line_height(const font_cache_t *cache) { return cache ? cache->line_height : 0; }
float font_cache_bitmap_scale(const font_cache_t *cache) { return cache ? cache->bitmap_scale : 1.0f; }
