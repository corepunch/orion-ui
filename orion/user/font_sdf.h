#ifndef __UI_FONT_SDF_H__
#define __UI_FONT_SDF_H__

#include <stdbool.h>
#include <stdint.h>

// Signed-distance-field glyph atlas for resolution-independent text: one
// atlas renders any size, scale or rotation crisply, and its distance can be
// thickened, outlined or glowed by the shader. CPU only; the caller uploads
// the R8 atlas (see font_sdf_take_dirty) with its own renderer.
//
// Metrics are in pixels at the atlas base size; multiply by size / base.
// Edge value is 128; each texel of distance changes the value by 128 / padding.

typedef struct font_sdf_s font_sdf_t;

typedef struct {
  uint16_t x, y, w, h;      // atlas texels (w == 0 for blank glyphs)
  float x_offset, y_offset; // bitmap top-left from the pen at the baseline, y down
  float advance;
  int index;                // font glyph index (for kerning)
} font_sdf_glyph_t;

font_sdf_t *font_sdf_create(const char *path, float base_size, int padding, int atlas_size);
void font_sdf_destroy(font_sdf_t *font);

const font_sdf_glyph_t *font_sdf_glyph(font_sdf_t *font, uint32_t codepoint);
float font_sdf_kern(font_sdf_t *font, const font_sdf_glyph_t *a, const font_sdf_glyph_t *b);
void font_sdf_vmetrics(const font_sdf_t *font, float *ascent, float *descent, float *line_gap);
float font_sdf_base_size(const font_sdf_t *font);
int font_sdf_padding(const font_sdf_t *font);
const uint8_t *font_sdf_pixels(const font_sdf_t *font, int *width, int *height);
// Returns the atlas region changed since the last call, if any.
bool font_sdf_take_dirty(font_sdf_t *font, int *x, int *y, int *w, int *h);
// Decodes one UTF-8 codepoint and advances *text; invalid bytes yield U+FFFD.
uint32_t font_sdf_utf8_next(const char **text);

#endif
