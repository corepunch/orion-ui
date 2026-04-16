#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"

#define MAX_COMBOBOX_STRINGS 256
typedef char combobox_string_t[64];

// Forward declare list control procedure  
extern result_t win_list(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
extern result_t win_button(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

// Helper functions (will be moved to ui/user/window.c later)
extern window_t *get_root_window(window_t *window);
extern int titlebar_height(window_t const *win);
extern void show_window(window_t *win, bool visible);

// Combobox control window procedure
result_t win_combobox(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  combobox_string_t *texts = win->userdata;
  switch (msg) {
    case kWindowMessageCreate:
      win_button(win, msg, wparam, lparam);
      win->frame.w = MAX(win->frame.w, strwidth(win->title)+16);
      win->userdata = malloc(sizeof(combobox_string_t) * MAX_COMBOBOX_STRINGS);
      return true;
    case kWindowMessageDestroy:
      free(win->userdata);
      return true;
    case kWindowMessagePaint:
      win_button(win, msg, wparam, lparam);
      draw_icon8(icon8_maximize, win->frame.x+win->frame.w-10, win->frame.y+(win->frame.h-8)/2, get_sys_color(kColorTextNormal));
      return true;
    case kWindowMessageLeftButtonUp: {
      win_button(win, msg, wparam, lparam);
      window_t *root = get_root_window(win);
      int root_t = titlebar_height(root);
      rect_t rect = {
        root->frame.x + win->frame.x,
        root->frame.y + root_t + win->frame.y + win->frame.h + 2,
        win->frame.w,
        100,
      };
      window_t *list = create_window("", WINDOW_NOTITLE|WINDOW_NORESIZE|WINDOW_VSCROLL|WINDOW_ALWAYSONTOP|WINDOW_NOTRAYBUTTON, &rect, NULL, win_list, win->hinstance, win);
      result_t sel = send_message(win, kComboBoxMessageGetCurrentSelection, 0, NULL);
      send_message(list, 0x5001 /*LIST_SELITEM*/, (uint32_t)sel, NULL);
      show_window(list, true);
      set_capture(list);
      return true;
    }
    case kComboBoxMessageAddString:
      if (win->cursor_pos < MAX_COMBOBOX_STRINGS) {
        strncpy(texts[win->cursor_pos++], lparam, sizeof(combobox_string_t));
        strncpy(win->title, lparam, sizeof(win->title));
        return true;
      } else {
        return false;
      }
    case kComboBoxMessageGetListBoxText:
      if (wparam < win->cursor_pos) {
        strcpy(lparam, texts[wparam]);
        return true;
      } else {
        return false;
      }
    case kComboBoxMessageSetCurrentSelection:
      if (wparam < win->cursor_pos) {
        strncpy(win->title, texts[wparam], sizeof(win->title));
        return true;
      } else {
        return false;
      }
    case kComboBoxMessageGetCurrentSelection:
      for (uint32_t i = 0; i < win->cursor_pos; i++) {
        if (!strncmp(texts[i], win->title, sizeof(win->title)))
          return i;
      }
      return kComboBoxError;
    default:
      return win_button(win, msg, wparam, lparam);
  }
}
