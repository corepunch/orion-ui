#include <SDL2/SDL.h>
#include <string.h>
#include <stdio.h>

#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"
#include "commctl.h"

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
    case kWindowMessageDestroy:
      // Free the owned button_image_t copy if one was set via kButtonMessageSetImage.
      if ((win->flags & BUTTON_BITMAP) && win->userdata) {
        free(win->userdata);
        win->userdata = NULL;
      }
      return true;
    case kWindowMessagePaint: {
      // BUTTON_PUSHLIKE: render as pressed whenever the button is checked (value==true)
      bool show_pressed = win->pressed ||
                          ((win->flags & BUTTON_PUSHLIKE) && win->value);
      fill_rect(_focused == win ? COLOR_FOCUSED : COLOR_PANEL_BG,
                win->frame.x-2, win->frame.y-2, win->frame.w+4, win->frame.h+4);
      draw_button(&win->frame, 1, 1, show_pressed);
      int px = show_pressed ? 1 : 0;
      if ((win->flags & BUTTON_BITMAP) && win->userdata) {
        // Draw the icon centered within the button area (analogous to WinAPI BS_BITMAP)
        button_image_t *img = (button_image_t *)win->userdata;
        float u0 = (float)(img->icon_col * img->icon_w) / (float)img->sheet_w;
        float v0 = (float)(img->icon_row * img->icon_h) / (float)img->sheet_h;
        float u1 = u0 + (float)img->icon_w / (float)img->sheet_w;
        float v1 = v0 + (float)img->icon_h / (float)img->sheet_h;
        int ix = win->frame.x + (win->frame.w - img->icon_w) / 2 + px;
        int iy = win->frame.y + (win->frame.h - img->icon_h) / 2 + px;
        draw_sprite_region((int)img->tex, ix, iy, img->icon_w, img->icon_h,
                           u0, v0, u1, v1, 1.0f);
      } else {
        if (!show_pressed) {
          draw_text_small(win->title, win->frame.x+4, win->frame.y+4, COLOR_DARK_EDGE);
        }
        draw_text_small(win->title,
                        win->frame.x + (show_pressed ? 4 : 3),
                        win->frame.y + (show_pressed ? 4 : 3),
                        COLOR_TEXT_NORMAL);
      }
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
    case kButtonMessageSetImage: {
      // Analogous to WinAPI BM_SETIMAGE: make a private copy of the button_image_t
      // so that the button owns its own image descriptor and callers do not need to
      // keep the original alive for the button's lifetime.
      if (lparam) {
        button_image_t *copy = malloc(sizeof(button_image_t));
        if (!copy) return false;  // OOM: keep old image, do not invalidate
        memcpy(copy, lparam, sizeof(button_image_t));
        if (win->userdata) free(win->userdata);
        win->userdata = copy;
      } else {
        if (win->userdata) free(win->userdata);
        win->userdata = NULL;
      }
      invalidate_window(win);
      return true;
    }
  }
  return false;
}

result_t win_space(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  return false;
}
