// Runtime .orion forms. Mirrors orionc's form rules one for one, so a form
// loaded here equals the one orionc compiles (tests/form_loader_test.c and the
// Ledger parity test compare them field by field). Everything a library builds
// lives in its arena and goes away with form_library_free().

#include "form_loader.h"
#include "commctl.h"

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <orion/user/database.h>
#include <orion/user/enum_parse.h>

#define FL_DEFAULT_SPACING 4
#define FL_ARENA_BLOCK 16384
#define FL_EACH_ELEMENT(n, parent) \
  for (xmlNodePtr n = (parent) ? (parent)->children : NULL; n; n = n->next) if ((n)->type == XML_ELEMENT_NODE)

typedef struct fl_block_s {
  struct fl_block_s *next;
  size_t used, cap;
  max_align_t data[];
} fl_block_t;

typedef struct { const char *name, *value; } fl_attr_t;

typedef struct {
  const char *name;
  const fl_attr_t *attrs;
  int attr_count;
} fl_control_t;

// A TableView whose form= names another form, resolved once all are built.
typedef struct fl_pending_s {
  struct fl_pending_s *next;
  tableview_params_t *params;
  const char *form_name, *control_name;
} fl_pending_t;

typedef struct {
  form_def_t def;
  form_ctrl_def_t *controls;   // def.children: flat, linked by parent id as orionc emits them
  fl_control_t *meta;          // name and declared attributes, parallel to controls
  int control_count;
  const char *name;
  bool rejected;
} fl_form_t;

typedef struct {
  db_schema_def_t def;
} fl_schema_t;

struct form_library_s {
  fl_block_t *arena;
  const char *origin;
  fl_schema_t *schemas;        // <databases> declared by the document
  int schema_count;
  fl_form_t *forms;
  int form_count;
  uint32_t next_id;
  fl_pending_t *pending;
};

// Build state of one form: the XML nodes of its controls in id order.
typedef struct {
  form_library_t *lib;
  fl_form_t *form;
  xmlNodePtr form_node;
  xmlNodePtr *nodes;
  int count;
  bool ok;
  // The table every field= binding of the form refers to.
  const char *bind_db;
  const db_table_schema_t *bind_table;
  ctrl_binding_t *bindings;
  int binding_count;
} fl_build_t;

// ── Arena ───────────────────────────────────────────────────────────────────

static void *fl_alloc(form_library_t *lib, size_t size) {
  size = (size + sizeof(max_align_t) - 1) / sizeof(max_align_t) * sizeof(max_align_t);
  if (!lib->arena || lib->arena->used + size > lib->arena->cap) {
    size_t cap = size > FL_ARENA_BLOCK ? size : FL_ARENA_BLOCK;
    fl_block_t *block = malloc(sizeof(*block) + cap);
    if (!block) {
      fprintf(stderr, "[form_loader] %s: out of memory allocating %zu bytes\n", lib->origin, cap);
      fflush(stderr);
      return NULL;
    }
    block->next = lib->arena;
    block->used = 0;
    block->cap = cap;
    lib->arena = block;
  }
  void *p = (char *)lib->arena->data + lib->arena->used;
  lib->arena->used += size;
  memset(p, 0, size);
  return p;
}

static const char *fl_strdup(form_library_t *lib, const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s) + 1;
  char *d = fl_alloc(lib, n);
  if (d) memcpy(d, s, n);
  return d;
}

// ── XML helpers (orionc's) ──────────────────────────────────────────────────

static bool fl_eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }
static const char *fl_nz(const char *s, const char *d) { return (s && *s) ? s : d; }

static bool fl_elem(xmlNodePtr n, const char *name) {
  return n && n->type == XML_ELEMENT_NODE && xmlStrcasecmp(n->name, BAD_CAST name) == 0;
}

static xmlNodePtr fl_child(xmlNodePtr n, const char *name) {
  FL_EACH_ELEMENT(c, n) if (fl_elem(c, name)) return c;
  return NULL;
}

// Attribute values are copied into the arena; NULL when absent.
static const char *fl_attr(form_library_t *lib, xmlNodePtr n, const char *name) {
  if (!n) return NULL;
  xmlChar *v = xmlGetProp(n, BAD_CAST name);
  const char *s = v ? fl_strdup(lib, (const char *)v) : NULL;
  if (v) xmlFree(v);
  return s;
}

// orionc writes a missing caption, name or column as "" rather than NULL.
static const char *fl_text(form_library_t *lib, xmlNodePtr n, const char *name) {
  const char *v = fl_attr(lib, n, name);
  return v ? v : "";
}

static const char *fl_attrs_first(form_library_t *lib, xmlNodePtr n, const char *a, const char *b) {
  const char *v = fl_attr(lib, n, a);
  if (v && *v) return v;
  return b ? fl_attr(lib, n, b) : NULL;
}

static void fl_ident(char *out, size_t cap, const char *s) {
  size_t n = 0;
  for (const unsigned char *p = (const unsigned char *)fl_nz(s, "item"); *p && n + 1 < cap; p++) {
    bool ok = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9');
    out[n++] = ok ? (char)*p : '_';
  }
  if (!n) out[n++] = '_';
  out[n] = 0;
}

static void fl_error(fl_build_t *b, xmlNodePtr node, const char *fmt, ...) {
  va_list ap;
  fprintf(stderr, "[form_loader] %s:%ld: form '%s': ", b->lib->origin, node ? xmlGetLineNo(node) : 0L,
          fl_nz(b->form->name, "?"));
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  fflush(stderr);
  b->ok = false;
}

static void fl_warn(fl_build_t *b, xmlNodePtr node, const char *fmt, ...) {
  va_list ap;
  fprintf(stderr, "[form_loader] %s:%ld: form '%s': warning: ", b->lib->origin, node ? xmlGetLineNo(node) : 0L,
          fl_nz(b->form->name, "?"));
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
  fflush(stderr);
}

// ── Attribute values ────────────────────────────────────────────────────────

