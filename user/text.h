#ifndef __UI_TEXT_H__
#define __UI_TEXT_H__

#include <stdint.h>
#include <stdbool.h>

// ── Font role identifiers ─────────────────────────────────────────────────────
// Mirrors how Mac System 1–7 distinguished systemFont (Chicago 12) from
// application/content and compact icon-label fonts. Role names, not size names.
// At UI_WINDOW_SCALE >= 2 both map to the same atlas (SmallFont).
typedef enum {
  FONT_SYSTEM = 0,  // Chrome: titlebars, menus, buttons, dialogs (ChiKareGo2)
  FONT_SMALL  = 1,  // Content: list items, column rows, status bar (Geneva12)
  FONT_ICON   = 2,  // Compact labels in large icon/grid views (Geneva9)
} ui_font_t;

// ── Dynamic metric accessors ─────────────────────────────────────────────────
// FONT_SYSTEM metrics (backward-compatible; equivalent to FONT_SYSTEM variants).
// SmallFont (UI_WINDOW_SCALE > 1): char_height=8, line_height=12, space_width=3.
// ChiKareGo2 (UI_WINDOW_SCALE == 1): char_height=16, line_height=20, space_width=5.
// Return safe defaults before init_text_rendering() is called.
int get_char_height(void);
int get_line_height(void);
int get_space_width(void);

#define CHAR_HEIGHT       (get_char_height())
#define SMALL_LINE_HEIGHT (get_line_height())
#define SPACE_WIDTH       (get_space_width())

// Forward declaration
typedef struct irect16_s irect16_t;

// Initialize the text rendering system
void init_text_rendering(void);

// Clean up text rendering resources
void shutdown_text_rendering(void);

// Returns the pixel width of a single glyph from the FONT_SYSTEM atlas.
// Returns 0 when the text system is not yet initialized.
int char_width(unsigned char c);

#define TEXT_PADDING_LEFT  (1u << 0)   // add WIN_PADDING (4px) to the left
#define TEXT_ALIGN_RIGHT   (1u << 1)   // right-align to viewport's right edge
#define TEXT_ALIGN_CENTER   (1u << 2)   // center-align within the viewport (ignores TEXT_PADDING_LEFT)

// ── New explicit-font API ─────────────────────────────────────────────────────
// Pass font role at every call site — no hidden global state.
void draw_text(ui_font_t font, const char *text, int x, int y, uint32_t col);
void draw_text_clipped(ui_font_t font, const char *text,
                       irect16_t const *viewport, uint32_t col, uint32_t flags);
int  text_char_height(ui_font_t font);   // cell pixel height for the given font
int  text_strwidth(ui_font_t font, const char *text);  // pixel width of string
int  text_strnwidth(ui_font_t font, const char *text, int len); // pixel width of first len chars

// ── Legacy FONT_SYSTEM aliases (backward-compatible) ─────────────────────────
// These remain as real callable functions so existing extern declarations and
// call sites compile without change.
void draw_text_small(const char* text, int x, int y, uint32_t col);
void draw_text_small_clipped(const char* text, irect16_t const *viewport,
                              uint32_t col, uint32_t flags);
int strwidth(const char* text);
int strnwidth(const char* text, int text_length);

// ── Advanced text rendering ───────────────────────────────────────────────────
int calc_text_height(const char* text, int width);
void draw_text_wrapped(const char* text, irect16_t const *viewport, uint32_t col);

#endif // __UI_TEXT_H__
