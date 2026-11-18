// Interactive content in the same themed silhouette as a tooltip.
#include "commctl.h"
#include <stdio.h>
#include <orion/user/draw.h>

static void speechbubble_padding(window_t *win, tooltip_tail_side_t side) {
  const char *sel = ui_selector(win, "SpeechBubble");
  int shadow = ui_px(sel, "--shadow-size"), x = ui_px(sel, "padding-left"), y = ui_px(sel, "padding-top");
  int tail = ui_px(sel, "--tail-length");
  win->layout.layout_padding = R(shadow + x, shadow + y + (side == TOOLTIP_TAIL_TOP ? tail : 0),
                                 shadow + x, shadow + y + (side == TOOLTIP_TAIL_BOTTOM ? tail : 0));
}

// The tail's nearest approach to either end: past the shadow, the rounded corner and the tail's own width.
static int speechbubble_tail_margin(window_t *win) {
  const char *sel = ui_selector(win, "SpeechBubble");
  return ui_px(sel, "--shadow-size") + ui_radius(sel) + ui_px(sel, "--tail-width");
}

result_t win_speechbubble(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  tooltip_tail_side_t *side = win->userdata;
  switch (msg) {
    case evCreate:
      side = allocate_window_data(win, sizeof(*side));
      if (!side) return false;
      *side = TOOLTIP_TAIL_BOTTOM;
      win_stack(win, msg, wparam, lparam);
      speechbubble_padding(win, *side);
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
    case bbSetTailSide:
      if (!side || (wparam != TOOLTIP_TAIL_TOP && wparam != TOOLTIP_TAIL_BOTTOM)) {
        fprintf(stderr, "[speechbubble] invalid tail side win=%u side=%u\n", win->id, wparam);
        fflush(stderr);
        return false;
      }
      if (*side == (tooltip_tail_side_t)wparam) return true;
      *side = wparam;
      speechbubble_padding(win, *side);
      window_layout_sync(win);
      invalidate_window(win);
      return true;
    case evThemeChanged:
    case evMeasure:
    case evArrange:
      if (side) speechbubble_padding(win, *side);
      break;
    case evPaint: {
      int margin = speechbubble_tail_margin(win);
      int offset = win->value ? (int)win->value : win->frame.w / 2;
      offset = MAX(margin, MIN(offset, win->frame.w - margin));
      draw_speech_bubble(get_client_rect(win), offset, side ? *side : TOOLTIP_TAIL_BOTTOM);
      return false; // The window system paints the ordinary child controls.
    }
    case evHitTest: {
      ipoint16_t point = {(int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)};
      if (lparam && !rect_contains_point(speech_bubble_body_rect(get_client_rect(win), side ? *side : TOOLTIP_TAIL_BOTTOM), point)) {
        *(window_t **)lparam = NULL;
        return true;
      }
      return false;
    }
  }
  return win_stack(win, msg, wparam, lparam);
}
