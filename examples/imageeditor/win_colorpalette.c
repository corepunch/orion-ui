// Color palette floating window

#include "imageeditor.h"

#define SWATCH_COLS      4
#define SWATCH_W        (COLOR_WIN_W / SWATCH_COLS)

const uint32_t kPalette[NUM_COLORS] = {
#if USE_EGA
  MAKE_COLOR(0x00,0x00,0x00,0xFF), // black
  MAKE_COLOR(0x00,0x00,0xAA,0xFF), // blue
  MAKE_COLOR(0x00,0xAA,0x00,0xFF), // green
  MAKE_COLOR(0x00,0xAA,0xAA,0xFF), // cyan
  MAKE_COLOR(0xAA,0x00,0x00,0xFF), // red
  MAKE_COLOR(0xAA,0x00,0xAA,0xFF), // magenta
  MAKE_COLOR(0xAA,0x55,0x00,0xFF), // brown
  MAKE_COLOR(0xAA,0xAA,0xAA,0xFF), // light gray
  MAKE_COLOR(0x55,0x55,0x55,0xFF), // dark gray
  MAKE_COLOR(0x55,0x55,0xFF,0xFF), // light blue
  MAKE_COLOR(0x55,0xFF,0x55,0xFF), // light green
  MAKE_COLOR(0x55,0xFF,0xFF,0xFF), // light cyan
  MAKE_COLOR(0xFF,0x55,0x55,0xFF), // light red
  MAKE_COLOR(0xFF,0x55,0xFF,0xFF), // light magenta
  MAKE_COLOR(0xFF,0xFF,0x55,0xFF), // yellow
  MAKE_COLOR(0xFF,0xFF,0xFF,0xFF), // white
#else
  MAKE_COLOR(0x0F,0x17,0x2A,0xFF), // black (slate-900)
  MAKE_COLOR(0x25,0x63,0xEB,0xFF), // blue  (blue-600)
  MAKE_COLOR(0x16,0xA3,0x4A,0xFF), // green (green-600)
  MAKE_COLOR(0x06,0xB6,0xD4,0xFF), // cyan  (cyan-500)
  MAKE_COLOR(0xDC,0x26,0x26,0xFF), // red   (red-600)
  MAKE_COLOR(0xC0,0x26,0xD3,0xFF), // magenta (fuchsia-600)
  MAKE_COLOR(0xB4,0x53,0x09,0xFF), // brown (amber-700-ish)
  MAKE_COLOR(0xD1,0xD5,0xDB,0xFF), // light gray gray-300
  MAKE_COLOR(0x47,0x55,0x69,0xFF), // dark gray (slate-600)
  MAKE_COLOR(0x60,0xA5,0xFA,0xFF), // light blue (blue-400)
  MAKE_COLOR(0x4A,0xDE,0x80,0xFF), // light green (green-400)
  MAKE_COLOR(0x67,0xE8,0xF9,0xFF), // light cyan (cyan-300)
  MAKE_COLOR(0xFB,0x71,0x71,0xFF), // light red (red-400)
  MAKE_COLOR(0xE8,0x79,0xF9,0xFF), // light magenta (fuchsia-400)
  MAKE_COLOR(0xFA,0xD8,0x35,0xFF), // yellow (yellow-400)
  MAKE_COLOR(0xF8,0xFA,0xFC,0xFF), // white (slate-50)
#endif
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
