#include "font_sdf.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tools/stb_truetype.h>

#define FONT_SDF_CODEPOINTS 0x10000
#define FONT_SDF_MAX_GLYPHS 4096
#define FONT_SDF_EDGE 128
#define FONT_SDF_GAP 1

struct font_sdf_s {
  uint8_t *ttf;
  stbtt_fontinfo info;
  float base, scale, ascent, descent, line_gap;
  int padding, size;
  uint8_t *pixels;
  int shelf_x, shelf_y, shelf_h;
  int dirty_x0, dirty_y0, dirty_x1, dirty_y1;
  int nglyphs;
  uint16_t slots[FONT_SDF_CODEPOINTS];
  font_sdf_glyph_t glyphs[FONT_SDF_MAX_GLYPHS];
};

static uint8_t *font_sdf_read(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) return NULL;
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  rewind(file);
  uint8_t *data = size > 0 && size <= 32 * 1024 * 1024 ? malloc((size_t)size) : NULL;
  if (data && fread(data, 1, (size_t)size, file) != (size_t)size) { free(data); data = NULL; }
  fclose(file);
  return data;
}

font_sdf_t *font_sdf_create(const char *path, float base_size, int padding, int atlas_size) {
  if (!path || base_size <= 0 || padding < 1 || atlas_size < 64) {
    fprintf(stderr, "[font_sdf] rejected create path=%s base=%g padding=%d atlas=%d\n",
            path ? path : "(null)", base_size, padding, atlas_size);
    fflush(stderr);
    return NULL;
  }
  font_sdf_t *font = calloc(1, sizeof(*font));
  if (!font) return NULL;
  font->ttf = font_sdf_read(path);
  int offset = font->ttf ? stbtt_GetFontOffsetForIndex(font->ttf, 0) : -1;
  font->pixels = calloc((size_t)atlas_size * (size_t)atlas_size, 1);
  if (offset < 0 || !font->pixels || !stbtt_InitFont(&font->info, font->ttf, offset)) {
    fprintf(stderr, "[font_sdf] failed to load %s\n", path);
    fflush(stderr);
    font_sdf_destroy(font);
    return NULL;
  }
  int ascent, descent, line_gap;
  stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &line_gap);
  font->base = base_size;
  font->scale = stbtt_ScaleForMappingEmToPixels(&font->info, base_size);
  font->ascent = ascent * font->scale;
  font->descent = descent * font->scale;
  font->line_gap = line_gap * font->scale;
  font->padding = padding;
  font->size = atlas_size;
  font->dirty_x0 = font->dirty_y0 = atlas_size;
  return font;
}

void font_sdf_destroy(font_sdf_t *font) {
  if (!font) return;
  free(font->pixels);
  free(font->ttf);
  free(font);
}

static bool font_sdf_place(font_sdf_t *font, int w, int h, int *x, int *y) {
  if (font->shelf_x + w + FONT_SDF_GAP > font->size) {
    font->shelf_y += font->shelf_h + FONT_SDF_GAP;
    font->shelf_x = font->shelf_h = 0;
  }
  if (w + FONT_SDF_GAP > font->size || font->shelf_y + h + FONT_SDF_GAP > font->size) return false;
  *x = font->shelf_x + FONT_SDF_GAP;
  *y = font->shelf_y + FONT_SDF_GAP;
  font->shelf_x += w + FONT_SDF_GAP;
  if (h > font->shelf_h) font->shelf_h = h;
  return true;
}