static const enum_token_t kAlignH[] = {
  {"stretch", LAYOUT_ALIGN_STRETCH}, {"left", LAYOUT_ALIGN_START}, {"start", LAYOUT_ALIGN_START},
  {"center", LAYOUT_ALIGN_CENTER}, {"right", LAYOUT_ALIGN_END}, {"end", LAYOUT_ALIGN_END},
};
static const enum_token_t kAlignV[] = {
  {"stretch", LAYOUT_ALIGN_STRETCH}, {"top", LAYOUT_ALIGN_START}, {"start", LAYOUT_ALIGN_START},
  {"center", LAYOUT_ALIGN_CENTER}, {"bottom", LAYOUT_ALIGN_END}, {"end", LAYOUT_ALIGN_END},
};
static const enum_token_t kOrient[] = {
  {"vertical", WINDOW_STACK_VERTICAL}, {"horizontal", WINDOW_STACK_HORIZONTAL},
};
static const enum_token_t kColors[] = {
  {"transparent", brTransparent}, {"control-bg", brControlBg},
  {"window-dark-bg", brWindowDarkBg}, {"workspace-bg", brWorkspaceBg},
  {"active-titlebar", brActiveTitlebar}, {"active-titlebar-text", brActiveTitlebarText},
  {"inactive-titlebar", brInactiveTitlebar}, {"inactive-titlebar-text", brInactiveTitlebarText},
  {"statusbar-bg", brStatusbarBg}, {"light-edge", brLightEdge},
  {"dark-edge", brDarkEdge}, {"flare", brFlare}, {"accent", brAccent},
  {"button-inner", brButtonInner}, {"button-hover", brButtonHover}, {"text-normal", brTextNormal},
  {"text-disabled", brTextDisabled}, {"text-error", brTextError},
  {"text-success", brTextSuccess}, {"border-focus", brBorderFocus},
  {"border-active", brBorderActive}, {"folder-text", brFolderText},
  {"column-view-bg", brColumnViewBg}, {"modal-overlay", brModalOverlay},
  {"toolbar-foreground", brToolbarForeground},
  {"info-background", brInfoBackground}, {"info-text", brInfoText},
  {"tooltip-background", brTooltipBackground}, {"tooltip-text", brTooltipText},
  {"panel-dark", brPanelDark}, {"panel-darker", brPanelDarker}, {"desktop-bg", brDesktopBg},
  {"text-warning", brTextWarning}, {"text-info", brTextInfo}, {"text-secondary", brTextSecondary},
  {"success", brTextSuccess}, {"warning", brTextWarning}, {"error", brTextError},
  {"info", brTextInfo}, {"secondary", brTextSecondary},
};
static const enum_token_t kRowIconColors[] = {
  {"success", brTextSuccess}, {"error", brTextError}, {"warning", brTextWarning}, {"info", brTextInfo},
  {"secondary", brTextSecondary}, {"disabled", brTextDisabled}, {"accent", brAccent},
};

// Flag words a .orion file may use: orionc's short names, and the C macros it
// passes through verbatim.
static const struct { const char *name; flags_t value; } kFlags[] = {
  {"notitle", WINDOW_NOTITLE}, {"nofill", WINDOW_NOFILL}, {"vscroll", WINDOW_VSCROLL},
  {"flexspace", WINDOW_FLEXSPACE}, {"toolbar", WINDOW_TOOLBAR}, {"titletoolbar", WINDOW_TITLETOOLBAR},
  {"statusbar", WINDOW_STATUSBAR}, {"notitlebar", WINDOW_NOTITLE}, {"default", 0},
  {"WINDOW_NOTITLE", WINDOW_NOTITLE}, {"WINDOW_TRANSPARENT", WINDOW_TRANSPARENT},
  {"WINDOW_VSCROLL", WINDOW_VSCROLL}, {"WINDOW_HSCROLL", WINDOW_HSCROLL},
  {"WINDOW_NORESIZE", WINDOW_NORESIZE}, {"WINDOW_NOFILL", WINDOW_NOFILL},
  {"WINDOW_ALWAYSONTOP", WINDOW_ALWAYSONTOP}, {"WINDOW_ALWAYSINBACK", WINDOW_ALWAYSINBACK},
  {"WINDOW_HIDDEN", WINDOW_HIDDEN}, {"WINDOW_NOTRAYBUTTON", WINDOW_NOTRAYBUTTON},
  {"WINDOW_DIALOG", WINDOW_DIALOG}, {"WINDOW_TOOLBAR", WINDOW_TOOLBAR},
  {"WINDOW_TITLETOOLBAR", WINDOW_TITLETOOLBAR}, {"WINDOW_NOCOLLAPSE", WINDOW_NOCOLLAPSE},
  {"WINDOW_STATUSBAR", WINDOW_STATUSBAR}, {"WINDOW_NOCLOSE", WINDOW_NOCLOSE},
  {"WINDOW_NOACTIVATE", WINDOW_NOACTIVATE}, {"WINDOW_NOTABSTOP", WINDOW_NOTABSTOP},
  {"WINDOW_STACK_HORIZONTAL", WINDOW_STACK_HORIZONTAL}, {"WINDOW_FLEXSPACE", WINDOW_FLEXSPACE},
  {"WINDOW_AUTO_LAYOUT", WINDOW_AUTO_LAYOUT}, {"WINDOW_LAYOUT_CONTAINER", WINDOW_LAYOUT_CONTAINER},
  {"WINDOW_NODRAG", WINDOW_NODRAG}, {"WINDOW_TOOLWINDOW", WINDOW_TOOLWINDOW},
  {"WINDOW_MAXIMIZEBOX", WINDOW_MAXIMIZEBOX}, {"WINDOW_PLASTIC", WINDOW_PLASTIC}, {"WINDOW_ROUND", WINDOW_ROUND},
  {"BUTTON_PUSHLIKE", BUTTON_PUSHLIKE}, {"BUTTON_AUTORADIO", BUTTON_AUTORADIO}, {"BUTTON_DEFAULT", BUTTON_DEFAULT},
  {"BUTTON_COMMANDLINK", BUTTON_COMMANDLINK},
  {"LABEL_COLLAPSE_EMPTY", LABEL_COLLAPSE_EMPTY},
  {"SPRITE_BUTTON_DOUBLECLICK", SPRITE_BUTTON_DOUBLECLICK},
  {"TBSTYLE_CHECKGROUP", TBSTYLE_CHECKGROUP},
  {"TOOLBAR_ITEM_FLAG_FLEXSPACE", TOOLBAR_ITEM_FLAG_FLEXSPACE}, {"TOOLBAR_ITEM_FLAG_ARTWORK", TOOLBAR_ITEM_FLAG_ARTWORK},
  {"CONTROL_SIZE_REGULAR", CONTROL_SIZE_REGULAR}, {"CONTROL_SIZE_SMALL", CONTROL_SIZE_SMALL},
  {"CONTROL_SIZE_MINI", CONTROL_SIZE_MINI}, {"CONTROL_SIZE_LARGE", CONTROL_SIZE_LARGE},
};

static bool fl_flags(fl_build_t *b, xmlNodePtr node, const char *raw, flags_t *out) {
  *out = 0;
  if (!raw || !*raw) return true;
  char buf[256];
  snprintf(buf, sizeof(buf), "%s", raw);
  for (char *p = buf, *tok; *p; ) {
    p += strspn(p, ",|+ ");
    if (!*p) break;
    tok = p;
    p += strcspn(p, ",|+ ");
    if (*p) *p++ = 0;
    char *end = NULL;
    unsigned long long n = strtoull(tok, &end, 0);
    if (end && end != tok && !*end) { *out |= (flags_t)n; continue; }
    bool found = false;
    for (size_t i = 0; i < sizeof(kFlags) / sizeof(kFlags[0]) && !found; i++)
      if (fl_eq(tok, kFlags[i].name)) { *out |= kFlags[i].value; found = true; }
    if (!found) { fl_error(b, node, "unknown flag '%s' in flags=\"%s\"", tok, raw); return false; }
  }
  return true;
}

static void fl_rect(form_library_t *lib, xmlNodePtr n, const char *name, const char *alias, irect16_t *r) {
  const char *s = fl_attrs_first(lib, n, name, alias);
  int a = 0, b = 0, c = 0, d = 0, count = s ? sscanf(s, "%d %d %d %d", &a, &b, &c, &d) : 0;
  if (count == 1) *r = (irect16_t){a, a, a, a};
  else if (count == 2) *r = (irect16_t){a, b, a, b};
  else if (count == 4) *r = (irect16_t){a, b, c, d};
}

