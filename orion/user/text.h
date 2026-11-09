#ifndef __UI_TEXT_H__
#define __UI_TEXT_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// ── Fonts ─────────────────────────────────────────────────────────────────────
// A font is a CSS selector: text drawn with "TableView::row" takes the `font`
// the style sheet gives that element and part, or :root's when it sets none.
// Controls name their own element; free text uses the elements base.css
// defines for it: "Text", "Caption", "Heading", "Title" and "Display"
// (as HTML has p, small, h3, h2 and h1). NULL is "Text".
typedef const char *ui_font_t;

// Forward declaration
typedef struct irect16_s irect16_t;

// Initialize the text rendering system
void init_text_rendering(void);

// Clean up text rendering resources
void shutdown_text_rendering(void);


#define TEXT_PADDING_LEFT  (1u << 0)   // add WIN_PADDING (4px) to the left
#define TEXT_ALIGN_RIGHT   (1u << 1)   // right-align to viewport's right edge
#define TEXT_ALIGN_CENTER   (1u << 2)   // center-align within the viewport (ignores TEXT_PADDING_LEFT)

// ── Drawing and measuring ─────────────────────────────────────────────────────
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

// ── Advanced text rendering ───────────────────────────────────────────────────
typedef struct {
  int  width;
  int  height;
  bool wrapped;
} text_wrap_result_t;

text_wrap_result_t text_wrap_layout_font(ui_font_t font, const char* text,
                                         irect16_t const *viewport,
                                         uint32_t col, bool draw);
int calc_text_height_font(ui_font_t font, const char* text, int width);
void draw_text_wrapped_font(ui_font_t font, const char* text,
                            irect16_t const *viewport, uint32_t col);

#endif // __UI_TEXT_H__
