// Text-to-canvas rendering using FreeType.
// Rasterizes a UTF-8 string into the canvas pixel buffer.

#include <ft2build.h>
#include FT_FREETYPE_H
#include "imageeditor.h"

// System font fallback paths tried when the bundled font is unavailable.
static const char * const k_system_fonts[] = {
  "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
  "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
  "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
  "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
  NULL
};

static FT_Library s_font_library;
static FT_Face s_font;
static bool s_font_ready;

static bool try_load_font(const char *path) {
  if (!s_font_library && FT_Init_FreeType(&s_font_library)) return false;
  if (FT_New_Face(s_font_library, path, 0, &s_font)) return false;
  s_font_ready = true;
  return true;
}

static bool ensure_font(void) {
  if (s_font_ready) return true;

#ifdef SHAREDIR
  // Primary: bundled font in the per-application share directory,
  // resolved relative to the running executable's location.
  char bundled[4096];
  snprintf(bundled, sizeof(bundled), "%s/" SHAREDIR "/Inconsolata-Regular.ttf",
           ui_get_exe_dir());
  if (try_load_font(bundled)) return true;
#endif

  char default_font[4096];
  snprintf(default_font, sizeof(default_font), "%s/../share/orion/fonts/DejaVuSans.ttf", ui_get_exe_dir());
  if (try_load_font(default_font)) return true;

  // Fallback: system-installed fonts.
  for (int i = 0; k_system_fonts[i]; i++) {
    if (try_load_font(k_system_fonts[i])) return true;
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
bool canvas_draw_text(canvas_doc_t *doc, int cx, int cy,
                           const text_options_t *opts) {
  if (!opts || !opts->text[0]) return false;
  if (!ensure_font()) return false;

  float em = (float)opts->font_size * s_font->units_per_EM / (s_font->ascender - s_font->descender);
  if (FT_Set_Char_Size(s_font, 0, (FT_F26Dot6)lroundf(em * 64), 72, 72)) return false;
  int baseline_y = cy + (int)(s_font->ascender * em / s_font->units_per_EM);

  bool drew = false;
  int pen_x = cx;
  const char *p = opts->text;
  while (*p) {
    int cp = utf8_next(&p);

    if (FT_Load_Char(s_font, cp, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP | FT_LOAD_RENDER)) {
      fprintf(stderr, "[imageeditor] text glyph render failed U+%04X\n", cp); fflush(stderr); continue;
    }
    FT_GlyphSlot glyph = s_font->glyph;
    int bmp_w = glyph->bitmap.width, bmp_h = glyph->bitmap.rows;
    int bmp_ox = glyph->bitmap_left, bmp_oy = -glyph->bitmap_top;
    uint8_t *bmp = glyph->bitmap.buffer;

    if (bmp) {
      int dst_x0 = pen_x + bmp_ox;
      int dst_y0 = baseline_y + bmp_oy;

      for (int row = 0; row < bmp_h; row++) {
        for (int col = 0; col < bmp_w; col++) {
          int px = dst_x0 + col;
          int py = dst_y0 + row;
          if (!canvas_in_bounds(doc, px, py)) continue;

          uint8_t alpha = bmp[row * glyph->bitmap.pitch + col];
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
    }

    pen_x += (int)(glyph->advance.x / 64);
    if (*p) {
      const char *peek = p;
      FT_Vector kern = {0};
      int next_cp = utf8_next(&peek);
      if (!FT_Get_Kerning(s_font, FT_Get_Char_Index(s_font, cp), FT_Get_Char_Index(s_font, next_cp), FT_KERNING_DEFAULT, &kern))
        pen_x += (int)(kern.x / 64);
    }
  }

  return drew;
}