static isize16_t fl_size(form_library_t *lib, xmlNodePtr n) {
  const char *w = fl_attrs_first(lib, n, "w", "width"), *h = fl_attrs_first(lib, n, "h", "height");
  isize16_t s = { w ? atoi(w) : 0, h ? atoi(h) : 0 };
  if (fl_elem(n, "Column") && fl_eq(w, "auto")) s.w = -1;
  return s;
}

static uint8_t fl_byte(const char *s, unsigned def) {
  char *end = NULL;
  long v = s ? strtol(s, &end, 0) : (long)def;
  return (uint8_t)((s && end && !*end && v >= 0 && v <= 255) ? (unsigned)v : def);
}

static int fl_enum(const char *s, const enum_token_t *tokens, size_t count, int fallback) {
  return enum_parse_token(s, tokens, count, fallback);
}
#define FL_ENUM(s, t, d) fl_enum((s), (t), sizeof(t) / sizeof((t)[0]), (d))

static flags_t fl_control_size(fl_build_t *b, xmlNodePtr n) {
  const char *raw = fl_attrs_first(b->lib, n, "control-size", "control_size");
  if (!raw || !*raw || fl_eq(raw, "regular")) return 0;
  if (fl_eq(raw, "mini")) return CONTROL_SIZE_MINI;
  if (fl_eq(raw, "small")) return CONTROL_SIZE_SMALL;
  if (fl_eq(raw, "large")) return CONTROL_SIZE_LARGE;
  fl_warn(b, n, "invalid control-size '%s'; using regular", raw);
  return 0;
}

// ── Databases ───────────────────────────────────────────────────────────────

// "db.table" names a registered database or one the document declares; a
// bare "table" is looked up in all of them. A registered database wins, since
// the records a form edits come from it.
static const db_table_schema_t *fl_table(const form_library_t *lib, const char *source, const char **db_out) {
  char db_name[64] = {0}, table[128];
  const char *dot = source ? strchr(source, '.') : NULL;
  if (dot) snprintf(db_name, sizeof(db_name), "%.*s", (int)(dot - source), source);
  snprintf(table, sizeof(table), "%s", dot ? dot + 1 : fl_nz(source, ""));
  char *next = strchr(table, '.');
  if (next) *next = 0;
  for (int i = 0; ; i++) {
    database_t *db = get_database_at(i);
    if (!db) break;
    if (db_name[0] && !fl_eq(db->name, db_name)) continue;
    const db_table_schema_t *t = db->schema ? db_schema_table_named(db->schema, table) : NULL;
    if (t) { if (db_out) *db_out = db->name; return t; }
  }
  for (int i = 0; lib && i < lib->schema_count; i++) {
    const db_schema_def_t *schema = &lib->schemas[i].def;
    if (db_name[0] && !fl_eq(schema->name, db_name)) continue;
    const db_table_schema_t *t = db_schema_table_named(schema, table);
    if (t) { if (db_out) *db_out = schema->name; return t; }
  }
  return NULL;
}

static const db_field_meta_t *fl_meta(const db_table_schema_t *t, const char *field) {
  for (int i = 0; t && i < t->meta_count; i++)
    if (fl_eq(t->meta[i].name, field)) return &t->meta[i];
  return NULL;
}

// ── Controls ────────────────────────────────────────────────────────────────

static bool fl_is_control(xmlNodePtr parent, xmlNodePtr n) {
  return n && n->type == XML_ELEMENT_NODE && !fl_elem(n, "requires") && !fl_elem(n, "toolbar") &&
         !fl_elem(n, "panetoolbar") && !(fl_elem(parent, "tableview") && (fl_elem(n, "column") || fl_elem(n, "rowicon"))) &&
         !(fl_elem(parent, "activitybar") && fl_elem(n, "page"));
}

static bool fl_has_controls(xmlNodePtr n) {
  FL_EACH_ELEMENT(c, n) if (fl_is_control(n, c)) return true;
  return false;
}

static int fl_count(xmlNodePtr parent) {
  int n = 0;
  FL_EACH_ELEMENT(c, parent) if (fl_is_control(parent, c)) n += 1 + fl_count(c);
  return n;
}

// Pre-order, as orionc emits: a control, then its subtree, then its siblings.
static void fl_collect(fl_build_t *b, xmlNodePtr parent, int *at) {
  FL_EACH_ELEMENT(c, parent) if (fl_is_control(parent, c)) {
    b->nodes[(*at)++] = c;
    fl_collect(b, c, at);
  }
}

static int fl_index_of(const fl_build_t *b, xmlNodePtr node) {
  for (int i = 0; i < b->count; i++) if (b->nodes[i] == node) return i;
  return -1;
}

// orionc names a control by its name=; a target or master names it the same way.
static uint32_t fl_id_named(const fl_build_t *b, const char *name) {
  for (int i = 0; name && i < b->count; i++)
    if (fl_eq(b->form->meta[i].name, name)) return b->form->controls[i].id;
  return 0;
}

static xmlNodePtr fl_tableview_for_table(form_library_t *lib, xmlNodePtr parent, const char *table) {
  FL_EACH_ELEMENT(c, parent) {
    if (fl_elem(c, "tableview")) {
      const char *source = fl_attrs_first(lib, c, "source", "database"), *dot = source ? strchr(source, '.') : NULL;
      char found[128];
      snprintf(found, sizeof(found), "%s", dot ? dot + 1 : fl_nz(source, "table"));
      char *next = strchr(found, '.');
      if (next) *next = 0;
      if (fl_eq(found, table)) return c;
    }
    xmlNodePtr nested = fl_tableview_for_table(lib, c, table);
    if (nested) return nested;
  }
  return NULL;
}

static xmlNodePtr fl_tableview_named(form_library_t *lib, xmlNodePtr parent, const char *name) {
  FL_EACH_ELEMENT(c, parent) {
    if (fl_elem(c, "tableview") && fl_eq(fl_attr(lib, c, "name"), name)) return c;
    xmlNodePtr nested = fl_tableview_named(lib, c, name);
    if (nested) return nested;
  }
  return NULL;
}

