#ifndef __UI_RECT_H__
#define __UI_RECT_H__

// rect_t layout helpers — analogous to WinAPI InflateRect / OffsetRect /
// IntersectRect, but oriented around the split-and-slice idiom used by
// toolbar and widget layout code.
//
// All helpers work on value copies and return a new rect_t.  The split
// variants return only the sliced strip; the caller retains the original
// rect and may compute the remainder with a complementary trim if needed.

#include "user.h"

// Convenience literal rectangle pointer, analogous to MAKERECT.
#ifndef R
#define R(X, Y, W, H) (&(rect_t){ (X), (Y), (W), (H) })
#endif

// ── Inflation / deflation ─────────────────────────────────────────────────

// Shrink all four sides by d (negative d expands).
static inline rect_t rect_inset(rect_t r, int d) {
  return (rect_t){ r.x + d, r.y + d, r.w - 2*d, r.h - 2*d };
}

// Shrink horizontal sides by dx and vertical sides by dy independently.
static inline rect_t rect_inset_xy(rect_t r, int dx, int dy) {
  return (rect_t){ r.x + dx, r.y + dy, r.w - 2*dx, r.h - 2*dy };
}

// ── Translation ───────────────────────────────────────────────────────────

// Translate without changing size.
static inline rect_t rect_offset(rect_t r, int dx, int dy) {
  return (rect_t){ r.x + dx, r.y + dy, r.w, r.h };
}

// ── Centering ─────────────────────────────────────────────────────────────

// Return a rect of given size centered inside r.
static inline rect_t rect_center(rect_t r, int w, int h) {
  return (rect_t){ r.x + (r.w - w) / 2, r.y + (r.h - h) / 2, w, h };
}

// ── Edge splits (return sliced strip; original rect is unchanged) ─────────

// Slice a strip of width w off the left edge of r.
// Returns the sliced strip.
static inline rect_t rect_split_left(rect_t r, int w) {
  return (rect_t){ r.x, r.y, w, r.h };
}

// Slice a strip of height h off the top edge of r.
// Returns the sliced strip.
static inline rect_t rect_split_top(rect_t r, int h) {
  return (rect_t){ r.x, r.y, r.w, h };
}

// Slice a strip of width w off the right edge of r.
// Returns the sliced strip.
static inline rect_t rect_split_right(rect_t r, int w) {
  return (rect_t){ r.x + r.w - w, r.y, w, r.h };
}

// Slice a strip of height h off the bottom edge of r.
// Returns the sliced strip.
static inline rect_t rect_split_bottom(rect_t r, int h) {
  return (rect_t){ r.x, r.y + r.h - h, r.w, h };
}

#endif /* __UI_RECT_H__ */
