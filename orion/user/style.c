// CSS subset parser and cascade. See style.h for what is supported.

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "style.h"

#define CSS_NAME 48
#define CSS_MAX_CLASSES 4
#define CSS_VAR_DEPTH 16

typedef struct {
  char element[CSS_NAME], part[CSS_NAME], id[CSS_NAME];
  char classes[CSS_MAX_CLASSES][CSS_NAME];
  int class_count;
  uint32_t state;
  bool root;
  int specificity;
} css_selector_t;

typedef struct { char *name, *value; bool important; } css_decl_t;

enum { CSS_MEDIA_ALL, CSS_MEDIA_DARK, CSS_MEDIA_LIGHT, CSS_MEDIA_NEVER };

typedef struct {
  css_selector_t *selectors; int selector_count;
  css_decl_t *decls; int decl_count;
  int media;
} css_rule_t;

struct style_sheet_s {
  css_rule_t *rules; int count, capacity;
  int errors;
  bool dark;
  char origin[256];
  style_var_fn var_hook, var_default;
  void *var_ctx;
};

typedef struct {
  style_sheet_t *sheet;
  const char *src;    // comments blanked, newlines kept for line numbers
  size_t pos, len;
} css_parser_t;

// ── diagnostics ───────────────────────────────────────────────────────────────

static int css_line(const css_parser_t *p, size_t pos) {
  int line = 1;
  for (size_t i = 0; i < pos && i < p->len; i++) if (p->src[i] == '\n') line++;
  return line;
}

static void css_error(css_parser_t *p, size_t pos, const char *what, const char *detail) {
  p->sheet->errors++;
  fprintf(stderr, "[style] %s:%d: %s%s%s\n", p->sheet->origin, css_line(p, pos), what,
          detail ? ": " : "", detail ? detail : "");
  fflush(stderr);
}

// ── small text helpers ────────────────────────────────────────────────────────

static char *css_strndup(const char *s, size_t n) {
  char *out = malloc(n + 1);
  if (!out) return NULL;
  memcpy(out, s, n); out[n] = 0;
  return out;
}

static void css_trim(const char **s, const char **e) {
  while (*s < *e && isspace((unsigned char)**s)) (*s)++;
  while (*e > *s && isspace((unsigned char)(*e)[-1])) (*e)--;
}

static bool css_ident_char(char c) { return isalnum((unsigned char)c) || c == '-' || c == '_'; }

// Copies an identifier at *s into out; false when there is none or it does not fit.
static bool css_read_ident(const char **s, char *out, size_t n) {
  size_t len = 0;
  while (css_ident_char((*s)[len])) len++;
  if (!len || len >= n) return false;
  memcpy(out, *s, len); out[len] = 0;
  *s += len;
  return true;
}

// ── selectors ─────────────────────────────────────────────────────────────────

static const struct { const char *name; uint32_t bit; } k_pseudo_classes[] = {
  {"hover", STYLE_HOVER},       {"active", STYLE_ACTIVE},   {"checked", STYLE_CHECKED},
  {"disabled", STYLE_DISABLED}, {"focus", STYLE_FOCUS},     {"default", STYLE_DEFAULT},
  {"window-inactive", STYLE_WINDOW_INACTIVE},
};

