#include "user.h"
#include "rect.h"
#include <math.h>
#include <stdio.h>

static bool view_ready(const window_t *win) {
  if (win && win->view.enabled) return true;
  fprintf(stderr, "[view] unavailable content view win=%u\n", win ? win->id : 0);
  fflush(stderr);
  return false;
}

static void view_changed(window_t *win) {
  // view_matrix_t *m = &win->view.matrix;
  // fprintf(stderr, "[view] change win=%u matrix=(%.3f,%.3f,%.1f,%.1f)\n", win->id, m->a, m->b, m->tx, m->ty);
  // fflush(stderr);
  invalidate_window(win);
}

static void view_unmap(const window_t *win, float *x, float *y) {
  const view_matrix_t *m = &win->view.matrix;
  float dx = *x - m->tx, dy = *y - m->ty, determinant = m->a * m->a + m->b * m->b;
  *x = (m->a * dx + m->b * dy) / determinant;
  *y = (m->a * dy - m->b * dx) / determinant;
}

float window_view_zoom(const window_t *win) {
  return win && win->view.enabled ? hypotf(win->view.matrix.a, win->view.matrix.b) * win->view.pixel_ratio : 1;
}

frect_t window_view_bounds(const window_t *win) {
  if (!view_ready(win)) return (frect_t){0};
  const view_matrix_t *m = &win->view.matrix;
  float ax = m->a * win->view.width, bx = -m->b * win->view.height;
  float ay = m->b * win->view.width, by = m->a * win->view.height;
  return (frect_t){m->tx + MIN(0, ax) + MIN(0, bx), m->ty + MIN(0, ay) + MIN(0, by),
                   fabsf(ax) + fabsf(bx), fabsf(ay) + fabsf(by)};
}

static void view_clamp(window_t *win) {
  if (win->view.free_pan) return;
  frect_t bounds = window_view_bounds(win);
  irect16_t client = get_client_rect(win);
  float x = bounds.w < client.w ? floorf((client.w - bounds.w) / 2) : CLAMP(bounds.x, client.w - bounds.w, 0);
  float y = bounds.h < client.h ? floorf((client.h - bounds.h) / 2) : CLAMP(bounds.y, client.h - bounds.h, 0);
  win->view.matrix.tx += x - bounds.x;
  win->view.matrix.ty += y - bounds.y;
}

void window_view_center(window_t *win) {
  if (!view_ready(win)) return;
  frect_t bounds = window_view_bounds(win);
  irect16_t client = get_client_rect(win);
  win->view.matrix.tx += floorf((client.w - bounds.w) / 2) - bounds.x;
  win->view.matrix.ty += floorf((client.h - bounds.h) / 2) - bounds.y;
  view_changed(win);
}

void window_view_init(window_t *win, int width, int height, float pixel_ratio, bool free_pan) {
  if (!win || width <= 0 || height <= 0 || !isfinite(pixel_ratio) || pixel_ratio <= 0) {
    fprintf(stderr, "[view] invalid initialization win=%u size=%dx%d ratio=%f\n", win ? win->id : 0, width, height, pixel_ratio);
    fflush(stderr);
    return;
  }
  win->view = (window_view_t){.enabled = true, .free_pan = free_pan, .width = width, .height = height,
                            .pixel_ratio = pixel_ratio, .matrix = {.a = 1 / pixel_ratio}};
  view_clamp(win);
  if (free_pan) window_view_center(win); else view_changed(win);
}

void window_view_set_size(window_t *win, int width, int height) {
  if (!view_ready(win)) return;
  if (width <= 0 || height <= 0) {
    fprintf(stderr, "[view] invalid content size win=%u size=%dx%d\n", win->id, width, height);
    fflush(stderr);
    return;
  }
  win->view.width = width; win->view.height = height;
  view_clamp(win);
  view_changed(win);
}

void window_view_apply_gesture(window_t *win, const ax_gesture_t *gesture) {
  if (!view_ready(win)) return;
  if (!gesture || !isfinite(gesture->scale) || gesture->scale <= 0 || !isfinite(gesture->rotation) ||
      !isfinite(gesture->x) || !isfinite(gesture->y) || !isfinite(gesture->previous_x) || !isfinite(gesture->previous_y)) {
    fprintf(stderr, "[view] invalid gesture win=%u\n", win->id); fflush(stderr); return;
  }
  view_matrix_t old = win->view.matrix;
  float zoom = window_view_zoom(win), factor = CLAMP(zoom * gesture->scale, 0.05f, 32.0f) / zoom;
  float c = cosf(gesture->rotation) * factor, s = sinf(gesture->rotation) * factor;
  float x = old.tx - gesture->previous_x, y = old.ty - gesture->previous_y;
  win->view.matrix = (view_matrix_t){c * old.a - s * old.b, s * old.a + c * old.b,
                                    gesture->x + c * x - s * y, gesture->y + s * x + c * y};
  view_clamp(win);
  view_changed(win);
}

