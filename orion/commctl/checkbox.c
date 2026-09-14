#include <string.h>
#include <stdio.h>

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/rect.h>
#include <orion/user/theme.h>

// Helper function (will be moved to ui/user/window.c later)
extern window_t *get_root_window(window_t *window);

// Checkbox control window procedure
result_t win_checkbox(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      win->frame.w = MAX(win->frame.w,
                         text_strwidth(FONT_SMALL, win->title) +
                         CHECKBOX_BOX_SIZE + CHECKBOX_GAP);
      win->frame.h = MAX(win->frame.h,
                         text_char_height(FONT_SMALL) + 4);
      return true;
    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (m) {
        int desired_w = text_strwidth(FONT_SMALL, win->title) +
                        CHECKBOX_BOX_SIZE + CHECKBOX_GAP;
        int desired_h = MAX(text_char_height(FONT_SMALL) + 4, CHECKBOX_BOX_SIZE + 2);
        m->desired_w = MAX(win->frame.w, desired_w);
        m->desired_h = MAX(win->frame.h, desired_h);
      }
      return true;
    }
    case evPaint: {
      irect16_t local = {0, 0, win->frame.w, win->frame.h};
      irect16_t box = {
        local.x,
        local.y + MAX(0, (local.h - CHECKBOX_BOX_SIZE) / 2)+1,
        CHECKBOX_BOX_SIZE,
        CHECKBOX_BOX_SIZE
      };
      ctrl_state_t state = CTRL_NORMAL;
      if (g_ui_runtime.focused == win)                  state |= CTRL_FOCUSED;
      if (window_has_state(win, WINDOW_STATE_PRESSED))  state |= CTRL_PRESSED;
      if (window_has_state(win, WINDOW_STATE_HOVERED))  state |= CTRL_HOVER;
      if (window_has_state(win, WINDOW_STATE_DISABLED)) state |= CTRL_DISABLED;
      theme_draw(THEME_PART_CHECKBOX, box, state | (win->value ? CTRL_SELECTED : 0));
      irect16_t text_rect = {
        box.x + box.w + CHECKBOX_GAP,
        0,
        win->frame.w - box.w - CHECKBOX_GAP,
        win->frame.h
      };
      bool disabled = (state & CTRL_DISABLED) != 0;
      uint32_t text_col = disabled ? get_sys_color(brTextDisabled) : get_sys_color(brTextNormal);
      if (!disabled) {
        irect16_t shadow_rect = rect_offset(text_rect, TEXT_SHADOW_OFFSET, TEXT_SHADOW_OFFSET);
        draw_text_clipped(FONT_SMALL, win->title, &shadow_rect, get_sys_color(brDarkEdge), 0);
      }
      draw_text_clipped(FONT_SMALL, win->title, &text_rect, text_col, 0);
      return true;
    }
    case evMouseMove:
      track_mouse(win);
      if (!window_has_state(win, WINDOW_STATE_HOVERED)) {
        window_set_state(win, WINDOW_STATE_HOVERED, true);
        invalidate_window(win);
      }
      return false;
    case evMouseLeave:
      window_set_state(win, WINDOW_STATE_HOVERED, false);
      invalidate_window(win);
      return false;
    case evLeftButtonDown:
      window_set_state(win, WINDOW_STATE_PRESSED, true);
      invalidate_window(win);
      return true;
    case evLeftButtonUp:
      window_set_state(win, WINDOW_STATE_PRESSED, false);
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
        window_set_state(win, WINDOW_STATE_PRESSED, true);
        invalidate_window(win);
        return true;
      }
      return false;
    case evKeyUp:
      if (wparam == AX_KEY_ENTER || wparam == AX_KEY_SPACE) {
        window_set_state(win, WINDOW_STATE_PRESSED, false);
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