// Parses one compound selector; on failure *why names the problem.
static bool css_parse_selector(const char *s, const char *e, css_selector_t *sel, const char **why) {
  memset(sel, 0, sizeof(*sel));
  css_trim(&s, &e);
  if (s == e) { *why = "empty selector"; return false; }
  char buf[256];
  if ((size_t)(e - s) >= sizeof(buf)) { *why = "selector too long"; return false; }
  memcpy(buf, s, (size_t)(e - s)); buf[e - s] = 0;
  const char *c = buf;
  int ids = 0, classes = 0, elements = 0;
  bool first = true;
  while (*c) {
    char name[CSS_NAME];
    if (*c == '*' && first) { c++; }
    else if (first && (isalpha((unsigned char)*c) || *c == '_')) {
      if (!css_read_ident(&c, sel->element, sizeof(sel->element))) { *why = "bad element name"; return false; }
      elements++;
    } else if (*c == '#') {
      c++;
      if (sel->id[0] || !css_read_ident(&c, sel->id, sizeof(sel->id))) { *why = "bad id"; return false; }
      ids++;
    } else if (*c == '.') {
      c++;
      if (sel->class_count >= CSS_MAX_CLASSES || !css_read_ident(&c, sel->classes[sel->class_count], CSS_NAME)) {
        *why = "bad class (at most 4 per selector)"; return false;
      }
      sel->class_count++; classes++;
    } else if (c[0] == ':' && c[1] == ':') {
      c += 2;
      if (sel->part[0] || !css_read_ident(&c, sel->part, sizeof(sel->part))) { *why = "bad part (::name)"; return false; }
      elements++;
    } else if (*c == ':') {
      c++;
      if (!css_read_ident(&c, name, sizeof(name))) { *why = "bad pseudo-class"; return false; }
      if (!strcmp(name, "root")) { sel->root = true; classes++; }
      else {
        size_t k = 0;
        while (k < sizeof(k_pseudo_classes) / sizeof(k_pseudo_classes[0]) && strcmp(name, k_pseudo_classes[k].name)) k++;
        if (k == sizeof(k_pseudo_classes) / sizeof(k_pseudo_classes[0])) { *why = "unsupported pseudo-class"; return false; }
        sel->state |= k_pseudo_classes[k].bit; classes++;
      }
    } else if (isspace((unsigned char)*c) || *c == '>' || *c == '+' || *c == '~') {
      *why = "combinators are not supported; style the element or its ::part"; return false;
    } else {
      *why = "unexpected character in selector"; return false;
    }
    first = false;
  }
  if (sel->root && (sel->element[0] || sel->part[0] || sel->id[0] || sel->class_count || sel->state)) {
    *why = ":root takes no other selectors"; return false;
  }
  sel->specificity = ids * 10000 + classes * 100 + elements;
  return true;
}

static bool css_has_class(const char *list, const char *cls) {
  size_t n = strlen(cls);
  for (const char *p = list ? list : ""; *p;) {
    while (*p == ' ') p++;
    const char *end = p; while (*end && *end != ' ') end++;
    if ((size_t)(end - p) == n && !strncmp(p, cls, n)) return true;
    p = end;
  }
  return false;
}

static bool css_matches(const css_selector_t *sel, style_query_t q) {
  if (!q.element) {   // the root
    return sel->root || (!sel->element[0] && !sel->part[0] && !sel->id[0] && !sel->class_count && !sel->state);
  }
  if (sel->root) return false;
  if (sel->element[0] && strcmp(sel->element, q.element)) return false;
  if (strcmp(sel->part, q.part ? q.part : "")) return false;
  if (sel->id[0] && (!q.id || strcmp(sel->id, q.id))) return false;
  for (int i = 0; i < sel->class_count; i++) if (!css_has_class(q.classes, sel->classes[i])) return false;
  return (sel->state & ~q.state) == 0;
}

// ── rules ─────────────────────────────────────────────────────────────────────

static void css_free_rule(css_rule_t *r) {
  for (int i = 0; i < r->decl_count; i++) { free(r->decls[i].name); free(r->decls[i].value); }
  free(r->decls); free(r->selectors);
}

static css_rule_t *css_add_rule(style_sheet_t *sheet) {
  if (sheet->count == sheet->capacity) {
    int cap = sheet->capacity ? sheet->capacity * 2 : 32;
    css_rule_t *grown = realloc(sheet->rules, (size_t)cap * sizeof(*grown));
    if (!grown) return NULL;
    sheet->rules = grown; sheet->capacity = cap;
  }
  css_rule_t *r = &sheet->rules[sheet->count++];
  memset(r, 0, sizeof(*r));
  return r;
}

