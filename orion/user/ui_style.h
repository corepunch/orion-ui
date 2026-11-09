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
// A selector is one compound selector: Element, ::part, .class, #id and
// :pseudo-class (TabView::tab, Toolbar.compact, IconView::thumbnail:checked).
// Answers are cached until the style sheet or the palette changes. A property
// the sheet does not set is a framework error: it is reported once and reads
// as zero, so base.css (the user-agent sheet) must set every value code asks for.

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <orion/user/user.h>

typedef struct style_sheet_s style_sheet_t;

typedef struct {
  int blur;
  ipoint16_t offset;
  uint32_t color;
} ui_shadow_t;

style_sheet_t *ui_stylesheet(void);    // the active sheet: base.css and the theme's

// ── A window's own selectors ──────────────────────────────────────────────────
// A control asks for its element as it appears in its window: the window's
// classes and #id (its name in the form) join the selector, and its app
// instance's sheet answers. ui_selector(win, "TableView::header") is
// "TableView.compact#checkouts::header" for <TableView name="checkouts"
// class="compact">, and the element selector itself for an unstyled window.
// The result is interned: pass it to ui_px(), draw_text() and the rest.
typedef struct ui_scope_s ui_scope_t;
const char *ui_selector(const window_t *win, const char *element_selector);
const ui_scope_t *ui_window_scope(const window_t *win);   // NULL for an unstyled window
const char *ui_scope_id(const ui_scope_t *scope);
const char *ui_scope_classes(const ui_scope_t *scope);
style_sheet_t *ui_scope_stylesheet(const ui_scope_t *scope);
// Sets a window's #id and classes, as a form does from name= and class=.
void window_set_style(window_t *win, const char *id, const char *classes);

// ── Instance variables ────────────────────────────────────────────────────────
// Data a style sheet cannot know, such as the accent of one card or the tone of
// one badge, is a custom property on the instance (≈ style="--tone: #D29922").
// It answers var(--name) in that window's queries before the sheet does, so the
// sheet decides what the value means: `Badge { background: color-mix(in srgb,
// var(--tone) 15%, transparent) }`. A value may itself be var(--text-success),
// which then follows the live palette.
#define UI_MAX_STYLE_VARS 4
typedef struct ui_style_vars_s {
  int  count;
  char name[UI_MAX_STYLE_VARS][24];    // with the leading "--"
  char value[UI_MAX_STYLE_VARS][48];
} ui_style_vars_t;
// Sets, replaces or (value NULL or "") removes one variable. False when the
// name does not start with "--", or the value does not fit, or no slot is left.
bool ui_style_vars_set(ui_style_vars_t *vars, const char *name, const char *value);
bool ui_style_vars_set_color(ui_style_vars_t *vars, const char *name, uint32_t color);   // as #RRGGBBAA
bool ui_style_vars_equal(const ui_style_vars_t *a, const ui_style_vars_t *b);            // NULL = none
// A window's own variables (NULL when it has none), and setters that repaint it.
const ui_style_vars_t *window_style_vars(const window_t *win);
bool window_set_style_var(window_t *win, const char *name, const char *value);
bool window_set_style_var_color(window_t *win, const char *name, uint32_t color);

int  ui_px(const char *selector, const char *property);            // first length of the value
int  ui_radius(const char *selector);                              // border-radius, capsule = CORNER_RADIUS_CIRCULAR
float ui_number(const char *selector, const char *property);       // a number or a percentage (0.5 for 50%)
uint32_t ui_color(const char *selector, const char *property);
int  ui_role(const char *selector, const char *property);          // the colour role var(--role) names, 0 for none
bool ui_flag(const char *selector, const char *property);          // set, and not none, 0 or false
bool ui_keyword_is(const char *selector, const char *property, const char *keyword);
ui_shadow_t ui_shadow(const char *selector, const char *property); // "dx dy blur colour" or none
// The cascaded text of a property, var() resolved; false (quietly) when unset.
bool ui_string(const char *selector, const char *property, char *out, size_t n);

#endif
