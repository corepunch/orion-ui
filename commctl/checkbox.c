#include <string.h>
#include <stdio.h>

#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"

// Helper function (will be moved to ui/user/window.c later)
extern window_t *get_root_window(window_t *window);

// Checkbox control window procedure
result_t win_checkbox(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case kWindowMessageCreate:
      win->frame.w = MAX(win->frame.w, strwidth(win->title)+16);
      win->frame.h = MAX(win->frame.h, BUTTON_HEIGHT);
      return true;
    case kWindowMessagePaint:
      fill_rect(g_ui_runtime.focused == win?get_sys_color(kColorFocusRing):get_sys_color(kColorWindowBg), win->frame.x-2, win->frame.y-2, 14, 14);
      draw_button(MAKERECT(win->frame.x, win->frame.y, 10, 10), 1, 1, win->pressed);
      draw_text_small(win->title, win->frame.x + 17, win->frame.y + 3, get_sys_color(kColorDarkEdge));
      draw_text_small(win->title, win->frame.x + 16, win->frame.y + 2, get_sys_color(kColorTextNormal));
      if (win->value) {
        draw_icon8(icon8_checkbox, win->frame.x+1, win->frame.y+1, get_sys_color(kColorTextNormal));
      }
      return true;
    case kWindowMessageLeftButtonDown:
      win->pressed = true;
      invalidate_window(win);
      return true;
    case kWindowMessageLeftButtonUp:
      win->pressed = false;
      send_message(win, kButtonMessageSetCheck, !send_message(win, kButtonMessageGetCheck, 0, NULL), NULL);
      send_message(get_root_window(win), kWindowMessageCommand, MAKEDWORD(win->id, kButtonNotificationClicked), win);
      invalidate_window(win);
      return true;
    case kButtonMessageSetCheck:
      win->value = (wparam != kButtonStateUnchecked);
      return true;
    case kButtonMessageGetCheck:
      return win->value ? kButtonStateChecked : kButtonStateUnchecked;
    case kWindowMessageKeyDown:
      if (wparam == AX_KEY_ENTER || wparam == AX_KEY_SPACE) {
        win->pressed = true;
        invalidate_window(win);
        return true;
      }
      return false;
    case kWindowMessageKeyUp:
      if (wparam == AX_KEY_ENTER || wparam == AX_KEY_SPACE) {
        win->pressed = false;
        send_message(win, kButtonMessageSetCheck, !send_message(win, kButtonMessageGetCheck, 0, NULL), NULL);
        send_message(get_root_window(win), kWindowMessageCommand, MAKEDWORD(win->id, kButtonNotificationClicked), win);
        invalidate_window(win);
        return true;
      } else {
        return false;
      }
  }
  return false;
}
