// Interactive content in the same themed silhouette as a tooltip.
#include "commctl.h"
#include <stdio.h>
#include <orion/user/draw.h>

// The sheet's SpeechBubble box: shadow room, then padding; the tail adds to the bottom.
static void speechbubble_padding(window_t *win) {
  const char *sel = ui_selector(win, "SpeechBubble");
  int shadow = ui_px(sel, "--shadow-size"), x = ui_px(sel, "padding-left"), y = ui_px(sel, "padding-top");
  win->layout.layout_padding = R(shadow + x, shadow + y, shadow + x, shadow + y + ui_px(sel, "--tail-length"));
}

// The tail's nearest approach to either end: past the shadow, the rounded corner and the tail's own width.
static int speechbubble_tail_margin(window_t *win) {
  const char *sel = ui_selector(win, "SpeechBubble");
  return ui_px(sel, "--shadow-size") + ui_radius(sel) + ui_px(sel, "--tail-width");
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
      int margin = speechbubble_tail_margin(win);
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
