// tools/gen_vga_font.c
//
// Headless tool that generates share/vga-rom-font-8x16.png — a 128x256 RGBA
// monospace character sheet with 256 glyphs arranged in a 16-column x 16-row
// grid (each cell is 8x16 pixels, white glyphs on transparent background).
//
// The source bitmap is the Orion built-in 6x8 console font
// (user/font_6x8.c, exported as console_font_6x8[]).  Each glyph is scaled
// to 8x16 by:
//   - Using all 8 bits of each row byte as-is (6 significant bits, 2 trailing
//     zeros → glyph left-aligned in the 8-pixel-wide cell).
//   - Doubling each of the 8 source rows → 16 destination rows.
//
// Glyphs 128-255 are left blank (the 6x8 font only covers 0-127).
//
// Usage:
//   gen_vga_font [output_path]
//   (default output path: share/vga-rom-font-8x16.png)
//
// Build: this tool is compiled by the standard Makefile tools rule and links
// against liborion.so.  It calls save_image_png() (user/image.h) which is a
// pure stb_image_write wrapper — no GL context required.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../user/image.h"

// Declared in user/font_6x8.c; available via liborion.so.
extern unsigned char console_font_6x8[];

#define GLYPH_W     8
#define GLYPH_H     16
#define GRID_COLS   16
#define GRID_ROWS   16
#define SHEET_W     (GRID_COLS * GLYPH_W)   /* 128 */
#define SHEET_H     (GRID_ROWS * GLYPH_H)   /* 256 */
#define SRC_ROWS    8   /* rows in the 6x8 source glyph */

int main(int argc, char *argv[]) {
  const char *out_path = (argc > 1) ? argv[1] : "share/vga-rom-font-8x16.png";

  // Allocate RGBA pixel buffer, initialised to transparent black.
  uint8_t *pixels = (uint8_t *)calloc((size_t)(SHEET_W * SHEET_H * 4), 1);
  if (!pixels) {
    fprintf(stderr, "gen_vga_font: out of memory\n");
    return 1;
  }

  for (int ch = 0; ch < 128; ch++) {
    // Position of this glyph's top-left corner in the sheet.
    int base_x = (ch & 0xF) * GLYPH_W;
    int base_y = (ch >> 4)  * GLYPH_H;

    for (int src_row = 0; src_row < SRC_ROWS; src_row++) {
      uint8_t row_bits = console_font_6x8[ch * SRC_ROWS + src_row];

      // Emit this row twice (vertical 2x scale).
      for (int dup = 0; dup < 2; dup++) {
        int dst_y = base_y + src_row * 2 + dup;

        for (int bit = 0; bit < GLYPH_W; bit++) {
          // MSB of row_bits = leftmost pixel (column 0).
          int pixel_on = (row_bits >> (GLYPH_W - 1 - bit)) & 1;
          if (!pixel_on) continue;

          int idx = (dst_y * SHEET_W + base_x + bit) * 4;
          pixels[idx + 0] = 0xFF;  /* R — white */
          pixels[idx + 1] = 0xFF;  /* G */
          pixels[idx + 2] = 0xFF;  /* B */
          pixels[idx + 3] = 0xFF;  /* A — fully opaque */
        }
      }
    }
  }

  if (!save_image_png(out_path, pixels, SHEET_W, SHEET_H)) {
    fprintf(stderr, "gen_vga_font: failed to write %s\n", out_path);
    free(pixels);
    return 1;
  }

  printf("gen_vga_font: wrote %s (%dx%d)\n", out_path, SHEET_W, SHEET_H);
  free(pixels);
  return 0;
}
