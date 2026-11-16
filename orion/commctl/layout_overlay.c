#include <orion/user/user.h>
#include <orion/user/messages.h>
#include "commctl_internal.h"

// OverlayView (XUL <stack>, SwiftUI ZStack): every child fills the content box, later children
// over earlier ones. Painting follows child order and the pointer goes to the topmost child, so
// a view can cover another while both stay laid out, as a transition between them needs.

static void layout_overlay_arrange(window_t *win) {
  irect16_t content = layout_content_rect(win, get_client_rect(win));
  for (window_t *child = win->children; child; child = child->next)
    if (!layout_child_is_collapsed(child)) layout_arrange_child(child, content);
}

static void layout_overlay_measure(window_t *win, layout_measure_t *m) {
  irect16_t content = layout_content_rect(win, R(0, 0, m->avail_w, m->avail_h));
  int w = 0, h = 0;
  for (window_t *child = win->children; child; child = child->next) {
    if (layout_child_is_collapsed(child)) continue;
    layout_measure_t cm = layout_measure_child(child, content.w, content.h);
    w = MAX(w, cm.desired_w);
    h = MAX(h, cm.desired_h);
  }
  irect16_t pad = layout_padding_for(win);
  m->desired_w = MAX(1, w + pad.x + pad.w);
  m->desired_h = MAX(1, h + pad.y + pad.h);
}

static window_t *layout_overlay_hit(window_t *win, ipoint16_t p) {
  window_t *top = NULL;
  for (window_t *child = win->children; child; child = child->next)
    if (window_has_state(child, WINDOW_STATE_VISIBLE) && rect_contains_point(child->frame, p)) top = child;
  return top;
}

result_t win_overlay(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evMeasure: {
      layout_measure_t *m = lparam;
      if (!m) return MAKEDWORD(1, 1);
      layout_overlay_measure(win, m);
      return MAKEDWORD((uint16_t)m->desired_w, (uint16_t)m->desired_h);
    }
    case evArrange: {
      layout_arrange_t *a = lparam;
      if (a) {
        win->frame = a->rect;
        layout_overlay_arrange(win);
      }
      return MAKEDWORD((uint16_t)MAX(1, win->frame.w), (uint16_t)MAX(1, win->frame.h));
    }
    case evResize:
      layout_overlay_arrange(win);
      return true;
    case evChildHitTest:
      return (result_t)layout_overlay_hit(win, (ipoint16_t){(int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)});
    default:
      // Creation, padding, painting and notification forwarding are a StackView's.
      return win_stack(win, msg, wparam, lparam);
  }
}
