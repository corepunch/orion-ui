#ifndef __UI_TEXT_H__
#define __UI_TEXT_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// ── Font role identifiers ─────────────────────────────────────────────────────
// Semantic font roles backed by proportional sans-serif TTF atlases.
typedef enum {
  FONT_SYSTEM = 0,  // Chrome: titlebars, menus, buttons, dialogs
  FONT_SMALL  = 1,  // Content: list items, column rows, status bar
  FONT_SMALLEST = 2,  // Compact labels in icon/grid views and dense controls
} ui_font_t;

// ── Dynamic metric accessors ─────────────────────────────────────────────────
// FONT_SYSTEM metrics (backward-compatible; equivalent to FONT_SYSTEM variants).
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
// Copies text into out (capacity n), cutting on a UTF-8 boundary and appending "..." so the
// result is at most max_w pixels wide. Returns the pixel width of out.
int  text_ellipsize(ui_font_t font, const char *text, int max_w, char *out, size_t n);
void draw_text_ellipsized(ui_font_t font, const char *text, int x, int y, int max_w, uint32_t col);

// ── Legacy FONT_SYSTEM aliases (backward-compatible) ─────────────────────────
// These remain as real callable functions so existing extern declarations and
// call sites compile without change.
void draw_text_small(const char* text, int x, int y, uint32_t col);
void draw_text_small_clipped(const char* text, irect16_t const *viewport,
                              uint32_t col, uint32_t flags);
int strwidth(const char* text);
int strnwidth(const char* text, int text_length);

// ── Advanced text rendering ───────────────────────────────────────────────────
typedef struct {
  int  width;
  int  height;
  bool wrapped;
} text_wrap_result_t;

text_wrap_result_t text_wrap_layout(const char* text, irect16_t const *viewport,
                                    uint32_t col, bool draw);
text_wrap_result_t text_wrap_layout_font(ui_font_t font, const char* text,
                                         irect16_t const *viewport,
                                         uint32_t col, bool draw);
int calc_text_height(const char* text, int width);
int calc_text_height_font(ui_font_t font, const char* text, int width);
void draw_text_wrapped(const char* text, irect16_t const *viewport, uint32_t col);
void draw_text_wrapped_font(ui_font_t font, const char* text,
                            irect16_t const *viewport, uint32_t col);

#endif // __UI_TEXT_H__
