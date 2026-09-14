#include <string.h>
#include <stdio.h>

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/svg_icon_loader.h>
#include <orion/user/rect.h>
#include <orion/user/theme.h>
#include "commctl.h"

// Helper function (will be moved to ui/user/window.c later)
extern window_t *get_root_window(window_t *window);

// For BUTTON_AUTORADIO: clear all checked siblings then mark this one checked.
static void autoradio_select(window_t *win) {
  if (win->parent) {
    toolbar_state_t *tb = window_toolbar_state(win->parent);
    for (window_t *sib = win->parent->children; sib; sib = sib->next) {
      if (sib != win && (sib->flags & BUTTON_AUTORADIO) && sib->value) {
        sib->value = false;
        invalidate_window(sib);
      }
    }
    for (window_t *sib = tb ? tb->children : NULL; sib; sib = sib->next) {
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
      win->frame.w = MAX(win->frame.w, strwidth(win->title) + BUTTON_PADDING * 2);
      control_apply_predefined_height(win, "button");
      return true;
    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (m) {
        m->desired_w = MAX(win->frame.w, strwidth(win->title) + BUTTON_PADDING * 2);
        m->desired_h = control_predefined_height(win->flags);
      }
      return true;
    }
    case evArrange: {
      layout_arrange_t *a = (layout_arrange_t *)lparam;
      if (a) {
        control_arrange_predefined_height(win, a);
      }
      return true;
    }
    case evPaint: {
      // BUTTON_PUSHLIKE: render as pressed whenever the button is checked (value==true)
      bool show_pressed = window_has_state(win, WINDOW_STATE_PRESSED) ||
                          ((win->flags & BUTTON_PUSHLIKE) && win->value);
      // Build full control state so the theme can vary appearance by focus/default.
      ctrl_state_t state = CTRL_NORMAL;
      if (show_pressed)                                 state |= CTRL_PRESSED;
      if (window_has_state(win, WINDOW_STATE_HOVERED))  state |= CTRL_HOVER;
      if (window_has_state(win, WINDOW_STATE_DISABLED)) state |= CTRL_DISABLED;
      if (g_ui_runtime.focused == win)                  state |= CTRL_FOCUSED;
      if (win->flags & BUTTON_DEFAULT)                  state |= CTRL_DEFAULT;
      irect16_t local = {0, 0, win->frame.w, win->frame.h};
      // BUTTON_DEFAULT (BS_DEFPUSHBUTTON analogue): use black for the outer 1-px
      // gap so a thin black outline is visible around the button bevel.
      // When the button has keyboard focus brAccent takes precedence.
      irect16_t outer = rect_inset(local, -1);
      fill_rect((state & CTRL_FOCUSED)  ? get_sys_color(brAccent) :
                (state & CTRL_DEFAULT)  ? 0xff000000             :
                                          get_sys_color(brControlBg), outer);
      get_theme()->draw_button_bg(local, state);
      irect16_t content = rect_inset_xy(local, BUTTON_PADDING, 2);
      irect16_t label = rect_center(content, strwidth(win->title), CHAR_HEIGHT);
      bool disabled = (state & CTRL_DISABLED) != 0;
      uint32_t text_col = disabled ? get_sys_color(brTextDisabled) : get_sys_color(brTextNormal);
      if (!show_pressed && !disabled)
        draw_text_small(win->title, label.x + TEXT_SHADOW_OFFSET, label.y + TEXT_SHADOW_OFFSET, get_sys_color(brDarkEdge));
      irect16_t label_draw = rect_offset(label, show_pressed ? 1 : 0, show_pressed ? 1 : 0);
      draw_text_small(win->title, label_draw.x, label_draw.y, text_col);
      return true;
    }
    case evMouseMove:
      track_mouse(win);
      if (!window_has_state(win, WINDOW_STATE_HOVERED)) {
        window_set_state(win, WINDOW_STATE_HOVERED, true);
        invalidate_window(win);
      }
      return false;
    case evMouseLeave:
      window_set_state(win, WINDOW_STATE_HOVERED, false);
      invalidate_window(win);
      return false;
    case evLeftButtonDown:
      window_set_state(win, WINDOW_STATE_PRESSED, true);
      invalidate_window(win);
      return true;
    case evLeftButtonUp:
      window_set_state(win, WINDOW_STATE_PRESSED, false);
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
        window_set_state(win, WINDOW_STATE_PRESSED, true);
        invalidate_window(win);
        return true;
      }
      return false;
    case evKeyUp:
      if (wparam == AX_KEY_ENTER || wparam == AX_KEY_SPACE) {
        window_set_state(win, WINDOW_STATE_PRESSED, false);
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
typedef struct {
  bitmap_strip_t strip;        // private copy of the strip descriptor (btnSetImage)
  int            index;        // which icon in the strip (btnSetImage)
  char           icon_name[64]; // SVG base name (btnSetIconName); takes priority over strip
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
      bool show_pressed = window_has_state(win, WINDOW_STATE_PRESSED) ||
                          ((win->flags & BUTTON_PUSHLIKE) && win->value);
      irect16_t local = {0, 0, win->frame.w, win->frame.h};
      {
        ctrl_state_t tbs = CTRL_NORMAL;
        if (show_pressed)                                 tbs |= CTRL_PRESSED;
        if (window_has_state(win, WINDOW_STATE_HOVERED))  tbs |= CTRL_HOVER;
        if (window_has_state(win, WINDOW_STATE_DISABLED)) tbs |= CTRL_DISABLED;
        if (g_ui_runtime.focused == win)                  tbs |= CTRL_FOCUSED;
        get_theme()->draw_button_bg(local, tbs);
      }
      int px = show_pressed ? 1 : 0;
      toolbar_button_data_t *bd = (toolbar_button_data_t *)win->userdata;
      bool drew_icon = false;
      if (bd && bd->icon_name[0]) {
        sysicon_resolved_t res;
        if (sysicon_resolve(bd->icon_name, &res)) {
          irect16_t ic = rect_offset(rect_center(local, res.w, res.h), px, px);
          draw_sprite_region((int)res.tex, R(ic.x, ic.y, res.w, res.h),
                             UV_RECT(res.u0, res.v0, res.u1, res.v1), 0xFFFFFFFF, 0);
          drew_icon = true;
        }
      } else if (bd && bd->strip.cols > 0) {
        bitmap_strip_t *s = &bd->strip;
        int col = bd->index % s->cols;
        int row = bd->index / s->cols;
        float u0 = (float)(col * s->icon_w) / (float)s->sheet_w;
        float v0 = (float)(row * s->icon_h) / (float)s->sheet_h;
        float u1 = u0 + (float)s->icon_w / (float)s->sheet_w;
        float v1 = v0 + (float)s->icon_h / (float)s->sheet_h;
        irect16_t ic = rect_offset(rect_center(local, s->icon_w, s->icon_h), px, px);
        draw_sprite_region((int)s->tex, R(ic.x, ic.y, s->icon_w, s->icon_h),
                           UV_RECT(u0, v0, u1, v1), 0xFFFFFFFF, 0);
        drew_icon = true;
      }
      if (!drew_icon) {
        // Fallback: draw text label when no image has been set.
        irect16_t inner = rect_inset_xy(local, BUTTON_PADDING, 2);
        if (!show_pressed)
          draw_text_small(win->title, inner.x + TEXT_SHADOW_OFFSET, inner.y + TEXT_SHADOW_OFFSET, get_sys_color(brDarkEdge));
        irect16_t inner_draw = rect_offset(inner, px, px);
        draw_text_small(win->title, inner_draw.x, inner_draw.y, get_sys_color(brTextNormal));
      }
      return true;
    }
    case evLeftButtonDown:
      window_set_state(win, WINDOW_STATE_PRESSED, true);
      invalidate_window(win);
      return true;
    case evLeftButtonUp:
      window_set_state(win, WINDOW_STATE_PRESSED, false);
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
        window_set_state(win, WINDOW_STATE_PRESSED, true);
        invalidate_window(win);
        return true;
      }
      return false;
    case evKeyUp:
      if (wparam == AX_KEY_ENTER || wparam == AX_KEY_SPACE) {
        window_set_state(win, WINDOW_STATE_PRESSED, false);
        if (win->flags & BUTTON_AUTORADIO)
          autoradio_select(win);
        // Same ordering fix as evLeftButtonUp.
        invalidate_window(win);
        send_message(get_root_window(win), tbButtonClick, win->id, win);
        return true;
      }
      return false;
    case evMouseMove:
      track_mouse(win);
      if (!window_has_state(win, WINDOW_STATE_HOVERED)) {
        window_set_state(win, WINDOW_STATE_HOVERED, true);
        invalidate_window(win);
      }
      return false;
    case evMouseLeave:
      window_set_state(win, WINDOW_STATE_HOVERED, false);
      invalidate_window(win);
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
      if (lparam) {
        bitmap_strip_t *src = (bitmap_strip_t *)lparam;
        toolbar_button_data_t *bd = calloc(1, sizeof(toolbar_button_data_t));
        if (!bd) return false;
        memcpy(&bd->strip, src, sizeof(bitmap_strip_t));
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
    case btnSetIconName: {
      const char *name = (const char *)lparam;
      toolbar_button_data_t *bd = (toolbar_button_data_t *)win->userdata;
      if (!bd) {
        bd = calloc(1, sizeof(toolbar_button_data_t));
        if (!bd) return false;
        win->userdata = bd;
      }
      if (name && name[0])
        strncpy(bd->icon_name, name, sizeof(bd->icon_name) - 1);
      else
        bd->icon_name[0] = '\0';
      invalidate_window(win);
      return true;
    }
  }
  return false;
}

result_t win_space(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)wparam;
  switch (msg) {
    case evCreate:
      return true;
    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (m) {
        m->desired_w = MAX(0, win->frame.w);
        m->desired_h = MAX(0, win->frame.h);
      }
      return true;
    }
    case evArrange: {
      layout_arrange_t *a = (layout_arrange_t *)lparam;
      if (a) {
        win->frame = a->rect;
      }
      return true;
    }
    case evDestroy:
      return true;
    default:
      return false;
  }
}
