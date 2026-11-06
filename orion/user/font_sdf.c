#include "font_sdf.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H

#define FONT_SDF_CODEPOINTS 0x10000
#define FONT_SDF_MAX_GLYPHS 4096
#define FONT_SDF_GAP 1

struct font_sdf_s {
  FT_Library library;
  FT_Face face;
  float base, scale, ascent, descent, line_gap;
  int padding, size;
  uint8_t *pixels;
  int shelf_x, shelf_y, shelf_h;
  int dirty_x0, dirty_y0, dirty_x1, dirty_y1;
  int nglyphs;
  uint16_t slots[FONT_SDF_CODEPOINTS];
  font_sdf_glyph_t glyphs[FONT_SDF_MAX_GLYPHS];
};

font_sdf_t *font_sdf_create(const char *path, float base_size, int padding, int atlas_size) {
  if (!path || base_size <= 0 || padding < 2 || padding > 32 || atlas_size < 64) {
    fprintf(stderr, "[font_sdf] rejected create path=%s base=%g padding=%d atlas=%d\n",
            path ? path : "(null)", base_size, padding, atlas_size);
    fflush(stderr);
    return NULL;
  }
  font_sdf_t *font = calloc(1, sizeof(*font));
  if (!font) return NULL;
  font->pixels = calloc((size_t)atlas_size * (size_t)atlas_size, 1);
  if (!font->pixels || FT_Init_FreeType(&font->library) || FT_New_Face(font->library, path, 0, &font->face) ||
      FT_Set_Char_Size(font->face, 0, (FT_F26Dot6)lroundf(base_size * 64), 72, 72)) {
    fprintf(stderr, "[font_sdf] failed to load %s\n", path); fflush(stderr); font_sdf_destroy(font); return NULL;
  }
  FT_UInt spread = (FT_UInt)padding;
  if (FT_Property_Set(font->library, "sdf", "spread", &spread)) {
    fprintf(stderr, "[font_sdf] unsupported spread=%d\n", padding); fflush(stderr); font_sdf_destroy(font); return NULL;
  }
  font->base = base_size;
  font->scale = base_size / font->face->units_per_EM;
  font->ascent = font->face->ascender * font->scale;
  font->descent = font->face->descender * font->scale;
  font->line_gap = (font->face->height - font->face->ascender + font->face->descender) * font->scale;
  font->padding = padding;
  font->size = atlas_size;
  font->dirty_x0 = font->dirty_y0 = atlas_size;
  return font;
}

void font_sdf_destroy(font_sdf_t *font) {
  if (!font) return;
  free(font->pixels);
  if (font->face) FT_Done_Face(font->face);
  if (font->library) FT_Done_FreeType(font->library);
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
  if (codepoint >= FONT_SDF_CODEPOINTS || !FT_Get_Char_Index(font->face, codepoint))
    codepoint = FT_Get_Char_Index(font->face, 0xFFFD) ? 0xFFFD : '?';
  if (font->slots[codepoint]) return &font->glyphs[font->slots[codepoint] - 1];
  if (font->nglyphs >= FONT_SDF_MAX_GLYPHS) {
    fprintf(stderr, "[font_sdf] glyph table full at U+%04X\n", codepoint);
    fflush(stderr);
    return NULL;
  }
  font_sdf_glyph_t *glyph = &font->glyphs[font->nglyphs];
  memset(glyph, 0, sizeof(*glyph));
  glyph->index = FT_Get_Char_Index(font->face, codepoint);
  if (FT_Load_Glyph(font->face, glyph->index, FT_LOAD_NO_SCALE)) {
    fprintf(stderr, "[font_sdf] metrics failed U+%04X\n", codepoint); fflush(stderr); return NULL;
  }
  glyph->advance = font->face->glyph->advance.x * font->scale;
  if (FT_Load_Glyph(font->face, glyph->index, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP)) {
    fprintf(stderr, "[font_sdf] load failed U+%04X\n", codepoint); fflush(stderr); return NULL;
  }
  FT_GlyphSlot slot = font->face->glyph;
  if (slot->outline.n_contours && FT_Render_Glyph(slot, FT_RENDER_MODE_SDF)) {
    fprintf(stderr, "[font_sdf] render failed U+%04X\n", codepoint); fflush(stderr); return NULL;
  }
  int w = slot->bitmap.width, h = slot->bitmap.rows;
  if (w > 0 && h > 0) {
    int x, y;
    if (!font_sdf_place(font, w, h, &x, &y)) {
      fprintf(stderr, "[font_sdf] atlas %dx%d full at U+%04X\n", font->size, font->size, codepoint); fflush(stderr); return NULL;
    }
    for (int row = 0; row < h; row++)
      memcpy(font->pixels + (size_t)(y + row) * font->size + x, slot->bitmap.buffer + (size_t)row * slot->bitmap.pitch, (size_t)w);
    glyph->x = (uint16_t)x; glyph->y = (uint16_t)y; glyph->w = (uint16_t)w; glyph->h = (uint16_t)h;
    glyph->x_offset = (float)slot->bitmap_left; glyph->y_offset = (float)-slot->bitmap_top;
    if (x < font->dirty_x0) font->dirty_x0 = x;
    if (y < font->dirty_y0) font->dirty_y0 = y;
    if (x + w > font->dirty_x1) font->dirty_x1 = x + w;
    if (y + h > font->dirty_y1) font->dirty_y1 = y + h;
  }
  font->slots[codepoint] = (uint16_t)++font->nglyphs;
  return glyph;
}

float font_sdf_kern(font_sdf_t *font, const font_sdf_glyph_t *a, const font_sdf_glyph_t *b) {
  if (!font || !a || !b) return 0;
  FT_Vector delta = {0};
  if (FT_Get_Kerning(font->face, a->index, b->index, FT_KERNING_UNSCALED, &delta)) return 0;
  return delta.x * font->scale;
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
