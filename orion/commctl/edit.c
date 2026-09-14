#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/theme.h>
#include "commctl.h"

#define BUFFER_SIZE 512
#define TEXTEDIT_MIN_WIDTH  80

// Helper function (will be moved to ui/user/window.c later)
extern window_t *get_root_window(window_t *window);

// Text edit control window procedure
result_t win_textedit(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      win->frame.w = MAX(win->frame.w, text_strwidth(FONT_SMALL, win->title) + TEXTEDIT_PADDING_HORZ* 2);
      control_apply_predefined_height(win, "textedit");
      return true;
    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (m) {
        m->desired_w = MAX(TEXTEDIT_MIN_WIDTH,
                           text_strwidth(FONT_SMALL, win->title) + TEXTEDIT_PADDING_HORZ * 2);
        m->desired_h = control_predefined_height(win->flags);
      }
      return true;
    }
    case evArrange:
      return control_arrange_predefined_height(win, (layout_arrange_t *)lparam);
    case evPaint: {
      irect16_t local = {0, 0, win->frame.w, win->frame.h};
      ctrl_state_t state = g_ui_runtime.focused == win ? CTRL_FOCUSED : CTRL_NORMAL;
      if (window_has_state(win, WINDOW_STATE_DISABLED)) state |= CTRL_DISABLED;
      theme_draw(THEME_PART_FIELD, local, state);
      int th = text_char_height(FONT_SMALL);
      int text_x = TEXTEDIT_PADDING_HORZ;
      int text_y = (win->frame.h - th) / 2;
      draw_text(FONT_SMALL, win->title, text_x, text_y, get_sys_color(brTextNormal));
      if (g_ui_runtime.focused == win && window_has_state(win, WINDOW_STATE_EDITING)) {
        fill_rect(get_sys_color(brTextNormal),
                  R(text_x + text_strnwidth(FONT_SMALL, win->title, win->cursor_pos),
                    text_y,
                    2, th));
      }
      return true;
    }
#ifdef AX_PLATFORM_IOS
    case evKillFocus:
    case evDestroy:
      axSetTextInput(FALSE);
      return true;
#endif
    case evLeftButtonUp:
      if (g_ui_runtime.focused == win) {
        invalidate_window(win);
        window_set_state(win, WINDOW_STATE_EDITING, true);
#ifdef AX_PLATFORM_IOS
        axSetTextInput(TRUE);
#endif
        int text_x = TEXTEDIT_PADDING_HORZ;
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
          if (window_has_state(win, WINDOW_STATE_EDITING)) {
            send_message(get_root_window(win), evCommand, MAKEDWORD(win->id, edUpdate), win);
            window_set_state(win, WINDOW_STATE_EDITING, false);
          }
          return false;
        case AX_KEY_ENTER:
          if (!window_has_state(win, WINDOW_STATE_EDITING)) {
            win->cursor_pos = (int)strlen(win->title);
            window_set_state(win, WINDOW_STATE_EDITING, true);
          } else {
            send_message(get_root_window(win), evCommand, MAKEDWORD(win->id, edUpdate), win);
            window_set_state(win, WINDOW_STATE_EDITING, false);
          }
          break;
        case AX_KEY_ESCAPE:
          window_set_state(win, WINDOW_STATE_EDITING, false);
          break;
        case AX_KEY_BACKSPACE:
          if (win->cursor_pos > 0 && window_has_state(win, WINDOW_STATE_EDITING)) {
            memmove(win->title + win->cursor_pos - 1,
                    win->title + win->cursor_pos,
                    strlen(win->title + win->cursor_pos) + 1);
            win->cursor_pos--;
          }
          break;
        case AX_KEY_LEFTARROW:
          if (win->cursor_pos > 0 && window_has_state(win, WINDOW_STATE_EDITING)) {
            win->cursor_pos--;
          }
          break;
        case AX_KEY_RIGHTARROW:
          if (win->cursor_pos < strlen(win->title) && window_has_state(win, WINDOW_STATE_EDITING)) {
            win->cursor_pos++;
          }
          break;
        default:
          return window_has_state(win, WINDOW_STATE_EDITING);
      }
      invalidate_window(win);
      return true;

    case edGetText: {
      if (!lparam || wparam == 0)
        return (result_t)strlen(win->title);
      size_t sz = (size_t)wparam;
      strncpy((char *)lparam, win->title, sz - 1);
      ((char *)lparam)[sz - 1] = '\0';
      return (result_t)strlen((char *)lparam);
    }

    case edSetText:
      if (lparam) {
        strncpy(win->title, (const char *)lparam, BUFFER_SIZE - 1);
        win->title[BUFFER_SIZE - 1] = '\0';
        if ((size_t)win->cursor_pos > strlen(win->title))
          win->cursor_pos = (uint32_t)strlen(win->title);
        invalidate_window(win);
      }
      return true;

    case evGetCursor:
      return curIBeam;
  }
  return false;
}
