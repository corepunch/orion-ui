#include <stdio.h>
#include "user.h"
#include "draw.h"
#include "theme.h"
#include "rect.h"

// Paint orchestration for drag visuals: the lift shadow under a lifted window and the pass that
// paints lifted windows above their root (≈ the layered-child pass of a compositor).

// True while the lifted copies are composited above the root. A drag copy is
// painted in both passes; a plain drag visual only in this one.
static bool g_lift_pass;

bool paint_lifted_now(const window_t *a) {
  return window_is_lifted(a) && (!window_lift_is_copy(a) || g_lift_pass);
}

bool window_lift_offset(const window_t *win, int *dx, int *dy) {
  bool lifted = false;
  *dx = *dy = 0;
  for (const window_t *a = win; a; a = a->parent) {
    if (!paint_lifted_now(a)) continue;
    *dx += window_lift_delta(a).x;
    *dy += window_lift_delta(a).y;
    lifted = true;
  }
  return lifted;
}

// Paint the whole lifted frame before its non-client chrome and client content.
void paint_lift_shadow(window_t *root, window_t *win) {
  theme_t *theme = get_theme();
  int blur = theme->drag.shadow.blur;
  if (blur <= 0) return;
  int dx, dy;
  window_lift_offset(win, &dx, &dy);
  irect16_t frame = R(window_screen_x(win) - root->frame.x + dx,
                     window_screen_y(win) - root->frame.y + dy, win->frame.w, win->frame.h);
  set_viewport_for_fbo(root);
  set_projection(0, 0, root->frame.w, root->frame.h);
  set_scissor_fbo(root, R(0, 0, root->frame.w, root->frame.h));
  draw_rect_shadow(frame, (float)theme->card.corner_radius, (float)blur,
                   theme->drag.shadow.offset, theme->drag.shadow.color);
}

// Lifted windows were omitted from the in-place walk. Paint them above the root.
static void paint_lifted(window_t *win) {
  for (window_t *c = win->children; c; c = c->next) {
    if (!window_has_state(c, WINDOW_STATE_VISIBLE)) continue;
    if (window_is_lifted(c)) send_message(c, evPaint, 0, NULL);
    paint_lifted(c);
  }
}

void paint_drag_visuals(window_t *win) {
  g_lift_pass = true;
  paint_lifted(win);
  g_lift_pass = false;
}

