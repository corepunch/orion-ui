#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/text.h>
#include "commctl.h"

int control_predefined_height(flags_t flags) {
  switch (flags & CONTROL_SIZE_MASK) {
    case CONTROL_SIZE_MINI:  return CONTROL_HEIGHT_MINI;
    case CONTROL_SIZE_SMALL: return CONTROL_HEIGHT_SMALL;
    case CONTROL_SIZE_LARGE: return CONTROL_HEIGHT_LARGE;
    default:                 return CONTROL_HEIGHT_REGULAR;
  }
}

int control_text_padding_y(flags_t flags) {
  return MAX(0, (control_predefined_height(flags) - text_char_height(FONT_SMALL)) / 2);
}

void control_apply_predefined_height(window_t *win, const char *module) {
  if (!win) return;
  (void)module;
  int applied = control_predefined_height(win->flags);
  win->layout.layout_fixed_h = 0;
  win->frame.h = applied;
}

bool control_arrange_predefined_height(window_t *win, const layout_arrange_t *a) {
  if (!win || !a) return false;
  int h = control_predefined_height(win->flags);
  if (a->rect.h > 0 && a->rect.h < h) h = a->rect.h;
  win->frame.x = a->rect.x;
  win->frame.w = MAX(1, a->rect.w);
  win->frame.h = MAX(1, h);
  win->frame.y = a->rect.y + MAX(0, (a->rect.h - win->frame.h) / 2);
  return true;
}