// Index of the character that closes the construct opened before `from`
// (depth 0 → the first unnested `stop`), honouring quotes and parentheses.
static size_t css_scan(const css_parser_t *p, size_t from, const char *stops) {
  int paren = 0; char quote = 0;
  for (size_t i = from; i < p->len; i++) {
    char c = p->src[i];
    if (quote) { if (c == '\\') i++; else if (c == quote) quote = 0; continue; }
    if (c == '"' || c == '\'') { quote = c; continue; }
    if (c == '(') paren++;
    else if (c == ')' && paren) paren--;
    else if (!paren && strchr(stops, c)) return i;
  }
  return p->len;
}

static void css_parse_declarations(css_parser_t *p, size_t s, size_t e, css_rule_t *rule) {
  while (s < e) {
    size_t semi = css_scan(p, s, ";");
    if (semi > e) semi = e;
    const char *ds = p->src + s, *de = p->src + semi;
    css_trim(&ds, &de);
    if (ds < de) {
      const char *colon = memchr(ds, ':', (size_t)(de - ds));
      const char *ns = ds, *ne = colon ? colon : de;
      css_trim(&ns, &ne);
      bool ok = colon && ns < ne;
      for (const char *c = ns; ok && c < ne; c++) ok = css_ident_char(*c);
      const char *vs = colon ? colon + 1 : de, *ve = de;
      css_trim(&vs, &ve);
      bool important = false;
      if (ok && ve - vs >= 10 && !strncasecmp(ve - 10, "!important", 10)) {
        important = true; ve -= 10;
        css_trim(&vs, &ve);
      }
      if (!ok) css_error(p, (size_t)(ds - p->src), "bad declaration", "expected name: value");
      else if (vs == ve) css_error(p, (size_t)(ds - p->src), "empty value", NULL);
      else {
        css_decl_t *grown = realloc(rule->decls, (size_t)(rule->decl_count + 1) * sizeof(*grown));
        if (!grown) return;
        rule->decls = grown;
        css_decl_t *d = &rule->decls[rule->decl_count++];
        d->name = css_strndup(ns, (size_t)(ne - ns));
        d->value = css_strndup(vs, (size_t)(ve - vs));
        d->important = important;
        if (!d->name || !d->value) { free(d->name); free(d->value); rule->decl_count--; return; }
        // Property names are ASCII case-insensitive; custom properties are not.
        if (strncmp(d->name, "--", 2)) for (char *c = d->name; *c; c++) *c = (char)tolower((unsigned char)*c);
      }
    }
    s = semi + 1;
  }
}

// A style rule at p->pos: "selectors { declarations }".
static void css_parse_rule(css_parser_t *p, int media) {
  size_t start = p->pos;
  size_t open = css_scan(p, start, "{};");
  if (open >= p->len || p->src[open] != '{') {
    css_error(p, start, "expected '{' after selector", NULL);
    p->pos = open < p->len && p->src[open] == ';' ? open + 1 : open;   // a '}' closes the enclosing block
    return;
  }
  size_t close = css_scan(p, open + 1, "}");
  p->pos = close < p->len ? close + 1 : p->len;
  if (close >= p->len) { css_error(p, open, "unterminated rule", NULL); return; }

  css_selector_t sels[16]; int nsel = 0;
  for (size_t s = start; s < open;) {
    size_t comma = css_scan(p, s, ",{");
    if (comma > open) comma = open;
    const char *why = NULL;
    if (nsel == 16) { css_error(p, s, "too many selectors in one rule (16)", NULL); return; }
    if (!css_parse_selector(p->src + s, p->src + comma, &sels[nsel], &why)) {
      css_error(p, s, "rule dropped", why);   // one bad selector drops the rule, as in CSS
      return;
    }
    nsel++;
    s = comma + 1;
  }
  css_rule_t *rule = css_add_rule(p->sheet);
  if (!rule) return;
  rule->media = media;
  rule->selectors = malloc((size_t)nsel * sizeof(*rule->selectors));
  if (!rule->selectors) { p->sheet->count--; return; }
  memcpy(rule->selectors, sels, (size_t)nsel * sizeof(*sels));
  rule->selector_count = nsel;
  css_parse_declarations(p, open + 1, close, rule);
}

