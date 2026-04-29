#include <string.h>
#include <stdio.h>

#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"
#include "../user/rect.h"
#include "../user/theme.h"

// Helper function (will be moved to ui/user/window.c later)
extern window_t *get_root_window(window_t *window);

// Checkbox control window procedure
result_t win_checkbox(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      win->frame.w = MAX(win->frame.w, strwidth(win->title)+16);
      win->frame.h = MAX(win->frame.h, BUTTON_HEIGHT);
      return true;
    case evPaint: {
      rect_t local = {0, 0, win->frame.w, win->frame.h};
      rect_t box = rect_split_left(local, win->frame.h);
      rect_t focus_bg = rect_inset(box, -CHECKBOX_FOCUS_PAD);
      fill_rect(g_ui_runtime.focused == win ? get_sys_color(brFocusRing) : get_sys_color(brWindowBg),
                focus_bg);
      draw_button(box, 1, 1, win->pressed);
      int lx = box.x + box.w + CHECKBOX_GAP;
      int ly = CHECKBOX_TEXT_Y;
      draw_text_small(win->title, lx + TEXT_SHADOW_OFFSET, ly + TEXT_SHADOW_OFFSET, get_sys_color(brDarkEdge));
      draw_text_small(win->title, lx, ly, get_sys_color(brTextNormal));
      if (win->value) {
        draw_theme_icon_in_rect(THEME_ICON_CHECKMARK, box, get_sys_color(brTextNormal));
      }
      return true;
    }
    case evLeftButtonDown:
      win->pressed = true;
      invalidate_window(win);
      return true;
    case evLeftButtonUp:
      win->pressed = false;
      send_message(win, btnSetCheck, !send_message(win, btnGetCheck, 0, NULL), NULL);
      send_message(get_root_window(win), evCommand, MAKEDWORD(win->id, btnClicked), win);
      invalidate_window(win);
      return true;
    case btnSetCheck:
      win->value = (wparam != btnStateUnchecked);
      return true;
    case btnGetCheck:
      return win->value ? btnStateChecked : btnStateUnchecked;
    case evKeyDown:
      if (wparam == AX_KEY_ENTER || wparam == AX_KEY_SPACE) {
        win->pressed = true;
        invalidate_window(win);
        return true;
      }
      return false;
    case evKeyUp:
      if (wparam == AX_KEY_ENTER || wparam == AX_KEY_SPACE) {
        win->pressed = false;
        send_message(win, btnSetCheck, !send_message(win, btnGetCheck, 0, NULL), NULL);
        send_message(get_root_window(win), evCommand, MAKEDWORD(win->id, btnClicked), win);
        invalidate_window(win);
        return true;
      } else {
        return false;
      }
  }
  return false;
}
