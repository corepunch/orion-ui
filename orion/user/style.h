#ifndef __UI_STYLE_H__
#define __UI_STYLE_H__

// Style sheets: the subset of CSS that Orion themes are written in.
//
// A sheet is a list of rules. A selector is one compound selector (no
// combinators) built from:
//
//   Button  *  :root            element (a control or part name), any element, the root
//   .primary  #save             classes and an id
//   :hover :active :checked :disabled :focus :default :window-inactive
//   ::row  ::header  ::tab      a part of the element
//
// Selectors are separated by commas. Cascade order is CSS's: !important,
// then specificity (ids, classes and pseudo-classes, elements and parts),
// then source order. Custom properties (--name: value) cascade like any
// property; var(--name, fallback) looks them up on the queried element, then
// on :root. @media (prefers-color-scheme: dark|light) blocks apply when the
// sheet's appearance matches.
//
// Values are kept as text and parsed on lookup by the typed getters below:
// colours (#rgb, #rrggbb, #rrggbbaa, rgb(), rgba(), color-mix(in srgb, ...),
// transparent and a few names) and lengths (px or a bare number). A malformed rule or declaration
// is skipped with a diagnostic naming its line; the rest of the sheet still
// applies, as browsers recover.

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct style_sheet_s style_sheet_t;

// Pseudo-classes a query can carry.
enum {
  STYLE_HOVER           = 1 << 0,
  STYLE_ACTIVE          = 1 << 1,
  STYLE_CHECKED         = 1 << 2,
  STYLE_DISABLED        = 1 << 3,
  STYLE_FOCUS           = 1 << 4,
  STYLE_DEFAULT         = 1 << 5,
  STYLE_WINDOW_INACTIVE = 1 << 6,
};

// What is being styled: an element, an optional part, its classes (space
// separated), id and pseudo-class state. A NULL element is the :root.
typedef struct {
  const char *element;
  const char *part;
  const char *classes;
  const char *id;
  uint32_t    state;
} style_query_t;

#define STYLE_ROOT ((style_query_t){0})

// Parses CSS text. origin names it in diagnostics. Never returns NULL for
// non-NULL text except on allocation failure; errors are counted.
style_sheet_t *style_sheet_parse(const char *text, const char *origin);
style_sheet_t *style_sheet_load(const char *path);   // NULL when unreadable
void style_sheet_free(style_sheet_t *sheet);
int  style_sheet_error_count(const style_sheet_t *sheet);
int  style_sheet_rule_count(const style_sheet_t *sheet);

// Parses more CSS into the sheet, after its rules (a base sheet, then a theme).
bool style_sheet_append(style_sheet_t *sheet, const char *text, const char *origin);

// Which @media (prefers-color-scheme) blocks apply. Light by default.
void style_sheet_set_dark(style_sheet_t *sheet, bool dark);
bool style_sheet_dark(const style_sheet_t *sheet);

// Custom properties from outside the sheet, called with the name without "--";
// each returns true after writing the value. `override` answers before the
// sheet (themes route the colour roles through it, so var(--accent) follows
// the live palette and set_sys_colors() overrides); `fallback` answers names
// the sheet leaves undefined (platform metrics). Either may be NULL.
typedef bool (*style_var_fn)(const char *name, char *out, size_t n, void *ctx);
void style_sheet_set_var_hook(style_sheet_t *sheet, style_var_fn override, style_var_fn fallback, void *ctx);

// The cascaded value of property for query with var() resolved, copied into
// out. Returns false when no rule sets it (out is then empty).
bool style_value(const style_sheet_t *sheet, style_query_t q, const char *property, char *out, size_t n);
// The same, with the winning declaration's cascade rank (higher wins), so a
// caller can choose between a shorthand and a longhand; -1 when unset.
bool style_value_ranked(const style_sheet_t *sheet, style_query_t q, const char *property, char *out, size_t n, long *rank);

// Typed lookups: true when the property is set and parses; *out is left
// unchanged otherwise, so callers pass their default in.
bool style_color(const style_sheet_t *sheet, style_query_t q, const char *property, uint32_t *out);
bool style_length(const style_sheet_t *sheet, style_query_t q, const char *property, int *out);
bool style_number(const style_sheet_t *sheet, style_query_t q, const char *property, float *out);
// The custom property a value names directly, as "var(--light-edge)" names
// "light-edge"; for properties that bind to a system colour role.
bool style_var_name(const style_sheet_t *sheet, style_query_t q, const char *property, char *out, size_t n);

// Value parsers, shared with the getters. Colours are packed 0xAABBGGRR.
bool style_parse_color(const char *text, uint32_t *out);
bool style_parse_length(const char *text, int *out);

#endif
