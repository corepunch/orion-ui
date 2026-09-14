#include <string.h>

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include "commctl.h"

// Separator controls are intended to occupy a visible band, not a 1px hairline.
// The line itself is drawn centered inside this band.
#define SEPARATOR_BAND 6

static bool separator_is_vertical(window_t *win) {
  if (!win || !win->parent) return false;
  if (win->parent->flags & WINDOW_TOOLBAR) return true;
  return (win->parent->flags & WINDOW_STACK_HORIZONTAL) != 0;
}

result_t win_separator(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)wparam;
  switch (msg) {
    case evCreate:
      return true;
    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (m) {
        if (separator_is_vertical(win)) {
          m->desired_w = SEPARATOR_BAND;
          m->desired_h = MAX(1, m->avail_h);
        } else {
          m->desired_w = MAX(1, m->avail_w);
          m->desired_h = SEPARATOR_BAND;
        }
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
    case evPaint: {
      bool vertical = separator_is_vertical(win);
      irect16_t pad = win->parent ? win->parent->layout.layout_padding : (irect16_t){0, 0, 0, 0};
      if (vertical) {
        theme_draw(THEME_PART_SEPARATOR, R(win->frame.w / 2, -pad.y, 1, win->frame.h + pad.y + pad.h), CTRL_NORMAL);
      } else {
        theme_draw(THEME_PART_SEPARATOR, R(-pad.x, win->frame.h / 2, win->frame.w + pad.x + pad.w, 1), CTRL_NORMAL);
      }
      return true;
    }
    case evDestroy:
      return true;
    default:
      return false;
  }
}