static const tableview_params_t *fl_tableview(fl_build_t *b, xmlNodePtr c, const char *name) {
  form_library_t *lib = b->lib;
  const char *source = fl_attrs_first(lib, c, "source", "database");
  const db_table_schema_t *t = fl_table(lib, source, NULL);
  if (!t) { fl_error(b, c, "TableView '%s': unknown source '%s'", fl_nz(name, ""), fl_nz(source, "")); return NULL; }
  tableview_params_t *p = fl_alloc(lib, sizeof(*p));
  int columns = 0;
  FL_EACH_ELEMENT(col, c) if (fl_elem(col, "column")) columns++;
  const char **fields = fl_alloc(lib, (size_t)(columns + 1) * sizeof(*fields));
  const char **titles = fl_alloc(lib, (size_t)(columns + 1) * sizeof(*titles));
  int *widths = fl_alloc(lib, (size_t)(columns + 1) * sizeof(*widths));
  int *min_widths = fl_alloc(lib, (size_t)(columns + 1) * sizeof(*min_widths));
  bool *totals = fl_alloc(lib, (size_t)(columns + 1) * sizeof(*totals));
  if (!p || !fields || !titles || !widths || !min_widths || !totals) { b->ok = false; return NULL; }
  int i = 0;
  bool any_total = false;
  FL_EACH_ELEMENT(col, c) if (fl_elem(col, "column")) {
    const char *w = fl_attr(lib, col, "width"), *mw = fl_attr(lib, col, "min-width"), *total = fl_attr(lib, col, "total");
    fields[i] = fl_text(lib, col, "field");
    titles[i] = fl_text(lib, col, "title");
    widths[i] = w ? atoi(w) : 0;
    min_widths[i] = mw ? atoi(mw) : 0;
    totals[i] = fl_eq(total, "sum");
    any_total |= totals[i];
    if (total && !totals[i]) fl_warn(b, col, "TableView '%s': unknown total '%s' (want sum)", fl_nz(name, ""), total);
    i++;
  }
  widths[columns] = min_widths[columns] = -1;
  p->db = NULL;
  p->table_id = t->table_id;
  p->field_names = fields;
  p->column_titles = titles;
  p->column_widths = widths;
  p->column_min_widths = min_widths;

  // Master/detail: the table's first relation names the master's table.
  const char *master_attr = fl_attr(lib, c, "master");
  for (int f = 0; f < t->field_count; f++) {
    if (!t->fields[f].relation_table) continue;
    p->master_filter_field = f;
    xmlNodePtr master = NULL;
    if (master_attr && *master_attr && !fl_eq(master_attr, "none")) master = fl_tableview_named(lib, b->form_node, master_attr);
    else if (!master_attr || !*master_attr) master = fl_tableview_for_table(lib, b->form_node, t->fields[f].relation_table);
    int at = master ? fl_index_of(b, master) : -1;
    if (at >= 0) {
      p->master_id = b->form->controls[at].id;
      p->master_key = t->fields[f].relation_field;
    }
    break;
  }
  const char *check_field = fl_attrs_first(lib, c, "check-field", "check_field");
  p->check_field = check_field && *check_field ? check_field : NULL;
  const char *filter_name = fl_attrs_first(lib, c, "filter-field", "filter_field"), *filter_value = fl_attrs_first(lib, c, "filter-value", "filter_value");
  if (filter_name) {
    for (int f = 0; f < t->field_count; f++)
      if (fl_eq(t->fields[f].name, filter_name)) { p->filter_field = f; break; }
    if (!p->filter_field) fl_warn(b, c, "TableView '%s': unknown or key filter-field '%s'", fl_nz(name, ""), filter_name);
  }
  p->filter_value = filter_value ? atoi(filter_value) : 0;
  p->cell_style = fl_eq(fl_attr(lib, c, "cell-style"), "two-line") ? REPORTVIEW_CELL_TWO_LINE : REPORTVIEW_CELL_COLUMNS;
  p->hide_column_titles = fl_eq(fl_attr(lib, c, "column-titles"), "false");
  p->blank_zeros = fl_eq(fl_attr(lib, c, "zeros"), "blank");
  p->framed = fl_eq(fl_attr(lib, c, "frame"), "true");
  const char *group_field = fl_attr(lib, c, "group-field");
  p->group_field = group_field && *group_field ? group_field : NULL;
  p->fit_rows = fl_eq(fl_attr(lib, c, "fit-rows"), "true");

  int icons = 0;
  FL_EACH_ELEMENT(ri, c) if (fl_elem(ri, "rowicon")) icons++;
  if (icons) {
    tableview_row_icon_t *row_icons = fl_alloc(lib, (size_t)icons * sizeof(*row_icons));
    if (!row_icons) { b->ok = false; return NULL; }
    int k = 0;
    FL_EACH_ELEMENT(ri, c) if (fl_elem(ri, "rowicon")) {
      const char *color = fl_attr(lib, ri, "color"), *icon = fl_attr(lib, ri, "icon");
      int role = FL_ENUM(color, kRowIconColors, -1);
      if (color && role == -1) fl_warn(b, ri, "TableView '%s': unknown RowIcon color '%s'", fl_nz(name, ""), color);
      if (!icon) fl_warn(b, ri, "TableView '%s': RowIcon requires icon=", fl_nz(name, ""));
      row_icons[k++] = (tableview_row_icon_t){ fl_attr(lib, ri, "field"), fl_attr(lib, ri, "value"), fl_nz(icon, "missing"), role };
    }
    p->row_icons = row_icons;
    p->row_icon_count = icons;
  }
  if (any_total) p->column_totals = totals;
  p->totals_label = fl_attr(lib, c, "totals-label");
  p->totals_skip = fl_attr(lib, c, "totals-skip");

  const char *record_form = fl_attr(lib, c, "form");
  if (record_form && *record_form) {
    fl_pending_t *pending = fl_alloc(lib, sizeof(*pending));
    if (!pending) { b->ok = false; return NULL; }
    *pending = (fl_pending_t){ lib->pending, p, record_form, fl_nz(name, "") };
    lib->pending = pending;
  }
  return p;
}

static const combobox_params_t *fl_combobox(fl_build_t *b, xmlNodePtr c, const char *name) {
  form_library_t *lib = b->lib;
  const char *source = fl_attr(lib, c, "source"), *display = fl_attr(lib, c, "display"), *value = fl_attr(lib, c, "value");
  if (!source) return NULL;
  if (!display || !value) { fl_error(b, c, "ComboBox '%s': source= needs display= and value=", fl_nz(name, "")); return NULL; }
  const db_table_schema_t *t = fl_table(lib, source, NULL);
  if (!t) { fl_error(b, c, "ComboBox '%s': unknown source '%s'", fl_nz(name, ""), source); return NULL; }
  combobox_params_t *p = fl_alloc(lib, sizeof(*p));
  if (!p) { b->ok = false; return NULL; }
  const char *filter_field = fl_attrs_first(lib, c, "filter-field", "filter_field");
  const char *filter_value = fl_attrs_first(lib, c, "filter-value", "filter_value");
  *p = (combobox_params_t){ NULL, t->table_id, display, value, filter_field ? filter_field : "", filter_value ? filter_value : "" };
  return p;
}