// Position after the block that opens at `open`, counting nested braces.
static size_t css_skip_block(const css_parser_t *p, size_t open) {
  int depth = 0;
  for (size_t i = open; i < p->len;) {
    size_t next = css_scan(p, i, "{}");
    if (next >= p->len) return p->len;
    depth += p->src[next] == '{' ? 1 : -1;
    if (!depth) return next + 1;
    i = next + 1;
  }
  return p->len;
}

static int css_media_kind(const char *s, const char *e) {
  char flat[128]; size_t n = 0;
  for (; s < e && n + 1 < sizeof(flat); s++) if (!isspace((unsigned char)*s)) flat[n++] = (char)tolower((unsigned char)*s);
  flat[n] = 0;
  if (!strcmp(flat, "(prefers-color-scheme:dark)")) return CSS_MEDIA_DARK;
  if (!strcmp(flat, "(prefers-color-scheme:light)")) return CSS_MEDIA_LIGHT;
  if (!strcmp(flat, "all") || !strcmp(flat, "screen")) return CSS_MEDIA_ALL;
  return CSS_MEDIA_NEVER;
}

static void css_parse_block(css_parser_t *p, int media, bool nested) {
  for (;;) {
    while (p->pos < p->len && isspace((unsigned char)p->src[p->pos])) p->pos++;
    if (p->pos >= p->len) { if (nested) css_error(p, p->len, "unterminated @media block", NULL); return; }
    char c = p->src[p->pos];
    if (c == '}') {
      if (nested) { p->pos++; return; }
      css_error(p, p->pos, "unexpected '}'", NULL); p->pos++; continue;
    }
    if (c != '@') { css_parse_rule(p, media); continue; }
    size_t at = p->pos++;
    const char *name = p->src + p->pos; char keyword[32];
    if (!css_read_ident(&name, keyword, sizeof(keyword))) keyword[0] = 0;
    p->pos = (size_t)(name - p->src);
    size_t stop = css_scan(p, p->pos, "{;");
    if (!strcmp(keyword, "media") && stop < p->len && p->src[stop] == '{') {
      int kind = css_media_kind(p->src + p->pos, p->src + stop);
      if (kind == CSS_MEDIA_NEVER) {
        char cond[96]; size_t n = stop - p->pos < sizeof(cond) - 1 ? stop - p->pos : sizeof(cond) - 1;
        memcpy(cond, p->src + p->pos, n); cond[n] = 0;
        css_error(p, at, "unsupported @media condition, block ignored", cond);
      }
      if (media != CSS_MEDIA_ALL && kind != media) kind = CSS_MEDIA_NEVER;   // nested media must agree
      p->pos = stop + 1;
      css_parse_block(p, kind, true);
      continue;
    }
    css_error(p, at, "unsupported at-rule, skipped", keyword[0] ? keyword : NULL);
    if (stop >= p->len || p->src[stop] == ';') { p->pos = stop < p->len ? stop + 1 : p->len; continue; }
    p->pos = css_skip_block(p, stop);
  }
}

