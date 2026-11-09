// Computed style queries over the active style sheet (ui_style.h).

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "theme.h"
#include "style.h"
#include "ui_style.h"

style_sheet_t *ui_stylesheet(void) { return theme_stylesheet(get_theme()); }

// ── selectors ─────────────────────────────────────────────────────────────────

typedef struct {
  char element[48], part[48], classes[64];
  uint32_t state;
} ui_selector_t;

static const struct { const char *name; uint32_t bit; } k_ui_states[] = {
  {"hover", STYLE_HOVER}, {"active", STYLE_ACTIVE}, {"checked", STYLE_CHECKED}, {"disabled", STYLE_DISABLED},
  {"focus", STYLE_FOCUS}, {"default", STYLE_DEFAULT}, {"window-inactive", STYLE_WINDOW_INACTIVE},
};

static size_t ui_ident(const char *s) {
  size_t n = 0;
  while (isalnum((unsigned char)s[n]) || s[n] == '-' || s[n] == '_') n++;
  return n;
}

static void ui_parse_selector(const char *selector, ui_selector_t *out) {
  memset(out, 0, sizeof(*out));
  const char *c = selector;
  size_t n = ui_ident(c);
  snprintf(out->element, sizeof(out->element), "%.*s", (int)n, c);
  c += n;
  while (*c) {
    if (c[0] == ':' && c[1] == ':') {
      c += 2; n = ui_ident(c);
      snprintf(out->part, sizeof(out->part), "%.*s", (int)n, c);
    } else if (*c == '.') {
      c++; n = ui_ident(c);
      size_t used = strlen(out->classes);
      snprintf(out->classes + used, sizeof(out->classes) - used, "%s%.*s", used ? " " : "", (int)n, c);
    } else if (*c == ':') {
      c++; n = ui_ident(c);
      for (size_t i = 0; i < sizeof(k_ui_states) / sizeof(k_ui_states[0]); i++)
        if (strlen(k_ui_states[i].name) == n && !strncmp(c, k_ui_states[i].name, n)) out->state |= k_ui_states[i].bit;
    } else {
      fprintf(stderr, "[ui_style] bad selector \"%s\"\n", selector); fflush(stderr);
      return;
    }
    c += n;
  }
}

static style_query_t ui_query(const ui_selector_t *s) {
  return (style_query_t){ .element = s->element, .part = s->part[0] ? s->part : NULL,
                          .classes = s->classes[0] ? s->classes : NULL, .state = s->state };
}

// ── cache ─────────────────────────────────────────────────────────────────────

enum { UI_PX, UI_RADIUS, UI_NUMBER, UI_COLOR, UI_ROLE, UI_FLAG, UI_KEYWORD, UI_SHADOW };

typedef struct {
  const char *selector, *property, *keyword;
  int kind;
  unsigned generation;
  union { int i; float f; uint32_t c; bool b; ui_shadow_t shadow; } value;
} ui_cache_entry_t;

#define UI_CACHE_SIZE 1024
static ui_cache_entry_t g_ui_cache[UI_CACHE_SIZE];

static ui_cache_entry_t *ui_slot(const char *selector, const char *property, const char *keyword, int kind, bool *hit) {
  uintptr_t h = ((uintptr_t)selector * 31u) ^ ((uintptr_t)property * 17u) ^ ((uintptr_t)keyword * 13u) ^ (uintptr_t)kind;
  h ^= h >> 7;
  for (int probe = 0; probe < 8; probe++) {
    ui_cache_entry_t *e = &g_ui_cache[(h + (uintptr_t)probe) % UI_CACHE_SIZE];
    bool same = e->selector == selector && e->property == property && e->keyword == keyword && e->kind == kind;
    if (same || !e->selector || e->generation != g_theme_style_generation) {
      *hit = same && e->generation == g_theme_style_generation;
      e->selector = selector; e->property = property; e->keyword = keyword; e->kind = kind;
      return e;
    }
  }
  *hit = false;
  ui_cache_entry_t *e = &g_ui_cache[h % UI_CACHE_SIZE];   // evict
  e->selector = selector; e->property = property; e->keyword = keyword; e->kind = kind;
  return e;
}

// padding-left and its kin, from the longhand or the 1..4 value shorthand,
// whichever cascades higher.
static bool ui_side_value(style_sheet_t *sheet, style_query_t q, const char *property, char *out, size_t n) {
  static const char *const sides[4] = {"top", "right", "bottom", "left"};
  const char *dash = strrchr(property, '-');
  if (!dash || (strncmp(property, "padding-", 8) && strncmp(property, "margin-", 7))) return false;
  int side = -1;
  for (int i = 0; i < 4; i++) if (!strcmp(dash + 1, sides[i])) side = i;
  if (side < 0) return false;
  char shorthand[16], longv[256], shortv[256];
  snprintf(shorthand, sizeof(shorthand), "%.*s", (int)(dash - property), property);
  long long_rank, short_rank;
  bool has_long = style_value_ranked(sheet, q, property, longv, sizeof(longv), &long_rank);
  bool has_short = style_value_ranked(sheet, q, shorthand, shortv, sizeof(shortv), &short_rank);
  if (has_long && (!has_short || long_rank >= short_rank)) { snprintf(out, n, "%s", longv); return true; }
  if (!has_short) return false;
  char *tok[4]; int count = 0;
  for (char *c = strtok(shortv, " \t"); c && count < 4; c = strtok(NULL, " \t")) tok[count++] = c;
  if (!count) return false;
  static const int map[5][4] = {{0}, {0, 0, 0, 0}, {0, 1, 0, 1}, {0, 1, 2, 1}, {0, 1, 2, 3}};
  snprintf(out, n, "%s", tok[map[count][side]]);
  return true;
}

