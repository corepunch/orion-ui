// canvas_coords.c — Coordinate conversion between viewport and document space
// Extracts repeated math from win_canvas.c

#include "imageeditor.h"

// ── Helper functions ───────────────────────────────────────────────────────

static inline int cc_scaled_px(int px, float scale) {
  return (int)lroundf((float)px * scale);
}

static inline int cc_canvas_view_w(int win_w) {
#if IMAGEEDITOR_BW
  return MAX(0, win_w);
#endif
  return MAX(0, win_w - SCROLLBAR_WIDTH);
}

static inline int cc_canvas_scaled_w(const canvas_doc_t *doc, float scale) {
  return doc ? cc_scaled_px(doc->canvas_w, scale) : 0;
}

static inline int cc_canvas_scaled_h(const canvas_doc_t *doc, float scale) {
  return doc ? cc_scaled_px(doc->canvas_h, scale) : 0;
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

static inline int cc_canvas_view_axis_to_doc(int view_px, int origin_px, float scale) {
  if (scale <= 0.0f) return 0;
  return (int)floorf((float)(view_px - origin_px) / scale);
}

// ── Public API ─────────────────────────────────────────────────────────────

void canvas_view_to_doc(window_t *win, canvas_win_state_t *state,
                        int view_x, int view_y, int *doc_x, int *doc_y) {
  if (!win || !state) {
    if (doc_x) *doc_x = 0;
    if (doc_y) *doc_y = 0;
    return;
  }
  if (doc_x) {
    *doc_x = cc_canvas_view_axis_to_doc(view_x, cc_canvas_doc_origin_x(win, state), state->scale);
  }
  if (doc_y) {
    *doc_y = cc_canvas_view_axis_to_doc(view_y, cc_canvas_doc_origin_y(win, state), state->scale);
  }
}

ipoint16_t canvas_view_to_doc_point(window_t *win, canvas_win_state_t *state,
                                    int view_x, int view_y) {
  ipoint16_t pt;
  pt.x = cc_canvas_view_axis_to_doc(view_x, cc_canvas_doc_origin_x(win, state), state->scale);
  pt.y = cc_canvas_view_axis_to_doc(view_y, cc_canvas_doc_origin_y(win, state), state->scale);
  return pt;
}

void canvas_doc_to_view(window_t *win, canvas_win_state_t *state,
                        int doc_x, int doc_y, int *view_x, int *view_y) {
  if (!win || !state) {
    if (view_x) *view_x = 0;
    if (view_y) *view_y = 0;
    return;
  }
  if (view_x) {
    *view_x = cc_canvas_doc_origin_x(win, state) + cc_scaled_px(doc_x, state->scale);
  }
  if (view_y) {
    *view_y = cc_canvas_doc_origin_y(win, state) + cc_scaled_px(doc_y, state->scale);
  }
}

ipoint16_t canvas_doc_to_view_point(window_t *win, canvas_win_state_t *state,
                                    int doc_x, int doc_y) {
  ipoint16_t pt;
  pt.x = cc_canvas_doc_origin_x(win, state) + cc_scaled_px(doc_x, state->scale);
  pt.y = cc_canvas_doc_origin_y(win, state) + cc_scaled_px(doc_y, state->scale);
  return pt;
}

irect16_t canvas_doc_rect_to_view(window_t *win, canvas_win_state_t *state,
                                  int x0, int y0, int x1, int y1) {
  ipoint16_t p0 = canvas_doc_to_view_point(win, state, x0, y0);
  ipoint16_t p1 = canvas_doc_to_view_point(win, state, x1, y1);
  return R(p0.x, p0.y, p1.x - p0.x, p1.y - p0.y);
}
