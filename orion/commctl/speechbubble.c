// Interactive content in the same themed silhouette as a tooltip.
#include "commctl.h"
#include <stdio.h>
#include <orion/user/draw.h>

static void speechbubble_padding(window_t *win) {
  const theme_t *theme = get_theme();
  int shadow = theme->speechbubble.shadow.size;
  int x = MAX(theme->tooltip.padding_x, theme->card.padding_x);
  int y = MAX(theme->tooltip.padding_y, theme->card.padding_y);
  win->layout.layout_padding = R(shadow + x, shadow + y, shadow + x,
                                 shadow + y + theme->speechbubble.tail_length);
}

result_t win_speechbubble(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      win_stack(win, msg, wparam, lparam);
      speechbubble_padding(win);
      return true;
    case evDestroy: return true;
    case bbSetTailOffset:
      if (wparam > INT16_MAX) {
        fprintf(stderr, "[speechbubble] invalid tail offset win=%u offset=%u\n", win->id, wparam);
        fflush(stderr);
        return false;
      }
      if (win->value == wparam) return true;
      win->value = wparam;
      invalidate_window(win);
      return true;
    case evThemeChanged:
    case evMeasure:
    case evArrange:
      speechbubble_padding(win);
      break;
    case evPaint: {
      const theme_t *theme = get_theme();
      int margin = theme->speechbubble.shadow.size + theme->speechbubble.corner_radius + theme->speechbubble.tail_width;
      int offset = win->value ? (int)win->value : win->frame.w / 2;
      offset = MAX(margin, MIN(offset, win->frame.w - margin));
      draw_speech_bubble(get_client_rect(win), offset, TOOLTIP_TAIL_BOTTOM);
      return false; // The window system paints the ordinary child controls.
    }
    case evHitTest: {
      ipoint16_t point = {(int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)};
      if (lparam && !rect_contains_point(speech_bubble_body_rect(get_client_rect(win), TOOLTIP_TAIL_BOTTOM), point)) {
        *(window_t **)lparam = NULL;
        return true;
      }
      return false;
    }
  }
  return win_stack(win, msg, wparam, lparam);
}