bool style_sheet_append(style_sheet_t *sheet, const char *text, const char *origin) {
  if (!sheet || !text) return false;
  char *src = strdup(text);
  if (!src) return false;
  char saved[256];
  snprintf(saved, sizeof(saved), "%s", sheet->origin);
  if (origin) snprintf(sheet->origin, sizeof(sheet->origin), "%s", origin);
  css_parser_t p = { sheet, src, 0, strlen(src) };
  for (char *c = src; *c; c++) {   // blank comments, keeping newlines
    if (c[0] != '/' || c[1] != '*') continue;
    char *end = strstr(c + 2, "*/");
    char *stop = end ? end + 2 : c + strlen(c);
    if (!end) css_error(&p, (size_t)(c - src), "unterminated comment", NULL);
    for (; c < stop; c++) if (*c != '\n') *c = ' ';
    c--;
  }
  css_parse_block(&p, CSS_MEDIA_ALL, false);
  free(src);
  if (saved[0] && origin) snprintf(sheet->origin, sizeof(sheet->origin), "%s", saved);
  return true;
}

style_sheet_t *style_sheet_parse(const char *text, const char *origin) {
  if (!text) return NULL;
  style_sheet_t *sheet = calloc(1, sizeof(*sheet));
  if (!sheet) return NULL;
  snprintf(sheet->origin, sizeof(sheet->origin), "%s", origin ? origin : "<style>");
  if (!style_sheet_append(sheet, text, NULL)) { free(sheet); return NULL; }
  return sheet;
}

style_sheet_t *style_sheet_load(const char *path) {
  FILE *f = path ? fopen(path, "rb") : NULL;
  if (!f) { fprintf(stderr, "[style] cannot read %s\n", path ? path : "(null)"); fflush(stderr); return NULL; }
  fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
  char *text = size >= 0 ? malloc((size_t)size + 1) : NULL;
  size_t got = text ? fread(text, 1, (size_t)size, f) : 0;
  fclose(f);
  if (!text) return NULL;
  text[got] = 0;
  style_sheet_t *sheet = style_sheet_parse(text, path);
  free(text);
  return sheet;
}

void style_sheet_free(style_sheet_t *sheet) {
  if (!sheet) return;
  for (int i = 0; i < sheet->count; i++) css_free_rule(&sheet->rules[i]);
  free(sheet->rules);
  free(sheet);
}

int  style_sheet_error_count(const style_sheet_t *sheet) { return sheet ? sheet->errors : 0; }
int  style_sheet_rule_count(const style_sheet_t *sheet) { return sheet ? sheet->count : 0; }
void style_sheet_set_dark(style_sheet_t *sheet, bool dark) { if (sheet) sheet->dark = dark; }
bool style_sheet_dark(const style_sheet_t *sheet) { return sheet && sheet->dark; }
void style_sheet_set_var_hook(style_sheet_t *sheet, style_var_fn override, style_var_fn fallback, void *ctx) {
  if (sheet) { sheet->var_hook = override; sheet->var_default = fallback; sheet->var_ctx = ctx; }
}

// ── cascade ───────────────────────────────────────────────────────────────────

static const char *css_cascade_ranked(const style_sheet_t *sheet, style_query_t q, const char *property, long *rank) {
  if (rank) *rank = -1;
  if (!sheet || !property) return NULL;
  const char *best = NULL;
  long best_key = -1;
  for (int r = 0; r < sheet->count; r++) {
    const css_rule_t *rule = &sheet->rules[r];
    if (rule->media == CSS_MEDIA_NEVER || (rule->media == CSS_MEDIA_DARK && !sheet->dark) ||
        (rule->media == CSS_MEDIA_LIGHT && sheet->dark)) continue;
    for (int d = 0; d < rule->decl_count; d++) {
      if (strcmp(rule->decls[d].name, property)) continue;
      for (int s = 0; s < rule->selector_count; s++) {
        if (!css_matches(&rule->selectors[s], q)) continue;
        // importance, then specificity, then source order
        long key = ((long)rule->decls[d].important << 40) | ((long)rule->selectors[s].specificity << 20) | (long)(r * 64 + d);
        if (key > best_key) { best_key = key; best = rule->decls[d].value; }
      }
    }
  }
  if (rank) *rank = best_key;
  return best;
}

