#include "user.h"
#include "messages.h"
#include <orion/commctl/layout_shared.h>

static void layout_stack_measure_fallback(window_t *win, layout_measure_t *m) {
  irect16_t cr = get_client_rect(win);
  if (m) {
    if (m->avail_w > 0) cr.w = m->avail_w;
    if (m->avail_h > 0) cr.h = m->avail_h;
  }
  irect16_t content = layout_content_rect(win, cr);
  int content_w = content.w;
  int content_h = content.h;
  int count = 0;
  int desired_w = 0;
  int desired_h = 0;
  int gap = layout_spacing_for(win);

  for (window_t *child = win ? win->children : NULL; child; child = child->next) {
    layout_measure_t cm = layout_measure_child(child, content_w, content_h);
    if (layout_is_horizontal(win)) {
      if (count > 0) desired_w += gap;
      desired_w += cm.desired_w;
      if (cm.desired_h > desired_h) desired_h = cm.desired_h;
    } else {
      if (count > 0) desired_h += gap;
      desired_h += cm.desired_h;
      if (cm.desired_w > desired_w) desired_w = cm.desired_w;
    }
    count++;
  }

  if (count == 0) {
    desired_w = content.w;
    desired_h = content.h;
  }
  if (m) {
    irect16_t pad = layout_padding_for(win);
    m->desired_w = desired_w + pad.x + pad.w;
    m->desired_h = desired_h + pad.y + pad.h;
  }
}

static void layout_stack_arrange_fallback(window_t *win, const irect16_t *rect) {
  irect16_t cr = rect ? *rect : get_client_rect(win);
  irect16_t content = layout_content_rect(win, cr);
  int gap = layout_spacing_for(win);

  if (layout_is_horizontal(win)) {
    int total_fixed = 0;
    int stretch_count = 0;
    for (window_t *child = win ? win->children : NULL; child; child = child->next) {
      layout_measure_t cm = layout_measure_child(child, content.w, content.h);
      bool stretchable = (child->flags & WINDOW_FLEXSPACE) != 0;
      if (stretchable) stretch_count++;
      else total_fixed += cm.desired_w;
    }
    int total_gap = layout_horizontal_gap_total(win ? win->children : NULL, gap);
    int remaining = content.w - total_fixed - total_gap;
    if (remaining < 0) remaining = 0;
    int stretch_share = stretch_count > 0 ? remaining / stretch_count : 0;
    int x = content.x;
    bool placed_child = false;
    for (window_t *child = win ? win->children : NULL; child; child = child->next) {
      if (placed_child) x += gap;
      layout_measure_t cm = layout_measure_child(child, content.w, content.h);
      bool stretchable = (child->flags & WINDOW_FLEXSPACE) != 0;
      int cw = stretchable ? stretch_share : cm.desired_w;
      int ch = layout_apply_alignment(content.h, cm.desired_h, child->layout.v_align);
      int cy = content.y;
      if (child->layout.v_align == LAYOUT_ALIGN_CENTER)
        cy += (content.h - ch) / 2;
      else if (child->layout.v_align == LAYOUT_ALIGN_END)
        cy += content.h - ch;
      layout_arrange_child(child, R(x, cy, cw, ch));
      x += cw;
      placed_child = true;
    }
  } else {
    int total_fixed = 0;
    int stretch_count = 0;
    int count = 0;
    for (window_t *child = win ? win->children : NULL; child; child = child->next) {
      layout_measure_t cm = layout_measure_child(child, content.w, content.h);
      bool stretchable = (child->flags & WINDOW_FLEXSPACE) != 0;
      if (stretchable) stretch_count++;
      else total_fixed += cm.desired_h;
      count++;
    }
    int total_gap = (count > 0) ? gap * (count - 1) : 0;
    int remaining = content.h - total_fixed - total_gap;
    if (remaining < 0) remaining = 0;
    int stretch_share = stretch_count > 0 ? remaining / stretch_count : 0;

    int y = content.y;
    for (window_t *child = win ? win->children : NULL; child; child = child->next) {
      if (y > content.y) y += gap;
      layout_measure_t cm = layout_measure_child(child, content.w, content.h);
      int cw = layout_apply_alignment(content.w, cm.desired_w, child->layout.h_align);
      bool stretchable = (child->flags & WINDOW_FLEXSPACE) != 0;
      int ch = stretchable ? stretch_share : cm.desired_h;
      int cx = content.x;
      if (child->layout.h_align == LAYOUT_ALIGN_CENTER)
        cx += (content.w - cw) / 2;
      else if (child->layout.h_align == LAYOUT_ALIGN_END)
        cx += content.w - cw;
      layout_arrange_child(child, R(cx, y, cw, ch));
      y += ch;
    }
  }
}

void layout_measure_window(window_t *win, layout_measure_t *m) {
  if (!win || !m) return;
  if (win->flags & WINDOW_AUTO_LAYOUT) {
    layout_stack_measure_fallback(win, m);
    return;
  }
  irect16_t cr = get_client_rect(win);
  if (m->desired_w <= 0) m->desired_w = cr.w > 0 ? cr.w : 1;
  if (m->desired_h <= 0) m->desired_h = cr.h > 0 ? cr.h : 1;
}

void layout_arrange_window(window_t *win, const irect16_t *rect) {
  if (!win) return;
  if (win->flags & WINDOW_AUTO_LAYOUT)
    layout_stack_arrange_fallback(win, rect);
}

void window_set_layout(window_t *win, flags_t orientation, int spacing, irect16_t padding) {
  if (!win || spacing < 0 || spacing > 255) {
    fprintf(stderr, "[layout] window_set_layout rejected win=%u spacing=%d\n", win ? (unsigned)win->id : 0, spacing);
    fflush(stderr);
    return;
  }
  win->flags = (win->flags & ~WINDOW_STACK_HORIZONTAL) | (orientation & WINDOW_STACK_HORIZONTAL);
  win->layout.layout_spacing = (uint8_t)spacing;
  win->layout.layout_padding = padding;
  window_layout_sync(win);
}

void window_layout_sync(window_t *win) {
  if (!win || !(win->flags & WINDOW_AUTO_LAYOUT))
    return;
  if (win->flags & WINDOW_LAYOUT_CONTAINER) {
    send_message(win, evResize, 0, NULL);
    return;
  }
  irect16_t cr = get_client_rect(win);
  layout_arrange_window(win, &cr);
}
