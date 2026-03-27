#include <SDL2/SDL.h>
#include <string.h>
#include <stdio.h>

#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"

// Helper function (will be moved to ui/user/window.c later)
extern window_t *get_root_window(window_t *window);

// For BUTTON_AUTORADIO: clear all checked siblings then mark this one checked.
static void autoradio_select(window_t *win) {
  if (win->parent) {
    for (window_t *sib = win->parent->children; sib; sib = sib->next) {
      if (sib != win && (sib->flags & BUTTON_AUTORADIO) && sib->value) {
        sib->value = false;
        invalidate_window(sib);
      }
    }
  }
  win->value = true;
  invalidate_window(win);
}

// Button control window procedure
result_t win_button(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case kWindowMessageCreate:
      win->frame.w = MAX(win->frame.w, strwidth(win->title)+6);
      win->frame.h = MAX(win->frame.h, BUTTON_HEIGHT);
      return true;
    case kWindowMessagePaint: {
      // BUTTON_PUSHLIKE: render as pressed whenever the button is checked (value==true)
      bool show_pressed = win->pressed ||
                          ((win->flags & BUTTON_PUSHLIKE) && win->value);
      fill_rect(_focused == win ? COLOR_FOCUSED : COLOR_PANEL_BG,
                win->frame.x-2, win->frame.y-2, win->frame.w+4, win->frame.h+4);
      draw_button(&win->frame, 1, 1, show_pressed);
      if (!show_pressed) {
        draw_text_small(win->title, win->frame.x+4, win->frame.y+4, COLOR_DARK_EDGE);
      }
      draw_text_small(win->title,
                      win->frame.x + (show_pressed ? 4 : 3),
                      win->frame.y + (show_pressed ? 4 : 3),
                      COLOR_TEXT_NORMAL);
      return true;
    }
    case kWindowMessageLeftButtonDown:
      win->pressed = true;
      invalidate_window(win);
      return true;
    case kWindowMessageLeftButtonUp:
      win->pressed = false;
      if (win->flags & BUTTON_AUTORADIO)
        autoradio_select(win);
      send_message(get_root_window(win), kWindowMessageCommand, MAKEDWORD(win->id, kButtonNotificationClicked), win);
      invalidate_window(win);
      return true;
    case kWindowMessageKeyDown:
      if (wparam == SDL_SCANCODE_RETURN || wparam == SDL_SCANCODE_SPACE) {
        win->pressed = true;
        invalidate_window(win);
        return true;
      }
      return false;
    case kWindowMessageKeyUp:
      if (wparam == SDL_SCANCODE_RETURN || wparam == SDL_SCANCODE_SPACE) {
        win->pressed = false;
        if (win->flags & BUTTON_AUTORADIO)
          autoradio_select(win);
        send_message(get_root_window(win), kWindowMessageCommand, MAKEDWORD(win->id, kButtonNotificationClicked), win);
        invalidate_window(win);
        return true;
      } else {
        return false;
      }
    case kButtonMessageSetCheck: {
      bool checked = (wparam == kButtonStateChecked);
      if ((win->flags & BUTTON_AUTORADIO) && checked)
        autoradio_select(win);
      else {
        win->value = checked;
        invalidate_window(win);
      }
      return true;
    }
    case kButtonMessageGetCheck:
      return win->value ? kButtonStateChecked : kButtonStateUnchecked;
  }
  return false;
}

result_t win_space(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  return false;
}
