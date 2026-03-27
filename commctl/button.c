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

// Button control window procedure (text label buttons).
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
      int tx = win->frame.x + (win->frame.w - strwidth(win->title)) / 2;
      int ty = win->frame.y + (win->frame.h - CHAR_HEIGHT) / 2;
      int px = show_pressed ? 1 : 0;
      if (!show_pressed) {
        draw_text_small(win->title, tx + 1, ty + 1, COLOR_DARK_EDGE);
      }
      draw_text_small(win->title, tx + px, ty + px, COLOR_TEXT_NORMAL);
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

// -------------------------------------------------------------------------
// Toolbar button
// -------------------------------------------------------------------------

// Internal data owned by each toolbar button.
// Analogous to TBBUTTON.iBitmap: the button stores an index into a shared strip.
typedef struct {
  bitmap_strip_t strip; // private copy of the strip descriptor
  int            index; // which icon in the strip (iBitmap)
} toolbar_button_data_t;

// Toolbar button window procedure.
// Renders an icon from a bitmap_strip_t at a given index.
// Set via kButtonMessageSetImage: wparam = icon index; lparam = bitmap_strip_t*.
result_t win_toolbar_button(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case kWindowMessageDestroy:
      if (win->userdata) {
        free(win->userdata);
        win->userdata = NULL;
      }
      return true;
    case kWindowMessagePaint: {
      bool show_pressed = win->pressed ||
                          ((win->flags & BUTTON_PUSHLIKE) && win->value);
      fill_rect(_focused == win ? COLOR_FOCUSED : COLOR_PANEL_BG,
                win->frame.x-2, win->frame.y-2, win->frame.w+4, win->frame.h+4);
      draw_button(&win->frame, 1, 1, show_pressed);
      int px = show_pressed ? 1 : 0;
      toolbar_button_data_t *bd = (toolbar_button_data_t *)win->userdata;
      if (bd && bd->strip.cols > 0) {
        // Compute UV sub-region for the Nth icon in the strip.
        bitmap_strip_t *s = &bd->strip;
        int col = bd->index % s->cols;
        int row = bd->index / s->cols;
        float u0 = (float)(col * s->icon_w) / (float)s->sheet_w;
        float v0 = (float)(row * s->icon_h) / (float)s->sheet_h;
        float u1 = u0 + (float)s->icon_w / (float)s->sheet_w;
        float v1 = v0 + (float)s->icon_h / (float)s->sheet_h;
        int ix = win->frame.x + (win->frame.w - s->icon_w) / 2 + px;
        int iy = win->frame.y + (win->frame.h - s->icon_h) / 2 + px;
        draw_sprite_region((int)s->tex, ix, iy, s->icon_w, s->icon_h,
                           u0, v0, u1, v1, 1.0f);
      } else {
        // Fallback: draw text label when no image has been set.
        if (!show_pressed)
          draw_text_small(win->title, win->frame.x+4, win->frame.y+4, COLOR_DARK_EDGE);
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
      send_message(get_root_window(win), kWindowMessageCommand,
                   MAKEDWORD(win->id, kButtonNotificationClicked), win);
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
        send_message(get_root_window(win), kWindowMessageCommand,
                     MAKEDWORD(win->id, kButtonNotificationClicked), win);
        invalidate_window(win);
        return true;
      }
      return false;
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
      // Analogous to WinAPI TBBUTTON.iBitmap: store a private copy of the
      // bitmap_strip_t descriptor and the icon index.
      // wparam = icon index; lparam = bitmap_strip_t*
      if (lparam) {
        toolbar_button_data_t *bd = malloc(sizeof(toolbar_button_data_t));
        if (!bd) return false; // OOM: keep old image
        memcpy(&bd->strip, lparam, sizeof(bitmap_strip_t));
        bd->index = (int)(uint32_t)wparam;
        if (win->userdata) free(win->userdata);
        win->userdata = bd;
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
