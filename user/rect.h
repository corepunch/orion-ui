#ifndef __UI_RECT_H__
#define __UI_RECT_H__

// irect16_t layout helpers — analogous to WinAPI InflateRect / OffsetRect /
// IntersectRect, but oriented around the split-and-slice idiom used by
// toolbar and widget layout code.
//
// All helpers work on value copies and return a new irect16_t.
//
// Split/trim pairs: rect_split_* returns the sliced strip; rect_trim_*
// returns the complementary remainder (i.e. r minus the same strip).
// Together they cover the common "take N pixels from one edge, use both
// parts" pattern without pointer mutation.

#include "user.h"

// Convenience literal rectangle value (pass-by-value counterpart to MAKERECT).
#ifndef R
#define R(X, Y, W, H) ((irect16_t){ (X), (Y), (W), (H) })
#endif

// ── Inflation / deflation ─────────────────────────────────────────────────

// Shrink all four sides by d (negative d expands).
static inline irect16_t rect_inset(irect16_t r, int d) {
  return (irect16_t){ r.x + d, r.y + d, r.w - 2*d, r.h - 2*d };
}

// Shrink horizontal sides by dx and vertical sides by dy independently.
static inline irect16_t rect_inset_xy(irect16_t r, int dx, int dy) {
  return (irect16_t){ r.x + dx, r.y + dy, r.w - 2*dx, r.h - 2*dy };
}

// ── Translation ───────────────────────────────────────────────────────────

// Translate without changing size.
static inline irect16_t rect_offset(irect16_t r, int dx, int dy) {
  return (irect16_t){ r.x + dx, r.y + dy, r.w, r.h };
}

// ── Centering ─────────────────────────────────────────────────────────────

// Return a rect of given size centered inside r.
static inline irect16_t rect_center(irect16_t r, int w, int h) {
  return (irect16_t){ r.x + (r.w - w) / 2, r.y + (r.h - h) / 2, w, h };
}

// ── Edge splits (return sliced strip; original rect is unchanged) ─────────

// Slice a strip of width w off the left edge of r.
// Returns the sliced strip.
static inline irect16_t rect_split_left(irect16_t r, int w) {
  return (irect16_t){ r.x, r.y, w, r.h };
}

// Slice a strip of height h off the top edge of r.
// Returns the sliced strip.
static inline irect16_t rect_split_top(irect16_t r, int h) {
  return (irect16_t){ r.x, r.y, r.w, h };
}

// Slice a strip of width w off the right edge of r.
// Returns the sliced strip.
static inline irect16_t rect_split_right(irect16_t r, int w) {
  return (irect16_t){ r.x + r.w - w, r.y, w, r.h };
}

// Slice a strip of height h off the bottom edge of r.
// Returns the sliced strip.
static inline irect16_t rect_split_bottom(irect16_t r, int h) {
  return (irect16_t){ r.x, r.y + r.h - h, r.w, h };
}

// ── Edge trims (return remainder; original rect is unchanged) ────────────

// Return r with w pixels removed from the left edge (complement of rect_split_left).
static inline irect16_t rect_trim_left(irect16_t r, int w) {
  return (irect16_t){ r.x + w, r.y, r.w - w, r.h };
}

// Return r with h pixels removed from the top edge (complement of rect_split_top).
static inline irect16_t rect_trim_top(irect16_t r, int h) {
  return (irect16_t){ r.x, r.y + h, r.w, r.h - h };
}

// Return r with w pixels removed from the right edge (complement of rect_split_right).
static inline irect16_t rect_trim_right(irect16_t r, int w) {
  return (irect16_t){ r.x, r.y, r.w - w, r.h };
}

// Return r with h pixels removed from the bottom edge (complement of rect_split_bottom).
static inline irect16_t rect_trim_bottom(irect16_t r, int h) {
  return (irect16_t){ r.x, r.y, r.w, r.h - h };
}

#endif /* __UI_RECT_H__ */