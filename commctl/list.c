#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"
#include "../user/rect.h"

#define LIST_HEIGHT     13
#define LIST_X          3
#define LIST_Y          3

#define MAX_COMBOBOX_STRINGS 256
typedef char combobox_string_t[64];

// Helper functions (will be moved to ui/user/window.c later)
extern window_t *get_root_window(window_t *window);

// List control window procedure
result_t win_list(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  window_t *cb = win->userdata;
  combobox_string_t const *texts = cb?cb->userdata:NULL;
  switch (msg) {
    case kWindowMessageCreate:
      win->userdata = lparam;
      if (lparam) {
        window_t *owner = (window_t *)lparam;
        /* Allocate the full fixed-size title buffer so that the paired
           memcpy restore in the Escape handler copies exactly sizeof(title)
           bytes and leaves no stale data in the field. */
        win->userdata2 = malloc(sizeof(owner->title));
        if (win->userdata2)
          memcpy(win->userdata2, owner->title, sizeof(owner->title));
      }
      return true;
    case kWindowMessageDestroy:
      free(win->userdata2);
      win->userdata2 = NULL;
      return true;
    case kWindowMessagePaint:
      for (uint32_t i = 0; i < cb->cursor_pos; i++) {
        rect_t item = { 0, (int)(i * LIST_HEIGHT), win->frame.w, LIST_HEIGHT };
        rect_t text_pos = rect_inset_xy(item, LIST_X, LIST_Y);
        if (i == win->cursor_pos) {
          fill_rect(get_sys_color(kColorTextNormal), item.x, item.y, item.w, item.h);
          draw_text_small(texts[i], text_pos.x, text_pos.y, get_sys_color(kColorWindowBg));
        } else {
          draw_text_small(texts[i], text_pos.x, text_pos.y, get_sys_color(kColorTextNormal));
        }
      }
      return true;
    case kWindowMessageLeftButtonDown:
      win->cursor_pos = HIWORD(wparam)/LIST_HEIGHT;
      if (win->cursor_pos < cb->cursor_pos) {
        strncpy(cb->title, texts[win->cursor_pos], sizeof(cb->title));
      }
      invalidate_window(win);
      return true;
    case kWindowMessageLeftButtonUp:
      if (cb) set_focus(cb);
      send_message(get_root_window(cb), kWindowMessageCommand, MAKEDWORD(cb->id, kComboBoxNotificationSelectionChange), cb);
      destroy_window(win);
      return true;
    case kWindowMessageKeyDown: {
      uint32_t key = wparam;
      uint32_t count = cb ? cb->cursor_pos : 0;
      if (key == AX_KEY_UPARROW) {
        if (count > 0) {
          if (win->cursor_pos >= count)
            win->cursor_pos = count - 1;  // clamp out-of-range, don't decrement further
          else if (win->cursor_pos > 0)
            win->cursor_pos--;
          invalidate_window(win);
        }
        return true;
      }
      if (key == AX_KEY_DOWNARROW) {
        if (win->cursor_pos + 1 < count) {
          win->cursor_pos++;
          invalidate_window(win);
        } else if (win->cursor_pos >= count && count > 0) {
          win->cursor_pos = count - 1;  // clamp out-of-range to last item
          invalidate_window(win);
        }
        return true;
      }
      if (key == AX_KEY_ENTER || key == AX_KEY_KP_ENTER) {
        if (cb) {
          if (win->cursor_pos < cb->cursor_pos) {
            strncpy(cb->title, texts[win->cursor_pos], sizeof(cb->title));
            send_message(get_root_window(cb), kWindowMessageCommand, MAKEDWORD(cb->id, kComboBoxNotificationSelectionChange), cb);
          }
          set_focus(cb);
        }
        destroy_window(win);
        return true;
      }
      if (key == AX_KEY_ESCAPE) {
        if (cb) {
          if (win->userdata2)
            memcpy(cb->title, win->userdata2, sizeof(cb->title));
          set_focus(cb);
          invalidate_window(cb);
        }
        destroy_window(win);
        return true;
      }
      return false;
    }
    case kListMessageSetItem:
      win->cursor_pos = (cb && wparam < cb->cursor_pos) ? wparam : 0;
      return true;
  }
  return false;
}
