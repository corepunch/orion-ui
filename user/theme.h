#ifndef __UI_THEME_H__
#define __UI_THEME_H__

// Named spacing and geometry constants for widget draw code.
// Dimension constants that are owned by a specific subsystem (e.g.
// SCROLLBAR_WIDTH, TITLEBAR_HEIGHT) live in messages.h.  This file
// covers the per-widget magic numbers that would otherwise appear as
// bare integer literals inside paint handlers.

// ── Theme icons ───────────────────────────────────────────────────────────
//
// Indices into share/orion/theme.png (128×16 px grayscale, 8×8 tiles).
// The sheet contains 16 columns × 2 rows; only the first THEME_ICON_COUNT
// entries are defined.  Use draw_theme_icon() to render them.
typedef enum {
  THEME_ICON_CLOSE        = 0,   // window close (×)
  THEME_ICON_ARROW_UP     = 1,   // up arrow, no tail (e.g. scroll-up button)
  THEME_ICON_ARROW_DOWN   = 2,   // down arrow, no tail
  THEME_ICON_ARROW_UPDOWN = 3,   // up-and-down arrow (combobox)
  THEME_ICON_CHECKMARK    = 4,   // checkmark (checkbox)
  THEME_ICON_SCROLL_UP    = 5,   // scroll-bar up
  THEME_ICON_SCROLL_RIGHT = 6,   // scroll-bar right
  THEME_ICON_SCROLL_DOWN  = 7,   // scroll-bar down
  THEME_ICON_SCROLL_LEFT  = 8,   // scroll-bar left
  THEME_ICON_RESIZE       = 9,   // resize grip (bottom-right window corner)
  THEME_ICON_COUNT        = 10,
} theme_icon_t;

// Native tile size of theme icons in logical pixels.
#define THEME_ICON_SIZE  8

// ── Text rendering ────────────────────────────────────────────────────────

// Standard 1-pixel drop-shadow offset used for all text labels.
// Shadow is drawn at (x + TEXT_SHADOW_OFFSET, y + TEXT_SHADOW_OFFSET)
// before the main text pass.
#define TEXT_SHADOW_OFFSET   1

// ── Buttons ───────────────────────────────────────────────────────────────

// Pixel inset from the button frame to the text/icon content area.
// Derived from the two-layer bevel drawn by draw_button() (2 px) plus
// one pixel of inner padding.
#define BUTTON_TEXT_INSET    3

// ── Checkboxes ────────────────────────────────────────────────────────────

// Pixels by which the focus-ring background extends beyond the box on
// each side (i.e. focus rect = box expanded by CHECKBOX_FOCUS_PAD).
#define CHECKBOX_FOCUS_PAD   2

// Horizontal gap between the right edge of the box and the left edge of
// the label text.
#define CHECKBOX_GAP         6

// Vertical offset of the label text baseline from the top of the frame.
#define CHECKBOX_TEXT_Y      2

// ── Labels ────────────────────────────────────────────────────────────────

// Vertical padding applied at the top of a label control before drawing
// its text (keeps text away from the window border).
#define LABEL_TEXT_PADDING   3

#endif /* __UI_THEME_H__ */
