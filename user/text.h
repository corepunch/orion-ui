#ifndef __UI_TEXT_H__
#define __UI_TEXT_H__

#include <stdint.h>
#include <stdbool.h>

// Dynamic font metrics — actual values depend on which font is active.
// SmallFont (WINDOW_SCALE > 1): char_height=8, line_height=12, space_width=3.
// ChiKareGo2 (WINDOW_SCALE == 1): char_height=16, line_height=20, space_width=5.
// Return SmallFont defaults before init_text_rendering() is called.
int get_char_height(void);
int get_line_height(void);
int get_space_width(void);

#define CHAR_HEIGHT       (get_char_height())
#define SMALL_LINE_HEIGHT (get_line_height())
#define SPACE_WIDTH       (get_space_width())

// Forward declaration
typedef struct rect_s rect_t;

// Initialize the text rendering system
void init_text_rendering(void);

// Clean up text rendering resources
void shutdown_text_rendering(void);

// Returns the pixel width of a single glyph from the active UI font.
// Returns 0 when the text system is not yet initialized.
int char_width(unsigned char c);

// Small bitmap font rendering
void draw_text_small(const char* text, int x, int y, uint32_t col);
int strwidth(const char* text);
int strnwidth(const char* text, int text_length);

// Advanced text rendering with wrapping and viewport clipping
int calc_text_height(const char* text, int width);
void draw_text_wrapped(const char* text, rect_t const *viewport, uint32_t col);

#endif // __UI_TEXT_H__
