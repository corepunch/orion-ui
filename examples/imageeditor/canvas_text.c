// Text-to-canvas rendering using stb_truetype.
// Rasterizes a TrueType glyph string into the canvas pixel buffer.

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "imageeditor.h"

// Font search paths (in order of preference).
// Mirrors the fallback strategy used for tools.png.
static const char * const k_font_paths[] = {
  "build/share/LiberationSans-Regular.ttf",
  "../share/LiberationSans-Regular.ttf",
  "share/LiberationSans-Regular.ttf",
  "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
  "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
  "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
  "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
  "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
  NULL
};

// Load a font file from the first accessible candidate path.
// Returns heap-allocated data (caller must free) or NULL on failure.
static uint8_t *load_font_data(void) {
  for (int i = 0; k_font_paths[i]; i++) {
    FILE *fp = fopen(k_font_paths[i], "rb");
    if (!fp) continue;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); continue; }
    long sz = ftell(fp);
    if (sz <= 0) { fclose(fp); continue; }
    rewind(fp);
    uint8_t *data = (uint8_t *)malloc((size_t)sz);
    if (!data) { fclose(fp); continue; }
    if ((long)fread(data, 1, (size_t)sz, fp) != sz) {
      free(data);
      fclose(fp);
      continue;
    }
    fclose(fp);
    return data;
  }
  return NULL;
}

// Render text into the canvas at canvas position (cx, cy).
// The y coordinate is the top of the text (ascent).
void canvas_draw_text_stb(canvas_doc_t *doc, int cx, int cy,
                           const text_options_t *opts) {
  if (!opts || !opts->text[0]) return;

  uint8_t *font_data = load_font_data();
  if (!font_data) return;  // no usable font found

  stbtt_fontinfo font;
  if (!stbtt_InitFont(&font, font_data, 0)) {
    free(font_data);
    return;
  }

  float scale = stbtt_ScaleForPixelHeight(&font, (float)opts->font_size);

  int ascent, descent, line_gap;
  stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
  int baseline_y = cy + (int)(ascent * scale);

  int pen_x = cx;
  const char *p = opts->text;
  while (*p) {
    int cp = (unsigned char)*p;

    // Determine glyph bitmap offsets and dimensions
    int bmp_w, bmp_h, bmp_ox, bmp_oy;
    uint8_t *bmp = stbtt_GetCodepointBitmap(
        &font, scale, scale, cp,
        &bmp_w, &bmp_h, &bmp_ox, &bmp_oy);

    if (bmp) {
      int dst_x0 = pen_x  + bmp_ox;
      int dst_y0 = baseline_y + bmp_oy;

      for (int row = 0; row < bmp_h; row++) {
        for (int col = 0; col < bmp_w; col++) {
          int px = dst_x0 + col;
          int py = dst_y0 + row;
          if (!canvas_in_bounds(px, py)) continue;

          uint8_t alpha = bmp[row * bmp_w + col];
          if (!opts->antialias) {
            // Hard threshold: fully opaque or fully transparent
            alpha = (alpha > 127) ? 255 : 0;
          }
          if (alpha == 0) continue;

          // Alpha-blend text color over the existing canvas pixel
          rgba_t bg = canvas_get_pixel(doc, px, py);
          rgba_t col_fg = opts->color;
          uint8_t inv = (uint8_t)(255 - alpha);
          rgba_t out = {
            (uint8_t)((col_fg.r * alpha + bg.r * inv) / 255),
            (uint8_t)((col_fg.g * alpha + bg.g * inv) / 255),
            (uint8_t)((col_fg.b * alpha + bg.b * inv) / 255),
            255
          };
          canvas_set_pixel(doc, px, py, out);
        }
      }
      stbtt_FreeBitmap(bmp, NULL);
    }

    // Advance pen by glyph width + kerning
    int advance, lsb;
    stbtt_GetCodepointHMetrics(&font, cp, &advance, &lsb);
    pen_x += (int)(advance * scale);

    // Kerning between this codepoint and the next
    if (*(p + 1)) {
      pen_x += (int)(stbtt_GetCodepointKernAdvance(
          &font, cp, (unsigned char)*(p + 1)) * scale);
    }

    p++;
  }

  free(font_data);
}
