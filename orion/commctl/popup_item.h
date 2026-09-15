#ifndef __POPUP_ITEM_H__
#define __POPUP_ITEM_H__

#include <orion/user/draw.h>
#include <orion/user/theme.h>
#include <string.h>

#define POPUP_ITEM_HEIGHT CONTROL_HEIGHT_REGULAR

static inline void popup_item_paint(irect16_t row, const char *text, ctrl_state_t state) {
  theme_draw(THEME_PART_MENU_ITEM, rect_inset_xy(row, 1, 0), state);
  irect16_t label = rect_inset_xy(row, MENU_SIDE_PAD, 0);
  char buf[256];
  const char *tab = text ? strchr(text, '\t') : NULL;
  if (tab) {
    size_t n = MIN((size_t)(tab - text), sizeof(buf) - 1);
    memcpy(buf, text, n);
    buf[n] = 0;
    text = buf;
  }
  if (text) draw_text_clipped(FONT_SYSTEM, text, &label,
                             theme_foreground(THEME_PART_MENU_ITEM, state), 0);
}

#endif
