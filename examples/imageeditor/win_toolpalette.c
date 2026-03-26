// Tool palette floating window

#include "imageeditor.h"

#define TOOL_ROW_H     24
#define SWATCH_H       22

result_t win_tool_palette_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam) {
  switch (msg) {
    case kWindowMessagePaint: {
      fill_rect(COLOR_PANEL_DARK_BG, 0, 0, win->frame.w, win->frame.h);
      fill_rect(COLOR_DARK_EDGE, win->frame.w - 1, 0, 1, win->frame.h);
      fill_rect(COLOR_DARK_EDGE, 0, win->frame.h - 1, win->frame.w, 1);

      for (int i = 0; i < NUM_TOOLS; i++) {
        int ty = i * TOOL_ROW_H;
        bool active = g_app && (g_app->current_tool == i);
        if (active)
          fill_rect(COLOR_FOCUSED, 1, ty, win->frame.w - 2, TOOL_ROW_H - 1);
        draw_text_small(tool_names[i], 4, ty + 7,
                        active ? COLOR_PANEL_BG : COLOR_TEXT_NORMAL);
      }

      int sy = NUM_TOOLS * TOOL_ROW_H + 2;
      draw_text_small("FG", 2, sy, COLOR_TEXT_DISABLED);
      draw_text_small("BG", 34, sy, COLOR_TEXT_DISABLED);
      sy += 8;
      if (g_app) {
        fill_rect(COLOR_DARK_EDGE, 1,  sy - 1, 28, 14);
        fill_rect(rgba_to_col(g_app->fg_color), 2,  sy, 26, 12);
        fill_rect(COLOR_DARK_EDGE, 33, sy - 1, 28, 14);
        fill_rect(rgba_to_col(g_app->bg_color), 34, sy, 26, 12);
      }
      return true;
    }

    case kWindowMessageLeftButtonDown: {
      if (!g_app) return true;
      int ly = (int16_t)HIWORD(wparam);
      if (ly >= 0 && ly < NUM_TOOLS * TOOL_ROW_H) {
        int idx = ly / TOOL_ROW_H;
        if (idx >= 0 && idx < NUM_TOOLS) {
          g_app->current_tool = (tool_id_t)idx;
          invalidate_window(win);
        }
      }
      return true;
    }

    default:
      return false;
  }
}