static const char *css_cascade(const style_sheet_t *sheet, style_query_t q, const char *property) {
  return css_cascade_ranked(sheet, q, property, NULL);
}

static const char *css_custom(const style_sheet_t *sheet, style_query_t q, const char *name) {
  const char *v = q.element ? css_cascade(sheet, q, name) : NULL;
  return v ? v : css_cascade(sheet, STYLE_ROOT, name);
}

static bool css_resolve(const style_sheet_t *sheet, style_query_t q, const char *value, char *out, size_t n, int depth) {
  if (depth > CSS_VAR_DEPTH) {
    fprintf(stderr, "[style] %s: var() nesting deeper than %d (a cycle?) in \"%s\"\n", sheet->origin, CSS_VAR_DEPTH, value);
    fflush(stderr);
    return false;
  }
  size_t used = 0; out[0] = 0;
  for (const char *c = value; *c;) {
    const char *v = strstr(c, "var(");
    size_t lit = v ? (size_t)(v - c) : strlen(c);
    if (used + lit >= n) return false;
    memcpy(out + used, c, lit); used += lit; out[used] = 0;
    if (!v) break;
    const char *args = v + 4; int paren = 1; const char *end = args, *comma = NULL;
    for (; *end && paren; end++) {
      if (*end == '(') paren++;
      else if (*end == ')') paren--;
      else if (*end == ',' && paren == 1 && !comma) comma = end;
    }
    if (paren) { fprintf(stderr, "[style] %s: unterminated var() in \"%s\"\n", sheet->origin, value); fflush(stderr); return false; }
    const char *ns = args, *ne = comma ? comma : end - 1;
    css_trim(&ns, &ne);
    char name[CSS_NAME + 2];
    if (ne - ns < 3 || (size_t)(ne - ns) >= sizeof(name) || strncmp(ns, "--", 2)) {
      fprintf(stderr, "[style] %s: var() needs a --name in \"%s\"\n", sheet->origin, value); fflush(stderr); return false;
    }
    memcpy(name, ns, (size_t)(ne - ns)); name[ne - ns] = 0;
    char hooked[128];
    const char *found = sheet->var_hook && sheet->var_hook(name + 2, hooked, sizeof(hooked), sheet->var_ctx)
                        ? hooked : css_custom(sheet, q, name);
    char *fallback = NULL;
    if (!found && sheet->var_default && sheet->var_default(name + 2, hooked, sizeof(hooked), sheet->var_ctx)) found = hooked;
    if (!found && comma) {
      const char *fs = comma + 1, *fe = end - 1;
      css_trim(&fs, &fe);
      found = fallback = css_strndup(fs, (size_t)(fe - fs));
    }
    if (!found) {
      fprintf(stderr, "[style] %s: undefined custom property %s\n", sheet->origin, name); fflush(stderr);
      return false;
    }
    char sub[512];
    bool ok = css_resolve(sheet, q, found, sub, sizeof(sub), depth + 1);
    free(fallback);
    if (!ok) return false;
    size_t sl = strlen(sub);
    if (used + sl >= n) return false;
    memcpy(out + used, sub, sl + 1); used += sl;
    c = end;
  }
  return true;
}

bool style_value(const style_sheet_t *sheet, style_query_t q, const char *property, char *out, size_t n) {
  return style_value_ranked(sheet, q, property, out, n, NULL);
}

bool style_value_ranked(const style_sheet_t *sheet, style_query_t q, const char *property, char *out, size_t n, long *rank) {
  if (rank) *rank = -1;
  if (!out || !n) return false;
  out[0] = 0;
  const char *raw = !strncmp(property ? property : "", "--", 2) ? css_custom(sheet, q, property)
                                                                : css_cascade_ranked(sheet, q, property, rank);
  if (!raw) return false;
  if (!css_resolve(sheet, q, raw, out, n, 0)) { out[0] = 0; return false; }
  return true;
}