static const void *fl_lparam(fl_build_t *b, xmlNodePtr c, const char *name, const char *orientation) {
  form_library_t *lib = b->lib;
  if (fl_elem(c, "tableview")) return fl_tableview(b, c, name);
  if (fl_elem(c, "combobox")) return fl_combobox(b, c, name);
  if (fl_elem(c, "activitybar")) {
    const char *labels = fl_attr(lib, c, "labels");
    if (fl_child(c, "toolbar") || fl_child(c, "page")) {
      fl_error(b, c, "ActivityBar '%s': pages and toolbars need compiled command ids; use orionc", fl_nz(name, ""));
      return NULL;
    }
    if (!labels) return NULL;
    activitybar_params_t *p = fl_alloc(lib, sizeof(*p));
    if (!p) { b->ok = false; return NULL; }
    p->style = fl_eq(labels, "true") || fl_eq(labels, "1") || fl_eq(labels, "below") ? TOOLBAR_STYLE_SHOW_LABELS
             : fl_eq(labels, "right") ? TOOLBAR_STYLE_LIST : 0;
    if (!p->style) fl_warn(b, c, "ActivityBar '%s': unknown labels '%s' (want below or right)", fl_nz(name, ""), labels);
    return p;
  }
  if (fl_elem(c, "tabview")) {
    const char *hide = fl_attr(lib, c, "hide-single-tab"), *style = fl_attr(lib, c, "tab-style");
    bool single = fl_eq(hide, "true") || fl_eq(hide, "1"), sidebar = fl_eq(style, "sidebar");
    if (style && !sidebar) fl_warn(b, c, "TabView '%s': unknown tab-style '%s' (sidebar)", fl_nz(name, ""), style);
    if (!single && !sidebar) return NULL;
    tabview_params_t *p = fl_alloc(lib, sizeof(*p));
    if (!p) { b->ok = false; return NULL; }
    p->style = (single ? TAB_STYLE_HIDE_SINGLE_TAB : 0) | (sidebar ? TAB_STYLE_SIDEBAR : 0);
    return p;
  }
  if (fl_elem(c, "splitview")) {
    int split = fl_eq(orientation, "vertical") ? SPLIT_HORZ : SPLIT_VERT;
    const char *raw = fl_attrs_first(lib, c, "split-ratio", "split_ratio");
    if (!raw) return (const void *)(intptr_t)split;
    char *end = NULL;
    double ratio = strtod(raw, &end);
    if (end == raw || *end || !(ratio >= 0.0 && ratio <= 1.0)) {
      fl_warn(b, c, "invalid split-ratio '%s' on SplitView '%s'; using 0.5", raw, fl_nz(name, "unnamed"));
      ratio = 0.5;
    }
    splitview_params_t *p = fl_alloc(lib, sizeof(*p));
    if (!p) { b->ok = false; return NULL; }
    *p = (splitview_params_t){ split, ratio };
    return p;
  }
  return NULL;
}

// field="db.table.field": every binding of a form edits one record of one table.
static void fl_binding(fl_build_t *b, xmlNodePtr c, uint32_t id, const char *klass, const char *path) {
  char db_name[64], table[128], field[128];
  const char *d1 = strchr(path, '.'), *d2 = d1 ? strchr(d1 + 1, '.') : NULL;
  if (!d1 || !d2 || !d1[1] || !d2[1]) return;
  snprintf(db_name, sizeof(db_name), "%.*s", (int)(d1 - path), path);
  snprintf(table, sizeof(table), "%.*s", (int)(d2 - d1 - 1), d1 + 1);
  snprintf(field, sizeof(field), "%s", d2 + 1);
  char source[200];
  snprintf(source, sizeof(source), "%s.%s", db_name, table);
  const char *db = NULL;
  const db_table_schema_t *t = fl_table(b->lib, source, &db);
  if (!t) { fl_error(b, c, "field '%s': unknown table '%s'", path, source); return; }
  if (!b->bind_table) { b->bind_db = db; b->bind_table = t; }
  else if (b->bind_table != t) { fl_error(b, c, "field '%s': a form edits one table, and '%s' came first", path, b->bind_table->name); return; }
  const db_field_meta_t *meta = fl_meta(t, field);
  if (!meta) { fl_error(b, c, "field '%s': table '%s' has no field '%s'", path, t->name, field); return; }

  ctrl_binding_t *x = &b->bindings[b->binding_count++];
  *x = (ctrl_binding_t){ .ctrl_id = id, .offset = meta->offset };
  bool text = !strcasecmp(klass, "textedit") || !strcasecmp(klass, "textbox") || !strcasecmp(klass, "multiedit");
  // Text controls carry the field's size whether a typed DDX pair converts
  // the value or edGetText copies it.
  if (text) {
    switch (meta->type) {
      case DB_TYPE_DATE:   x->wparam = (size_t)meta->length; x->push = ddx_push_date;   x->pull = ddx_pull_date;   return;
      case DB_TYPE_INT:    x->wparam = sizeof(int);          x->push = ddx_push_int;    x->pull = ddx_pull_int;    return;
      case DB_TYPE_DOUBLE: x->wparam = sizeof(double);       x->push = ddx_push_double; x->pull = ddx_pull_double; return;
      case DB_TYPE_FLOAT:  x->wparam = sizeof(float);        x->push = ddx_push_float;  x->pull = ddx_pull_float;  return;
      case DB_TYPE_STRING: x->wparam = (size_t)meta->length; x->getter = edGetText; return;
      case DB_TYPE_BOOL:   x->wparam = sizeof(bool);         x->getter = edGetText; return;
    }
  }
  if (!strcasecmp(klass, "label")) {  // shows the field, never writes it back
    switch (meta->type) {
      case DB_TYPE_DATE:   x->push = ddx_push_date;       return;
      case DB_TYPE_INT:    x->push = ddx_push_int;        return;
      case DB_TYPE_DOUBLE: x->push = ddx_push_double;     return;
      case DB_TYPE_FLOAT:  x->push = ddx_push_float;      return;
      case DB_TYPE_STRING: x->push = ddx_push_label_text; return;
      default: break;
    }
  }
  if (!strcasecmp(klass, "combobox")) { x->push = ddx_push_combo_value; x->pull = ddx_pull_combo_value; return; }
  if (!strcasecmp(klass, "checkbox"))  { x->push = ddx_push_check;       x->pull = ddx_pull_check;       return; }
}

