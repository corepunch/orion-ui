#include "vga_text.h"
#include "ansi.h"
#include "../../kernel/renderer.h"
#include <stdlib.h>

// Placeholder character for UTF-8/UTF-16 multibyte sequences
#define UTF8_REPLACEMENT_CHAR 0xB7

int vga_text_utf8_length(unsigned char first_byte) {
  if (first_byte < 0x80) return 1;        // ASCII
  if ((first_byte & 0xE0) == 0xC0) return 2;  // 2-byte sequence (110xxxxx)
  if ((first_byte & 0xF0) == 0xE0) return 3;  // 3-byte sequence (1110xxxx)
  if ((first_byte & 0xF8) == 0xF0) return 4;  // 4-byte sequence (11110xxx)
  return 1;  // Invalid sequence, treat as single byte
}

void vga_text_set_cell(vga_text_grid_t *grid,
                       int x, int y,
                       uint8_t ch,
                       int fg_idx, int bg_idx) {
  if (!grid || !grid->cells || x < 0 || y < 0 || x >= grid->cells_w || y >= grid->cells_h)
    return;

  if (fg_idx < 0) fg_idx = 0;
  if (fg_idx > 15) fg_idx = 15;
  if (bg_idx < 0) bg_idx = 0;
  if (bg_idx > 15) bg_idx = 15;

  int i = (y * grid->cells_w + x) * 2;
  grid->cells[i + 0] = ch;
  grid->cells[i + 1] = (uint8_t)((bg_idx << 4) | fg_idx);
}

void vga_text_clear_grid(vga_text_grid_t *grid,
                         int fg_idx, int bg_idx) {
  if (!grid || !grid->cells || grid->cells_w <= 0 || grid->cells_h <= 0)
    return;
  uint8_t packed = (uint8_t)(((bg_idx & 0xF) << 4) | (fg_idx & 0xF));
  int n = grid->cells_w * grid->cells_h;
  for (int i = 0; i < n; i++) {
    grid->cells[i * 2 + 0] = (uint8_t)' ';
    grid->cells[i * 2 + 1] = packed;
  }
}

bool vga_text_ensure_grid(vga_text_grid_t *grid, int w, int h) {
  if (!grid || w <= 0 || h <= 0)
    return false;

  if (grid->cells && grid->cells_tex && grid->cells_w == w && grid->cells_h == h)
    return true;

  free(grid->cells);
  grid->cells = NULL;
  if (grid->cells_tex)
    R_DeleteTexture(grid->cells_tex);
  grid->cells_tex = 0;

  grid->cells = (uint8_t *)malloc((size_t)w * (size_t)h * 2u);
  if (!grid->cells) {
    grid->cells_w = grid->cells_h = 0;
    return false;
  }

  grid->cells_tex = R_CreateTextureRG8(w, h, NULL, R_FILTER_NEAREST, R_WRAP_CLAMP);
  if (!grid->cells_tex) {
    free(grid->cells);
    grid->cells = NULL;
    grid->cells_w = grid->cells_h = 0;
    return false;
  }

  grid->cells_w = w;
  grid->cells_h = h;
  return true;
}

void vga_text_free_grid(vga_text_grid_t *grid) {
  if (!grid) return;
  free(grid->cells);
  grid->cells = NULL;
  if (grid->cells_tex)
    R_DeleteTexture(grid->cells_tex);
  grid->cells_tex = 0;
  grid->cells_w = grid->cells_h = 0;
}

void vga_text_write_ansi_line(const char *line,
                              vga_text_grid_t *grid,
                              int row,
                              int col_start,
                              int max_cols,
                              uint32_t def_fg_col,
                              uint32_t def_bg_col) {
  if (!line || !grid || !grid->cells || row < 0 || row >= grid->cells_h || max_cols <= 0)
    return;

  int def_fg = nearest_ansi_index(def_fg_col);
  int def_bg = nearest_ansi_index(def_bg_col);
  int fg = def_fg;
  int bg = def_bg;
  bool bold = false;
  int out_col = 0;

  for (const char *p = line; *p && out_col < max_cols; ) {
    // Check for ANSI escape sequence
    if ((unsigned char)p[0] == 0x1B && p[1] == '[') {
      const char *q = p + 2;
      int val = 0;
      bool have_val = false;
      int codes[16];
      int n = 0;

      while (*q && *q != 'm') {
        if (*q >= '0' && *q <= '9') {
          have_val = true;
          val = val * 10 + (*q - '0');
        } else if (*q == ';') {
          if (n < 16)
            codes[n++] = have_val ? val : 0;
          val = 0;
          have_val = false;
        } else {
          break;
        }
        q++;
      }

      if (*q == 'm') {
        if (n < 16)
          codes[n++] = have_val ? val : 0;
        if (n == 0)
          ansi_apply_sgr(0, &fg, &bg, def_fg, def_bg, &bold);
        ansi_apply_sgr_codes(codes, n, &fg, &bg, def_fg, def_bg, &bold);
        fg = clamp_ansi_index(fg);
        bg = clamp_ansi_index(bg);
        p = q + 1;
        continue;
      }
    }

    unsigned char ch = (unsigned char)*p;
    if (ch < 0x80) {
      // ASCII character, use it directly
      vga_text_set_cell(grid, col_start + out_col, row, ch, fg, bg);
      p++;
    } else {
      // UTF-8/multi-byte character, replace with placeholder
      int seq_len = vga_text_utf8_length(ch);
      vga_text_set_cell(grid, col_start + out_col, row, UTF8_REPLACEMENT_CHAR, fg, bg);
      p += seq_len;
    }
    out_col++;
  }
}
