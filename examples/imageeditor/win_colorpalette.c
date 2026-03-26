// Color palette floating window

#define COLOR_HEADER_H  12
#define SWATCH_COLS      2
#define SWATCH_W        (PALETTE_WIN_W / SWATCH_COLS)
#define SWATCH_ROW_H    22

const rgba_t kPalette[NUM_COLORS] = {
  {0xFF,0xFF,0xFF,0xFF}, // white
  {0xCC,0xCC,0xCC,0xFF}, // light gray
  {0x88,0x88,0x88,0xFF}, // gray
  {0x44,0x44,0x44,0xFF}, // dark gray
  {0x00,0x00,0x00,0xFF}, // black
  {0xFF,0x00,0x00,0xFF}, // red
  {0xFF,0x88,0x00,0xFF}, // orange
  {0xFF,0xFF,0x00,0xFF}, // yellow
  {0x00,0xFF,0x00,0xFF}, // green
  {0x00,0x88,0x44,0xFF}, // dark green
  {0x00,0xFF,0xFF,0xFF}, // cyan
  {0x00,0x88,0xFF,0xFF}, // sky blue
  {0x00,0x00,0xFF,0xFF}, // blue
  {0x88,0x00,0xFF,0xFF}, // purple
  {0xFF,0x00,0xFF,0xFF}, // magenta
  {0xFF,0x44,0x88,0xFF}, // pink
};

result_t win_color_palette_proc(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam) {
  switch (msg) {
    case kWindowMessageCreate:
      return true;

    case kWindowMessagePaint: {
      fill_rect(COLOR_PANEL_DARK_BG, 0, 0, win->frame.w, win->frame.h);
      fill_rect(COLOR_DARK_EDGE, win->frame.w - 1, 0, 1, win->frame.h);

      draw_text_small("Colors", 4, 2, COLOR_TEXT_DISABLED);
      fill_rect(COLOR_DARK_EDGE, 0, COLOR_HEADER_H - 1, win->frame.w, 1);

      for (int i = 0; i < NUM_COLORS; i++) {
        int col = i % SWATCH_COLS;
        int row = i / SWATCH_COLS;
        int sx = col * SWATCH_W + 1;
        int sy = COLOR_HEADER_H + row * SWATCH_ROW_H + 1;
        fill_rect(rgba_to_col(kPalette[i]), sx, sy, SWATCH_W - 2, SWATCH_ROW_H - 2);

        if (g_app && rgba_eq(g_app->fg_color, kPalette[i]))
          fill_rect(COLOR_FOCUSED, sx, sy, SWATCH_W - 2, 2);
      }
      return true;
    }

    case kWindowMessageLeftButtonDown: {
      if (!g_app) return true;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      if (ly < COLOR_HEADER_H) return true;
      int col = lx / SWATCH_W;
      int row = (ly - COLOR_HEADER_H) / SWATCH_ROW_H;
      int idx = row * SWATCH_COLS + col;
      if (idx >= 0 && idx < NUM_COLORS) {
        g_app->fg_color = kPalette[idx];
        invalidate_window(win);
        if (g_app->tool_win) invalidate_window(g_app->tool_win);
      }
      return true;
    }

    case kWindowMessageRightButtonDown: {
      if (!g_app) return true;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      if (ly < COLOR_HEADER_H) return true;
      int col = lx / SWATCH_W;
      int row = (ly - COLOR_HEADER_H) / SWATCH_ROW_H;
      int idx = row * SWATCH_COLS + col;
      if (idx >= 0 && idx < NUM_COLORS) {
        g_app->bg_color = kPalette[idx];
        if (g_app->tool_win) invalidate_window(g_app->tool_win);
      }
      return true;
    }

    default:
      return false;
  }
}
