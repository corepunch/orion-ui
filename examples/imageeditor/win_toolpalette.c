// Tool palette floating window

#include "imageeditor.h"

#define TOOL_ROW_H     24
#define SWATCH_H       22

result_t win_tool_palette_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam) {
  switch (msg) {
    case kWindowMessageCreate: {
      // Create one PUSHLIKE + AUTORADIO button per tool.
      // The initial tool (TOOL_PENCIL) starts checked.
      for (int i = 0; i < NUM_TOOLS; i++) {
        window_t *btn = create_window(
            tool_names[i],
            WINDOW_NOTITLE | WINDOW_NOFILL | BUTTON_PUSHLIKE | BUTTON_AUTORADIO,
            MAKERECT(1, i * TOOL_ROW_H, win->frame.w - 2, TOOL_ROW_H - 2),
            win, win_button, NULL);
        btn->id    = (uint16_t)(ID_TOOL_PENCIL + i);
        btn->value = (i == TOOL_PENCIL);
        show_window(btn, true);
      }
      return true;
    }

    case kWindowMessagePaint: {
      fill_rect(COLOR_PANEL_DARK_BG, 0, 0, win->frame.w, win->frame.h);
      fill_rect(COLOR_DARK_EDGE, win->frame.w - 1, 0, 1, win->frame.h);
      fill_rect(COLOR_DARK_EDGE, 0, win->frame.h - 1, win->frame.w, 1);

      // Color swatches below the tool buttons
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
      return false; // allow children (buttons) to paint themselves
    }

    case kWindowMessageCommand: {
      // Button children send kWindowMessageCommand with kButtonNotificationClicked
      // to their root window (this window).  Forward the command to the menubar
      // so that both button clicks and accelerator hotkeys are handled in one place.
      if (HIWORD(wparam) == kButtonNotificationClicked && g_app) {
        send_message(g_app->menubar_win, kWindowMessageCommand,
                     MAKEDWORD(LOWORD(wparam), kButtonNotificationClicked), lparam);
      }
      return true;
    }

    default:
      return false;
  }
}