void window_view_set_zoom(window_t *win, float zoom, const ipoint16_t *content_anchor) {
  if (!view_ready(win)) return;
  if (!isfinite(zoom) || zoom <= 0) {
    fprintf(stderr, "[view] invalid zoom win=%u zoom=%f\n", win->id, zoom); fflush(stderr); return;
  }
  irect16_t client = get_client_rect(win);
  ipoint16_t point = content_anchor ? window_content_to_client(win, *content_anchor) : (ipoint16_t){client.w / 2, client.h / 2};
  ax_gesture_t gesture = {AX_GESTURE_UPDATE, point.x, point.y, point.x, point.y, zoom / window_view_zoom(win), 0};
  window_view_apply_gesture(win, &gesture);
}

void window_view_pan(window_t *win, ipoint16_t delta) {
  if (!view_ready(win)) return;
  win->view.matrix.tx += delta.x; win->view.matrix.ty += delta.y;
  view_clamp(win);
  view_changed(win);
}

void window_view_begin_drag(window_t *win) {
  if (!view_ready(win)) return;
  win->view.drag_pointer = win->view.pointer;
}

void window_view_drag(window_t *win) {
  if (!view_ready(win)) return;
  ipoint16_t from = win->view.drag_pointer, to = win->view.pointer;
  window_view_pan(win, (ipoint16_t){to.x - from.x, to.y - from.y});
  win->view.drag_pointer = to;
}

int window_view_scroll(const window_t *win, int axis) {
  if (!view_ready(win)) return 0;
  if (axis != SB_HORZ && axis != SB_VERT) {
    fprintf(stderr, "[view] invalid scroll axis win=%u axis=%d\n", win->id, axis); fflush(stderr); return 0;
  }
  frect_t bounds = window_view_bounds(win);
  return (int)lroundf(MAX(0, -(axis == SB_HORZ ? bounds.x : bounds.y)));
}

void window_view_set_scroll(window_t *win, int axis, int pos) {
  if (!view_ready(win)) return;
  if ((axis != SB_HORZ && axis != SB_VERT) || pos < 0) {
    fprintf(stderr, "[view] invalid scroll win=%u axis=%d pos=%d\n", win->id, axis, pos); fflush(stderr); return;
  }
  frect_t bounds = window_view_bounds(win);
  if (axis == SB_HORZ) win->view.matrix.tx -= bounds.x + pos;
  else win->view.matrix.ty -= bounds.y + pos;
  view_clamp(win);
  view_changed(win);
}

ipoint16_t window_content_to_client(const window_t *win, ipoint16_t point) {
  if (!win || !win->view.enabled) return point;
  const view_matrix_t *m = &win->view.matrix;
  return (ipoint16_t){(int)lroundf(m->a * point.x - m->b * point.y + m->tx),
                     (int)lroundf(m->b * point.x + m->a * point.y + m->ty)};
}

ipoint16_t window_client_to_content(const window_t *win, ipoint16_t point) {
  if (!win || !win->view.enabled) return point;
  float x = point.x, y = point.y;
  view_unmap(win, &x, &y);
  return (ipoint16_t){(int)floorf(x + 0.00001f), (int)floorf(y + 0.00001f)};
}

irect16_t window_view_visible_rect(const window_t *win) {
  if (!view_ready(win)) return R(0, 0, 0, 0);
  irect16_t client = get_client_rect(win);
  float left = INFINITY, top = INFINITY, right = -INFINITY, bottom = -INFINITY;
  for (int corner = 0; corner < 4; corner++) {
    float x = (corner & 1) ? client.w : 0, y = (corner & 2) ? client.h : 0;
    view_unmap(win, &x, &y);
    left = MIN(left, x); top = MIN(top, y); right = MAX(right, x); bottom = MAX(bottom, y);
  }
  left = CLAMP(floorf(left), 0, win->view.width); top = CLAMP(floorf(top), 0, win->view.height);
  right = CLAMP(ceilf(right), left, win->view.width); bottom = CLAMP(ceilf(bottom), top, win->view.height);
  return R(left, top, right - left, bottom - top);
}
