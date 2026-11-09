#ifndef __UI_STYLE_QUERY_H__
#define __UI_STYLE_QUERY_H__

// Computed style for the framework: what the active style sheet says about an
// element, as a browser's getComputedStyle answers a page. Code asks for the
// value it lays out or paints with, by selector and property:
//
//   int gutter = ui_px("Scrollbar", "width");
//   int radius = ui_radius("Button");                     // CORNER_RADIUS_CIRCULAR for 9999px
//   ui_shadow_t s = ui_shadow("Tooltip", "box-shadow");
//
// A selector is one compound selector: Element, ::part, .class and
// :pseudo-class (TabView::tab, Toolbar.compact, IconView::thumbnail:checked).
// Answers are cached until the style sheet or the palette changes. A property
// the sheet does not set is a framework error: it is reported once and reads
// as zero, so base.css (the user-agent sheet) must set every value code asks for.

#include <stdbool.h>
#include <stdint.h>
#include <orion/user/user.h>

typedef struct style_sheet_s style_sheet_t;

typedef struct {
  int blur;
  ipoint16_t offset;
  uint32_t color;
} ui_shadow_t;

style_sheet_t *ui_stylesheet(void);    // the active sheet: base.css and the theme's

int  ui_px(const char *selector, const char *property);            // first length of the value
int  ui_radius(const char *selector);                              // border-radius, capsule = CORNER_RADIUS_CIRCULAR
float ui_number(const char *selector, const char *property);       // a number or a percentage (0.5 for 50%)
uint32_t ui_color(const char *selector, const char *property);
int  ui_role(const char *selector, const char *property);          // the colour role var(--role) names, 0 for none
bool ui_flag(const char *selector, const char *property);          // set, and not none, 0 or false
bool ui_keyword_is(const char *selector, const char *property, const char *keyword);
ui_shadow_t ui_shadow(const char *selector, const char *property); // "dx dy blur colour" or none

#endif
