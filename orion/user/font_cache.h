#ifndef __UI_FONT_CACHE_H__
#define __UI_FONT_CACHE_H__

#include <stdbool.h>
#include <stdint.h>

typedef struct font_cache_s font_cache_t;

typedef struct {
  uint16_t atlas_x, atlas_y;
  int16_t x_offset, y_offset;
  uint16_t width, height;
  int16_t advance;
} font_cache_glyph_t;

// em_size and advance/line metrics use logical pixels; bitmap bounds use texels.
font_cache_t *font_cache_create(const char *path, float em_size);
void font_cache_destroy(font_cache_t *cache);

const font_cache_glyph_t *font_cache_get_glyph(font_cache_t *cache,
                                                uint32_t codepoint);
uint32_t font_cache_texture(const font_cache_t *cache);
int font_cache_texture_width(const font_cache_t *cache);
int font_cache_texture_height(const font_cache_t *cache);
int font_cache_line_height(const font_cache_t *cache);
float font_cache_bitmap_scale(const font_cache_t *cache);

#endif
