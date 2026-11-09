// Screen management functions for the terminal.
// These handle the scrollback ring buffer and cursor state.

#include "vgat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void vgat_screen_init(vgat_screen *s, int rows, int cols) {
  memset(s, 0, sizeof(*s));
  s->total_rows = VGAT_SCROLLBACK_LINES;
  s->cols = cols;
  s->rows = (vgat_cell *)calloc((size_t)s->total_rows * (size_t)cols, sizeof(vgat_cell));
  s->head = 0;
  s->cursor_col = 0;
  s->cursor_row = 0;
  s->scroll_pos = 0;
  s->cursor_visible = true;
}

// A new width: the ring is copied row by row in logical order, clipped or
// padded to the new width.
void vgat_screen_resize(vgat_screen *s, int cols) {
  if (!s || !s->rows || cols <= 0 || cols == s->cols) return;
  vgat_cell *rows = (vgat_cell *)calloc((size_t)s->total_rows * (size_t)cols, sizeof(vgat_cell));
  if (!rows) {
    fprintf(stderr, "[terminal] screen resize failed: %d rows x %d cols\n", s->total_rows, cols);
    fflush(stderr);
    return;
  }
  int keep = cols < s->cols ? cols : s->cols;
  for (int r = 0; r < s->total_rows; r++)
    memcpy(rows + (size_t)r * cols, s->rows + (size_t)((s->head + r) % s->total_rows) * s->cols, (size_t)keep * sizeof(vgat_cell));
  free(s->rows);
  s->rows = rows;
  s->head = 0;
  s->cols = cols;
  if (s->cursor_col >= cols) s->cursor_col = cols - 1;
}

void vgat_screen_shutdown(vgat_screen *s) {
  if (!s) return;
  free(s->rows);
  s->rows = NULL;
  s->total_rows = 0;
  s->cols = 0;
}

void vgat_screen_clear(vgat_screen *s) {
  if (!s || !s->rows) return;
  memset(s->rows, 0, (size_t)s->total_rows * s->cols * sizeof(vgat_cell));
  s->head = 0;
  s->cursor_col = 0;
  s->cursor_row = 0;
  s->max_row = 0;
  s->scroll_pos = 0;
}

static int phys_row(vgat_screen *s, int logical_row) {
  if (logical_row < 0) logical_row = 0;
  if (logical_row >= s->total_rows) logical_row = s->total_rows - 1;
  return (s->head + logical_row) % s->total_rows;
}

void vgat_screen_scroll(vgat_screen *s) {
  if (!s || !s->rows) return;
  s->head = (s->head + 1) % s->total_rows;
  int row = phys_row(s, 0);
  memset(&s->rows[row * s->cols], 0, (size_t)s->cols * sizeof(vgat_cell));
}

void vgat_screen_write_cell(vgat_screen *s, uint32_t codepoint, int fg, int bg) {
  if (!s || !s->rows) return;
  int row = phys_row(s, s->cursor_row);
  int idx = row * s->cols + s->cursor_col;
  s->rows[idx].codepoint = codepoint;
  s->rows[idx].fg = (uint8_t)(fg & 0xFF);
  s->rows[idx].bg = (uint8_t)(bg & 0xFF);
  s->cursor_col++;
  if (s->cursor_col >= s->cols) {
    s->cursor_col = 0;
    s->cursor_row++;
    if (s->cursor_row > s->max_row) s->max_row = s->cursor_row;
    while (s->cursor_row >= s->total_rows) {
      vgat_screen_scroll(s);
      s->cursor_row--;
    }
  }
}

void vgat_screen_newline(vgat_screen *s) {
  if (!s) return;
  s->cursor_col = 0;
  s->cursor_row++;
  if (s->cursor_row > s->max_row) s->max_row = s->cursor_row;
  while (s->cursor_row >= s->total_rows) {
    vgat_screen_scroll(s);
    s->cursor_row--;
  }
}

void vgat_screen_backspace(vgat_screen *s) {
  if (!s) return;
  if (s->cursor_col > 0)        { s->cursor_col--; }
  else if (s->cursor_row > 0)   { s->cursor_row--; s->cursor_col = s->cols - 1; }
}

void vgat_screen_carriage_return(vgat_screen *s) {
  if (!s) return;
  s->cursor_col = 0;
}

