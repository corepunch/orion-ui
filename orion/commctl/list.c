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

// Helper functions (will be moved to ui/user/window.c later)
extern window_t *get_root_window(window_t *window);

static void list_sync_scroll(window_t *win) {
  window_t *cb = win ? win->userdata : NULL;
  int content_h = cb ? MENU_START_Y * 2 + (int)cb->cursor_pos * LIST_HEIGHT : 0;
  int page_h = win ? get_client_rect(win).h : 0;
  scroll_info_t si = { SIF_ALL, 0, content_h, page_h, win ? (int)win->vscroll.pos : 0 };
  set_scroll_info(win, SB_VERT, &si, false);
}

static void list_scroll_to_item(window_t *win) {
  window_t *cb = win ? win->userdata : NULL;
  if (!cb || win->cursor_pos >= cb->cursor_pos)
    return;
  int pos = (int)win->vscroll.pos;
  int page_h = get_client_rect(win).h;
  int top = (int)win->cursor_pos * LIST_HEIGHT;
  int bottom = top + LIST_HEIGHT + MENU_START_Y * 2;
  if (top < pos) pos = top;
  else if (bottom > pos + page_h) pos = bottom - page_h;
  win->vscroll.pos = (uint32_t)MAX(0, pos);
  list_sync_scroll(win);
}

static bool list_point_inside(window_t *win, uint32_t wparam) {
  int x = (int16_t)LOWORD(wparam) - (int)win->hscroll.pos;
  int y = (int16_t)HIWORD(wparam) - (int)win->vscroll.pos;
  return x >= 0 && y >= 0 && x < win->frame.w && y < win->frame.h;
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
      int item_w = get_client_rect(win).w;
      theme_draw(THEME_PART_MENU_POPUP,
          R(0, win->vscroll.pos, item_w, get_client_rect(win).h), CTRL_NORMAL);
      for (uint32_t i = 0; i < cb->cursor_pos; i++) {
        irect16_t item = { 0, MENU_START_Y + (int)(i * LIST_HEIGHT), item_w, LIST_HEIGHT };
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
      int y = (int16_t)HIWORD(wparam) - MENU_START_Y;
      if (y < 0 || y >= (int)cb->cursor_pos * LIST_HEIGHT) return true;
      win->cursor_pos = y / LIST_HEIGHT;
      fprintf(stderr, "[list] select win=%u index=%u\n", win->id, win->cursor_pos);
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
          list_scroll_to_item(win);
          invalidate_window(win);
        }
        return true;
      }
      if (key == AX_KEY_DOWNARROW) {
        if (win->cursor_pos + 1 < count) {
          win->cursor_pos++;
          list_scroll_to_item(win);
          invalidate_window(win);
        } else if (win->cursor_pos >= count && count > 0) {
          win->cursor_pos = count - 1;  // clamp out-of-range to last item
          list_scroll_to_item(win);
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
      list_scroll_to_item(win);
      return true;
    case evVScroll:
      win->vscroll.pos = wparam;
      list_sync_scroll(win);
      invalidate_window(win);
      return true;
  }
  return false;
}
