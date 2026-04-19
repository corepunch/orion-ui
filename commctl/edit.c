#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"

#define BUFFER_SIZE 64
#define PADDING 3

// Helper function (will be moved to ui/user/window.c later)
extern window_t *get_root_window(window_t *window);

// Text edit control window procedure
result_t win_textedit(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      win->frame.w = MAX(win->frame.w, strwidth(win->title)+PADDING*2);
      win->frame.h = MAX(win->frame.h, 13);
      return true;
    case evPaint:
      fill_rect(g_ui_runtime.focused == win?get_sys_color(brFocusRing):get_sys_color(brWindowBg), win->frame.x-1, win->frame.y-1, win->frame.w+2, win->frame.h+2);
      draw_button(&win->frame, 1, 1, true);
      draw_text_small(win->title, win->frame.x+PADDING, win->frame.y+PADDING, get_sys_color(brTextNormal));
      if (g_ui_runtime.focused == win && win->editing) {
        fill_rect(get_sys_color(brTextNormal),
                  win->frame.x+PADDING+strnwidth(win->title, win->cursor_pos),
                  win->frame.y+PADDING,
                  2, 8);
      }
      return true;
    case evLeftButtonUp:
      if (g_ui_runtime.focused == win) {
        invalidate_window(win);
        win->editing = true;
        win->cursor_pos = 0;
        for (int i = 0; i <= (int)strlen(win->title); i++) {
          int x1 = PADDING+strnwidth(win->title, i);
          int x2 = PADDING+strnwidth(win->title, win->cursor_pos);
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
            send_message(get_root_window(win), evCommand, MAKEDWORD(win->id, kEditNotificationUpdate), win);
            win->editing = false;
          }
          return false;
        case AX_KEY_ENTER:
          if (!win->editing) {
            win->cursor_pos = (int)strlen(win->title);
            win->editing = true;
          } else {
            send_message(get_root_window(win), evCommand, MAKEDWORD(win->id, kEditNotificationUpdate), win);
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
