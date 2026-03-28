// Text-to-canvas rendering using stb_truetype.
// Rasterizes a UTF-8 string into the canvas pixel buffer.

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "imageeditor.h"

// Font search paths (in order of preference).
// The bundled Inconsolata-Regular.ttf is tried first from standard share
// locations (mirroring the fallback strategy used for tools.png), then
// system-installed fonts are tried as a last resort.
static const char * const k_font_paths[] = {
  // Bundled font – copied to build/share/ by `make share`
  "build/share/Inconsolata-Regular.ttf",          // run from repository root
  "../share/Inconsolata-Regular.ttf",             // run from build/bin/
  "share/Inconsolata-Regular.ttf",                // run from build/
  "examples/imageeditor/Inconsolata-Regular.ttf", // fallback: source tree
  // System-installed fonts (last resort)
  "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
  "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
  "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
  "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
  "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
  NULL
};

// Cached font state – loaded once on first use, held for the lifetime of the
// process.  Avoids repeated disk I/O and stbtt_InitFont() overhead on every
// text insertion.
static uint8_t      *s_font_data   = NULL;
static stbtt_fontinfo s_font;
static bool           s_font_ready = false;

static bool ensure_font(void) {
  if (s_font_ready) return true;
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
      free(data); fclose(fp); continue;
    }
    fclose(fp);
    if (!stbtt_InitFont(&s_font, data, 0)) { free(data); continue; }
    s_font_data  = data;
    s_font_ready = true;
    return true;
  }
  return false;
}

// Decode one UTF-8 codepoint from *pp and advance *pp past it.
// Returns the Unicode codepoint, or '?' for an invalid byte sequence.
static int utf8_next(const char **pp) {
  const unsigned char *p = (const unsigned char *)*pp;
  int cp;
  if (*p < 0x80) {
    cp = *p; *pp += 1;
  } else if ((*p & 0xE0) == 0xC0 &&
             p[1] && (p[1] & 0xC0) == 0x80) {
    cp = (p[0] & 0x1F) << 6 | (p[1] & 0x3F); *pp += 2;
  } else if ((*p & 0xF0) == 0xE0 &&
             p[1] && (p[1] & 0xC0) == 0x80 &&
             p[2] && (p[2] & 0xC0) == 0x80) {
    cp = (p[0] & 0x0F) << 12 | (p[1] & 0x3F) << 6 | (p[2] & 0x3F); *pp += 3;
  } else if ((*p & 0xF8) == 0xF0 &&
             p[1] && (p[1] & 0xC0) == 0x80 &&
             p[2] && (p[2] & 0xC0) == 0x80 &&
             p[3] && (p[3] & 0xC0) == 0x80) {
    cp = (p[0] & 0x07) << 18 | (p[1] & 0x3F) << 12 | (p[2] & 0x3F) << 6 | (p[3] & 0x3F); *pp += 4;
  } else {
    cp = '?'; *pp += 1;  // invalid or truncated sequence: skip byte
  }
  return cp;
}

// Render a UTF-8 string into the canvas at canvas position (cx, cy).
// The y coordinate is the top of the text (ascent line).
// Returns true if at least one pixel was written, false otherwise.
bool canvas_draw_text_stb(canvas_doc_t *doc, int cx, int cy,
                           const text_options_t *opts) {
  if (!opts || !opts->text[0]) return false;
  if (!ensure_font()) return false;

  float scale = stbtt_ScaleForPixelHeight(&s_font, (float)opts->font_size);

  int ascent, descent, line_gap;
  stbtt_GetFontVMetrics(&s_font, &ascent, &descent, &line_gap);
  int baseline_y = cy + (int)(ascent * scale);

  bool drew = false;
  int pen_x = cx;
  const char *p = opts->text;
  while (*p) {
    int cp = utf8_next(&p);

    // Determine glyph bitmap offsets and dimensions
    int bmp_w, bmp_h, bmp_ox, bmp_oy;
    uint8_t *bmp = stbtt_GetCodepointBitmap(
        &s_font, scale, scale, cp,
        &bmp_w, &bmp_h, &bmp_ox, &bmp_oy);

    if (bmp) {
      int dst_x0 = pen_x + bmp_ox;
      int dst_y0 = baseline_y + bmp_oy;

      for (int row = 0; row < bmp_h; row++) {
        for (int col = 0; col < bmp_w; col++) {
          int px = dst_x0 + col;
          int py = dst_y0 + row;
          if (!canvas_in_bounds(doc, px, py)) continue;

          uint8_t alpha = bmp[row * bmp_w + col];
          if (!opts->antialias) {
            // Hard threshold: fully opaque or fully transparent
            alpha = (alpha > 127) ? 255 : 0;
          }
          if (alpha == 0) continue;

          // Alpha-blend text color over the existing canvas pixel
          uint32_t bg = canvas_get_pixel(doc, px, py);
          uint32_t fg = opts->color;
          uint8_t inv = (uint8_t)(255 - alpha);
          canvas_set_pixel(doc, px, py, MAKE_COLOR(
            (uint8_t)((COLOR_R(fg) * alpha + COLOR_R(bg) * inv) / 255),
            (uint8_t)((COLOR_G(fg) * alpha + COLOR_G(bg) * inv) / 255),
            (uint8_t)((COLOR_B(fg) * alpha + COLOR_B(bg) * inv) / 255),
            255
          ));
          drew = true;
        }
      }
      stbtt_FreeBitmap(bmp, NULL);
    }

    // Advance pen by glyph advance width
    int advance, lsb;
    stbtt_GetCodepointHMetrics(&s_font, cp, &advance, &lsb);
    pen_x += (int)(advance * scale);

    // Kerning between this codepoint and the next
    if (*p) {
      const char *peek = p;
      int next_cp = utf8_next(&peek);
      pen_x += (int)(stbtt_GetCodepointKernAdvance(&s_font, cp, next_cp) * scale);
    }
  }

  return drew;
}
