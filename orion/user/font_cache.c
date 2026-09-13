#include "font_cache.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <orion/kernel/renderer.h>
#include <platform/platform.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <tools/stb_truetype.h>
#undef STB_TRUETYPE_IMPLEMENTATION

#define FONT_ATLAS_COLS 64
#define FONT_ATLAS_ROWS 64
#define FONT_ATLAS_CAPACITY (FONT_ATLAS_COLS * FONT_ATLAS_ROWS)
#define FONT_CODEPOINT_LIMIT 0x10000

struct font_cache_s {
  uint8_t *ttf_data;
  stbtt_fontinfo font;
  float scale;
  float raster_scale;
  float bitmap_scale;
  int baseline;
  int line_height;
  int cell_w, cell_h;
  int texture_w, texture_h;
  uint32_t texture;
  uint16_t next_slot;
  uint16_t slots[FONT_CODEPOINT_LIMIT];
  font_cache_glyph_t glyphs[FONT_ATLAS_CAPACITY];
};

static uint8_t *read_ttf(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) return NULL;
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  rewind(file);
  if (size <= 0 || size > 32 * 1024 * 1024) { fclose(file); return NULL; }
  uint8_t *data = (uint8_t *)malloc((size_t)size);
  if (!data || fread(data, 1, (size_t)size, file) != (size_t)size) {
    free(data);
    fclose(file);
    return NULL;
  }
  fclose(file);
  return data;
}

font_cache_t *font_cache_create(const char *path, float em_size) {
  if (!path || em_size <= 0.0f) return NULL;
  font_cache_t *cache = (font_cache_t *)calloc(1, sizeof(*cache));
  if (!cache) return NULL;
  cache->ttf_data = read_ttf(path);
  int offset = cache->ttf_data ? stbtt_GetFontOffsetForIndex(cache->ttf_data, 0) : -1;
  if (offset < 0 || !stbtt_InitFont(&cache->font, cache->ttf_data, offset)) {
    fprintf(stderr, "[font] failed to load %s\n", path);
    font_cache_destroy(cache);
    return NULL;
  }

  int ascent, descent, line_gap, x0, y0, x1, y1;
  cache->bitmap_scale = axGetScaling();
  if (cache->bitmap_scale < 1.0f) cache->bitmap_scale = 1.0f;
  // Font size is the logical em size; display density only affects the bitmap.
  cache->scale = stbtt_ScaleForMappingEmToPixels(&cache->font, em_size);
  cache->raster_scale = cache->scale * cache->bitmap_scale;
  stbtt_GetFontVMetrics(&cache->font, &ascent, &descent, &line_gap);
  stbtt_GetFontBoundingBox(&cache->font, &x0, &y0, &x1, &y1);
  cache->baseline = (int)ceilf((float)ascent * cache->raster_scale);
  cache->line_height = (int)ceilf((float)(ascent - descent + line_gap) * cache->scale);
  cache->cell_w = (int)ceilf((float)(x1 - x0) * cache->raster_scale) +
                  (int)ceilf(2.0f * cache->bitmap_scale);
  cache->cell_h = (int)ceilf((float)(y1 - y0) * cache->raster_scale) +
                  (int)ceilf(2.0f * cache->bitmap_scale);
  if (cache->line_height < 1) cache->line_height = 1;
  if (cache->cell_w < 1) cache->cell_w = 1;
  if (cache->cell_h < 1) cache->cell_h = 1;
  cache->texture_w = FONT_ATLAS_COLS * cache->cell_w;
  cache->texture_h = FONT_ATLAS_ROWS * cache->cell_h;

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
  free(cache->ttf_data);
  free(cache);
}

static uint32_t supported_codepoint(font_cache_t *cache, uint32_t codepoint) {
  if (codepoint >= FONT_CODEPOINT_LIMIT ||
      stbtt_FindGlyphIndex(&cache->font, (int)codepoint) == 0) {
    if (stbtt_FindGlyphIndex(&cache->font, 0xFFFD) != 0) return 0xFFFD;
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
  if (cache->next_slot >= FONT_ATLAS_CAPACITY) {
    fprintf(stderr, "[font] atlas capacity exhausted at U+%04X\n", codepoint);
    fflush(stderr);
    return codepoint == '?' ? NULL : font_cache_get_glyph(cache, '?');
  }

  uint16_t slot = cache->next_slot++;
  font_cache_glyph_t *glyph = &cache->glyphs[slot];
  glyph->atlas_x = (slot % FONT_ATLAS_COLS) * cache->cell_w;
  glyph->atlas_y = (slot / FONT_ATLAS_COLS) * cache->cell_h;
  int advance, left_bearing, width, height, x_offset, y_offset;
  stbtt_GetCodepointHMetrics(&cache->font, (int)codepoint, &advance, &left_bearing);
  unsigned char *bitmap = stbtt_GetCodepointBitmap(&cache->font, 0,
                                                    cache->raster_scale,
                                                    (int)codepoint, &width, &height,
                                                    &x_offset, &y_offset);
  glyph->advance = (int16_t)lroundf((float)advance * cache->scale);
  glyph->x_offset = (int16_t)x_offset;
  glyph->y_offset = (int16_t)(cache->baseline + y_offset);
  if (width < 0) width = 0;
  if (height < 0) height = 0;
  glyph->width = (uint16_t)(width < cache->cell_w ? width : cache->cell_w);
  glyph->height = (uint16_t)(height < cache->cell_h ? height : cache->cell_h);
  cache->slots[codepoint] = slot + 1;

  if (bitmap && glyph->width && glyph->height) {
    R_UpdateTextureR8(cache->texture, glyph->atlas_x, glyph->atlas_y,
                      glyph->width, glyph->height, bitmap);
  }
  stbtt_FreeBitmap(bitmap, NULL);
  return glyph;
}

uint32_t font_cache_texture(const font_cache_t *cache) { return cache ? cache->texture : 0; }
int font_cache_texture_width(const font_cache_t *cache) { return cache ? cache->texture_w : 0; }
int font_cache_texture_height(const font_cache_t *cache) { return cache ? cache->texture_h : 0; }
int font_cache_line_height(const font_cache_t *cache) { return cache ? cache->line_height : 0; }
float font_cache_bitmap_scale(const font_cache_t *cache) { return cache ? cache->bitmap_scale : 1.0f; }
