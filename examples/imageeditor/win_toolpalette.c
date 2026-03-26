// Tool palette floating window

#include "imageeditor.h"

#define TOOL_HEADER_H  14
#define TOOL_ROW_H     24
#define SWATCH_H       22

typedef struct {
  bool dragging;
} tool_palette_data_t;

result_t win_tool_palette_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam) {
  tool_palette_data_t *d = (tool_palette_data_t *)win->userdata;
  switch (msg) {
    case kWindowMessageCreate: {
      tool_palette_data_t *nd = malloc(sizeof(tool_palette_data_t));
      memset(nd, 0, sizeof(tool_palette_data_t));
      win->userdata = nd;
      d = nd;
      return true;
    }

    case kWindowMessagePaint: {
      fill_rect(COLOR_PANEL_DARK_BG, 0, 0, win->frame.w, win->frame.h);
      fill_rect(COLOR_DARK_EDGE, win->frame.w - 1, 0, 1, win->frame.h);
      fill_rect(COLOR_DARK_EDGE, 0, win->frame.h - 1, win->frame.w, 1);

      draw_text_small("Tools", 4, 3, COLOR_TEXT_DISABLED);
      fill_rect(COLOR_DARK_EDGE, 0, TOOL_HEADER_H - 1, win->frame.w, 1);

      for (int i = 0; i < NUM_TOOLS; i++) {
        int ty = TOOL_HEADER_H + i * TOOL_ROW_H;
        bool active = g_app && (g_app->current_tool == i);
        if (active)
          fill_rect(COLOR_FOCUSED, 1, ty, win->frame.w - 2, TOOL_ROW_H - 1);
        draw_text_small(tools[i]->name, 4, ty + 7,
                        active ? COLOR_PANEL_BG : COLOR_TEXT_NORMAL);
      }

      int sy = TOOL_HEADER_H + NUM_TOOLS * TOOL_ROW_H + 2;
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
      // Click in header – begin drag
      if (ly < TOOL_HEADER_H) {
        if (d) d->dragging = true;
        set_capture(win);
        return true;
      }
      if (ly >= TOOL_HEADER_H && ly < TOOL_HEADER_H + NUM_TOOLS * TOOL_ROW_H) {
        int idx = (ly - TOOL_HEADER_H) / TOOL_ROW_H;
        if (idx >= 0 && idx < NUM_TOOLS) {
          g_app->current_tool = idx;
          invalidate_window(win);
        }
      }
      return true;
    }

    case kWindowMessageMouseMove: {
      if (!d || !d->dragging) return false;
      int16_t dx = (int16_t)LOWORD((uint32_t)(intptr_t)lparam) / UI_WINDOW_SCALE;
      int16_t dy = (int16_t)HIWORD((uint32_t)(intptr_t)lparam) / UI_WINDOW_SCALE;
      move_window(win, win->frame.x + dx, win->frame.y + dy);
      return true;
    }

    case kWindowMessageLeftButtonUp: {
      if (d && d->dragging) {
        d->dragging = false;
        set_capture(NULL);
      }
      return true;
    }

    case kWindowMessageDestroy: {
      if (d && d->dragging) set_capture(NULL);
      free(d);
      win->userdata = NULL;
      return true;
    }

    default:
      return false;
  }
}
