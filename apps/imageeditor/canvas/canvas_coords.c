// canvas_coords.c — Coordinate conversion between viewport and document space
// Extracts repeated math from win_canvas.c

#include "imageeditor.h"

// ── Helper functions ───────────────────────────────────────────────────────

static inline int cc_scaled_px(int px, float scale) {
  return (int)lroundf((float)px * scale);
}

static inline int cc_canvas_view_w(int win_w) {
  return MAX(0, win_w);
}

static inline int cc_canvas_scaled_w(const canvas_doc_t *doc, float scale) {
  return doc ? cc_scaled_px(doc->canvas_w, scale) / g_bw_retina_scale : 0;
}

static inline int cc_canvas_scaled_h(const canvas_doc_t *doc, float scale) {
  return doc ? cc_scaled_px(doc->canvas_h, scale) / g_bw_retina_scale : 0;
}

static inline int cc_canvas_center_offset_x(const canvas_doc_t *doc, float scale, int win_w) {
  if (!doc) return 0;
  int view_w = cc_canvas_view_w(win_w);
  int doc_w = cc_canvas_scaled_w(doc, scale);
  return (doc_w < view_w) ? (view_w - doc_w) / 2 : 0;
}

static inline int cc_canvas_center_offset_y(const canvas_doc_t *doc, float scale, int win_h) {
  if (!doc) return 0;
  int doc_h = cc_canvas_scaled_h(doc, scale);
  return (doc_h < win_h) ? (win_h - doc_h) / 2 : 0;
}

static inline int cc_canvas_doc_origin_x(window_t *win, canvas_win_state_t *state) {
  if (!win || !state) return 0;
  return cc_canvas_center_offset_x(state->doc, state->scale, win->frame.w) - state->pan.x;
}

static inline int cc_canvas_doc_origin_y(window_t *win, canvas_win_state_t *state) {
  if (!win || !state) return 0;
  return cc_canvas_center_offset_y(state->doc, state->scale, win->frame.h) - state->pan.y;
}

// ── Public API ─────────────────────────────────────────────────────────────

void canvas_transform_point(window_t *win, const canvas_win_state_t *state, float *x, float *y, bool inverse) {
  float cx = win->frame.w * 0.5f, cy = win->frame.h * 0.5f;
  float c = cosf(state->rotation), s = sinf(state->rotation);
  float px = *x - cx, py = *y - cy;
  if (inverse) {
    px -= state->translate_x; py -= state->translate_y;
    *x = cx + c * px + s * py;
    *y = cy - s * px + c * py;
  } else {
    *x = cx + c * px - s * py + state->translate_x;
    *y = cy + s * px + c * py + state->translate_y;
  }
}

void canvas_apply_gesture(window_t *win, canvas_win_state_t *state, const ax_gesture_t *gesture) {
  float x = gesture->previous_x, y = gesture->previous_y;
  canvas_transform_point(win, state, &x, &y, true);
  float old_scale = state->scale;
  float dx = (x - cc_canvas_doc_origin_x(win, state)) / old_scale;
  float dy = (y - cc_canvas_doc_origin_y(win, state)) / old_scale;
  state->scale = CLAMP(old_scale * gesture->scale, 0.05f, 32.0f);
  state->rotation = remainderf(state->rotation + gesture->rotation, 2.0f * (float)M_PI);
  state->translate_x = state->translate_y = 0;
  x = cc_canvas_doc_origin_x(win, state) + dx * state->scale;
  y = cc_canvas_doc_origin_y(win, state) + dy * state->scale;
  canvas_transform_point(win, state, &x, &y, false);
  state->translate_x = gesture->x - x;
  state->translate_y = gesture->y - y;
}

void canvas_view_to_doc(window_t *win, canvas_win_state_t *state,
                        int view_x, int view_y, int *doc_x, int *doc_y) {
  if (!win || !state || state->scale <= 0) {
    if (doc_x) *doc_x = 0;
    if (doc_y) *doc_y = 0;
    return;
  }
  float x = view_x, y = view_y;
  canvas_transform_point(win, state, &x, &y, true);
  if (doc_x) {
    *doc_x = (int)floorf((x - cc_canvas_doc_origin_x(win, state)) * g_bw_retina_scale / state->scale);
  }
  if (doc_y) {
    *doc_y = (int)floorf((y - cc_canvas_doc_origin_y(win, state)) * g_bw_retina_scale / state->scale);
  }
}

ipoint16_t canvas_view_to_doc_point(window_t *win, canvas_win_state_t *state,
                                    int view_x, int view_y) {
  ipoint16_t pt;
  int x, y;
  canvas_view_to_doc(win, state, view_x, view_y, &x, &y);
  pt.x = x; pt.y = y;
  return pt;
}

void canvas_doc_to_view(window_t *win, canvas_win_state_t *state,
                        int doc_x, int doc_y, int *view_x, int *view_y) {
  if (!win || !state) {
    if (view_x) *view_x = 0;
    if (view_y) *view_y = 0;
    return;
  }
  float x = cc_canvas_doc_origin_x(win, state) + cc_scaled_px(doc_x, state->scale / g_bw_retina_scale);
  float y = cc_canvas_doc_origin_y(win, state) + cc_scaled_px(doc_y, state->scale / g_bw_retina_scale);
  canvas_transform_point(win, state, &x, &y, false);
  if (view_x) *view_x = (int)lroundf(x);
  if (view_y) *view_y = (int)lroundf(y);
}

ipoint16_t canvas_doc_to_view_point(window_t *win, canvas_win_state_t *state,
                                    int doc_x, int doc_y) {
  ipoint16_t pt;
  int x, y;
  canvas_doc_to_view(win, state, doc_x, doc_y, &x, &y);
  pt.x = x; pt.y = y;
  return pt;
}

irect16_t canvas_doc_rect_to_view(window_t *win, canvas_win_state_t *state,
                                  int x0, int y0, int x1, int y1) {
  ipoint16_t p0 = canvas_doc_to_view_point(win, state, x0, y0);
  ipoint16_t p1 = canvas_doc_to_view_point(win, state, x1, y1);
  ipoint16_t p2 = canvas_doc_to_view_point(win, state, x0, y1);
  ipoint16_t p3 = canvas_doc_to_view_point(win, state, x1, y0);
  int left = MIN(MIN(p0.x, p1.x), MIN(p2.x, p3.x));
  int top = MIN(MIN(p0.y, p1.y), MIN(p2.y, p3.y));
  int right = MAX(MAX(p0.x, p1.x), MAX(p2.x, p3.x));
  int bottom = MAX(MAX(p0.y, p1.y), MAX(p2.y, p3.y));
  return R(left, top, right - left, bottom - top);
}
