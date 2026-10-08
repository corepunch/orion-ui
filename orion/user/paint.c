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

// Drop shadow under a lifted window. The proc stays on the tight client scissor,
// so the offset part of the shadow remains visible underneath the fill.
void paint_lift_shadow(window_t *root, window_t *win, irect16_t clip) {
  theme_t *theme = get_theme();
  int blur = theme->drag_shadow_blur;
  if (blur <= 0) return;
  int pad = blur * 3 + 1;
  int ax = theme->drag_shadow_offset.x < 0 ? -theme->drag_shadow_offset.x : theme->drag_shadow_offset.x;
  int ay = theme->drag_shadow_offset.y < 0 ? -theme->drag_shadow_offset.y : theme->drag_shadow_offset.y;
  irect16_t wide = R(clip.x - pad - ax - 1, clip.y - pad - ay - 1,
                     clip.w + 2 * (pad + ax + 1), clip.h + 2 * (pad + ay + 1));
  set_scissor_fbo(root, isect_rect(wide, R(0, 0, root->frame.w, root->frame.h)));
  draw_rect_shadow(get_client_rect(win), (float)theme->card_corner_radius, (float)blur,
                   theme->drag_shadow_offset, theme->drag_shadow_color);
  set_scissor_fbo(root, clip);
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

