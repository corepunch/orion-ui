#ifndef __UI_THEME_H__
#define __UI_THEME_H__

// Named spacing and geometry constants for widget draw code.
// Dimension constants that are owned by a specific subsystem (e.g.
// SCROLLBAR_WIDTH, TITLEBAR_HEIGHT) live in messages.h.  This file
// covers the per-widget magic numbers that would otherwise appear as
// bare integer literals inside paint handlers.

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

// Width and height of the hit-test box portion of a checkbox control.
#define CHECKBOX_BOX_SIZE    10

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
