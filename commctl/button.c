#include <string.h>
#include <stdio.h>

#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"
#include "../user/icons.h"
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
    case evCreate:
      win->frame.w = MAX(win->frame.w, strwidth(win->title)+6);
      win->frame.h = MAX(win->frame.h, BUTTON_HEIGHT);
      return true;
    case evPaint: {
      // BUTTON_PUSHLIKE: render as pressed whenever the button is checked (value==true)
      bool show_pressed = win->pressed ||
                          ((win->flags & BUTTON_PUSHLIKE) && win->value);
      // BUTTON_DEFAULT (BS_DEFPUSHBUTTON analogue): use black for the outer 1-px
      // gap so a thin black outline is visible around the button bevel.
      // When the button has keyboard focus brFocusRing takes precedence.
      uint32_t bg = (g_ui_runtime.focused == win) ? get_sys_color(brFocusRing) :
                    (win->flags & BUTTON_DEFAULT) ? 0xff000000 : get_sys_color(brWindowBg);
      irect16_t local = {0, 0, win->frame.w, win->frame.h};
      irect16_t outer = rect_inset(local, -1);
      fill_rect(bg, outer);
      draw_button(local, 1, 1, show_pressed);
      irect16_t label = rect_center(local, strwidth(win->title), CHAR_HEIGHT);
      if (!show_pressed)
        draw_text_small(win->title, label.x + TEXT_SHADOW_OFFSET, label.y + TEXT_SHADOW_OFFSET, get_sys_color(brDarkEdge));
      irect16_t label_draw = rect_offset(label, show_pressed ? 1 : 0, show_pressed ? 1 : 0);
      draw_text_small(win->title, label_draw.x, label_draw.y, get_sys_color(brTextNormal));
      return true;
    }
    case evLeftButtonDown:
      win->pressed = true;
      invalidate_window(win);
      return true;
    case evLeftButtonUp:
      win->pressed = false;
      if (win->flags & BUTTON_AUTORADIO)
        autoradio_select(win);
      // Invalidate BEFORE sending the command: send_message may trigger
      // end_dialog → destroy_window(win), freeing 'win'. Reading win->parent
      // in get_root_window() on freed memory causes SIGSEGV on macOS.
      invalidate_window(win);
      send_message(get_root_window(win), evCommand, MAKEDWORD(win->id, btnClicked), win);
      return true;
    case evKeyDown:
      if (wparam == AX_KEY_ENTER || wparam == AX_KEY_SPACE) {
        win->pressed = true;
        invalidate_window(win);
        return true;
      }
      return false;
    case evKeyUp:
      if (wparam == AX_KEY_ENTER || wparam == AX_KEY_SPACE) {
        win->pressed = false;
        if (win->flags & BUTTON_AUTORADIO)
          autoradio_select(win);
        // Same ordering fix as evLeftButtonUp.
        invalidate_window(win);
        send_message(get_root_window(win), evCommand, MAKEDWORD(win->id, btnClicked), win);
        return true;
      } else {
        return false;
      }
    case btnSetCheck: {
      bool checked = (wparam == btnStateChecked);
      if ((win->flags & BUTTON_AUTORADIO) && checked)
        autoradio_select(win);
      else {
        win->value = checked;
        invalidate_window(win);
      }
      return true;
    }
    case btnGetCheck:
      return win->value ? btnStateChecked : btnStateUnchecked;
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
  uint32_t       source_base; // 0 for custom strips, otherwise SILK_ICON_BASE / SYSICON_BASE
} toolbar_button_data_t;