void vgat_screen_cursor_left(vgat_screen *s) {
  if (!s) return;
  if (s->cursor_col > 0) s->cursor_col--;
}

void vgat_screen_cursor_right(vgat_screen *s) {
  if (!s) return;
  if (s->cursor_col < s->cols - 1) s->cursor_col++;
}

void vgat_screen_cursor_up(vgat_screen *s) {
  if (!s) return;
  if (s->cursor_row > 0) s->cursor_row--;
}

void vgat_screen_cursor_down(vgat_screen *s) {
  if (!s) return;
  if (s->cursor_row < s->total_rows - 1) {
    s->cursor_row++;
    if (s->cursor_row > s->max_row) s->max_row = s->cursor_row;
  }
}

void vgat_screen_set_fg(vgat_screen *s, int fg) {
  (void)s; (void)fg;
}

void vgat_screen_set_bg(vgat_screen *s, int bg) {
  (void)s; (void)bg;
}

void vgat_screen_reset(vgat_screen *s) {
  if (!s) return;
  vgat_screen_clear(s);
  s->cursor_visible = true;
}

void vgat_screen_save_cursor(vgat_screen *s) {
  if (!s) return;
  s->saved_cursor_col = s->cursor_col;
  s->saved_cursor_row = s->cursor_row;
  s->saved_cursor_visible = s->cursor_visible;
}

void vgat_screen_restore_cursor(vgat_screen *s) {
  if (!s) return;
  s->cursor_col = s->saved_cursor_col;
  s->cursor_row = s->saved_cursor_row;
  s->cursor_visible = s->saved_cursor_visible;
}

void vgat_screen_erase_display(vgat_screen *s, int mode) {
  if (!s || !s->rows) return;
  if (mode == 2) {
    memset(s->rows, 0, (size_t)s->total_rows * s->cols * sizeof(vgat_cell));
    s->head = 0;
    s->cursor_col = 0;
    s->cursor_row = 0;
    s->max_row = 0;
    return;
  }
  if (mode == 0) {
    // Erase from cursor to end of display
    int r = phys_row(s, s->cursor_row);
    memset(&s->rows[r * s->cols + s->cursor_col], 0,
           (size_t)(s->cols - s->cursor_col) * sizeof(vgat_cell));
    for (int lr = s->cursor_row + 1; lr < s->total_rows; lr++) {
      r = phys_row(s, lr);
      memset(&s->rows[r * s->cols], 0, (size_t)s->cols * sizeof(vgat_cell));
    }
    return;
  }
  if (mode == 1) {
    // Erase from beginning of display to cursor
    for (int lr = 0; lr < s->cursor_row; lr++) {
      int r = phys_row(s, lr);
      memset(&s->rows[r * s->cols], 0, (size_t)s->cols * sizeof(vgat_cell));
    }
    int r = phys_row(s, s->cursor_row);
    memset(&s->rows[r * s->cols], 0, (size_t)s->cursor_col * sizeof(vgat_cell));
    return;
  }
}

void vgat_screen_erase_line(vgat_screen *s, int mode) {
  if (!s || !s->rows) return;
  int row = phys_row(s, s->cursor_row);
  if (mode == 0 || mode == 2) {
    memset(&s->rows[row * s->cols + s->cursor_col], 0,
           (size_t)(s->cols - s->cursor_col) * sizeof(vgat_cell));
  }
  if (mode == 1 || mode == 2) {
    memset(&s->rows[row * s->cols], 0, (size_t)s->cursor_col * sizeof(vgat_cell));
  }
}

void vgat_screen_cursor_position(vgat_screen *s, int row, int col) {
  if (!s) return;
  if (row < 0) row = 0;
  if (row >= s->total_rows) row = s->total_rows - 1;
  if (col < 0) col = 0;
  if (col >= s->cols) col = s->cols - 1;
  s->cursor_row = row;
  s->cursor_col = col;
  if (row + 1 > s->max_row) s->max_row = row + 1;
}

void vgat_screen_write_string(vgat_screen *s, const char *str, int fg, int bg) {
  if (!s || !str) return;
  for (const char *p = str; *p; p++)
    if (*p == '\n') vgat_screen_newline(s);
    else            vgat_screen_write_cell(s, (uint8_t)*p, fg, bg);
}
