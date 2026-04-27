#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"

#define BUFFER_SIZE 512

// Helper function (will be moved to ui/user/window.c later)
extern window_t *get_root_window(window_t *window);

// Text edit control window procedure
result_t win_textedit(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      win->frame.w = MAX(win->frame.w, text_strwidth(FONT_SMALL, win->title) + 6);
      win->frame.h = MAX(win->frame.h, 13);
      return true;
    case evPaint: {
      rect_t local = {0, 0, win->frame.w, win->frame.h};
      fill_rect(g_ui_runtime.focused == win?get_sys_color(brFocusRing):get_sys_color(brWindowBg),
                R(-1, -1, win->frame.w+2, win->frame.h+2));
      draw_button(&local, 1, 1, true);
      int tw = text_strwidth(FONT_SMALL, win->title);
      int th = text_char_height(FONT_SMALL);
      rect_t label = rect_center(local, tw, th);
      draw_text(FONT_SMALL, win->title, label.x, label.y, get_sys_color(brTextNormal));
      if (g_ui_runtime.focused == win && win->editing) {
        fill_rect(get_sys_color(brTextNormal),
                  R(label.x + text_strnwidth(FONT_SMALL, win->title, win->cursor_pos),
                    label.y,
                    2, th));
      }
      return true;
    }
    case evLeftButtonUp:
      if (g_ui_runtime.focused == win) {
        invalidate_window(win);
        win->editing = true;
        int text_x = (win->frame.w - text_strwidth(FONT_SMALL, win->title)) / 2;
        win->cursor_pos = 0;
        for (int i = 0; i <= (int)strlen(win->title); i++) {
          int x1 = text_x + text_strnwidth(FONT_SMALL, win->title, i);
          int x2 = text_x + text_strnwidth(FONT_SMALL, win->title, win->cursor_pos);
          if (abs((int)LOWORD(wparam) - x1) < abs((int)LOWORD(wparam) - x2)) {
            win->cursor_pos = i;
          }
        }
      }
      return true;
    case evTextInput:
      if (strlen(win->title) + strlen(lparam) < BUFFER_SIZE - 1) {
        memmove(win->title + win->cursor_pos + 1,
                win->title + win->cursor_pos,
                strlen(win->title + win->cursor_pos) + 1);
        win->title[win->cursor_pos] = *(char *)lparam; // Only handle 1-byte characters
        win->cursor_pos++;
      }
      invalidate_window(win);
      return true;
    case evKeyDown:
      switch (wparam) {
        case AX_KEY_TAB:
          if (win->editing) {
            send_message(get_root_window(win), evCommand, MAKEDWORD(win->id, edUpdate), win);
            win->editing = false;
          }
          return false;
        case AX_KEY_ENTER:
          if (!win->editing) {
            win->cursor_pos = (int)strlen(win->title);
            win->editing = true;
          } else {
            send_message(get_root_window(win), evCommand, MAKEDWORD(win->id, edUpdate), win);
            win->editing = false;
          }
          break;
        case AX_KEY_ESCAPE:
          win->editing = false;
          break;
        case AX_KEY_BACKSPACE:
          if (win->cursor_pos > 0 && win->editing) {
            memmove(win->title + win->cursor_pos - 1,
                    win->title + win->cursor_pos,
                    strlen(win->title + win->cursor_pos) + 1);
            win->cursor_pos--;
          }
          break;
        case AX_KEY_LEFTARROW:
          if (win->cursor_pos > 0 && win->editing) {
            win->cursor_pos--;
          }
          break;
        case AX_KEY_RIGHTARROW:
          if (win->cursor_pos < strlen(win->title) && win->editing) {
            win->cursor_pos++;
          }
          break;
        default:
          return win->editing;
      }
      invalidate_window(win);
      return true;
  }
  return false;
}