// Toolbar button window procedure.
// Renders an icon from a bitmap_strip_t at a given index.
// Set via btnSetImage: wparam = icon index; lparam = bitmap_strip_t*.
result_t win_toolbar_button(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evDestroy:
      if (win->userdata) {
        free(win->userdata);
        win->userdata = NULL;
      }
      return true;
    case evPaint: {
      bool show_pressed = win->pressed ||
                          ((win->flags & BUTTON_PUSHLIKE) && win->value);
      irect16_t local = {0, 0, win->frame.w, win->frame.h};
      draw_button(local, 1, 1, show_pressed);
      int px = show_pressed ? 1 : 0;
      irect16_t icon_rect = rect_offset(rect_center(local, 16, 16), px, px);
      toolbar_button_data_t *bd = (toolbar_button_data_t *)win->userdata;
      if (bd && bd->strip.cols > 0) {
        // Compute UV sub-region for the Nth icon in the strip.
        bitmap_strip_t *s = &bd->strip;
        if (bd->source_base == SILK_ICON_BASE) {
          // Silk icons are outlined for readability.
          draw_silk_icon16(bd->index + SILK_ICON_BASE, icon_rect.x, icon_rect.y, 0xFFFFFFFF);
          return true;
        }
        int col = bd->index % s->cols;
        int row = bd->index / s->cols;
        float u0 = (float)(col * s->icon_w) / (float)s->sheet_w;
        float v0 = (float)(row * s->icon_h) / (float)s->sheet_h;
        float u1 = u0 + (float)s->icon_w / (float)s->sheet_w;
        float v1 = v0 + (float)s->icon_h / (float)s->sheet_h;
        irect16_t icon = rect_offset(rect_center(local, s->icon_w, s->icon_h), px, px);
        draw_sprite_region((int)s->tex, R(icon.x, icon.y, s->icon_w, s->icon_h),
                           UV_RECT(u0, v0, u1, v1), 0xFFFFFFFF, 0);
      } else {
        // Fallback: draw text label when no image has been set.
        irect16_t inner = rect_inset(local, BUTTON_TEXT_INSET);
        if (!show_pressed)
          draw_text_small(win->title, inner.x + TEXT_SHADOW_OFFSET, inner.y + TEXT_SHADOW_OFFSET, get_sys_color(brDarkEdge));
        irect16_t inner_draw = rect_offset(inner, px, px);
        draw_text_small(win->title, inner_draw.x, inner_draw.y, get_sys_color(brTextNormal));
      }
      return true;
    }
    case evLeftButtonDown:
      win->pressed = true;
      invalidate_window(win);
      return true;
    case evLeftButtonUp:
      win->pressed = false;
      if (win->flags & BUTTON_AUTORADIO)
        autoradio_select(win);
      // Invalidate BEFORE sending the command: send_message may trigger
      // end_dialog → destroy_window(win), freeing 'win'. Reading win->parent
      // in get_root_window() on freed memory causes SIGSEGV on macOS.
      invalidate_window(win);
      send_message(get_root_window(win), tbButtonClick, win->id, win);
      return true;
    case evKeyDown:
      if (wparam == AX_KEY_ENTER || wparam == AX_KEY_SPACE) {
        win->pressed = true;
        invalidate_window(win);
        return true;
      }
      return false;
    case evKeyUp:
      if (wparam == AX_KEY_ENTER || wparam == AX_KEY_SPACE) {
        win->pressed = false;
        if (win->flags & BUTTON_AUTORADIO)
          autoradio_select(win);
        // Same ordering fix as evLeftButtonUp.
        invalidate_window(win);
        send_message(get_root_window(win), tbButtonClick, win->id, win);
        return true;
      }
      return false;
    case btnSetCheck: {
      bool checked = (wparam == btnStateChecked);
      if ((win->flags & BUTTON_AUTORADIO) && checked)
        autoradio_select(win);
      else {
        win->value = checked;
        invalidate_window(win);
      }
      return true;
    }
    case btnGetCheck:
      return win->value ? btnStateChecked : btnStateUnchecked;
    case btnSetImage: {
      // Analogous to WinAPI TBBUTTON.iBitmap: store a private copy of the
      // bitmap_strip_t descriptor and the icon index.
      // wparam = icon index; lparam = bitmap_strip_t*
      if (lparam) {
        bitmap_strip_t *src = (bitmap_strip_t *)lparam;
        toolbar_button_data_t *bd = malloc(sizeof(toolbar_button_data_t));
        if (!bd) return false; // OOM: keep old image
        memcpy(&bd->strip, src, sizeof(bitmap_strip_t));
        bd->index = (int)(uint32_t)wparam;
        bd->source_base = 0;
        if (src == ui_get_silk_strip())
          bd->source_base = SILK_ICON_BASE;
        else if (src == ui_get_sysicon_strip())
          bd->source_base = SYSICON_BASE;
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