bool style_var_name(const style_sheet_t *sheet, style_query_t q, const char *property, char *out, size_t n) {
  const char *raw = css_cascade(sheet, q, property);
  if (!raw || strncmp(raw, "var(--", 6)) return false;
  const char *s = raw + 6, *e = strchr(s, ')');
  if (!e || e[1] || memchr(s, ',', (size_t)(e - s)) || (size_t)(e - s) >= n) return false;
  memcpy(out, s, (size_t)(e - s)); out[e - s] = 0;
  return true;
}

// ── values ────────────────────────────────────────────────────────────────────

static int css_hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  c = (char)tolower((unsigned char)c);
  return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static uint32_t css_pack(int r, int g, int b, int a) {
  return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

// One rgb() component: 0..255 or a percentage; alpha: 0..1 or a percentage.
static bool css_channel(const char **s, bool alpha, int *out) {
  while (isspace((unsigned char)**s)) (*s)++;
  char *end;
  double v = strtod(*s, &end);
  if (end == *s) return false;
  if (*end == '%') { v = v / 100.0 * 255.0; end++; }
  else if (alpha) v *= 255.0;
  if (v < 0) v = 0;
  if (v > 255) v = 255;
  *out = (int)lround(v);
  *s = end;
  while (isspace((unsigned char)**s)) (*s)++;
  return true;
}

// color-mix(in srgb, A p%, B q%): premultiplied, as CSS mixes, so mixing with
// transparent fades a colour instead of darkening it.
static bool css_color_mix(const char *args, uint32_t *out) {
  char buf[256];
  size_t len = strlen(args);
  if (!len || len >= sizeof(buf) || args[len - 1] != ')') return false;
  memcpy(buf, args, len - 1); buf[len - 1] = 0;
  char *part[3]; int n = 0, depth = 0;
  part[n++] = buf;
  for (char *c = buf; *c; c++) {
    if (*c == '(') depth++;
    else if (*c == ')') depth--;
    else if (*c == ',' && !depth) { if (n == 3) return false; *c = 0; part[n++] = c + 1; }
  }
  if (n != 3) return false;
  while (isspace((unsigned char)*part[0])) part[0]++;
  if (strncasecmp(part[0], "in srgb", 7)) return false;
  uint32_t color[2]; double pct[2] = {-1, -1};
  for (int i = 0; i < 2; i++) {
    char *s = part[i + 1];
    char *pc = strrchr(s, '%');
    if (pc && (!strrchr(s, ')') || strrchr(s, ')') < pc)) {   // a trailing percentage
      char *num = pc;
      while (num > s && (isdigit((unsigned char)num[-1]) || num[-1] == '.')) num--;
      pct[i] = strtod(num, NULL) / 100.0;
      *num = 0;
    }
    if (!style_parse_color(s, &color[i])) return false;
  }
  if (pct[0] < 0 && pct[1] < 0) pct[0] = pct[1] = 0.5;
  else if (pct[0] < 0) pct[0] = 1 - pct[1];
  else if (pct[1] < 0) pct[1] = 1 - pct[0];
  double sum = pct[0] + pct[1];
  if (sum <= 0) return false;
  double w0 = pct[0] / sum, w1 = pct[1] / sum;
  double a0 = (color[0] >> 24) / 255.0, a1 = (color[1] >> 24) / 255.0;
  double a = a0 * w0 + a1 * w1;
  int ch[3];
  for (int k = 0; k < 3; k++) {
    double c0 = ((color[0] >> (8 * k)) & 255) * a0, c1 = ((color[1] >> (8 * k)) & 255) * a1;
    double v = a > 0 ? (c0 * w0 + c1 * w1) / a : 0;
    ch[k] = (int)lround(v < 0 ? 0 : v > 255 ? 255 : v);
  }
  *out = css_pack(ch[0], ch[1], ch[2], (int)lround(a * 255));
  return true;
}

bool style_parse_color(const char *text, uint32_t *out) {
  if (!text || !out) return false;
  while (isspace((unsigned char)*text)) text++;
  size_t len = strlen(text);
  while (len && isspace((unsigned char)text[len - 1])) len--;
  if (text[0] == '#') {
    int d[8], n = (int)len - 1;
    if (n != 3 && n != 4 && n != 6 && n != 8) return false;
    for (int i = 0; i < n; i++) if ((d[i] = css_hex(text[1 + i])) < 0) return false;
    if (n <= 4) *out = css_pack(d[0] * 17, d[1] * 17, d[2] * 17, n == 4 ? d[3] * 17 : 255);
    else *out = css_pack(d[0] * 16 + d[1], d[2] * 16 + d[3], d[4] * 16 + d[5], n == 8 ? d[6] * 16 + d[7] : 255);
    return true;
  }
  if (!strncasecmp(text, "rgb(", 4) || !strncasecmp(text, "rgba(", 5)) {
    const char *s = strchr(text, '(') + 1;
    int c[4] = {0, 0, 0, 255};
    for (int i = 0; i < 3; i++) {
      if (!css_channel(&s, false, &c[i])) return false;
      if (i < 2 && *s == ',') s++;
    }
    if (*s == '/' || *s == ',') { s++; if (!css_channel(&s, true, &c[3])) return false; }
    if (*s != ')') return false;
    *out = css_pack(c[0], c[1], c[2], c[3]);
    return true;
  }
  if (!strncasecmp(text, "color-mix(", 10)) return css_color_mix(text + 10, out);
  static const struct { const char *name; uint32_t color; } named[] = {
    {"transparent", 0x00000000u}, {"black", 0xFF000000u}, {"white", 0xFFFFFFFFu},
  };
  for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++)
    if (strlen(named[i].name) == len && !strncasecmp(text, named[i].name, len)) { *out = named[i].color; return true; }
  return false;
}

