#include <string.h>
#include <stdio.h>

#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"

#define LIST_HEIGHT     13
#define LIST_X          3
#define LIST_Y          3
#define LIST_SELITEM    0x5001

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
      return true;
    case kWindowMessagePaint:
      for (uint32_t i = 0; i < cb->cursor_pos; i++) {
        if (i == win->cursor_pos) {
          fill_rect(get_sys_color(kColorTextNormal), 0, i*LIST_HEIGHT, win->frame.h, LIST_HEIGHT);
          draw_text_small(texts[i], LIST_X, i*LIST_HEIGHT+LIST_Y, get_sys_color(kColorWindowBg));
        } else {
          draw_text_small(texts[i], LIST_X, i*LIST_HEIGHT+LIST_Y, get_sys_color(kColorTextNormal));
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
      send_message(get_root_window(cb), kWindowMessageCommand, MAKEDWORD(cb->id, kComboBoxNotificationSelectionChange), cb);
      destroy_window(win);
      return true;
    case LIST_SELITEM:
      win->cursor_pos = wparam;
      return true;
  }
  return false;
}