static void fl_control(fl_build_t *b, int i, uint32_t parent) {
  form_library_t *lib = b->lib;
  xmlNodePtr c = b->nodes[i];
  form_ctrl_def_t *d = &b->form->controls[i];
  fl_control_t *m = &b->form->meta[i];
  char klass[128];
  fl_ident(klass, sizeof(klass), (const char *)c->name);

  const char *text = fl_attr(lib, c, "text"), *flags_raw = fl_attr(lib, c, "flags");
  const char *orientation = fl_attrs_first(lib, c, "orientation", "layout_orientation");
  const char *spacing = fl_attrs_first(lib, c, "spacing", "layout_spacing");
  const char *font = fl_attr(lib, c, "font"), *color = fl_attr(lib, c, "color");
  isize16_t size = fl_size(lib, c);
  if (fl_has_controls(c) && !fl_elem(c, "column")) size = (isize16_t){0};

  flags_t flags = 0;
  if (!fl_flags(b, c, flags_raw, &flags)) return;
  if (FL_ENUM(orientation, kOrient, WINDOW_STACK_VERTICAL) & WINDOW_STACK_HORIZONTAL) flags |= WINDOW_STACK_HORIZONTAL;
  if (fl_elem(c, "space") || (fl_elem(c, "multiedit") && size.h <= 0)) flags |= WINDOW_FLEXSPACE;
  flags |= fl_control_size(b, c);

  d->class_name = fl_strdup(lib, klass);
  d->size = size;
  d->flags = flags;
  d->text = text ? text : "";
  d->name = m->name ? m->name : "";
  d->h_align = (uint8_t)FL_ENUM(fl_attrs_first(lib, c, "h-align", "h_align"), kAlignH, 0);
  d->v_align = (uint8_t)FL_ENUM(fl_attrs_first(lib, c, "v-align", "v_align"), kAlignV, 0);
  d->layout_spacing = fl_byte(spacing, FL_DEFAULT_SPACING);
  fl_rect(lib, c, "padding", "layout_padding", &d->padding);
  fl_rect(lib, c, "margin", "layout_margin", &d->margin);
  d->parent = parent;
  d->font = fl_eq(font, "system") ? FONT_SYSTEM : fl_eq(font, "smallest") ? FONT_SMALLEST : fl_eq(font, "title") ? FONT_TITLE : FONT_SMALL;
  d->font_set = font != NULL;
  d->color = (uint8_t)FL_ENUM(color, kColors, brTextNormal);
  d->color_set = color != NULL;
  d->lparam = fl_lparam(b, c, m->name, orientation);
  const char *source = fl_attr(lib, c, "source");
  d->source = source && *source ? source : NULL;
  d->placeholder = fl_attr(lib, c, "placeholder");
  const char *icon = fl_attr(lib, c, "icon"), *tooltip = fl_attr(lib, c, "tooltip");
  d->icon = icon && *icon ? icon : NULL;
  d->tooltip = tooltip && *tooltip ? tooltip : NULL;

  const char *context = fl_attrs_first(lib, c, "context-menu", "context_menu");
  if (context && *context) fl_error(b, c, "control '%s': context menus need compiled command ids; use orionc", fl_nz(m->name, ""));

  const char *field = fl_attr(lib, c, "field");
  if (field) fl_binding(b, c, d->id, klass, field);

  const char *action = fl_attr(lib, c, "action");
  if (fl_elem(c, "button")) {
    if (action && (strstr(action, ".insert") || strstr(action, ".update"))) b->form->def.ok_id = d->id;
    if ((text && !strcasecmp(text, "Cancel")) || (m->name && !strcasecmp(m->name, "cancel"))) b->form->def.cancel_id = d->id;
  }
  const char *target = fl_attr(lib, c, "target");
  if (target && *target) {
    uint32_t message = fl_eq(action, "insert") ? tvInsertRecord : fl_eq(action, "edit") ? tvEditRecord
                     : fl_eq(action, "delete") ? tvDeleteRecord : 0;
    uint32_t target_id = fl_id_named(b, target);
    if (!message) fl_warn(b, c, "control '%s': unknown action '%s' for target '%s' (insert, edit, delete)",
                          fl_nz(m->name, ""), fl_nz(action, ""), target);
    else if (!target_id) fl_error(b, c, "control '%s': no control named '%s'", fl_nz(m->name, ""), target);
    else { d->target = target_id; d->target_message = message; }
  }
  const char *list = fl_elem(c, "textbox") || fl_elem(c, "textedit") ? fl_attr(lib, c, "list") : NULL;
  if (list && *list) {   // the list the field drives (edSetList)
    uint32_t list_id = fl_id_named(b, list);
    if (!list_id) fl_error(b, c, "control '%s': no list named '%s'", fl_nz(m->name, ""), list);
    else d->target = list_id;
  }
}

// Every declared attribute, for layers above (a script reads action=).
static void fl_keep_attrs(form_library_t *lib, xmlNodePtr c, fl_control_t *m) {
  int n = 0;
  for (xmlAttrPtr a = c->properties; a; a = a->next) n++;
  fl_attr_t *attrs = n ? fl_alloc(lib, (size_t)n * sizeof(*attrs)) : NULL;
  int k = 0;
  for (xmlAttrPtr a = c->properties; attrs && a; a = a->next) {
    attrs[k].name = fl_strdup(lib, (const char *)a->name);
    attrs[k].value = fl_attr(lib, c, (const char *)a->name);
    k++;
  }
  m->attrs = attrs;
  m->attr_count = k;
}

static void fl_controls(fl_build_t *b, xmlNodePtr parent, uint32_t parent_id, int *at) {
  FL_EACH_ELEMENT(c, parent) if (fl_is_control(parent, c)) {
    int i = (*at)++;
    fl_control(b, i, parent_id);
    fl_controls(b, c, b->form->controls[i].id, at);
  }
}

// ── Databases declared by the document ──────────────────────────────────────

typedef struct { const char *xml; db_field_type_t type; int length; size_t size, align; } fl_type_t;

// orionc's field types. A record lays its fields out as the C struct orionc
// would declare, so offsets match the compiler's: natural alignment, strings
// and dates as char arrays.
static const fl_type_t kTypes[] = {
  { "integer", DB_TYPE_INT, 0, sizeof(int), _Alignof(int) },     { "int", DB_TYPE_INT, 0, sizeof(int), _Alignof(int) },
  { "string", DB_TYPE_STRING, 256, 1, 1 },                         { "date", DB_TYPE_DATE, 11, 1, 1 },
  { "boolean", DB_TYPE_BOOL, 0, sizeof(bool), _Alignof(bool) },    { "bool", DB_TYPE_BOOL, 0, sizeof(bool), _Alignof(bool) },
  { "float", DB_TYPE_FLOAT, 0, sizeof(float), _Alignof(float) },   { "real", DB_TYPE_FLOAT, 0, sizeof(float), _Alignof(float) },
  { "double", DB_TYPE_DOUBLE, 0, sizeof(double), _Alignof(double) },
};

static const fl_type_t *fl_type(const char *xml) {
  for (size_t i = 0; i < sizeof(kTypes) / sizeof(kTypes[0]); i++)
    if (fl_eq(xml, kTypes[i].xml)) return &kTypes[i];
  return &kTypes[0];
}

