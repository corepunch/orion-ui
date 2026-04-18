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

// Open the dropdown list popup for 'win' (combobox).
static void open_dropdown(window_t *win) {
  // Determine the screen-absolute position of the combobox bottom edge.
  // Toolbar children have toolbar-band-relative frame.x/y; regular body
  // children have root-client-relative frames.
  int abs_x, abs_y;
  bool is_toolbar_child = false;
  if (win->parent) {
    for (window_t *tc = win->parent->toolbar_children; tc; tc = tc->next) {
      if (tc == win) { is_toolbar_child = true; break; }
    }
  }
  if (is_toolbar_child) {
    window_t *parent = win->parent;
    int parent_title_h = (parent->flags & WINDOW_NOTITLE) ? 0 : TITLEBAR_HEIGHT;
    abs_x = parent->frame.x + win->frame.x;
    abs_y = parent->frame.y + parent_title_h + win->frame.y + win->frame.h + 2;
  } else {
    window_t *root = get_root_window(win);
    int root_t = titlebar_height(root);
    abs_x = root->frame.x + win->frame.x;
    abs_y = root->frame.y + root_t + win->frame.y + win->frame.h + 2;
  }
  rect_t rect = {abs_x, abs_y, win->frame.w, 100};
  window_t *list = create_window("", WINDOW_NOTITLE|WINDOW_NORESIZE|WINDOW_VSCROLL|WINDOW_ALWAYSONTOP|WINDOW_NOTRAYBUTTON, &rect, NULL, win_list, win->hinstance, win);
  result_t sel = send_message(win, kComboBoxMessageGetCurrentSelection, 0, NULL);
  if (sel != (result_t)kComboBoxError)
    send_message(list, kListMessageSetItem, (uint32_t)sel, NULL);
  show_window(list, true);
  set_capture(list);
  set_focus(list);
}

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
    case kWindowMessageLeftButtonUp:
      win_button(win, msg, wparam, lparam);
      open_dropdown(win);
      return true;
    case kWindowMessageKeyDown: {
      uint32_t key = wparam;
      if (key == AX_KEY_SPACE || key == AX_KEY_ENTER || key == AX_KEY_KP_ENTER) {
        open_dropdown(win);
        return true;
      }
      if (key == AX_KEY_UPARROW) {
        result_t sel = send_message(win, kComboBoxMessageGetCurrentSelection, 0, NULL);
        if (sel != (result_t)kComboBoxError && sel > 0) {
          send_message(win, kComboBoxMessageSetCurrentSelection, (uint32_t)(sel - 1), NULL);
          invalidate_window(win);
          send_message(get_root_window(win), kWindowMessageCommand, MAKEDWORD(win->id, kComboBoxNotificationSelectionChange), win);
        }
        return true;
      }
      if (key == AX_KEY_DOWNARROW) {
        result_t sel = send_message(win, kComboBoxMessageGetCurrentSelection, 0, NULL);
        if (sel == (result_t)kComboBoxError && win->cursor_pos > 0) {
          send_message(win, kComboBoxMessageSetCurrentSelection, 0, NULL);
          invalidate_window(win);
          send_message(get_root_window(win), kWindowMessageCommand, MAKEDWORD(win->id, kComboBoxNotificationSelectionChange), win);
        } else if (sel != (result_t)kComboBoxError && (uint32_t)(sel + 1) < win->cursor_pos) {
          send_message(win, kComboBoxMessageSetCurrentSelection, (uint32_t)(sel + 1), NULL);
          invalidate_window(win);
          send_message(get_root_window(win), kWindowMessageCommand, MAKEDWORD(win->id, kComboBoxNotificationSelectionChange), win);
        }
        return true;
      }
      return false;
    }
    case kWindowMessageKeyUp:
      /* Consume Space/Enter key-up to prevent win_button's handler from
         sending a spurious kButtonNotificationClicked to the parent. */
      if (wparam == AX_KEY_SPACE || wparam == AX_KEY_ENTER || wparam == AX_KEY_KP_ENTER)
        return true;
      return win_button(win, msg, wparam, lparam);
    case kComboBoxMessageClear:
      memset(texts, 0, sizeof(combobox_string_t) * win->cursor_pos);
      win->cursor_pos = 0;
      win->title[0] = '\0';
      return true;
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