const font_sdf_glyph_t *font_sdf_glyph(font_sdf_t *font, uint32_t codepoint) {
  if (!font) return NULL;
  if (codepoint >= FONT_SDF_CODEPOINTS || !stbtt_FindGlyphIndex(&font->info, (int)codepoint))
    codepoint = stbtt_FindGlyphIndex(&font->info, 0xFFFD) ? 0xFFFD : '?';
  if (font->slots[codepoint]) return &font->glyphs[font->slots[codepoint] - 1];
  if (font->nglyphs >= FONT_SDF_MAX_GLYPHS) {
    fprintf(stderr, "[font_sdf] glyph table full at U+%04X\n", codepoint);
    fflush(stderr);
    return NULL;
  }
  font_sdf_glyph_t *glyph = &font->glyphs[font->nglyphs];
  memset(glyph, 0, sizeof(*glyph));
  int advance, bearing, w = 0, h = 0, xoff = 0, yoff = 0;
  glyph->index = stbtt_FindGlyphIndex(&font->info, (int)codepoint);
  stbtt_GetGlyphHMetrics(&font->info, glyph->index, &advance, &bearing);
  glyph->advance = advance * font->scale;
  uint8_t *sdf = stbtt_GetGlyphSDF(&font->info, font->scale, glyph->index, font->padding,
                                   FONT_SDF_EDGE, (float)FONT_SDF_EDGE / font->padding,
                                   &w, &h, &xoff, &yoff);
  if (sdf && w > 0 && h > 0) {
    int x, y;
    if (!font_sdf_place(font, w, h, &x, &y)) {
      fprintf(stderr, "[font_sdf] atlas %dx%d full at U+%04X\n", font->size, font->size, codepoint);
      fflush(stderr);
      stbtt_FreeSDF(sdf, NULL);
      return NULL;
    }
    for (int row = 0; row < h; row++)
      memcpy(font->pixels + (size_t)(y + row) * font->size + x, sdf + (size_t)row * w, (size_t)w);
    glyph->x = (uint16_t)x; glyph->y = (uint16_t)y;
    glyph->w = (uint16_t)w; glyph->h = (uint16_t)h;
    glyph->x_offset = (float)xoff; glyph->y_offset = (float)yoff;
    if (x < font->dirty_x0) font->dirty_x0 = x;
    if (y < font->dirty_y0) font->dirty_y0 = y;
    if (x + w > font->dirty_x1) font->dirty_x1 = x + w;
    if (y + h > font->dirty_y1) font->dirty_y1 = y + h;
  }
  if (sdf) stbtt_FreeSDF(sdf, NULL);
  font->slots[codepoint] = (uint16_t)++font->nglyphs;
  return glyph;
}

float font_sdf_kern(font_sdf_t *font, const font_sdf_glyph_t *a, const font_sdf_glyph_t *b) {
  if (!font || !a || !b) return 0;
  return stbtt_GetGlyphKernAdvance(&font->info, a->index, b->index) * font->scale;
}

void font_sdf_vmetrics(const font_sdf_t *font, float *ascent, float *descent, float *line_gap) {
  if (ascent) *ascent = font ? font->ascent : 0;
  if (descent) *descent = font ? font->descent : 0;
  if (line_gap) *line_gap = font ? font->line_gap : 0;
}

float font_sdf_base_size(const font_sdf_t *font) { return font ? font->base : 0; }
int font_sdf_padding(const font_sdf_t *font) { return font ? font->padding : 0; }

const uint8_t *font_sdf_pixels(const font_sdf_t *font, int *width, int *height) {
  if (width) *width = font ? font->size : 0;
  if (height) *height = font ? font->size : 0;
  return font ? font->pixels : NULL;
}

bool font_sdf_take_dirty(font_sdf_t *font, int *x, int *y, int *w, int *h) {
  if (!font || font->dirty_x1 <= font->dirty_x0 || font->dirty_y1 <= font->dirty_y0) return false;
  *x = font->dirty_x0; *y = font->dirty_y0;
  *w = font->dirty_x1 - font->dirty_x0; *h = font->dirty_y1 - font->dirty_y0;
  font->dirty_x0 = font->dirty_y0 = font->size;
  font->dirty_x1 = font->dirty_y1 = 0;
  return true;
}

uint32_t font_sdf_utf8_next(const char **text) {
  const unsigned char *s = (const unsigned char *)*text;
  if (!s[0]) return 0;
  int n = s[0] < 0x80 ? 1 : (s[0] & 0xE0) == 0xC0 ? 2 : (s[0] & 0xF0) == 0xE0 ? 3 : (s[0] & 0xF8) == 0xF0 ? 4 : 0;
  if (!n) { (*text)++; return 0xFFFD; }
  uint32_t cp = n == 1 ? s[0] : s[0] & (0x3F >> (n - 1));
  for (int i = 1; i < n; i++) {
    if ((s[i] & 0xC0) != 0x80) { *text += i; return 0xFFFD; }
    cp = (cp << 6) | (s[i] & 0x3F);
  }
  *text += n;
  return cp;
}
