#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/rect.h>
#include <orion/user/theme.h>
#include "commctl.h"
#include "popup_item.h"

#define LIST_HEIGHT     POPUP_ITEM_HEIGHT

static void list_sync_scroll(window_t *win) {
  window_t *cb = win->userdata;
  window_set_scroll_content(win, 0, cb ? (int)cb->cursor_pos * LIST_HEIGHT : 0,
                            (irect16_t){0, MENU_START_Y, 0, MENU_START_Y}, (irect16_t){0});
}

static irect16_t list_item_rect(window_t *win, int index) {
  irect16_t content = window_scroll_content_rect(win);
  return R(content.x, content.y + index * LIST_HEIGHT, content.w, LIST_HEIGHT);
}

static void list_reveal_selection(window_t *win) {
  window_t *cb = win->userdata;
  if (cb && win->cursor_pos < cb->cursor_pos)
    window_ensure_rect_visible(win, list_item_rect(win, win->cursor_pos), SB_VERT);
}

static bool list_point_inside(window_t *win, uint32_t wparam) {
  irect16_t point = window_scroll_rect_to_client(win, R((int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam), 0, 0));
  return rect_contains_point(R(0, 0, win->frame.w, win->frame.h), (ipoint16_t){point.x, point.y});
}

static void list_cancel(window_t *win) {
  window_t *cb = win ? win->userdata : NULL;
  if (cb) {
    if (win->userdata2)
      memcpy(cb->title, win->userdata2, sizeof(cb->title));
    set_focus(cb);
    invalidate_window(cb);
  }
  destroy_window(win);
}

// List control window procedure
result_t win_list(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  window_t *cb = win->userdata;
  combobox_state_t *cb_state = cb ? (combobox_state_t *)cb->userdata : NULL;
  combobox_string_t const *texts = cb_state ? cb_state->texts : NULL;
  
  switch (msg) {
    case evCreate:
      return true;
    case evDestroy:
      free(win->userdata2);
      win->userdata2 = NULL;
      return true;
    case evPaint:
      if (!cb || !texts)
        return true;
      theme_draw(THEME_PART_MENU_POPUP,
          get_client_rect(win), CTRL_NORMAL);
      for (uint32_t i = 0; i < cb->cursor_pos; i++) {
        irect16_t item = window_scroll_rect_to_client(win, list_item_rect(win, i));
        popup_item_paint(item, texts[i], i == win->cursor_pos ? CTRL_SELECTED : CTRL_NORMAL, FONT_SMALL);
      }
      return true;
    case evLeftButtonDown: {
      if (!cb || !texts)
        return true;
      if (!list_point_inside(win, wparam)) {
        list_cancel(win);
        return true;
      }
      int y = (int16_t)HIWORD(wparam) - window_scroll_content_rect(win).y;
      if (y < 0 || y >= (int)cb->cursor_pos * LIST_HEIGHT) return true;
      win->cursor_pos = y / LIST_HEIGHT;
      if (win->cursor_pos < cb->cursor_pos) {
        window_set_state(win, WINDOW_STATE_PRESSED, true);
      } else {
        window_set_state(win, WINDOW_STATE_PRESSED, false);
      }
      invalidate_window(win);
      return true;
    }
    case evLeftButtonUp:
      if (!list_point_inside(win, wparam)) {
        list_cancel(win);
        return true;
      }
      if (!window_has_state(win, WINDOW_STATE_PRESSED))
        return true;
      window_set_state(win, WINDOW_STATE_PRESSED, false);
      if (cb) {
        if (win->cursor_pos < cb->cursor_pos) {
          strncpy(cb->title, texts[win->cursor_pos], sizeof(cb->title));
          cb->title[sizeof(cb->title) - 1] = '\0';
          invalidate_window(cb);
        }
        set_focus(cb);
        send_message(get_root_window(cb), evCommand,
                     MAKEDWORD(cb->id, cbSelectionChange), cb);
      }
      destroy_window(win);
      return true;
    case evKeyDown: {
      uint32_t key = wparam;
      uint32_t count = cb ? cb->cursor_pos : 0;
      if (key == AX_KEY_UPARROW) {
        if (count > 0) {
          if (win->cursor_pos >= count)
            win->cursor_pos = count - 1;  // clamp out-of-range, don't decrement further
          else if (win->cursor_pos > 0)
            win->cursor_pos--;
          list_reveal_selection(win);
          invalidate_window(win);
        }
        return true;
      }
      if (key == AX_KEY_DOWNARROW) {
        if (win->cursor_pos + 1 < count) {
          win->cursor_pos++;
          list_reveal_selection(win);
          invalidate_window(win);
        } else if (win->cursor_pos >= count && count > 0) {
          win->cursor_pos = count - 1;  // clamp out-of-range to last item
          list_reveal_selection(win);
          invalidate_window(win);
        }
        return true;
      }
      if (key == AX_KEY_ENTER || key == AX_KEY_KP_ENTER) {
        if (cb) {
          if (win->cursor_pos < cb->cursor_pos) {
            strncpy(cb->title, texts[win->cursor_pos], sizeof(cb->title));
            send_message(get_root_window(cb), evCommand, MAKEDWORD(cb->id, cbSelectionChange), cb);
          }
          set_focus(cb);
        }
        destroy_window(win);
        return true;
      }
      if (key == AX_KEY_ESCAPE) {
        list_cancel(win);
        return true;
      }
      return false;
    }
    case lstSetItem:
      win->cursor_pos = (cb && wparam < cb->cursor_pos) ? wparam : 0;
      list_sync_scroll(win);
      list_reveal_selection(win);
      return true;
    case evResize:
      list_sync_scroll(win);
      return true;
    case evVScroll:
      invalidate_window(win);
      return true;
  }
  return false;
}
