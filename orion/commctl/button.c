#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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

// Optional per-button extras, allocated on first use (btnSetIconName / btnSetFaceColor).
typedef struct {
  char     icon[64];
  uint32_t face_color;   // plastic face; 0 = theme accent
  char     tooltip[96];
} button_extras_t;

static button_extras_t *button_extras(window_t *win, bool create) {
  if (!win->userdata && create) win->userdata = calloc(1, sizeof(button_extras_t));
  return win->userdata;
}

// Button control window procedure (text label buttons).
result_t win_button(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evDestroy:
      free(win->userdata);
      win->userdata = NULL;
      return true;
    case evGetTooltipText: {
      button_extras_t *x = button_extras(win, false);
      if (!x || !x->tooltip[0] || !lparam) return false;
      snprintf(lparam, 256, "%s", x->tooltip);
      return true;
    }
    case btnSetIconName:
    case btnSetTooltip:
    case btnSetFaceColor: {
      button_extras_t *x = button_extras(win, true);
      if (!x) {
        fprintf(stderr, "[button] extras allocation failed win=%u\n", win->id);
        fflush(stderr);
        return false;
      }
      if (msg == btnSetIconName) snprintf(x->icon, sizeof(x->icon), "%s", lparam ? (const char *)lparam : "");
      else if (msg == btnSetTooltip) snprintf(x->tooltip, sizeof(x->tooltip), "%s", lparam ? (const char *)lparam : "");
      else {
        uint32_t color = lparam ? *(const uint32_t *)lparam : 0;
        if (x->face_color == color) return true;
        x->face_color = color;
      }
      invalidate_window(win);
      return true;
    }
    case evCreate:
      win->frame.w = MAX(win->frame.w, strwidth(win->title) + MAX(BUTTON_PADDING, (control_predefined_height(win->flags) + 1) / 2) * 2);
      control_apply_predefined_height(win, "button");
      return true;
    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (m) {
        m->desired_w = MAX(win->frame.w, strwidth(win->title) + MAX(BUTTON_PADDING, (control_predefined_height(win->flags) + 1) / 2) * 2);
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
      ctrl_state_t state = CTRL_NORMAL;
      if (window_has_state(win, WINDOW_STATE_PRESSED)) state |= CTRL_PRESSED;
      if ((win->flags & BUTTON_PUSHLIKE) && win->value) state |= CTRL_SELECTED;
      if (window_has_state(win, WINDOW_STATE_HOVERED))  state |= CTRL_HOVER;
      if (window_has_state(win, WINDOW_STATE_DISABLED)) state |= CTRL_DISABLED;
      if (g_ui_runtime.focused == win)                  state |= CTRL_FOCUSED;
      if (win->flags & BUTTON_DEFAULT)                  state |= CTRL_DEFAULT;
      irect16_t local = {0, 0, win->frame.w, win->frame.h};
      button_extras_t *x = button_extras(win, false);
      bool plastic = (win->flags & WINDOW_PLASTIC) != 0;
      theme_draw_ex(THEME_PART_BUTTON, local, plastic ? state | CTRL_PLASTIC : state,
                    &(theme_draw_opts_t){.color = x ? x->face_color : 0, .icon = x && x->icon[0] ? x->icon : NULL,
                                         .control_size = win->flags & CONTROL_SIZE_MASK,
                                         .round = (win->flags & WINDOW_ROUND) != 0});
      if (plastic && x && x->icon[0]) return true;   // the plastic face engraves the glyph itself
      if (x && x->icon[0]) {
        sysicon_resolved_t glyph;
        if (sysicon_resolve(x->icon, &glyph)) {
          irect16_t at = rect_center(local, glyph.w, glyph.h);
          draw_sprite_region((int)glyph.tex, at, UV_RECT(glyph.u0, glyph.v0, glyph.u1, glyph.v1),
                             theme_foreground(THEME_PART_BUTTON, state), 0);
        }
        return true;
      }
      irect16_t content = rect_inset_xy(local, get_theme()->control_padding, 2);
      irect16_t label = rect_center(content, strwidth(win->title), CHAR_HEIGHT);
      get_theme()->draw_button_label(label, win->title, state);
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
      ctrl_state_t state = CTRL_NORMAL;
      if (window_has_state(win, WINDOW_STATE_PRESSED)) state |= CTRL_PRESSED;
      if ((win->flags & BUTTON_PUSHLIKE) && win->value) state |= CTRL_SELECTED;
      if (window_has_state(win, WINDOW_STATE_HOVERED)) state |= CTRL_HOVER;
      if (window_has_state(win, WINDOW_STATE_DISABLED)) state |= CTRL_DISABLED;
      if (g_ui_runtime.focused == win) state |= CTRL_FOCUSED;
      irect16_t local = {0, 0, win->frame.w, win->frame.h};
      theme_draw(THEME_PART_TOOLBAR_BUTTON, local, state);
      int px = (state & CTRL_PRESSED) && !(state & CTRL_DISABLED) ? get_theme()->press_icon_offset : 0;
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
        irect16_t inner = rect_inset_xy(local, get_theme()->control_padding, 2);
        get_theme()->draw_button_label(inner, win->title, state);
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
      send_message(win->parent ? win->parent : win, evCommand, MAKEDWORD(win->id, btnClicked), win);
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
        send_message(win->parent ? win->parent : win, evCommand, MAKEDWORD(win->id, btnClicked), win);
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
        // A flexible spacer is a spring: no intrinsic size (1 px; measure treats 0 as "use the frame"),
        // it takes what the container leaves. Measuring its previous arranged frame would feed the
        // layout back into itself.
        bool spring = (win->flags & WINDOW_FLEXSPACE) != 0;
        m->desired_w = spring ? 1 : MAX(0, win->frame.w);
        m->desired_h = spring ? 1 : MAX(0, win->frame.h);
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