bool style_parse_length(const char *text, int *out) {
  if (!text || !out) return false;
  char *end;
  double v = strtod(text, &end);
  if (end == text) return false;
  while (isspace((unsigned char)*end)) end++;
  if (!strncasecmp(end, "px", 2)) end += 2;
  else if (v != 0 && *end) return false;   // other units are not supported
  while (isspace((unsigned char)*end)) end++;
  if (*end) return false;
  *out = (int)lround(v);
  return true;
}

bool style_color(const style_sheet_t *sheet, style_query_t q, const char *property, uint32_t *out) {
  char v[256];
  if (!style_value(sheet, q, property, v, sizeof(v))) return false;
  if (style_parse_color(v, out)) return true;
  fprintf(stderr, "[style] %s: %s: \"%s\" is not a colour\n", sheet->origin, property, v); fflush(stderr);
  return false;
}

bool style_length(const style_sheet_t *sheet, style_query_t q, const char *property, int *out) {
  char v[256];
  if (!style_value(sheet, q, property, v, sizeof(v))) return false;
  if (style_parse_length(v, out)) return true;
  fprintf(stderr, "[style] %s: %s: \"%s\" is not a px length\n", sheet->origin, property, v); fflush(stderr);
  return false;
}

bool style_number(const style_sheet_t *sheet, style_query_t q, const char *property, float *out) {
  char v[256], *end;
  if (!style_value(sheet, q, property, v, sizeof(v))) return false;
  double d = strtod(v, &end);
  if (end != v && *end == '%') { d /= 100.0; end++; }
  while (isspace((unsigned char)*end)) end++;
  if (end == v || *end) {
    fprintf(stderr, "[style] %s: %s: \"%s\" is not a number\n", sheet->origin, property, v); fflush(stderr);
    return false;
  }
  *out = (float)d;
  return true;
}