static bool fl_table_schema(form_library_t *lib, xmlNodePtr node, int table_id, db_table_schema_t *t) {
  int fields = 0, joins = 0;
  FL_EACH_ELEMENT(f, node) if (fl_elem(f, "field")) {
    fields++;
    const char *rel = fl_attr(lib, f, "relation");
    joins += rel && strrchr(rel, '.') != NULL;
  }
  db_field_schema_t *schema = fl_alloc(lib, (size_t)(fields ? fields : 1) * sizeof(*schema));
  db_field_meta_t *meta = fl_alloc(lib, (size_t)(fields ? fields : 1) * sizeof(*meta));
  db_join_schema_t *join = joins ? fl_alloc(lib, (size_t)joins * sizeof(*join)) : NULL;
  if (!schema || !meta || (joins && !join)) return false;
  t->table_id = table_id;
  t->name = fl_attr(lib, node, "name");
  t->model = fl_attr(lib, node, "model");
  const char *transient = fl_attr(lib, node, "transient");
  t->transient = fl_eq(transient, "true") || fl_eq(transient, "1");
  size_t offset = 0, align = 1;
  int i = 0, j = 0;
  FL_EACH_ELEMENT(f, node) if (fl_elem(f, "field")) {
    const fl_type_t *type = fl_type(fl_attr(lib, f, "type"));
    const char *len = fl_attr(lib, f, "length"), *key = fl_attr(lib, f, "key"), *precision = fl_attr(lib, f, "precision");
    const char *rel = fl_attr(lib, f, "relation"), *on_delete = fl_attrs_first(lib, f, "on-delete", "on_delete");
    bool chars = type->type == DB_TYPE_STRING || type->type == DB_TYPE_DATE;
    int length = chars ? (len ? atoi(len) : type->length) : 0;
    size_t size = chars ? (size_t)length : type->size;
    offset = (offset + type->align - 1) / type->align * type->align;
    if (type->align > align) align = type->align;
    db_field_schema_t *fs = &schema[i];
    fs->name = fl_attr(lib, f, "name");
    fs->type = type->type;
    fs->length = length;
    fs->primary_key = key && (fl_eq(key, "YES") || fl_eq(key, "yes") || fl_eq(key, "true") || fl_eq(key, "1"));
    fs->default_value = fl_text(lib, f, "default");
    fs->delete_rule = !on_delete ? DB_DELETE_NO_ACTION : fl_eq(on_delete, "cascade") ? DB_DELETE_CASCADE
                    : fl_eq(on_delete, "nullify") ? DB_DELETE_NULLIFY : fl_eq(on_delete, "deny") ? DB_DELETE_DENY : DB_DELETE_NO_ACTION;
    if (on_delete && fs->delete_rule == DB_DELETE_NO_ACTION) {
      fprintf(stderr, "[form_loader] %s:%ld: field '%s': unknown on-delete '%s' (cascade, nullify, deny)\n",
              lib->origin, xmlGetLineNo(f), fl_nz(fs->name, ""), on_delete);
      fflush(stderr);
    }
    const char *dot = rel ? strrchr(rel, '.') : NULL;
    if (dot) {
      char *table = fl_alloc(lib, (size_t)(dot - rel) + 1);
      if (!table) return false;
      memcpy(table, rel, (size_t)(dot - rel));
      fs->relation_table = table;
      fs->relation_field = fl_strdup(lib, dot + 1);
      // The join alias: join=, or the field name without its _id.
      const char *alias = fl_attr(lib, f, "join");
      char buf[128];
      snprintf(buf, sizeof(buf), "%s", alias && *alias ? alias : fl_nz(fs->name, ""));
      size_t n = strlen(buf);
      if (!(alias && *alias) && n > 3 && !strcmp(buf + n - 3, "_id")) buf[n - 3] = 0;
      join[j++] = (db_join_schema_t){ fl_strdup(lib, buf), fs->name, fs->relation_table, fs->relation_field };
    }
    meta[i] = (db_field_meta_t){ fs->name, type->type, offset, length, precision ? atoi(precision) : 0 };
    offset += size;
    i++;
  }
  t->fields = schema;
  t->field_count = fields;
  t->joins = join;
  t->join_count = joins;
  t->meta = meta;
  t->meta_count = fields;
  t->record_size = (offset + align - 1) / align * align;
  return true;
}

static bool fl_schema(form_library_t *lib, xmlNodePtr db, fl_schema_t *out) {
  int tables = 0;
  FL_EACH_ELEMENT(t, db) if (fl_elem(t, "table")) tables++;
  db_table_schema_t *table = fl_alloc(lib, (size_t)(tables ? tables : 1) * sizeof(*table));
  if (!table) return false;
  int i = 0;
  FL_EACH_ELEMENT(t, db) if (fl_elem(t, "table")) {
    if (!fl_table_schema(lib, t, i, &table[i])) return false;
    i++;
  }
  out->def = (db_schema_def_t){ fl_nz(fl_attr(lib, db, "name"), "db"), fl_attr(lib, db, "class"),
                                fl_attr(lib, db, "source"), table, tables };
  return true;
}

// <databases><database>...</databases>, or a lone <database> under <orion>.
static bool fl_schemas(form_library_t *lib, xmlNodePtr root) {
  xmlNodePtr databases = fl_child(root, "databases");
  xmlNodePtr scope = databases ? databases : root;
  FL_EACH_ELEMENT(db, scope) if (fl_elem(db, "database")) lib->schema_count++;
  if (!lib->schema_count) return true;
  lib->schemas = fl_alloc(lib, (size_t)lib->schema_count * sizeof(*lib->schemas));
  if (!lib->schemas) return false;
  int i = 0;
  FL_EACH_ELEMENT(db, scope) if (fl_elem(db, "database")) {
    if (!fl_schema(lib, db, &lib->schemas[i++])) return false;
  }
  return true;
}

// ── Forms ───────────────────────────────────────────────────────────────────

static void fl_form(form_library_t *lib, xmlNodePtr node, fl_form_t *form) {
  fl_build_t b = { .lib = lib, .form = form, .form_node = node, .ok = true };
  form_def_t *def = &form->def;
  form->name = fl_attr(lib, node, "name");
  const char *title = fl_attr(lib, node, "title"), *role = fl_attr(lib, node, "role");
  char id[128];
  fl_ident(id, sizeof(id), form->name);
  def->id = fl_strdup(lib, id);
  def->name = fl_nz(title, form->name);

  isize16_t size = fl_size(lib, node);
  def->width = size.w;
  def->height = size.h;
  if (!def->width) fl_error(&b, node, "requires width=");
  def->role = !role || !*role || fl_eq(role, "normal") ? WINDOW_ROLE_NORMAL
            : fl_eq(role, "host") ? WINDOW_ROLE_HOST : fl_eq(role, "page") ? WINDOW_ROLE_PAGE : WINDOW_ROLE_NORMAL;
  if (role && *role && !fl_eq(role, "normal") && !fl_eq(role, "host") && !fl_eq(role, "page"))
    fl_error(&b, node, "invalid role '%s'", role);
  if (fl_child(node, "toolbar") || fl_child(node, "panetoolbar"))
    fl_error(&b, node, "toolbars need compiled command ids; use orionc");
  flags_t flags = 0;
  fl_flags(&b, node, fl_attr(lib, node, "flags"), &flags);
  def->flags = flags | WINDOW_AUTO_LAYOUT;
  def->layout_spacing = fl_byte(fl_attrs_first(lib, node, "spacing", "layout_spacing"), FL_DEFAULT_SPACING);
  fl_rect(lib, node, "padding", "layout_padding", &def->padding);
  fl_rect(lib, node, "margin", "layout_margin", &def->margin);
  const char *toolbar_style = fl_attr(lib, node, "toolbar-style");
  def->toolbar_style = fl_eq(toolbar_style, "labels") ? TOOLBAR_STYLE_SHOW_LABELS : fl_eq(toolbar_style, "list") ? TOOLBAR_STYLE_LIST : 0;
  def->icon = fl_attr(lib, node, "icon");
  def->checked_icon = fl_attr(lib, node, "checked-icon");
  def->tooltip = fl_attr(lib, node, "tooltip");
  def->label = fl_attr(lib, node, "label");

  b.count = fl_count(node);
  if (b.count) {
    b.nodes = calloc((size_t)b.count, sizeof(*b.nodes));
    form->controls = fl_alloc(lib, (size_t)b.count * sizeof(*form->controls));
    form->meta = fl_alloc(lib, (size_t)b.count * sizeof(*form->meta));
    b.bindings = fl_alloc(lib, (size_t)b.count * sizeof(*b.bindings));
    if (!b.nodes || !form->controls || !form->meta || !b.bindings) {
      free(b.nodes);
      fprintf(stderr, "[form_loader] %s: out of memory for form '%s'\n", lib->origin, fl_nz(form->name, "?"));
      fflush(stderr);
      form->rejected = true;
      return;
    }
    int at = 0;
    fl_collect(&b, node, &at);
    // Ids and names first, so targets and masters can name any control.
    for (int i = 0; i < b.count; i++) {
      form->controls[i].id = lib->next_id++;
      form->meta[i].name = fl_attr(lib, b.nodes[i], "name");
      fl_keep_attrs(lib, b.nodes[i], &form->meta[i]);
    }
    form->control_count = b.count;
    at = 0;
    fl_controls(&b, node, 0, &at);
  }
  def->children = form->controls;
  def->child_count = form->control_count;
  if (b.binding_count) {
    def->bindings = b.bindings;
    def->binding_count = b.binding_count;
    def->db_name = b.bind_db;
    def->db_table = b.bind_table->name;
    def->db_table_id = b.bind_table->table_id;
    def->db_fields = b.bind_table->meta;
    def->db_field_count = b.bind_table->field_count;
    def->db_record_size = b.bind_table->record_size;
  }
  free(b.nodes);
  form->rejected = !b.ok;
  if (!b.ok) {
    fprintf(stderr, "[form_loader] %s: form '%s' left out\n", lib->origin, fl_nz(form->name, "?"));
    fflush(stderr);
  }
}