bool ui_string(const char *selector, const char *property, char *out, size_t n) {
  ui_selector_t sel;
  ui_parse_selector(selector, &sel);
  style_sheet_t *sheet = ui_stylesheet();
  if (!sheet || !out || !n) return false;
  if (!strcmp(selector, ":root")) return style_value(sheet, STYLE_ROOT, property, out, n);
  return style_value(sheet, ui_query(&sel), property, out, n);
}

// The cascaded text of a property; reports a property the sheet leaves unset.
static bool ui_value(const char *selector, const char *property, char *out, size_t n) {
  ui_selector_t sel;
  ui_parse_selector(selector, &sel);
  style_sheet_t *sheet = ui_stylesheet();
  if (sheet && ui_side_value(sheet, ui_query(&sel), property, out, n)) return true;
  if (sheet && style_value(sheet, ui_query(&sel), property, out, n)) return true;
  fprintf(stderr, "[ui_style] %s { %s } is not set by the style sheet\n", selector, property);
  fflush(stderr);
  return false;
}

static bool ui_first_length(const char *value, int *out) {
  char buf[128];
  snprintf(buf, sizeof(buf), "%s", value);
  if (strncmp(buf, "calc(", 5)) buf[strcspn(buf, " \t")] = 0;
  return style_parse_length(buf, out);
}

#define UI_CACHED(kind, field, ...) do {                                      \
    bool hit;                                                                 \
    ui_cache_entry_t *e = ui_slot(selector, property, keyword, kind, &hit);  \
    if (!hit) { __VA_ARGS__; e->generation = g_theme_style_generation; }      \
    return e->value.field;                                                    \
  } while (0)

int ui_px(const char *selector, const char *property) {
  const char *keyword = NULL;
  UI_CACHED(UI_PX, i, {
    char v[256]; e->value.i = 0;
    if (ui_value(selector, property, v, sizeof(v)) && !ui_first_length(v, &e->value.i)) {
      fprintf(stderr, "[ui_style] %s { %s: %s } is not a px length\n", selector, property, v); fflush(stderr);
    }
  });
}

int ui_radius(const char *selector) {
  const char *property = "border-radius", *keyword = NULL;
  UI_CACHED(UI_RADIUS, i, {
    char v[256]; int r = 0;
    if (ui_value(selector, property, v, sizeof(v)) && !ui_first_length(v, &r)) {
      fprintf(stderr, "[ui_style] %s { border-radius: %s } is not a px length\n", selector, v); fflush(stderr);
    }
    e->value.i = r >= 9999 ? CORNER_RADIUS_CIRCULAR : r;
  });
}

float ui_number(const char *selector, const char *property) {
  const char *keyword = NULL;
  UI_CACHED(UI_NUMBER, f, {
    char v[256], *end; e->value.f = 0;
    if (ui_value(selector, property, v, sizeof(v))) {
      double d = strtod(v, &end);
      if (end != v && *end == '%') { d /= 100.0; end++; }
      if (end == v || *end) { fprintf(stderr, "[ui_style] %s { %s: %s } is not a number\n", selector, property, v); fflush(stderr); }
      else e->value.f = (float)d;
    }
  });
}

uint32_t ui_color(const char *selector, const char *property) {
  const char *keyword = NULL;
  UI_CACHED(UI_COLOR, c, {
    char v[256]; e->value.c = 0;
    if (ui_value(selector, property, v, sizeof(v)) && !style_parse_color(v, &e->value.c)) {
      fprintf(stderr, "[ui_style] %s { %s: %s } is not a colour\n", selector, property, v); fflush(stderr);
    }
  });
}

int ui_role(const char *selector, const char *property) {
  const char *keyword = NULL;
  UI_CACHED(UI_ROLE, i, {
    ui_selector_t sel; char name[64];
    ui_parse_selector(selector, &sel);
    style_sheet_t *sheet = ui_stylesheet();
    e->value.i = 0;
    if (sheet && style_var_name(sheet, ui_query(&sel), property, name, sizeof(name))) {
      int role = theme_sys_color_role(name);
      e->value.i = role > 0 ? role : 0;
    }
  });
}

bool ui_flag(const char *selector, const char *property) {
  const char *keyword = NULL;
  UI_CACHED(UI_FLAG, b, {
    char v[256];
    e->value.b = ui_value(selector, property, v, sizeof(v)) &&
                 strcmp(v, "none") && strcmp(v, "0") && strcmp(v, "0px") && strcmp(v, "false");
  });
}

bool ui_keyword_is(const char *selector, const char *property, const char *keyword) {
  UI_CACHED(UI_KEYWORD, b, {
    char v[256];
    e->value.b = ui_value(selector, property, v, sizeof(v)) && !strcmp(v, keyword);
  });
}

ui_shadow_t ui_shadow(const char *selector, const char *property) {
  const char *keyword = NULL;
  UI_CACHED(UI_SHADOW, shadow, {
    char v[256];
    memset(&e->value.shadow, 0, sizeof(e->value.shadow));
    if (ui_value(selector, property, v, sizeof(v)) && strcmp(v, "none")) {
      char *tok = strtok(v, " \t"); int len[3], nl = 0; bool ok = true;
      for (; tok; tok = strtok(NULL, " \t")) {
        if (nl < 3 && style_parse_length(tok, &len[nl])) nl++;
        else if (!style_parse_color(tok, &e->value.shadow.color)) ok = false;
      }
      if (!ok || nl < 2) { fprintf(stderr, "[ui_style] %s { %s } is not \"dx dy blur colour\"\n", selector, property); fflush(stderr); }
      else {
        e->value.shadow.offset = (ipoint16_t){(int16_t)len[0], (int16_t)len[1]};
        e->value.shadow.blur = nl > 2 ? len[2] : 0;
      }
    }
  });
}
