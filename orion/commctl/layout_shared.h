#ifndef __UI_COMMCTL_LAYOUT_SHARED_H__
#define __UI_COMMCTL_LAYOUT_SHARED_H__

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/rect.h>

static inline int layout_apply_alignment(int avail, int desired, uint8_t align) {
  if (avail < 0) avail = 0;
  if (desired < 0) desired = 0;
  if (align == LAYOUT_ALIGN_STRETCH) return avail;
  if (desired > avail) desired = avail;
  return desired;
}

static inline bool layout_is_horizontal(const window_t *win) {
  return win && (win->flags & WINDOW_STACK_HORIZONTAL) != 0;
}

static inline bool layout_child_is_flex(const window_t *child) {
  return child && (child->flags & WINDOW_FLEXSPACE) != 0;
}

static inline int layout_child_count(const window_t *win) {
  int count = 0;
  for (window_t *child = win ? win->children : NULL; child; child = child->next)
    count++;
  return count;
}

static inline window_t *layout_child_at(window_t *win, int index) {
  int i = 0;
  for (window_t *child = win ? win->children : NULL; child; child = child->next, i++) {
    if (i == index) return child;
  }
  return NULL;
}

static inline int layout_spacing_for(window_t *win) {
  return win ? (int)win->layout.layout_spacing : 0;
}

static inline irect16_t layout_padding_for(window_t *win) {
  return win ? win->layout.layout_padding : (irect16_t){0, 0, 0, 0};
}

static inline irect16_t layout_margin_for(window_t *win) {
  return win ? win->layout.layout_margin : (irect16_t){0, 0, 0, 0};
}

static inline irect16_t layout_inset_rect(irect16_t r, irect16_t inset) {
  r.x += inset.x;
  r.y += inset.y;
  r.w -= inset.x + inset.w;
  r.h -= inset.y + inset.h;
  if (r.w < 0) r.w = 0;
  if (r.h < 0) r.h = 0;
  return r;
}

static inline irect16_t layout_content_rect(window_t *win, irect16_t r) {
  return layout_inset_rect(r, layout_padding_for(win));
}

static inline layout_measure_t layout_measure_child(window_t *child, int avail_w, int avail_h) {
  irect16_t margin = child ? child->layout.layout_margin : (irect16_t){0, 0, 0, 0};
  avail_w -= margin.x + margin.w;
  avail_h -= margin.y + margin.h;
  if (avail_w < 0) avail_w = 0;
  if (avail_h < 0) avail_h = 0;
  layout_measure_t m = {
    .avail_w = avail_w,
    .avail_h = avail_h,
    .desired_w = child && child->layout.layout_fixed_w > 0 ? child->layout.layout_fixed_w : 1,
    .desired_h = child && child->layout.layout_fixed_h > 0 ? child->layout.layout_fixed_h : 1,
  };
  if (child) {
    send_message(child, evMeasure, 0, &m);
    m.desired_w += margin.x + margin.w;
    m.desired_h += margin.y + margin.h;
    if (m.desired_w < 1) m.desired_w = 1;
    if (m.desired_h < 1) m.desired_h = 1;
  }
  return m;
}

// Docked panes are placed by their dock host (dock_layout), never by the host's auto-layout.
static inline void layout_arrange_child(window_t *child, irect16_t rect) {
  if (child && child->dock) return;
  rect = layout_inset_rect(rect, layout_margin_for(child));
  layout_arrange_t a = {
    .rect = rect,
    .h_align = child ? child->layout.h_align : LAYOUT_ALIGN_STRETCH,
    .v_align = child ? child->layout.v_align : LAYOUT_ALIGN_STRETCH,
  };
  send_message(child, evArrange, 0, &a);
}

static inline int layout_horizontal_gap_total(window_t *first, int gap) {
  int count = 0;
  for (window_t *child = first; child; child = child->next) count++;
  return count > 1 ? gap * (count - 1) : 0;
}

static inline void layout_paint_children(window_t *win) {
  if (!win) return;
  for (window_t *child = win->children; child; child = child->next) {
    send_message(child, evPaint, 0, NULL);
  }
}

#endif
