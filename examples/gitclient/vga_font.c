// VGA-style monospace font renderer — implementation.
//
// See vga_font.h for the public API.
//
// Rendering approach (no custom shader required):
//   1. fill_rect(bg, &cell)            — opaque background per character
//   2. draw_sprite_region(tex, &cell,  — white glyph tinted with fg
//                         u0, v0, u1, v1, fg)
//
// The font sheet (128×256 RGBA, white glyphs on transparent background) is
// loaded via load_image() + R_CreateTextureRGBA(NEAREST, CLAMP).
// Transparent pixels are discarded by the sprite fragment shader
// (outColor.a < 0.1 → discard), so the solid bg fill shows through the
// inter-stroke gaps correctly.

#include "gitclient.h"
#include "vga_font.h"

#include "../../user/draw.h"
#include "../../user/image.h"
#include "../../kernel/renderer.h"

// ============================================================
// Module state
// ============================================================

static uint32_t g_vga_tex    = 0;   // GL texture ID; 0 = not loaded
static int      g_sheet_w    = 128;
static int      g_sheet_h    = 256;

// ============================================================
// Public: init / shutdown
// ============================================================

bool vga_font_init(const char *sheet_path) {
  if (g_vga_tex) return true;  // already loaded
  if (!sheet_path) return false;

  int w = 0, h = 0;
  uint8_t *pixels = load_image(sheet_path, &w, &h);
  if (!pixels) {
    GC_LOG("vga_font_init: could not load %s", sheet_path);
    return false;
  }

  g_vga_tex = R_CreateTextureRGBA(w, h, pixels,
                                   R_FILTER_NEAREST, R_WRAP_CLAMP);
  image_free(pixels);

  if (!g_vga_tex) {
    GC_LOG("vga_font_init: R_CreateTextureRGBA failed");
    return false;
  }

  g_sheet_w = w;
  g_sheet_h = h;
  GC_LOG("vga_font_init: loaded %s (%dx%d), tex=%u",
         sheet_path, w, h, g_vga_tex);
  return true;
}

void vga_font_shutdown(void) {
  if (g_vga_tex) {
    R_DeleteTexture(g_vga_tex);
    g_vga_tex = 0;
  }
}

bool vga_font_loaded(void) {
  return g_vga_tex != 0;
}

// ============================================================
// Internal: UV coordinates for a glyph
// ============================================================

static void glyph_uv(int ch, float *u0, float *v0, float *u1, float *v1) {
  // Sheet: 16 cols × 16 rows of GLYPH_W×GLYPH_H cells.
  // Character N: col = N & 0xF, row = N >> 4.
  int col = ch & 0xF;
  int row = ch >> 4;
  *u0 = (float)(col    ) / 16.0f;
  *u1 = (float)(col + 1) / 16.0f;
  *v0 = (float)(row    ) / 16.0f;
  *v1 = (float)(row + 1) / 16.0f;
}

// ============================================================
// Public: draw a single character cell
// ============================================================

void vga_draw_char(int ch, int x, int y, uint32_t fg, uint32_t bg) {
  rect_t cell = { x, y, VGA_CHAR_W, VGA_CHAR_H };

  // 1. Background (always, even when font not loaded).
  fill_rect(bg, &cell);

  // 2. Glyph.
  if (!g_vga_tex) return;

  // Clamp to 0-255; replace non-printable with block (0xDB in CP437, but
  // we use 0x7F which has a recognisable pixel pattern in most VGA fonts).
  if (ch < 0 || ch > 255) ch = 0x7F;

  float u0, v0, u1, v1;
  glyph_uv(ch, &u0, &v0, &u1, &v1);
  draw_sprite_region((int)g_vga_tex, &cell, u0, v0, u1, v1, fg);
}

// ============================================================
// Public: draw a string
// ============================================================

int vga_draw_textn(const char *text, int max_chars,
                   int x, int y, uint32_t fg, uint32_t bg) {
  if (!text || max_chars <= 0) return 0;
  int cx = x;
  int drawn = 0;
  for (const char *p = text; *p && drawn < max_chars; p++, drawn++) {
    vga_draw_char((unsigned char)*p, cx, y, fg, bg);
    cx += VGA_CHAR_W;
  }
  return cx - x;
}

int vga_draw_text(const char *text, int x, int y, uint32_t fg, uint32_t bg) {
  if (!text) return 0;
  return vga_draw_textn(text, (int)strlen(text), x, y, fg, bg);
}

int vga_text_width(int char_count) {
  return char_count * VGA_CHAR_W;
}