static fl_form_t *fl_find(const form_library_t *lib, const char *name) {
  for (int i = 0; lib && name && i < lib->form_count; i++)
    if (!lib->forms[i].rejected && fl_eq(lib->forms[i].name, name)) return &lib->forms[i];
  return NULL;
}

static fl_form_t *fl_owner(const form_library_t *lib, const form_def_t *def) {
  for (int i = 0; lib && def && i < lib->form_count; i++)
    if (&lib->forms[i].def == def && !lib->forms[i].rejected) return &lib->forms[i];
  return NULL;
}

static form_library_t *fl_build(xmlDocPtr doc, const char *origin) {
  xmlNodePtr root = doc ? xmlDocGetRootElement(doc) : NULL;
  if (!fl_elem(root, "orion")) {
    fprintf(stderr, "[form_loader] %s: not an <orion> document\n", origin);
    fflush(stderr);
    if (doc) xmlFreeDoc(doc);
    return NULL;
  }
  form_library_t *lib = calloc(1, sizeof(*lib));
  if (!lib) { xmlFreeDoc(doc); return NULL; }
  lib->origin = "";
  lib->origin = fl_strdup(lib, origin);
  lib->next_id = FORM_LIBRARY_ID_BASE;
  if (!fl_schemas(lib, root)) { xmlFreeDoc(doc); form_library_free(lib); return NULL; }
  xmlNodePtr forms = fl_child(root, "forms");
  FL_EACH_ELEMENT(f, forms) if (fl_elem(f, "form")) lib->form_count++;
  lib->forms = lib->form_count ? fl_alloc(lib, (size_t)lib->form_count * sizeof(*lib->forms)) : NULL;
  if (lib->form_count && !lib->forms) { xmlFreeDoc(doc); form_library_free(lib); return NULL; }
  int i = 0;
  FL_EACH_ELEMENT(f, forms) if (fl_elem(f, "form")) fl_form(lib, f, &lib->forms[i++]);
  xmlFreeDoc(doc);

  // Record forms may come later in the file; a TableView's form must exist.
  for (fl_pending_t *p = lib->pending; p; p = p->next) {
    fl_form_t *target = fl_find(lib, p->form_name);
    if (target) { p->params->record_form = &target->def; continue; }
    for (int k = 0; k < lib->form_count; k++) {
      fl_form_t *owner = &lib->forms[k];
      for (int c = 0; c < owner->control_count; c++)
        if (owner->controls[c].lparam == p->params && !owner->rejected) {
          fprintf(stderr, "[form_loader] %s: form '%s': TableView '%s': unknown form '%s'; form left out\n",
                  lib->origin, fl_nz(owner->name, "?"), p->control_name, p->form_name);
          fflush(stderr);
          owner->rejected = true;
        }
    }
  }
  return lib;
}

// ── Public API ──────────────────────────────────────────────────────────────

form_library_t *form_library_load(const char *path) {
  if (!path || !*path) {
    fprintf(stderr, "[form_loader] form_library_load: no path\n");
    fflush(stderr);
    return NULL;
  }
  xmlDocPtr doc = xmlReadFile(path, NULL, XML_PARSE_NONET);
  if (!doc) {
    fprintf(stderr, "[form_loader] %s: cannot read the document\n", path);
    fflush(stderr);
    return NULL;
  }
  return fl_build(doc, path);
}

form_library_t *form_library_parse(const char *xml, size_t length, const char *origin) {
  origin = fl_nz(origin, "<memory>");
  if (!xml || length > (size_t)INT32_MAX) {
    fprintf(stderr, "[form_loader] %s: no document\n", origin);
    fflush(stderr);
    return NULL;
  }
  xmlDocPtr doc = xmlReadMemory(xml, (int)length, origin, NULL, XML_PARSE_NONET);
  if (!doc) {
    fprintf(stderr, "[form_loader] %s: cannot parse the document\n", origin);
    fflush(stderr);
    return NULL;
  }
  return fl_build(doc, origin);
}

void form_library_free(form_library_t *lib) {
  if (!lib) return;
  for (fl_block_t *b = lib->arena, *next; b; b = next) { next = b->next; free(b); }
  free(lib);
}

int form_library_form_count(const form_library_t *lib) {
  int n = 0;
  for (int i = 0; lib && i < lib->form_count; i++) n += !lib->forms[i].rejected;
  return n;
}

const form_def_t *form_library_form_at(const form_library_t *lib, int index) {
  for (int i = 0; lib && i < lib->form_count; i++)
    if (!lib->forms[i].rejected && index-- == 0) return &lib->forms[i].def;
  return NULL;
}

const form_def_t *form_library_form(const form_library_t *lib, const char *name) {
  fl_form_t *f = fl_find(lib, name);
  return f ? &f->def : NULL;
}

uint32_t form_library_control_id(const form_library_t *lib, const form_def_t *form, const char *name) {
  fl_form_t *f = fl_owner(lib, form);
  for (int i = 0; f && name && i < f->control_count; i++)
    if (fl_eq(f->meta[i].name, name)) return f->controls[i].id;
  return 0;
}

const form_ctrl_def_t *form_library_control(const form_library_t *lib, const form_def_t *form, uint32_t id) {
  fl_form_t *f = fl_owner(lib, form);
  for (int i = 0; f && i < f->control_count; i++)
    if (f->controls[i].id == id) return &f->controls[i];
  return NULL;
}

int form_library_schema_count(const form_library_t *lib) { return lib ? lib->schema_count : 0; }

const db_schema_def_t *form_library_schema_at(const form_library_t *lib, int index) {
  return lib && index >= 0 && index < lib->schema_count ? &lib->schemas[index].def : NULL;
}

const db_schema_def_t *form_library_schema(const form_library_t *lib, const char *name) {
  for (int i = 0; lib && name && i < lib->schema_count; i++)
    if (fl_eq(lib->schemas[i].def.name, name)) return &lib->schemas[i].def;
  return NULL;
}

const char *form_library_control_attr(const form_library_t *lib, const form_def_t *form,
                                      uint32_t id, const char *attr) {
  fl_form_t *f = fl_owner(lib, form);
  for (int i = 0; f && attr && i < f->control_count; i++) {
    if (f->controls[i].id != id) continue;
    for (int k = 0; k < f->meta[i].attr_count; k++)
      if (fl_eq(f->meta[i].attrs[k].name, attr)) return f->meta[i].attrs[k].value;
    return NULL;
  }
  return NULL;
}
