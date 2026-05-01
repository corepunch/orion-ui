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
      irect16_t local = {0, 0, win->frame.w, win->frame.h};
      irect16_t box = rect_split_left(local, win->frame.h);
      irect16_t focus_bg = rect_inset(box, -CHECKBOX_FOCUS_PAD);
      fill_rect(g_ui_runtime.focused == win ? get_sys_color(brFocusRing) : get_sys_color(brWindowBg),
                focus_bg);
      draw_button(box, 1, 1, win->pressed);
      irect16_t text_rect = {
        box.x + box.w + CHECKBOX_GAP,
        0,
        win->frame.w - box.w - CHECKBOX_GAP,
        win->frame.h
      };
      irect16_t shadow_rect = rect_offset(text_rect, TEXT_SHADOW_OFFSET, TEXT_SHADOW_OFFSET);
      draw_text_clipped(FONT_SYSTEM, win->title, &shadow_rect, get_sys_color(brDarkEdge), 0);
      draw_text_clipped(FONT_SYSTEM, win->title, &text_rect, get_sys_color(brTextNormal), 0);
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
