// Color palette floating window

#include "imageeditor.h"

#define SWATCH_COLS      8
#define SWATCH_W        (COLOR_WIN_W / SWATCH_COLS)

const uint32_t kPalette[NUM_COLORS] = {
  MAKE_COLOR(0xFF,0xFF,0xFF,0xFF), // white
  MAKE_COLOR(0xCC,0xCC,0xCC,0xFF), // light gray
  MAKE_COLOR(0x88,0x88,0x88,0xFF), // gray
  MAKE_COLOR(0x44,0x44,0x44,0xFF), // dark gray
  MAKE_COLOR(0x00,0x00,0x00,0xFF), // black
  MAKE_COLOR(0xFF,0x00,0x00,0xFF), // red
  MAKE_COLOR(0xFF,0x88,0x00,0xFF), // orange
  MAKE_COLOR(0xFF,0xFF,0x00,0xFF), // yellow
  MAKE_COLOR(0x00,0xFF,0x00,0xFF), // green
  MAKE_COLOR(0x00,0x88,0x44,0xFF), // dark green
  MAKE_COLOR(0x00,0xFF,0xFF,0xFF), // cyan
  MAKE_COLOR(0x00,0x88,0xFF,0xFF), // sky blue
  MAKE_COLOR(0x00,0x00,0xFF,0xFF), // blue
  MAKE_COLOR(0x88,0x00,0xFF,0xFF), // purple
  MAKE_COLOR(0xFF,0x00,0xFF,0xFF), // magenta
  MAKE_COLOR(0xFF,0x44,0x88,0xFF), // pink
};

result_t win_color_palette_proc(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam) {
  switch (msg) {
    case kWindowMessagePaint: {
      fill_rect(COLOR_PANEL_DARK_BG, 0, 0, win->frame.w, win->frame.h);
      fill_rect(COLOR_DARK_EDGE, win->frame.w - 1, 0, 1, win->frame.h);

      for (int i = 0; i < NUM_COLORS; i++) {
        int col = i % SWATCH_COLS;
        int row = i / SWATCH_COLS;
        int sx = col * SWATCH_W + 1;
        int sy = row * SWATCH_ROW_H + 1;
        fill_rect(kPalette[i], sx, sy, SWATCH_W - 2, SWATCH_ROW_H - 2);

        if (g_app && g_app->fg_color == kPalette[i])
          fill_rect(COLOR_FOCUSED, sx, sy, SWATCH_W - 2, 2);
      }
      return true;
    }

    case kWindowMessageLeftButtonDown: {
      if (!g_app) return true;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      int col = lx / SWATCH_W;
      int row = ly / SWATCH_ROW_H;
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
      int col = lx / SWATCH_W;
      int row = ly / SWATCH_ROW_H;
      int idx = row * SWATCH_COLS + col;
      if (idx >= 0 && idx < NUM_COLORS) {
        uint32_t result;
        if (show_color_picker(win, kPalette[idx], &result)) {
          g_app->fg_color = result;
          invalidate_window(win);
          if (g_app->tool_win) invalidate_window(g_app->tool_win);
        }
      }
      return true;
    }

    default:
      return false;
  }
}
