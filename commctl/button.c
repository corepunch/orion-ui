#include <string.h>
#include <stdio.h>

#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"
#include "../user/rect.h"
#include "../user/theme.h"
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
    for (window_t *sib = win->parent->toolbar_children; sib; sib = sib->next) {
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
      // BUTTON_DEFAULT (BS_DEFPUSHBUTTON analogue): use black for the outer 1-px
      // gap so a thin black outline is visible around the button bevel.
      // When the button has keyboard focus kColorFocusRing takes precedence.
      uint32_t bg = (g_ui_runtime.focused == win) ? get_sys_color(kColorFocusRing) :
                    (win->flags & BUTTON_DEFAULT) ? 0xff000000 : get_sys_color(kColorWindowBg);
      rect_t outer = rect_inset(win->frame, -1);
      fill_rect(bg, outer.x, outer.y, outer.w, outer.h);
      draw_button(&win->frame, 1, 1, show_pressed);
      rect_t label = rect_center(win->frame, strwidth(win->title), CHAR_HEIGHT);
      if (!show_pressed)
        draw_text_small(win->title, label.x + TEXT_SHADOW_OFFSET, label.y + TEXT_SHADOW_OFFSET, get_sys_color(kColorDarkEdge));
      rect_t label_draw = rect_offset(label, show_pressed ? 1 : 0, show_pressed ? 1 : 0);
      draw_text_small(win->title, label_draw.x, label_draw.y, get_sys_color(kColorTextNormal));
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
      // Invalidate BEFORE sending the command: send_message may trigger
      // end_dialog → destroy_window(win), freeing 'win'. Reading win->parent
      // in get_root_window() on freed memory causes SIGSEGV on macOS.
      invalidate_window(win);
      send_message(get_root_window(win), kWindowMessageCommand, MAKEDWORD(win->id, kButtonNotificationClicked), win);
      return true;
    case kWindowMessageKeyDown:
      if (wparam == AX_KEY_ENTER || wparam == AX_KEY_SPACE) {
        win->pressed = true;
        invalidate_window(win);
        return true;
      }
      return false;
    case kWindowMessageKeyUp:
      if (wparam == AX_KEY_ENTER || wparam == AX_KEY_SPACE) {
        win->pressed = false;
        if (win->flags & BUTTON_AUTORADIO)
          autoradio_select(win);
        // Same ordering fix as kWindowMessageLeftButtonUp.
        invalidate_window(win);
        send_message(get_root_window(win), kWindowMessageCommand, MAKEDWORD(win->id, kButtonNotificationClicked), win);
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
      rect_t focus_outer = rect_inset(win->frame, -2);
      fill_rect(g_ui_runtime.focused == win ? get_sys_color(kColorFocusRing) : get_sys_color(kColorWindowBg),
                focus_outer.x, focus_outer.y, focus_outer.w, focus_outer.h);
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
        rect_t icon = rect_offset(rect_center(win->frame, s->icon_w, s->icon_h), px, px);
        draw_sprite_region((int)s->tex, icon.x, icon.y, s->icon_w, s->icon_h,
                           u0, v0, u1, v1, 1.0f);
      } else {
        // Fallback: draw text label when no image has been set.
        rect_t inner = rect_inset(win->frame, BUTTON_TEXT_INSET);
        if (!show_pressed)
          draw_text_small(win->title, inner.x + TEXT_SHADOW_OFFSET, inner.y + TEXT_SHADOW_OFFSET, get_sys_color(kColorDarkEdge));
        rect_t inner_draw = rect_offset(inner, px, px);
        draw_text_small(win->title, inner_draw.x, inner_draw.y, get_sys_color(kColorTextNormal));
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
      // Invalidate BEFORE sending the command: send_message may trigger
      // end_dialog → destroy_window(win), freeing 'win'. Reading win->parent
      // in get_root_window() on freed memory causes SIGSEGV on macOS.
      invalidate_window(win);
      send_message(get_root_window(win), kWindowMessageCommand,
                   MAKEDWORD(win->id, kButtonNotificationClicked), win);
      return true;
    case kWindowMessageKeyDown:
      if (wparam == AX_KEY_ENTER || wparam == AX_KEY_SPACE) {
        win->pressed = true;
        invalidate_window(win);
        return true;
      }
      return false;
    case kWindowMessageKeyUp:
      if (wparam == AX_KEY_ENTER || wparam == AX_KEY_SPACE) {
        win->pressed = false;
        if (win->flags & BUTTON_AUTORADIO)
          autoradio_select(win);
        // Same ordering fix as kWindowMessageLeftButtonUp.
        invalidate_window(win);
        send_message(get_root_window(win), kWindowMessageCommand,
                     MAKEDWORD(win->id, kButtonNotificationClicked), win);
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
