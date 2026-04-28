// Color palette floating window

#include "imageeditor.h"

#define SWATCH_W        (COLOR_WIN_W / COLOR_SWATCH_COLS)

result_t win_color_palette_proc(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam) {
  switch (msg) {
    case evPaint: {
      fill_rect(get_sys_color(brWindowDarkBg), R(0, 0, win->frame.w, win->frame.h));
      fill_rect(get_sys_color(brDarkEdge), R(win->frame.w - 1, 0, 1, win->frame.h));
      if (!g_app) return true;

      for (int i = 0; i < NUM_COLORS; i++) {
        int col = i % COLOR_SWATCH_COLS;
        int row = i / COLOR_SWATCH_COLS;
        int sx = col * SWATCH_W + 1;
        int sy = row * SWATCH_ROW_H + 1;
        fill_rect(g_app->palette[i], R(sx, sy, SWATCH_W - 2, SWATCH_ROW_H - 2));

        if (g_app->fg_color == g_app->palette[i])
          fill_rect(get_sys_color(brFocusRing), R(sx, sy, SWATCH_W - 2, 2));
      }
      return true;
    }

    case evLeftButtonDown: {
      if (!g_app) return true;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      int col = lx / SWATCH_W;
      int row = ly / SWATCH_ROW_H;
      int idx = row * COLOR_SWATCH_COLS + col;
      if (idx >= 0 && idx < NUM_COLORS) {
        g_app->fg_color = g_app->palette[idx];
        invalidate_window(win);
        if (g_app->tool_win) invalidate_window(g_app->tool_win);
      }
      return true;
    }

    case evRightButtonDown: {
      if (!g_app) return true;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      int col = lx / SWATCH_W;
      int row = ly / SWATCH_ROW_H;
      int idx = row * COLOR_SWATCH_COLS + col;
      if (idx >= 0 && idx < NUM_COLORS) {
        uint32_t result;
        if (show_color_picker(win, g_app->palette[idx], &result)) {
          g_app->fg_color = result;
          invalidate_window(win);
          if (g_app->tool_win) invalidate_window(g_app->tool_win);
        }
      }
      return true;
    }

    case evDestroy:
      if (g_app && g_app->color_win == win) g_app->color_win = NULL;
      return false;

    default:
      return false;
  }
}
