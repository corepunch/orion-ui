#include <libxml/parser.h>
#include <libxml/tree.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <orion/user/enum_parse.h>
#include <orion/user/user.h>

#define ORIONC_MAX_IDS 512
#define ORIONC_MAX_IDENT 128
#define ORIONC_DEFAULT_SPACING 4
#define ORIONC_STRING_SIZE 512
#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define EACH_ELEMENT(n, parent) for (xmlNodePtr n = (parent) ? (parent)->children : NULL; n; n = n->next) if ((n)->type == XML_ELEMENT_NODE)
#define OUT(...) fprintf(f, __VA_ARGS__)
#define LINE(s) fputs((s), f)

typedef struct { int x, y, w, h; } rect_t;
typedef struct { const char *key, *value; } kv_t;
typedef struct { char name[ORIONC_MAX_IDENT]; } orion_id_t;
typedef struct { orion_id_t v[ORIONC_MAX_IDS]; int n; } ids_t;
typedef struct { char *v[10]; } attrs_t;
enum { A_NAME, A_TEXT, A_FLAGS, A_HA, A_VA, A_ORIENT, A_SPACING, A_FONT, A_COLOR, A_FIELD };
typedef struct { char ctrl[128], db[64], table[64], field[64], klass[64]; } binding_t;
typedef struct { binding_t v[128]; int n; char db[64], table[64]; } bindings_t;
typedef struct { char ok_id[256], cancel_id[256]; } button_ids_t;

// A surface reference to a menu-declared action (toolbar/context `menu="x.y"`
// or `command="x.y"`). Kept separate so unknown references can be rejected.
typedef struct { char name[ORIONC_MAX_IDENT]; char id[ORIONC_MAX_IDENT]; } cmd_ref_t;
typedef struct { cmd_ref_t v[ORIONC_MAX_IDS]; int n; } cmd_refs_t;

// Action metadata derived from a menu declaration (menu items are the sole
// action registry). Emitted as gc_action_meta[].
typedef struct {
  char name[ORIONC_MAX_IDENT];     // dotted "remote.sync"
  char id[ORIONC_MAX_IDENT];       // "ID_REMOTE_SYNC"
  char label[ORIONC_STRING_SIZE];  // "Sync Current Branch"
  char category[ORIONC_MAX_IDENT]; // menu name "remote"
  char hotkey[ORIONC_MAX_IDENT];   // "F5" / "Ctrl+K" / ""
} action_meta_t;
typedef struct { action_meta_t v[ORIONC_MAX_IDS]; int n; } action_meta_list_t;

typedef struct {
  const char *xml, *c, *db;
  int size;
} field_type_t;

static const field_type_t kFieldTypes[] = {
  { "integer", "int", "DB_TYPE_INT", 0 }, { "int", "int", "DB_TYPE_INT", 0 },
  { "string", "char", "DB_TYPE_STRING", 256 }, { "date", "char", "DB_TYPE_DATE", 11 }, { "boolean", "bool", "DB_TYPE_BOOL", 0 },
  { "bool", "bool", "DB_TYPE_BOOL", 0 }, { "float", "float", "DB_TYPE_FLOAT", 0 },
  { "real", "float", "DB_TYPE_FLOAT", 0 }, { "double", "double", "DB_TYPE_DOUBLE", 0 },
};

static const enum_token_t kAlignH[] = {
  {"stretch", LAYOUT_ALIGN_STRETCH}, {"left", LAYOUT_ALIGN_START},
  {"start", LAYOUT_ALIGN_START}, {"center", LAYOUT_ALIGN_CENTER},
  {"right", LAYOUT_ALIGN_END}, {"end", LAYOUT_ALIGN_END},
};
static const enum_token_t kAlignV[] = {
  {"stretch", LAYOUT_ALIGN_STRETCH}, {"top", LAYOUT_ALIGN_START},
  {"start", LAYOUT_ALIGN_START}, {"center", LAYOUT_ALIGN_CENTER},
  {"bottom", LAYOUT_ALIGN_END}, {"end", LAYOUT_ALIGN_END},
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
  {"button-inner", brButtonInner},
  {"button-hover", brButtonHover}, {"text-normal", brTextNormal},
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

static bool eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }
static const char *nz(const char *s, const char *d) { return (s && *s) ? s : d; }
static bool elem(xmlNodePtr n, const char *name) {
  return n && n->type == XML_ELEMENT_NODE && xmlStrcasecmp(n->name, BAD_CAST name) == 0;
}
static char *attr(xmlNodePtr n, const char *name) { xmlChar *r = xmlGetProp(n, BAD_CAST name); char *s = r ? strdup((char *)r) : NULL; if (r) xmlFree(r); return s; }
static char *attrs_first(xmlNodePtr n, const char *a, const char *b) { char *v = attr(n, a); if (v && *v) return v; free(v); return b ? attr(n, b) : NULL; }
static xmlNodePtr child(xmlNodePtr n, const char *name) { EACH_ELEMENT(c, n) if (elem(c, name)) return c; return NULL; }

static void ident(char *out, size_t cap, const char *s, bool upper) {
  size_t n = 0;
  for (const unsigned char *p = (const unsigned char *)nz(s, "item"); *p && n + 1 < cap; p++) {
    bool ok = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9');
    char c = ok ? (char)*p : '_';
    out[n++] = (upper && c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
  }
  if (!n) out[n++] = '_';
  out[n] = 0;
}

static bool ident_expr(const char *s) {
  if (!s || !((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || *s == '_')) return false;
  for (s++; *s; s++) if (!((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '_')) return false;
  return true;
}

static bool db_path(const char *s, char *db, size_t db_cap, char *table, size_t table_cap, char *field, size_t field_cap) {
  const char *a = s ? strchr(s, '.') : NULL, *b = a ? strchr(a + 1, '.') : NULL;
  if (!a || !b || !a[1] || !b[1]) return false;
  snprintf(db, db_cap, "%.*s", (int)(a - s), s);
  snprintf(table, table_cap, "%.*s", (int)(b - a - 1), a + 1);
  snprintf(field, field_cap, "%s", b + 1);
  return true;
}

static void scoped(char *out, size_t cap, const char *scope, const char *name, const char *fallback) {
  char s[ORIONC_MAX_IDENT], n[ORIONC_MAX_IDENT];
  ident(s, sizeof(s), nz(scope, "scope"), true);
  ident(n, sizeof(n), nz(name, nz(fallback, "item")), true);
  snprintf(out, cap, "ID_%s_%s", s, n);
}

static void control_id(char *out, size_t cap, const char *form, const char *name, const char *klass, int ordinal) {
  char fallback[ORIONC_MAX_IDENT];
  snprintf(fallback, sizeof(fallback), "%s%d", nz(klass, "control"), ordinal);
  scoped(out, cap, form, name, fallback);
}

static void cstr(char *out, size_t cap, const char *s) {
  size_t n = 0;
  out[n++] = '"';
  for (const unsigned char *p = (const unsigned char *)nz(s, ""); *p && n + 5 < cap; p++) {
    if (*p == '\\' || *p == '"') { out[n++] = '\\'; out[n++] = (char)*p; }
    else if (*p == '\n') { out[n++] = '\\'; out[n++] = 'n'; }
    else if (*p == '\t') { out[n++] = '\\'; out[n++] = 't'; }
    else out[n++] = (char)*p;
  }
  out[n++] = '"';
  out[n] = 0;
}

static void cstr_shortcut(char *out, size_t cap, const char *label, const char *shortcut) {
  char display[ORIONC_MAX_IDENT];
  snprintf(display, sizeof(display), "%s", nz(shortcut, ""));
  char *alternate = strchr(display, ';');
  if (alternate) *alternate = 0;
  char joined[ORIONC_STRING_SIZE];
  snprintf(joined, sizeof(joined), "%s%s%s", nz(label, ""), display[0] ? "\t" : "", display);
  cstr(out, cap, joined);
}

// `shortcut` is the established .orion spelling used by imageeditor.
// `hotkey` remains accepted for compatibility with early action-manifest work.
static char *menu_shortcut(xmlNodePtr item) {
  char *shortcut = attr(item, "shortcut");
  if (shortcut && *shortcut) return shortcut;
  free(shortcut);
  return attr(item, "hotkey");
}

static void tpl(FILE *f, const char *s, const kv_t *kv) {
  for (const char *p = s; *p;) {
    const char *open = strstr(p, "{{");
    if (!open) { fputs(p, f); break; }
    fwrite(p, 1, (size_t)(open - p), f);
    const char *close = strstr(open + 2, "}}");
    if (!close) { fputs(open, f); break; }
    char key[64];
    snprintf(key, sizeof(key), "%.*s", (int)(close - open - 2), open + 2);
    for (int i = 0; kv[i].key; i++) if (eq(kv[i].key, key)) { fputs(kv[i].value, f); break; }
    p = close + 2;
  }
}

static bool add_id(ids_t *ids, const char *name) {
  if (!ident_expr(name) || eq(name, "ID_OK") || eq(name, "ID_CANCEL")) return true;
  for (int i = 0; i < ids->n; i++) if (eq(ids->v[i].name, name)) return true;
  if (ids->n >= ORIONC_MAX_IDS) return false;
  snprintf(ids->v[ids->n++].name, ORIONC_MAX_IDENT, "%s", name);
  return true;
}

static bool rect_attr(xmlNodePtr n, const char *name, rect_t *r) {
  char *s = attr(n, name);
  int a = 0, b = 0, c = 0, d = 0, count = s ? sscanf(s, "%d %d %d %d", &a, &b, &c, &d) : 0;
  free(s);
  if (count == 1) *r = (rect_t){a, a, a, a};
  else if (count == 2) *r = (rect_t){a, b, a, b};
  else if (count == 4) *r = (rect_t){a, b, c, d};
  return count == 1 || count == 2 || count == 4;
}

static rect_t size_attr(xmlNodePtr n) {
  char *w = attrs_first(n, "w", "width"), *h = attrs_first(n, "h", "height");
  rect_t r = {0, 0, w ? atoi(w) : 0, h ? atoi(h) : 0};
  if (elem(n, "Column") && eq(w, "auto")) r.w = -1;
  free(w); free(h);
  return r;
}

static unsigned byte_attr(const char *s, unsigned def) {
  char *end = NULL; long v = s ? strtol(s, &end, 0) : def;
  return (s && end && !*end && v >= 0 && v <= 255) ? (unsigned)v : def;
}

static void read_control_attrs(xmlNodePtr n, attrs_t *a) {
  static const struct { int slot; const char *name, *alias; } cfg[] = {
    {A_NAME, "name", NULL}, {A_TEXT, "text", NULL}, {A_FLAGS, "flags", NULL},
    {A_HA, "h-align", "h_align"}, {A_VA, "v-align", "v_align"},
    {A_ORIENT, "orientation", "layout_orientation"}, {A_SPACING, "spacing", "layout_spacing"},
    {A_FONT, "font", NULL}, {A_COLOR, "color", NULL}, {A_FIELD, "field", NULL},
  };
  memset(a, 0, sizeof(*a));
  for (int i = 0; i < ARRAY_LEN(cfg); i++) a->v[cfg[i].slot] = attrs_first(n, cfg[i].name, cfg[i].alias);
}

static void free_attrs(attrs_t *a) { for (int i = 0; i < ARRAY_LEN(a->v); i++) free(a->v[i]); }
static bool is_control(xmlNodePtr parent, xmlNodePtr n) {
  return n && n->type == XML_ELEMENT_NODE && !elem(n, "requires") && !elem(n, "toolbar") && !elem(n, "panetoolbar") &&
         !(elem(parent, "tableview") && (elem(n, "column") || elem(n, "rowicon"))) &&
         !(elem(parent, "activitybar") && elem(n, "page"));
}
static bool has_controls(xmlNodePtr n) { EACH_ELEMENT(c, n) if (is_control(n, c)) return true; return false; }

static void emit_defines(FILE *f, const ids_t *ids, const char *base) {
  for (int i = 0; i < ids->n; i++) OUT("#define %s (%s + %d)\n", ids->v[i].name, base, i + 1);
  if (ids->n) LINE("\n");
}

static void collect_menu_ids(ids_t *ids, action_meta_list_t *meta,
                             xmlNodePtr menu, const char *scope,
                             const char *category) {
  EACH_ELEMENT(it, menu) {
    if (elem(it, "submenu")) { collect_menu_ids(ids, meta, it, scope, category); continue; }
    if (!elem(it, "item")) continue;
    char *name = attr(it, "name"), *label = attr(it, "label"), *hotkey = menu_shortcut(it), id[256];
    scoped(id, sizeof(id), scope, name, label);
    add_id(ids, id);
    // Record metadata for gc_action_meta[].
    if (meta && meta->n < ORIONC_MAX_IDS) {
      char dotted[ORIONC_MAX_IDENT];
      snprintf(dotted, sizeof(dotted), "%s.%s", category, nz(name, label));
      action_meta_t *m = &meta->v[meta->n++];
      memset(m, 0, sizeof(*m));
      snprintf(m->name, sizeof(m->name), "%s", dotted);
      snprintf(m->id, sizeof(m->id), "%s", id);
      snprintf(m->label, sizeof(m->label), "%s", nz(label, name));
      snprintf(m->category, sizeof(m->category), "%s", category);
      if (hotkey && *hotkey) snprintf(m->hotkey, sizeof(m->hotkey), "%s", hotkey);
    }
    free(name); free(label); free(hotkey);
  }
}

static const char *toolbar_type(xmlNodePtr n) {
  if (elem(n, "button")) return "TOOLBAR_ITEM_BUTTON";
  if (elem(n, "label")) return "TOOLBAR_ITEM_LABEL";
  if (elem(n, "combobox")) return "TOOLBAR_ITEM_COMBOBOX";
  if (elem(n, "slider")) return "TOOLBAR_ITEM_SLIDER";
  if (elem(n, "segmentedcontrol")) return "TOOLBAR_ITEM_SEGMENTED";
  if (elem(n, "textedit")) return "TOOLBAR_ITEM_TEXTEDIT";
  if (elem(n, "separator")) return "TOOLBAR_ITEM_SEPARATOR";
  if (elem(n, "spacer")) return "TOOLBAR_ITEM_SPACER";
  if (elem(n, "title")) return "TOOLBAR_ITEM_TITLE";
  if (elem(n, "sidebar-separator")) return "TOOLBAR_ITEM_SIDEBAR_SEPARATOR";
  return NULL;
}

// Resolve a surface item to its command ID. A dotted menu/command reference
// ("remote.sync") resolves to the menu-declared ID_GROUP_ACTION and returns
// true so the caller can validate it. Otherwise falls back to the legacy
// scoped ID_<scope>_<name> and returns false.
static bool resolve_action(char *out, size_t cap, const char *command,
                           const char *menu, const char *scope,
                           const char *name, const char *fallback) {
  const char *ref = (command && *command) ? command
                  : (menu && strchr(menu, '.')) ? menu : NULL;
  if (ref) {
    char id[ORIONC_MAX_IDENT];
    ident(id, sizeof(id), ref, true);
    snprintf(out, cap, "ID_%s", id);
    return true;
  }
  scoped(out, cap, nz(menu, scope), name, fallback);
  return false;
}

// Parse a hotkey spec ("F5", "Ctrl+K", "Ctrl+Shift+F") into C expressions for
// the accel_t fVirt and key fields. Returns true on success.
static bool parse_hotkey(const char *spec, char *fvirt, size_t fv_cap,
                         char *key, size_t key_cap) {
  if (!spec || !*spec) return false;
  char buf[64];
  snprintf(buf, sizeof(buf), "%s", spec);
  for (char *p = buf; *p; p++) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 'a' + 'A');

  bool ctrl = false, shift = false, alt = false;
  const char *keyname = NULL;
  char *tok = buf;
  while (*tok) {
    char *sep = strchr(tok, '+');
    if (sep) *sep = 0;
    if (eq(tok, "CTRL")) ctrl = true;
    else if (eq(tok, "SHIFT")) shift = true;
    else if (eq(tok, "ALT")) alt = true;
    else keyname = tok;
    if (!sep) break;
    tok = sep + 1;
  }
  if (!keyname) return false;

  char k[64] = {0};
  if (keyname[0] == 'F' && keyname[1] >= '1' && keyname[1] <= '9') {
    int fn = atoi(keyname + 1);
    if (fn >= 1 && fn <= 12) snprintf(k, sizeof(k), "AX_KEY_F%d", fn);
  } else if (eq(keyname, "SPACE")) snprintf(k, sizeof(k), "AX_KEY_SPACE");
  else if (eq(keyname, "ENTER")) snprintf(k, sizeof(k), "AX_KEY_ENTER");
  else if (eq(keyname, "ESCAPE") || eq(keyname, "ESC")) snprintf(k, sizeof(k), "AX_KEY_ESCAPE");
  else if (eq(keyname, "BACKSPACE")) snprintf(k, sizeof(k), "AX_KEY_BACKSPACE");
  else if (eq(keyname, "TAB")) snprintf(k, sizeof(k), "AX_KEY_TAB");
  else if (eq(keyname, "HOME")) snprintf(k, sizeof(k), "AX_KEY_HOME");
  else if (eq(keyname, "END")) snprintf(k, sizeof(k), "AX_KEY_END");
  else if (eq(keyname, "LEFT")) snprintf(k, sizeof(k), "AX_KEY_LEFTARROW");
  else if (eq(keyname, "RIGHT")) snprintf(k, sizeof(k), "AX_KEY_RIGHTARROW");
  else if (eq(keyname, "UP")) snprintf(k, sizeof(k), "AX_KEY_UPARROW");
  else if (eq(keyname, "DOWN")) snprintf(k, sizeof(k), "AX_KEY_DOWNARROW");
  else if (eq(keyname, "PAGE UP")) snprintf(k, sizeof(k), "AX_KEY_PGUP");
  else if (eq(keyname, "PAGE DOWN")) snprintf(k, sizeof(k), "AX_KEY_PGDN");
  else if (eq(keyname, "DELETE") || eq(keyname, "DEL")) snprintf(k, sizeof(k), "AX_KEY_DEL");
  else if (eq(keyname, "EQUALS")) snprintf(k, sizeof(k), "AX_KEY_EQUALS");
  else if (eq(keyname, "MINUS")) snprintf(k, sizeof(k), "AX_KEY_MINUS");
  else if (eq(keyname, "SLASH")) snprintf(k, sizeof(k), "AX_KEY_SLASH");
  else if (keyname[0] >= 'A' && keyname[0] <= 'Z' && keyname[1] == 0)
    snprintf(k, sizeof(k), "AX_KEY_%c", keyname[0]);
  else if (keyname[0] >= '0' && keyname[0] <= '9' && keyname[1] == 0)
    snprintf(k, sizeof(k), "AX_KEY_%c", keyname[0]);
  if (!k[0]) return false;

  snprintf(fvirt, fv_cap, "FVIRTKEY%s%s%s",
           ctrl ? " | FCONTROL" : "", shift ? " | FSHIFT" : "", alt ? " | FALT" : "");
  snprintf(key, key_cap, "%s", k);
  return true;
}

// A menu action may expose more than one equivalent accelerator, for example
// Delete and Backspace or a primary key plus its Shift duplicate.  Keep the
// manifest compact while emitting one framework accelerator per spelling.
static bool next_hotkey(const char **cursor, char *out, size_t cap) {
  const char *p = *cursor;
  if (!p || !*p) return false;
  const char *end = strchr(p, ';');
  size_t n = end ? (size_t)(end - p) : strlen(p);
  if (!n || n >= cap) return false;
  memcpy(out, p, n); out[n] = 0;
  *cursor = end ? end + 1 : p + n;
  return true;
}

static int hotkey_count(const char *spec) {
  int n = 0; char token[64]; const char *cursor = spec;
  while (next_hotkey(&cursor, token, sizeof(token))) n++;
  return n;
}

static void collect_toolbar_ids(ids_t *ids, cmd_refs_t *refs,
                                xmlNodePtr toolbar, const char *scope_name) {
  if (toolbar) {
    char tb_scope[128]; ident(tb_scope, sizeof(tb_scope), scope_name, true);
    EACH_ELEMENT(it, toolbar) if (toolbar_type(it) && !elem(it, "separator") && !elem(it, "spacer") && !elem(it, "sidebar-separator")) {
      char *command = attr(it, "command"), *menu = attr(it, "menu"), *name = attr(it, "name"), *text = attr(it, "text"), id[256];
      if (resolve_action(id, sizeof(id), command, menu, tb_scope, name, text)) {
        if (refs && refs->n < ORIONC_MAX_IDS) {
          snprintf(refs->v[refs->n].name, ORIONC_MAX_IDENT, "%s", nz(command, menu));
          snprintf(refs->v[refs->n].id, ORIONC_MAX_IDENT, "%s", id);
          refs->n++;
        }
      } else add_id(ids, id);
      char *alt = attr(it, "alt-command");
      if (alt && resolve_action(id, sizeof(id), alt, NULL, tb_scope, NULL, NULL) && refs && refs->n < ORIONC_MAX_IDS) {
        snprintf(refs->v[refs->n].name, ORIONC_MAX_IDENT, "%s", alt);
        snprintf(refs->v[refs->n].id, ORIONC_MAX_IDENT, "%s", id);
        refs->n++;
      }
      free(alt); free(command); free(menu); free(name); free(text);
    }
  }
}

static void collect_context_ids(ids_t *ids, cmd_refs_t *refs, xmlNodePtr contexts) {
  EACH_ELEMENT(menu, contexts) if (elem(menu, "contextmenu")) {
    char *scope = attr(menu, "name");
    EACH_ELEMENT(it, menu) if (elem(it, "item")) {
      char *command = attr(it, "command"), *menu_attr = attr(it, "menu"), *name = attr(it, "name"), *label = attr(it, "label"), id[256];
      if (resolve_action(id, sizeof(id), command, menu_attr, scope, name, label)) {
        if (refs && refs->n < ORIONC_MAX_IDS) {
          snprintf(refs->v[refs->n].name, ORIONC_MAX_IDENT, "%s", nz(command, menu_attr));
          snprintf(refs->v[refs->n].id, ORIONC_MAX_IDENT, "%s", id);
          refs->n++;
        }
      } else add_id(ids, id);
      free(command); free(menu_attr); free(name); free(label);
    }
    free(scope);
  }
}

static void collect_control_ids(ids_t *ids, xmlNodePtr parent, const char *form) {
  int ordinal = 0;
  EACH_ELEMENT(c, parent) if (is_control(parent, c)) {
    char *name = attr(c, "name"), id[256];
    control_id(id, sizeof(id), form, name, (char *)c->name, ordinal++);
    add_id(ids, id);
    collect_control_ids(ids, c, form);
    free(name);
  }
}

static void collect_activitybar_ids(ids_t *ids, cmd_refs_t *refs, xmlNodePtr parent, const char *scope) {
  EACH_ELEMENT(c, parent) if (is_control(parent, c)) {
    if (elem(c, "activitybar")) {
      collect_toolbar_ids(ids, refs, child(c, "toolbar"), scope);
      EACH_ELEMENT(page, c) if (elem(page, "page")) {
        char *command = attr(page, "command"), *name = attr(page, "form"), id[256];
        if (resolve_action(id, sizeof(id), command, NULL, scope, name, name)) {
          if (refs && refs->n < ORIONC_MAX_IDS) {
            snprintf(refs->v[refs->n].name, ORIONC_MAX_IDENT, "%s", command);
            snprintf(refs->v[refs->n].id, ORIONC_MAX_IDENT, "%s", id);
            refs->n++;
          }
        } else add_id(ids, id);
        free(command); free(name);
      }
    }
    collect_activitybar_ids(ids, refs, c, scope);
  }
}

static int count_menu_items(xmlNodePtr menu) {
  int n = 0; EACH_ELEMENT(it, menu) if (elem(it, "item") || elem(it, "separator") || elem(it, "submenu")) n++; return n;
}

static void emit_if(FILE *f, xmlNodePtr n, bool end) {
  char *e = attr(n, "if");
  if (e && *e) OUT(end ? "#endif\n" : "#if %s\n", e);
  free(e);
}

static void menu_base(char *out, size_t cap, const char *parent, xmlNodePtr n) {
  char *name = attr(n, "name"), id[128]; ident(id, sizeof(id), name, true);
  snprintf(out, cap, "%s_%s", nz(parent, "MENU"), id);
  free(name);
}

static void emit_menu_items(FILE *f, xmlNodePtr menu, const char *base, const char *scope) {
  EACH_ELEMENT(it, menu) if (elem(it, "submenu")) { char b[256]; menu_base(b, sizeof(b), base, it); emit_menu_items(f, it, b, scope); }
  if (!count_menu_items(menu)) return;
  OUT("static const menu_item_t %s_ITEMS[] = {\n", base);
  EACH_ELEMENT(it, menu) {
    emit_if(f, it, false);
    if (elem(it, "separator")) LINE("  { NULL, 0, NULL, 0 },\n");
    else if (elem(it, "submenu")) {
      char b[256], label[ORIONC_STRING_SIZE], *raw = attr(it, "label");
      menu_base(b, sizeof(b), base, it); cstr(label, sizeof(label), raw);
      if (count_menu_items(it))
        OUT("  { %s, 0, %s_ITEMS, (int)(sizeof(%s_ITEMS) / sizeof(%s_ITEMS[0])) },\n", label, b, b, b);
      else
        OUT("  { %s, 0, NULL, 0 },\n", label);
      free(raw);
    } else if (elem(it, "item")) {
      char *name = attr(it, "name"), *raw = attr(it, "label"), *hotkey = menu_shortcut(it);
      char id[256], label[ORIONC_STRING_SIZE]; scoped(id, sizeof(id), scope, name, raw); cstr_shortcut(label, sizeof(label), raw, hotkey);
      OUT("  { %s, %s, NULL, 0 },\n", label, id);
      free(name); free(raw); free(hotkey);
    }
    emit_if(f, it, true);
  }
  LINE("};\n\n");
}

static void emit_menus(FILE *f, xmlNodePtr menus) {
  if (!menus) return;
  EACH_ELEMENT(m, menus) if (elem(m, "menu")) { char *name = attr(m, "name"), scope[128], base[256]; ident(scope, sizeof(scope), name, true); snprintf(base, sizeof(base), "MENU_%s", scope); emit_menu_items(f, m, base, scope); free(name); }
  char *var = attrs_first(menus, "var", NULL), *count = attrs_first(menus, "count", NULL);
  OUT("static menu_def_t %s[] = {\n", nz(var, "kMenus"));
  EACH_ELEMENT(m, menus) if (elem(m, "menu")) {
    char *name = attr(m, "name"), *raw = attr(m, "label"), label[ORIONC_STRING_SIZE], scope[128], base[256];
    ident(scope, sizeof(scope), name, true); snprintf(base, sizeof(base), "MENU_%s", scope); cstr(label, sizeof(label), raw);
    OUT("  { %s, %s_ITEMS, (int)(sizeof(%s_ITEMS) / sizeof(%s_ITEMS[0])) },\n", label, base, base, base);
    free(name); free(raw);
  }
  OUT("};\n#define %s ((int)(sizeof(%s) / sizeof(%s[0])))\n\n", nz(count, "kNumMenus"), nz(var, "kMenus"), nz(var, "kMenus"));
  free(var); free(count);
}

static void emit_context_menus(FILE *f, xmlNodePtr contexts) {
  EACH_ELEMENT(menu, contexts) if (elem(menu, "contextmenu")) {
    char *name = attr(menu, "name"), scope[128], base[160]; ident(scope, sizeof(scope), name, true); snprintf(base, sizeof(base), "CONTEXT_MENU_%s", scope);
    OUT("static const menu_item_t %s_ITEMS[] = {\n", base);
    EACH_ELEMENT(it, menu) {
      if (elem(it, "separator")) LINE("  { NULL, 0, NULL, 0 },\n");
      else if (elem(it, "item")) {
        char *command = attr(it, "command"), *menu_attr = attr(it, "menu"), *item_name = attr(it, "name"), *raw = attr(it, "label"), id[256], label[ORIONC_STRING_SIZE];
        resolve_action(id, sizeof(id), command, menu_attr, name, item_name, raw); cstr(label, sizeof(label), raw);
        OUT("  { %s, %s, NULL, 0 },\n", label, id);
        free(command); free(menu_attr); free(item_name); free(raw);
      }
    }
    OUT("};\n#define %s_COUNT ((int)(sizeof(%s_ITEMS) / sizeof(%s_ITEMS[0])))\n\n", base, base, base);
    free(name);
  }
}

static const char *control_size_flag(xmlNodePtr n);

static void emit_toolbar(FILE *f, xmlNodePtr toolbar, const char *symbol,
                         const char *scope_name) {
  if (toolbar) {
    char scope[128]; ident(scope, sizeof(scope), scope_name, true);
    OUT("static const toolbar_item_t %s[] = {\n", symbol);
    EACH_ELEMENT(it, toolbar) if (toolbar_type(it)) {
      char *command = attr(it, "command"), *menu = attr(it, "menu"), *name = attr(it, "name"), *icon = attr(it, "icon"), *w = attr(it, "w"), *flags = attr(it, "flags"), *text = attr(it, "text"), *tooltip = attr(it, "tooltip"), *checked = attr(it, "checked-icon");
      char id[256] = "0", textq[ORIONC_STRING_SIZE], tipq[ORIONC_STRING_SIZE], iconq[256], checkedq[260] = "";
      if (!elem(it, "separator") && !elem(it, "spacer") && !elem(it, "title") && !elem(it, "sidebar-separator"))
        resolve_action(id, sizeof(id), command, menu, scope, name, text);
      if (text && *text) cstr(textq, sizeof(textq), text); else snprintf(textq, sizeof(textq), "NULL");
      if (tooltip && *tooltip) cstr(tipq, sizeof(tipq), tooltip); else snprintf(tipq, sizeof(tipq), "NULL");
      if (icon && *icon) snprintf(iconq, sizeof(iconq), "\"%s\"", icon); else snprintf(iconq, sizeof(iconq), "NULL");
      if (checked && *checked) snprintf(checkedq, sizeof(checkedq), ", 0, \"%s\"", checked); // color, checked_icon
      emit_if(f, it, false);
      OUT("  { %s, %s, %s, %s, %s%s%s, %s, %s%s", toolbar_type(it), id, iconq, nz(w, "0"), nz(flags, "0"),
          elem(toolbar->parent, "activitybar") && elem(it, "button") ? " | TBSTYLE_CHECKGROUP" : "",
          elem(it, "button") ? control_size_flag(it) : "", textq, tipq, checkedq);
      char *alt = attr(it, "alt-command"), *alt_icon = attr(it, "alt-icon"), *alt_tip = attr(it, "alt-tooltip");
      if (alt) {
        char alt_id[256], aq[ORIONC_STRING_SIZE], tq[ORIONC_STRING_SIZE];
        resolve_action(alt_id, sizeof(alt_id), alt, NULL, scope, NULL, NULL);
        cstr(aq, sizeof(aq), nz(alt_icon, icon)); cstr(tq, sizeof(tq), nz(alt_tip, tooltip));
        OUT(", .alt_ident = %s, .alt_icon = %s, .alt_tooltip = %s", alt_id, aq, tq);
      }
      free(alt); free(alt_icon); free(alt_tip);
      LINE(" },\n");
      emit_if(f, it, true);
      free(command); free(menu); free(name); free(icon); free(w); free(flags); free(text); free(tooltip); free(checked);
    }
    OUT("};\n\n");
  }
}

static void emit_activitybars(FILE *f, xmlNodePtr parent, const char *form) {
  EACH_ELEMENT(c, parent) if (is_control(parent, c)) {
    if (elem(c, "activitybar")) {
      char *name = attr(c, "name"), symbol[256];
      snprintf(symbol, sizeof(symbol), "%s_%s_activity_items", form, nz(name, "unnamed"));
      xmlNodePtr toolbar = child(c, "toolbar");
      char *labels = attr(c, "labels");
      const char *style = eq(labels, "true") || eq(labels, "1") || eq(labels, "below") ? "TOOLBAR_STYLE_SHOW_LABELS" :
                          eq(labels, "right") ? "TOOLBAR_STYLE_LIST" : "0";
      if (labels && eq(style, "0")) fprintf(stderr, "orionc: warning: ActivityBar '%s': unknown labels '%s' (want below or right)\n", nz(name, ""), labels);
      if (toolbar) {
        emit_toolbar(f, toolbar, symbol, form);
        OUT("static const activitybar_params_t %s_%s_activity_params = { %s, ARRAY_LEN(%s), %s };\n\n", form, nz(name, "unnamed"), symbol, symbol, style);
      } else if (labels) {
        OUT("static const activitybar_params_t %s_%s_activity_params = { NULL, 0, %s };\n\n", form, nz(name, "unnamed"), style);
      }
      free(labels);
      free(name);
    }
    emit_activitybars(f, c, form);
  }
}

// Validate the action manifest: reject unknown references, duplicate hotkeys,
// duplicate action names, and malformed hotkey strings. Returns the number of
// errors found.
static int validate_actions(const ids_t *ids, const cmd_refs_t *refs,
                            const action_meta_list_t *meta) {
  int errors = 0;
  for (int i = 0; i < refs->n; i++) {
    bool found = false;
    for (int j = 0; j < ids->n; j++) if (eq(ids->v[j].name, refs->v[i].id)) { found = true; break; }
    if (!found) {
      fprintf(stderr, "orionc: unknown command reference '%s' (resolves to %s)\n",
              refs->v[i].name, refs->v[i].id);
      errors++;
    }
  }
  for (int i = 0; i < meta->n; i++) {
    const action_meta_t *m = &meta->v[i];
    for (int j = i + 1; j < meta->n; j++)
      if (eq(m->name, meta->v[j].name)) {
        fprintf(stderr, "orionc: duplicate action name '%s'\n", m->name);
        errors++;
      }
    if (!m->hotkey[0]) continue;
    char token[64]; const char *cursor = m->hotkey;
    while (next_hotkey(&cursor, token, sizeof(token))) {
      char fvirt[64], key[64];
      if (!parse_hotkey(token, fvirt, sizeof(fvirt), key, sizeof(key))) {
        fprintf(stderr, "orionc: malformed hotkey '%s' on action '%s'\n", token, m->name);
        errors++;
        continue;
      }
      for (int j = i; j < meta->n; j++) {
        char other[64]; const char *other_cursor = meta->v[j].hotkey;
        while (next_hotkey(&other_cursor, other, sizeof(other))) {
          if (j == i && eq(token, other)) continue;
          if (eq(token, other)) {
            fprintf(stderr, "orionc: duplicate hotkey '%s' on actions '%s' and '%s'\n",
                    token, m->name, meta->v[j].name);
            errors++;
          }
        }
      }
    }
  }
  return errors;
}

static void emit_action_meta(FILE *f, const char *prefix, const action_meta_list_t *meta) {
  if (!meta || !meta->n) return;
  OUT("typedef struct {\n  uint16_t id;\n  const char *name;\n  const char *label;\n  const char *category;\n  const char *hotkey;\n} %s_action_meta_t;\n\n", prefix);
  OUT("static const %s_action_meta_t %s_action_meta[] = {\n", prefix, prefix);
  for (int i = 0; i < meta->n; i++) {
    char label[ORIONC_STRING_SIZE], name[ORIONC_STRING_SIZE], category[ORIONC_STRING_SIZE], hotkey[ORIONC_STRING_SIZE];
    cstr(label, sizeof(label), meta->v[i].label);
    cstr(name, sizeof(name), meta->v[i].name);
    cstr(category, sizeof(category), meta->v[i].category);
    cstr(hotkey, sizeof(hotkey), meta->v[i].hotkey);
    OUT("  { %s, %s, %s, %s, %s },\n", meta->v[i].id, name, label, category, hotkey);
  }
  OUT("};\n#define %s_action_meta_count ((int)(sizeof(%s_action_meta) / sizeof(%s_action_meta[0])))\n\n", prefix, prefix, prefix);
}

static void emit_accelerators(FILE *f, const char *prefix, const action_meta_list_t *meta) {
  if (!meta) return;
  int n = 0;
  for (int i = 0; i < meta->n; i++) n += hotkey_count(meta->v[i].hotkey);
  if (!n) return;
  OUT("static const accel_t %s_default_accels[] = {\n", prefix);
  for (int i = 0; i < meta->n; i++) {
    char token[64]; const char *cursor = meta->v[i].hotkey;
    while (next_hotkey(&cursor, token, sizeof(token))) {
      char fvirt[64], key[64];
      if (!parse_hotkey(token, fvirt, sizeof(fvirt), key, sizeof(key))) continue;
      OUT("  { %s, %s, %s },\n", fvirt, key, meta->v[i].id);
    }
  }
  OUT("};\n#define %s_default_accel_count ((int)(sizeof(%s_default_accels) / sizeof(%s_default_accels[0])))\n\n", prefix, prefix, prefix);
}

static const field_type_t *field_type(const char *type) { for (int i = 0; i < ARRAY_LEN(kFieldTypes); i++) if (eq(type, kFieldTypes[i].xml)) return &kFieldTypes[i]; return &kFieldTypes[0]; }
static void singular_type(char *out, size_t cap, const char *table, const char *model) {
  if (model && *model) { snprintf(out, cap, "%s", model); return; }
  char id[96]; ident(id, sizeof(id), table, false); size_t n = strlen(id);
  static const char *es[] = { "xes", "ches", "shes", "sses", "zes" };  // taxes -> tax
  bool sibilant = false;
  for (size_t i = 0; i < sizeof(es) / sizeof(es[0]); i++) {
    size_t k = strlen(es[i]);
    if (n > k && !strcmp(id + n - k, es[i])) sibilant = true;
  }
  if (n > 3 && !strcmp(id + n - 3, "ies")) strcpy(id + n - 3, "y");
  else if (sibilant) id[n - 2] = 0;
  else if (n > 1 && id[n - 1] == 's') id[n - 1] = 0;
  snprintf(out, cap, "db_%s_t", id);
}

// Finds <table name=...> in a <database>, or across every database of a
// <databases> block (table names are unique application-wide).
static xmlNodePtr find_table_node(xmlNodePtr scope, const char *name) {
  if (!scope || !name) return NULL;
  EACH_ELEMENT(n, scope) {
    if (elem(n, "database")) { xmlNodePtr t = find_table_node(n, name); if (t) return t; }
    if (!elem(n, "table")) continue;
    char *candidate = attr(n, "name"); bool match = eq(candidate, name); free(candidate);
    if (match) return n;
  }
  return NULL;
}

static xmlNodePtr find_field_node(xmlNodePtr scope, const char *table, const char *field) {
  EACH_ELEMENT(n, find_table_node(scope, table)) if (elem(n, "field")) {
    char *candidate = attr(n, "name"); bool match = eq(candidate, field); free(candidate);
    if (match) return n;
  }
  return NULL;
}

// Emits the generated schema for one <database>: per-table field schema,
// joins derived from relation= fields, record sizes and reflection metadata.
// Relations may name another database: relation="cfg.accounts.id".
static void emit_database_schema(FILE *f, xmlNodePtr db, const char *prefix) {
  char *db_name = attr(db, "name"), *klass = attr(db, "class"), *source = attr(db, "source");
  char db_id[128]; ident(db_id, sizeof(db_id), nz(db_name, "db"), false);
  int tables = 0;
  EACH_ELEMENT(t, db) if (elem(t, "table")) {
    char *table = attr(t, "name"), meta[128]; ident(meta, sizeof(meta), nz(table, "table"), false);
    int joins = 0;
    OUT("static const db_field_schema_t %s_%s_schema_fields[] = {\n", db_id, meta);
    EACH_ELEMENT(field, t) if (elem(field, "field")) {
      char *name = attr(field, "name"), *kind = attr(field, "type"), *len = attr(field, "length"), *key = attr(field, "key"), *rel = attr(field, "relation");
      char *def = attr(field, "default"), *on_delete = attrs_first(field, "on-delete", "on_delete"), defq[ORIONC_STRING_SIZE];
      const field_type_t *ft = field_type(kind); char *dot = rel ? strrchr(rel, '.') : NULL;
      bool pk = key && (eq(key, "YES") || eq(key, "yes") || eq(key, "true") || eq(key, "1"));
      cstr(defq, sizeof(defq), def);
      // EOF delete rules: what deleting the referenced record does to this one.
      const char *rule = !on_delete ? "DB_DELETE_NO_ACTION" : eq(on_delete, "cascade") ? "DB_DELETE_CASCADE"
                       : eq(on_delete, "nullify") ? "DB_DELETE_NULLIFY" : eq(on_delete, "deny") ? "DB_DELETE_DENY" : NULL;
      if (!rule) { fprintf(stderr, "orionc: field '%s': unknown on-delete '%s' (cascade, nullify, deny)\n", nz(name, ""), on_delete); rule = "DB_DELETE_NO_ACTION"; }
      if (dot) { *dot = 0; OUT("  { \"%s\", %s, %d, %s, \"%s\", \"%s\", %s, %s },\n", name, ft->db, eq(ft->c, "char") ? (len ? atoi(len) : ft->size) : 0, pk ? "true" : "false", rel, dot + 1, defq, rule); joins++; }
      else OUT("  { \"%s\", %s, %d, %s, NULL, NULL, %s, %s },\n", name, ft->db, eq(ft->c, "char") ? (len ? atoi(len) : ft->size) : 0, pk ? "true" : "false", defq, rule);
      free(name); free(kind); free(len); free(key); free(rel); free(def); free(on_delete);
    }
    LINE("};\n");
    if (joins) {
      OUT("static const db_join_schema_t %s_%s_joins[] = {\n", db_id, meta);
      EACH_ELEMENT(field, t) if (elem(field, "field")) {
        char *name = attr(field, "name"), *rel = attr(field, "relation"), *join = attr(field, "join"), *dot = rel ? strrchr(rel, '.') : NULL;
        if (dot) {
          char alias[128]; snprintf(alias, sizeof(alias), "%s", join && *join ? join : name);
          size_t n = strlen(alias); if (!(join && *join) && n > 3 && !strcmp(alias + n - 3, "_id")) alias[n - 3] = 0;
          *dot = 0; OUT("  { \"%s\", \"%s\", \"%s\", \"%s\" },\n", alias, name, rel, dot + 1);
        }
        free(name); free(rel); free(join);
      }
      LINE("};\n");
    }
    tables++; free(table);
  }
  if (!tables) { free(db_name); free(klass); free(source); return; }
  OUT("static const db_table_schema_t %s_%s_tables[] = {\n", prefix, db_id);
  EACH_ELEMENT(t, db) if (elem(t, "table")) {
    char *table = attr(t, "name"), *model = attr(t, "model"), type[128], meta[128], up[128];
    singular_type(type, sizeof(type), table, model); ident(meta, sizeof(meta), nz(model, table), false); ident(up, sizeof(up), table, true);
    char lower[128], joins[192] = "NULL, 0"; ident(lower, sizeof(lower), table, false);
    int join_count = 0;
    EACH_ELEMENT(field, t) if (elem(field, "field")) { char *rel = attr(field, "relation"); if (rel && strchr(rel, '.')) join_count++; free(rel); }
    if (join_count) snprintf(joins, sizeof(joins), "%s_%s_joins, %d", db_id, lower, join_count);
    char *transient = attr(t, "transient");
    OUT("  { TABLE_%s, \"%s\", %s%s%s, %s_%s_schema_fields, ARRAY_LEN(%s_%s_schema_fields), %s, sizeof(%s), %s_fields, ARRAY_LEN(%s_fields), %s },\n",
        up, table, model ? "\"" : "", model ? model : "NULL", model ? "\"" : "", db_id, lower, db_id, lower, joins, type, meta, meta,
        transient && (eq(transient, "true") || eq(transient, "1")) ? "true" : "false");
    free(transient);
    free(table); free(model);
  }
  LINE("};\n");
  char klassq[ORIONC_STRING_SIZE], sourceq[ORIONC_STRING_SIZE]; cstr(klassq, sizeof(klassq), klass); cstr(sourceq, sizeof(sourceq), source);
  OUT("static const db_schema_def_t %s_%s_schema = { \"%s\", %s, %s, %s_%s_tables, ARRAY_LEN(%s_%s_tables) };\n\n",
      prefix, db_id, nz(db_name, "db"), klassq, sourceq, prefix, db_id, prefix, db_id);
  free(db_name); free(klass); free(source);
}

// Emits one <database> block. `primary` selects the original, unqualified
// symbol names (TABLE_COUNT, <prefix>_database_api) for backward
// compatibility with existing generated code and hand-written aliases
// (e.g. gc_database_api). Only the first <database> in the file is primary;
// any additional <database> elements still get per-table TABLE_<NAME>
// macros (table names must be unique across all databases in the app, same
// as before), but their TABLE_COUNT and *_database_api symbols are
// qualified with the database's own name so they cannot collide with the
// primary database's.
static void emit_database(FILE *f, xmlNodePtr db, const char *prefix, bool primary) {
  if (!db) return;
  char *db_name = attr(db, "name");
  char db_up[128]; ident(db_up, sizeof(db_up), nz(db_name, "db"), true);
  int table_i = 0;
  EACH_ELEMENT(t, db) if (elem(t, "table")) {
    char *table = attr(t, "name"), *model = attr(t, "model"), type[128], meta[128];
    if (!table || !*table) { free(table); free(model); continue; }
    singular_type(type, sizeof(type), table, model); ident(meta, sizeof(meta), nz(model, table), false);
    OUT("typedef struct {\n");
    EACH_ELEMENT(field, t) if (elem(field, "field")) {
      char *name = attr(field, "name"), *kind = attr(field, "type"), *len = attr(field, "length"); const field_type_t *ft = field_type(kind);
      if (eq(ft->c, "char")) OUT("  char %s[%d];\n", name, len ? atoi(len) : ft->size); else OUT("  %s %s;\n", ft->c, name);
      free(name); free(kind); free(len);
    }
    OUT("} %s;\n\nstatic const db_field_meta_t %s_fields[] = {\n", type, meta);
    EACH_ELEMENT(field, t) if (elem(field, "field")) {
      char *name = attr(field, "name"), *kind = attr(field, "type"), *len = attr(field, "length"), *precision = attr(field, "precision"); const field_type_t *ft = field_type(kind);
      OUT("  { \"%s\", %s, offsetof(%s, %s), %d, %d },\n", name, ft->db, type, name, eq(ft->c, "char") ? (len ? atoi(len) : ft->size) : 0, precision ? atoi(precision) : 0);
      free(name); free(kind); free(len); free(precision);
    }
    OUT("};\n");
    char up[128]; ident(up, sizeof(up), table, true); OUT("#define TABLE_%s %d\n", up, table_i++);
    free(table); free(model);
  }
  if (table_i) {
    if (primary) OUT("#define TABLE_COUNT %d\n\n", table_i);
    else OUT("#define %s_TABLE_COUNT %d\n\n", db_up, table_i);
  }
  if (primary) OUT("static const db_api_def_t %s_database_api = { NULL, 0, NULL, 0, NULL, 0 };\n\n", prefix);
  else OUT("static const db_api_def_t %s_%s_database_api = { NULL, 0, NULL, 0, NULL, 0 };\n\n", prefix, db_up);
  emit_database_schema(f, db, prefix);
  free(db_name);
}

static void table_name_from_source(char *out, size_t cap, const char *source) {
  const char *dot = source ? strchr(source, '.') : NULL; snprintf(out, cap, "%s", dot ? dot + 1 : nz(source, "table"));
  char *next = strchr(out, '.'); if (next) *next = 0;
}

// count_table_fields — count number of fields in a database table
static int count_table_fields(xmlNodePtr db, const char *table_name) {
  int count = 0;
  EACH_ELEMENT(field, find_table_node(db, table_name)) if (elem(field, "field")) count++;
  return count;
}

static xmlNodePtr find_tableview_for_table(xmlNodePtr parent, const char *table) {
  EACH_ELEMENT(c, parent) {
    if (elem(c, "tableview")) {
      char *source = attrs_first(c, "source", "database");
      char found[128]; table_name_from_source(found, sizeof(found), source);
      free(source);
      if (eq(found, table)) return c;
    }
    xmlNodePtr nested = find_tableview_for_table(c, table);
    if (nested) return nested;
  }
  return NULL;
}

static xmlNodePtr find_tableview_by_name(xmlNodePtr parent, const char *name) {
  EACH_ELEMENT(c, parent) {
    if (elem(c, "tableview")) {
      char *found = attr(c, "name"); bool match = eq(found, name); free(found);
      if (match) return c;
    }
    xmlNodePtr nested = find_tableview_by_name(c, name);
    if (nested) return nested;
  }
  return NULL;
}

static bool table_relation(xmlNodePtr db, const char *table, int *field_index,
                           char *foreign_table, size_t table_cap,
                           char *foreign_field, size_t field_cap) {
  int index = 0;
  EACH_ELEMENT(field, find_table_node(db, table)) if (elem(field, "field")) {
    char *relation = attr(field, "relation"), *dot = relation ? strrchr(relation, '.') : NULL;
    if (dot) {
      *dot = 0;
      snprintf(foreign_table, table_cap, "%s", relation);
      snprintf(foreign_field, field_cap, "%s", dot + 1);
      *field_index = index;
      free(relation);
      return true;
    }
    free(relation); index++;
  }
  return false;
}

static void emit_tableviews(FILE *f, xmlNodePtr parent, xmlNodePtr form_node,
                            xmlNodePtr database, const char *form, const char *prefix) {
  EACH_ELEMENT(c, parent) {
    if (elem(c, "tableview")) {
      char *name = attr(c, "name"), *source = attrs_first(c, "source", "database");
      char param[256], table[128], table_id[128]; snprintf(param, sizeof(param), "%s_%s_tableview_params", form, nz(name, "unnamed"));
      char *cell_style = attr(c, "cell-style");
      const char *cell_style_value = cell_style && eq(cell_style, "two-line")
                                   ? "REPORTVIEW_CELL_TWO_LINE" : "REPORTVIEW_CELL_COLUMNS";
      table_name_from_source(table, sizeof(table), source); ident(table_id, sizeof(table_id), table, true);
      OUT("static const char *%s_fields[] = { ", param); EACH_ELEMENT(col, c) if (elem(col, "column")) { char *v = attr(col, "field"), q[ORIONC_STRING_SIZE]; cstr(q, sizeof(q), v); OUT("%s, ", q); free(v); } LINE("NULL };\n");
      OUT("static const char *%s_titles[] = { ", param); EACH_ELEMENT(col, c) if (elem(col, "column")) { char *v = attr(col, "title"), q[ORIONC_STRING_SIZE]; cstr(q, sizeof(q), v); OUT("%s, ", q); free(v); } LINE("NULL };\n");
      OUT("static const int %s_widths[] = { ", param); EACH_ELEMENT(col, c) if (elem(col, "column")) { char *v = attr(col, "width"); OUT("%d, ", v ? atoi(v) : 0); free(v); } LINE("-1 };\n");
      OUT("static const int %s_min_widths[] = { ", param); EACH_ELEMENT(col, c) if (elem(col, "column")) { char *v = attr(col, "min-width"); OUT("%d, ", v ? atoi(v) : 0); free(v); } LINE("-1 };\n");
      int relation_field = 0; char foreign_table[128] = {0}, foreign_field[128] = {0};
      xmlNodePtr master = NULL;
      char *master_name_attr = attr(c, "master");
      if (table_relation(database, table, &relation_field, foreign_table,
                         sizeof(foreign_table), foreign_field, sizeof(foreign_field))) {
        if (master_name_attr && *master_name_attr && !eq(master_name_attr, "none"))
          master = find_tableview_by_name(form_node, master_name_attr);
        else if (!master_name_attr || !*master_name_attr)
          master = find_tableview_for_table(form_node, foreign_table);
      }
      char master_id[256] = "0";
      if (master) {
        char *master_name = attr(master, "name");
        control_id(master_id, sizeof(master_id), form, master_name,
                   (char *)master->name, 0);
        free(master_name);
      }
      char master_key_q[ORIONC_STRING_SIZE];
      if (master && foreign_field[0]) cstr(master_key_q, sizeof(master_key_q), foreign_field);
      else snprintf(master_key_q, sizeof(master_key_q), "NULL");
      char *check_field = attrs_first(c, "check-field", "check_field");
      char *column_titles = attr(c, "column-titles");
      char check_field_q[ORIONC_STRING_SIZE];
      if (check_field && *check_field) cstr(check_field_q, sizeof(check_field_q), check_field);
      else snprintf(check_field_q, sizeof(check_field_q), "NULL");
      // A static filter: filter-field="book" filter-value="1" shows only the
      // rows whose integer/boolean field equals the value.
      char *filter_name = attrs_first(c, "filter-field", "filter_field"), *filter_value = attrs_first(c, "filter-value", "filter_value");
      int filter_index = 0;
      if (filter_name) {
        int index = 0;
        EACH_ELEMENT(field, find_table_node(database, table)) if (elem(field, "field")) {
          char *n = attr(field, "name"); bool match = eq(n, filter_name); free(n);
          if (match) { filter_index = index; break; }
          index++;
        }
        if (!filter_index) fprintf(stderr, "orionc: TableView '%s': unknown or key filter-field '%s'\n", nz(name, ""), filter_name);
      }
      // LIMITATION: master_filter_field / filter_value are int-based,
      // so only integer FK values work.  String FKs (UUID, hash) would
      // be converted to 0 by the runtime's strtol() call.
      // form="doc_line": the database form that edits one row (EODisplayGroup).
      char *record_form = attr(c, "form"), record_form_ref[256] = "NULL", *zeros = attr(c, "zeros");
      if (record_form && *record_form) { char rf[128]; ident(rf, sizeof(rf), record_form, false); snprintf(record_form_ref, sizeof(record_form_ref), "&%s_%s_form", prefix, rf); }
      // <RowIcon field= value= icon= color=>: status icons, first match wins.
      int row_icons = 0;
      EACH_ELEMENT(ri, c) if (elem(ri, "rowicon")) {
        char *field = attr(ri, "field"), *value = attr(ri, "value"), *icon = attr(ri, "icon"), *color = attr(ri, "color");
        char fq[ORIONC_STRING_SIZE], vq[ORIONC_STRING_SIZE], iq[ORIONC_STRING_SIZE];
        static const char *roles[][2] = { {"success", "brTextSuccess"}, {"error", "brTextError"}, {"warning", "brTextWarning"},
                                          {"info", "brTextInfo"}, {"secondary", "brTextSecondary"}, {"disabled", "brTextDisabled"},
                                          {"accent", "brAccent"} };
        const char *role = "-1";
        for (int i = 0; color && i < ARRAY_LEN(roles); i++) if (eq(color, roles[i][0])) role = roles[i][1];
        if (color && eq(role, "-1")) fprintf(stderr, "orionc: warning: TableView '%s': unknown RowIcon color '%s'\n", nz(name, ""), color);
        if (!icon) fprintf(stderr, "orionc: warning: TableView '%s': RowIcon requires icon=\n", nz(name, ""));
        if (field) cstr(fq, sizeof(fq), field); else snprintf(fq, sizeof(fq), "NULL");
        if (value) cstr(vq, sizeof(vq), value); else snprintf(vq, sizeof(vq), "NULL");
        cstr(iq, sizeof(iq), nz(icon, "missing"));
        if (!row_icons++) OUT("static const tableview_row_icon_t %s_row_icons[] = {\n", param);
        OUT("  { %s, %s, %s, %s },\n", fq, vq, iq, role);
        free(field); free(value); free(icon); free(color);
      }
      if (row_icons) LINE("};\n");
      // total="sum" on a Column: a totals row under the rows.
      bool totals = false;
      EACH_ELEMENT(col, c) if (elem(col, "column")) { char *v = attr(col, "total"); totals |= eq(v, "sum"); if (v && !eq(v, "sum")) fprintf(stderr, "orionc: warning: TableView '%s': unknown total '%s' (want sum)\n", nz(name, ""), v); free(v); }
      if (totals) { OUT("static const bool %s_totals[] = { ", param); EACH_ELEMENT(col, c) if (elem(col, "column")) { char *v = attr(col, "total"); OUT("%s, ", eq(v, "sum") ? "true" : "false"); free(v); } LINE("};\n"); }
      char *totals_label = attr(c, "totals-label"), totals_label_q[ORIONC_STRING_SIZE] = "NULL";
      char *totals_skip = attr(c, "totals-skip"), totals_skip_q[ORIONC_STRING_SIZE] = "NULL", *frame = attr(c, "frame");
      char *group_field = attr(c, "group-field"), group_field_q[ORIONC_STRING_SIZE] = "NULL", *fit_rows = attr(c, "fit-rows");
      if (group_field && *group_field) cstr(group_field_q, sizeof(group_field_q), group_field);
      if (totals_label) cstr(totals_label_q, sizeof(totals_label_q), totals_label);
      if (totals_skip) cstr(totals_skip_q, sizeof(totals_skip_q), totals_skip);
      char row_icons_ref[300] = "NULL", totals_ref[300] = "NULL";
      if (row_icons) snprintf(row_icons_ref, sizeof(row_icons_ref), "%s_row_icons", param);
      if (totals) snprintf(totals_ref, sizeof(totals_ref), "%s_totals", param);
      OUT("static const tableview_params_t %s = { .db = NULL, .table_id = TABLE_%s, .filter_field = %d, .filter_value = %d, .field_names = %s_fields, .column_titles = %s_titles, .column_widths = %s_widths, .column_min_widths = %s_min_widths, .check_field = %s, .master_id = %s, .master_filter_field = %d, .master_key = %s, .cell_style = %s, .hide_column_titles = %s, .record_form = %s, .blank_zeros = %s, .row_icons = %s, .row_icon_count = %d, .column_totals = %s, .totals_label = %s, .totals_skip = %s, .framed = %s, .group_field = %s, .fit_rows = %s };\n\n",
          param, table_id, filter_index, filter_value ? atoi(filter_value) : 0, param, param, param, param, check_field_q, master_id,
          relation_field, master_key_q, cell_style_value, column_titles && eq(column_titles, "false") ? "true" : "false", record_form_ref, eq(zeros, "blank") ? "true" : "false",
          row_icons_ref, row_icons, totals_ref, totals_label_q, totals_skip_q, eq(frame, "true") ? "true" : "false",
          group_field_q, eq(fit_rows, "true") ? "true" : "false");
      free(record_form); free(zeros); free(totals_label); free(totals_skip); free(frame); free(group_field); free(fit_rows);
      free(column_titles);
      free(cell_style);
      free(check_field);
      free(filter_name); free(filter_value);
      free(master_name_attr);
      free(name); free(source);
    }
    emit_tableviews(f, c, form_node, database, form, prefix);
  }
}

static void emit_comboboxes(FILE *f, xmlNodePtr parent, const char *form) {
  EACH_ELEMENT(c, parent) {
    if (elem(c, "combobox")) {
      char *name = attr(c, "name"), *source = attr(c, "source");
      char *display = attr(c, "display"), *value = attr(c, "value");
      
      // Only generate params if source/display/value are all present
      if (source && display && value) {
        char param[256], table[128], table_id[128];
        char display_q[ORIONC_STRING_SIZE], value_q[ORIONC_STRING_SIZE];
        char filter_field_q[ORIONC_STRING_SIZE], filter_value_q[ORIONC_STRING_SIZE];
        char *filter_field = attrs_first(c, "filter-field", "filter_field");
        char *filter_value_attr = attrs_first(c, "filter-value", "filter_value");

        snprintf(param, sizeof(param), "%s_%s_combobox_params", form, nz(name, "unnamed"));
        table_name_from_source(table, sizeof(table), source);
        ident(table_id, sizeof(table_id), table, true);
        cstr(display_q, sizeof(display_q), display);
        cstr(value_q, sizeof(value_q), value);
        cstr(filter_field_q, sizeof(filter_field_q), filter_field);
        cstr(filter_value_q, sizeof(filter_value_q), filter_value_attr);

        OUT("static const combobox_params_t %s = { NULL, TABLE_%s, %s, %s, %s, %s };\n\n",
            param, table_id, display_q, value_q, filter_field_q, filter_value_q);
        free(filter_field); free(filter_value_attr);
      }

      free(name); free(source); free(display); free(value);
    }
    emit_comboboxes(f, c, form);
  }
}

static const char *binding_getter(const char *klass) {
  if (!strcasecmp(klass, "textedit") || !strcasecmp(klass, "textbox") || !strcasecmp(klass, "multiedit")) return "edGetText";
  if (!strcasecmp(klass, "combobox")) return "cbGetCurrentValue";  // Use value_field (ID) not row index
  if (!strcasecmp(klass, "checkbox")) return "chkIsChecked";
  return "0";
}

static const kv_t kWindowFlags[] = {
  {"notitle", "WINDOW_NOTITLE"}, {"nofill", "WINDOW_NOFILL"},
  {"vscroll", "WINDOW_VSCROLL"}, {"flexspace", "WINDOW_FLEXSPACE"},
  {"toolbar", "WINDOW_TOOLBAR"}, {"titletoolbar", "WINDOW_TITLETOOLBAR"}, {"statusbar", "WINDOW_STATUSBAR"},
  {"notitlebar", "WINDOW_NOTITLE"}, {"default", "0"},
};

static const char *control_size_flag(xmlNodePtr n) {
  char *raw = attrs_first(n, "control-size", "control_size");
  if (!raw || !*raw || eq(raw, "regular")) { free(raw); return ""; }
  const char *flag = eq(raw, "mini") ? " | CONTROL_SIZE_MINI" :
                     eq(raw, "small") ? " | CONTROL_SIZE_SMALL" :
                     eq(raw, "large") ? " | CONTROL_SIZE_LARGE" : NULL;
  if (!flag)
    fprintf(stderr, "orionc: invalid control-size '%s' on <%s>; using regular\n",
            raw, n && n->name ? (const char *)n->name : "control");
  free(raw);
  return flag ? flag : "";
}

static void resolve_flags(char *out, size_t cap, const char *raw) {
  if (!raw || !*raw) { snprintf(out, cap, "0"); return; }
  char buf[256]; snprintf(buf, sizeof(buf), "%s", raw);
  out[0] = 0;
  for (char *tok = strtok(buf, ",|+ "); tok; tok = strtok(NULL, ",|+ ")) {
    bool found = false;
    for (int i = 0; i < ARRAY_LEN(kWindowFlags); i++) {
      if (eq(tok, kWindowFlags[i].key)) {
        if (out[0]) strcat(out, " | ");
        strcat(out, kWindowFlags[i].value);
        found = true; break;
      }
    }
    if (!found) {
      if (out[0]) strcat(out, " | ");
      strcat(out, tok);
    }
  }
  if (!out[0]) snprintf(out, cap, "0");
}

static void binding_record_type(char *out, size_t cap, const char *table) {
  singular_type(out, cap, table, NULL);
}

static void add_binding(bindings_t *b, const char *ctrl, const char *path, const char *klass) {
  if (!b || b->n >= ARRAY_LEN(b->v)) return;
  binding_t next = {0};
  if (!db_path(path, next.db, sizeof(next.db), next.table, sizeof(next.table), next.field, sizeof(next.field))) return;
  snprintf(next.ctrl, sizeof(next.ctrl), "%s", ctrl);
  snprintf(next.klass, sizeof(next.klass), "%s", klass);
  if (!b->n) { snprintf(b->db, sizeof(b->db), "%s", next.db); snprintf(b->table, sizeof(b->table), "%s", next.table); }
  b->v[b->n++] = next;
}

static void emit_controls_ex(FILE *f, xmlNodePtr parent, const char *form, const char *parent_id, bindings_t *bindings, int *count, button_ids_t *btn_ids) {
  int ordinal = 0;
  EACH_ELEMENT(c, parent) if (is_control(parent, c)) {
    attrs_t a; rect_t sz = size_attr(c), pad = {0}, mar = {0}; char id[256], klass[128], classq[ORIONC_STRING_SIZE], textq[ORIONC_STRING_SIZE], nameq[ORIONC_STRING_SIZE], flags[256], spacing[16], font[16], color[16], lparam[256] = "NULL";
    read_control_attrs(c, &a); control_id(id, sizeof(id), form, a.v[A_NAME], (char *)c->name, ordinal++); ident(klass, sizeof(klass), (char *)c->name, false);
    if (has_controls(c) && !elem(c, "column")) sz = (rect_t){0};
    rect_attr(c, "padding", &pad) || rect_attr(c, "layout_padding", &pad); rect_attr(c, "margin", &mar) || rect_attr(c, "layout_margin", &mar);
    // Auto-add WINDOW_FLEXSPACE for space, and for multiedit without an explicit height (WPF-style)
    char resolved[256]; resolve_flags(resolved, sizeof(resolved), nz(a.v[A_FLAGS], "0"));
    const char *auto_flex = (elem(c, "space") || (elem(c, "multiedit") && sz.h <= 0)) ? " | WINDOW_FLEXSPACE" : "";
    snprintf(flags, sizeof(flags), "(%s)%s%s%s", resolved, enum_parse_token(a.v[A_ORIENT], kOrient, ARRAY_LEN(kOrient), WINDOW_STACK_VERTICAL) & WINDOW_STACK_HORIZONTAL ? " | WINDOW_STACK_HORIZONTAL" : "", auto_flex, control_size_flag(c));
    snprintf(spacing, sizeof(spacing), "%u", byte_attr(a.v[A_SPACING], ORIONC_DEFAULT_SPACING));
    snprintf(font, sizeof(font), "%s", eq(a.v[A_FONT], "system") ? "FONT_SYSTEM" : eq(a.v[A_FONT], "smallest") ? "FONT_SMALLEST" : eq(a.v[A_FONT], "title") ? "FONT_TITLE" : "FONT_SMALL");
    snprintf(color, sizeof(color), "%u", (unsigned)enum_parse_token(a.v[A_COLOR], kColors, ARRAY_LEN(kColors), brTextNormal));
    if (elem(c, "tableview")) snprintf(lparam, sizeof(lparam), "&%s_%s_tableview_params", form, nz(a.v[A_NAME], "unnamed"));
    if (elem(c, "activitybar") && (child(c, "toolbar") || xmlHasProp(c, BAD_CAST "labels"))) snprintf(lparam, sizeof(lparam), "&%s_%s_activity_params", form, nz(a.v[A_NAME], "unnamed"));
    if (elem(c, "combobox") && attr(c, "source")) snprintf(lparam, sizeof(lparam), "&%s_%s_combobox_params", form, nz(a.v[A_NAME], "unnamed"));
    if (elem(c, "tabview")) {
      // tab-style="sidebar": the tabs as a list down the left edge, for many pages.
      char *hide = attr(c, "hide-single-tab"), *style = attr(c, "tab-style");
      bool single = eq(hide, "true") || eq(hide, "1"), sidebar = eq(style, "sidebar");
      if (style && !sidebar) fprintf(stderr, "orionc: TabView '%s': unknown tab-style '%s' (sidebar)\n", nz(a.v[A_NAME], ""), style);
      if (single || sidebar)
        snprintf(lparam, sizeof(lparam), "&(const tabview_params_t){ .style = %s%s%s }", single ? "TAB_STYLE_HIDE_SINGLE_TAB" : "",
                 single && sidebar ? " | " : "", sidebar ? "TAB_STYLE_SIDEBAR" : "");
      free(hide); free(style);
    }
    if (elem(c, "SplitView")) {
      const char *orientation = eq(a.v[A_ORIENT], "vertical") ? "SPLIT_HORZ" : "SPLIT_VERT";
      char *raw_ratio = attrs_first(c, "split-ratio", "split_ratio");
      if (raw_ratio) {
        char *end;
        double ratio = strtod(raw_ratio, &end);
        if (end == raw_ratio || *end || !(ratio >= 0.0 && ratio <= 1.0)) {
          fprintf(stderr, "orionc: invalid split-ratio '%s' on SplitView '%s'; using 0.5\n", raw_ratio, nz(a.v[A_NAME], "unnamed"));
          ratio = 0.5;
        }
        snprintf(lparam, sizeof(lparam), "&(const splitview_params_t){ %s, %.17g }", orientation, ratio);
      } else snprintf(lparam, sizeof(lparam), "(void *)%s", orientation);
      free(raw_ratio);
    }
    if (a.v[A_FIELD]) add_binding(bindings, id, a.v[A_FIELD], klass);
    
    // Track button IDs for ok_id/cancel_id form metadata
    if (elem(c, "button") && btn_ids) {
      char *action = attr(c, "action");
      if (action && (strstr(action, ".insert") || strstr(action, ".update"))) {
        snprintf(btn_ids->ok_id, sizeof(btn_ids->ok_id), "%s", id);
      }
      free(action);
      
      // Detect cancel button by text or name
      if ((a.v[A_TEXT] && strcasecmp(a.v[A_TEXT], "Cancel") == 0) ||
          (a.v[A_NAME] && strcasecmp(a.v[A_NAME], "cancel") == 0)) {
        snprintf(btn_ids->cancel_id, sizeof(btn_ids->cancel_id), "%s", id);
      }
    }
    
    char *context = attrs_first(c, "context-menu", "context_menu"), *source = attr(c, "source"), context_items[192] = "NULL", context_count[192] = "0", sourceq[ORIONC_STRING_SIZE] = "NULL";
    if (context && *context) { char context_id[128]; ident(context_id, sizeof(context_id), context, true); snprintf(context_items, sizeof(context_items), "CONTEXT_MENU_%s_ITEMS", context_id); snprintf(context_count, sizeof(context_count), "CONTEXT_MENU_%s_COUNT", context_id); }
    if (source && *source) cstr(sourceq, sizeof(sourceq), source);
    char *placeholder = attr(c, "placeholder"), placeholderq[ORIONC_STRING_SIZE] = "NULL";
    if (placeholder) cstr(placeholderq, sizeof(placeholderq), placeholder);
    char *icon = attr(c, "icon"), *tooltip = attr(c, "tooltip"), iconq[ORIONC_STRING_SIZE] = "NULL", tooltipq[ORIONC_STRING_SIZE] = "NULL";
    if (icon && *icon) cstr(iconq, sizeof(iconq), icon);
    if (tooltip && *tooltip) cstr(tooltipq, sizeof(tooltipq), tooltip);
    cstr(classq, sizeof(classq), klass); cstr(textq, sizeof(textq), a.v[A_TEXT]); cstr(nameq, sizeof(nameq), a.v[A_NAME]);
    // <Button target="lines" action="insert"> sends the TableView a record action.
    char *target = attr(c, "target"), *taction = attr(c, "action"), target_id[256] = "0";
    const char *target_msg = "0";
    char *list = elem(c, "textbox") || elem(c, "textedit") ? attr(c, "list") : NULL;
    if (list && *list) control_id(target_id, sizeof(target_id), form, list, "control", 0);   // the list the field drives
    free(list);
    if (target && *target) {
      control_id(target_id, sizeof(target_id), form, target, "control", 0);
      target_msg = eq(taction, "insert") ? "tvInsertRecord" : eq(taction, "edit") ? "tvEditRecord" : eq(taction, "delete") ? "tvDeleteRecord" : NULL;
      if (!target_msg) { fprintf(stderr, "orionc: control '%s': unknown action '%s' for target '%s' (insert, edit, delete)\n", nz(a.v[A_NAME], ""), nz(taction, ""), target); target_msg = "0"; snprintf(target_id, sizeof(target_id), "0"); }
    }
    OUT("  { %s, %s, { %d, %d }, %s, %s, %s, %u, %u, NULL, 0, %s, { %d, %d, %d, %d }, { %d, %d, %d, %d }, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s },\n",
        classq, id, sz.w, sz.h, flags, textq, nameq,
        (unsigned)enum_parse_token(a.v[A_HA], kAlignH, ARRAY_LEN(kAlignH), 0),
        (unsigned)enum_parse_token(a.v[A_VA], kAlignV, ARRAY_LEN(kAlignV), 0),
        spacing, pad.x, pad.y, pad.w, pad.h, mar.x, mar.y, mar.w, mar.h,
        nz(parent_id, "0"), font, a.v[A_FONT] ? "true" : "false", color, a.v[A_COLOR] ? "true" : "false", lparam, context_items, context_count, sourceq, placeholderq,
        target_id, target_msg, iconq, tooltipq);
    free(target); free(taction); free(icon); free(tooltip);
    (*count)++;
    emit_controls_ex(f, c, form, id, bindings, count, btn_ids);
    free(context); free(source); free(placeholder); free_attrs(&a);
  }
}

// Text controls bound to numeric fields convert through typed DDX callbacks;
// ComboBoxes exchange their value field (0 = no selection). A Label shows its
// field formatted and never writes it back.
static void emit_bindings(FILE *f, const char *prefix, const char *form, const bindings_t *b, xmlNodePtr databases) {
  if (!b || !b->n) return;
  char type[128]; binding_record_type(type, sizeof(type), b->table);
  OUT("static const ctrl_binding_t %s_%s_bindings[] = {\n", prefix, form);
  for (int i = 0; i < b->n; i++) {
    const binding_t *x = &b->v[i];
    const char *getter = binding_getter(x->klass);
    char *kind = attr(find_field_node(databases, x->table, x->field), "type");
    const field_type_t *ft = field_type(kind);
    if (!strcasecmp(x->klass, "label")) {
      const char *push = eq(ft->db, "DB_TYPE_DATE") ? "date" : eq(ft->c, "int") ? "int" : eq(ft->c, "double") ? "double" :
                         eq(ft->c, "float") ? "float" : eq(ft->db, "DB_TYPE_STRING") ? "label_text" : NULL;
      if (!push) fprintf(stderr, "orionc: Label '%s' cannot show field '%s.%s' of this type\n", x->ctrl, x->table, x->field);
      if (push) OUT("  { %s, 0, 0, offsetof(%s, %s), 0, ddx_push_%s, NULL },\n", x->ctrl, type, x->field, push);
      else OUT("  { %s, 0, 0, offsetof(%s, %s), 0, NULL, NULL },\n", x->ctrl, type, x->field);
      free(kind);
      continue;
    }
    const char *ddx = eq(getter, "edGetText") && eq(ft->db, "DB_TYPE_DATE") ? "date" :
                      eq(getter, "edGetText") && eq(ft->c, "int") ? "int" :
                      eq(getter, "edGetText") && eq(ft->c, "double") ? "double" :
                      eq(getter, "edGetText") && eq(ft->c, "float") ? "float" :
                      eq(getter, "cbGetCurrentValue") ? "combo_value" :
                      eq(getter, "chkIsChecked") ? "check" : NULL;
    if (eq(getter, "edGetText") && !ddx) getter = "edGetText";
    OUT("  { %s, 0, %s, offsetof(%s, %s), ", x->ctrl, ddx ? "0" : getter, type, x->field);
    if (eq(getter, "edGetText")) OUT("sizeof_field(%s, %s)", type, x->field);
    else LINE("0");
    if (ddx) OUT(", ddx_push_%s, ddx_pull_%s },\n", ddx, ddx);
    else LINE(", NULL, NULL },\n");
    free(kind);
  }
  LINE("};\n\n");
}

static xmlNodePtr activity_find_form(xmlNodePtr forms, const char *name) {
  EACH_ELEMENT(form, forms) if (elem(form, "form")) {
    char *candidate = attr(form, "name");
    bool match = candidate && name && eq(candidate, name);
    free(candidate);
    if (match) return form;
  }
  return NULL;
}

static bool activity_control_id(xmlNodePtr parent, const char *form, const char *name, char *out, size_t cap) {
  int ordinal = 0;
  EACH_ELEMENT(c, parent) if (is_control(parent, c)) {
    char *candidate = attr(c, "name");
    bool match = candidate && eq(candidate, name);
    if (match) control_id(out, cap, form, candidate, (char *)c->name, ordinal);
    free(candidate);
    if (match || activity_control_id(c, form, name, out, cap)) return true;
    ordinal++;
  }
  return false;
}

static bool activity_form_needed(xmlNodePtr form, xmlNodePtr forms, const char *only) {
  if (!only) return true;
  char *name = attr(form, "name");
  bool needed = eq(nz(name, ""), only);
  xmlNodePtr host = activity_find_form(forms, only);
  EACH_ELEMENT(bar, host) if (elem(bar, "activitybar")) {
    xmlNodePtr toolbar = child(bar, "toolbar");
    EACH_ELEMENT(button, toolbar ? toolbar : bar) {
      char *page = attr(button, toolbar ? "page" : "form");
      needed |= page && name && eq(page, name);
      free(page);
    }
  }
  free(name);
  return needed;
}

static bool activity_binding_locations(xmlNodePtr form, xmlNodePtr parent) {
  EACH_ELEMENT(node, parent) {
    if (elem(node, "activitybar") && node->parent != form) {
      xmlNodePtr toolbar = child(node, "toolbar");
      EACH_ELEMENT(button, toolbar ? toolbar : node) if (elem(button, "page") || xmlHasProp(button, BAD_CAST "page")) {
        fprintf(stderr, "orionc: bound ActivityBar must be a direct child of its host form\n");
        return false;
      }
    }
    if (!activity_binding_locations(form, node)) return false;
  }
  return true;
}

static int emit_page_bindings(FILE *f, xmlNodePtr form, const char *prefix, const char *form_id, char *initial, size_t initial_cap) {
  if (!activity_binding_locations(form, form)) return -1;
  char *role = attr(form, "role"), *active = attr(form, "active-page");
  char commands[64][256], names[64][128];
  int count = 0;
  xmlNodePtr bound_bar = NULL;
  const char *error = NULL;
  snprintf(initial, initial_cap, "0");
  EACH_ELEMENT(bar, form) if (elem(bar, "activitybar")) {
    xmlNodePtr toolbar = child(bar, "toolbar");
    if (toolbar && child(bar, "page")) {
      fprintf(stderr, "orionc: ActivityBar cannot mix Page entries and Toolbar buttons\n");
      free(role); free(active); return -1;
    }
    EACH_ELEMENT(button, toolbar ? toolbar : bar) {
      char *page = attr(button, toolbar ? "page" : "form");
      if (!page) {
        if (!toolbar && elem(button, "page")) {
          fprintf(stderr, "orionc: activity Page requires form=\n");
          free(role); free(active); return -1;
        }
        continue;
      }
      char *focus = attr(button, "focus"), *bar_name = attr(bar, "name");
      char *command = attr(button, "command"), *menu = attr(button, "menu"), *name = attr(button, "name"), *text = attr(button, "text");
      char id[256], bar_id[256] = "0", focus_id[256] = "0", page_id[128], pageq[256];
      xmlNodePtr target = activity_find_form(form->parent, page);
      char *target_role = attr(target, "role");
      if (!toolbar) { free(focus); focus = attr(target, "focus"); free(name); name = strdup(page); }
      resolve_action(id, sizeof(id), command, menu, form_id, name, text);
      ident(page_id, sizeof(page_id), page, false); cstr(pageq, sizeof(pageq), page);
      if (!role || !eq(role, "host")) error = "page bindings require role=host";
      else if (!(toolbar ? elem(button, "button") : elem(button, "page"))) error = "invalid activity page entry";
      else if (!target) error = "unknown activity page";
      else if (!target_role || !eq(target_role, "page")) error = "activity target requires role=page";
      else if (!bar_name || !activity_control_id(form, form_id, bar_name, bar_id, sizeof(bar_id))) error = "activity bar requires a name";
      else if (bound_bar && bound_bar != bar) error = "host requires one bound activity bar";
      else if (xmlHasProp(button, BAD_CAST "if")) error = "conditional activity page bindings are unsupported";
      else if (count >= ARRAY_LEN(commands)) error = "too many activity pages";
      else if (focus && !activity_control_id(target, page_id, focus, focus_id, sizeof(focus_id))) error = "unknown activity focus control";
      for (int i = 0; i < count && !error; i++)
        if (eq(commands[i], id) || eq(names[i], page)) error = "duplicate activity command or page";
      if (!error) {
        if (!count) OUT("static const page_binding_t %s_%s_page_bindings[] = {\n", prefix, form_id);
        OUT("  { %s, %s, %s, &%s_%s_form, %s },\n", id, bar_id, pageq, prefix, page_id, focus_id);
        snprintf(commands[count], sizeof(commands[count]), "%s", id);
        snprintf(names[count], sizeof(names[count]), "%s", page);
        count++; bound_bar = bar;
        if (!active && count == 1) snprintf(initial, initial_cap, "%s", id);
        if (active && eq(active, page)) snprintf(initial, initial_cap, "%s", id);
      }
      if (error) fprintf(stderr, "orionc: form '%s', page '%s': %s\n", form_id, page, error);
      free(page); free(focus); free(bar_name); free(command); free(menu); free(name); free(text); free(target_role);
      if (error) { free(role); free(active); return -1; }
    }
  }
  if (count) LINE("};\n\n");
  xmlNodePtr toolbar = child(bound_bar, "toolbar");
  EACH_ELEMENT(button, toolbar) if (elem(button, "button") && !xmlHasProp(button, BAD_CAST "page")) {
    fprintf(stderr, "orionc: form '%s': every button in a bound ActivityBar requires page=\n", form_id);
    free(role); free(active); return -1;
  }
  if (active && eq(initial, "0")) {
    fprintf(stderr, "orionc: form '%s': unknown active-page '%s'\n", form_id, active);
    free(role); free(active); return -1;
  }
  free(role); free(active);
  return count;
}

static bool emit_form(FILE *f, xmlNodePtr form, const char *prefix, xmlNodePtr database) {
  char *name = attr(form, "name"), *title = attr(form, "title"), *flags_raw = attr(form, "flags"), *role = attr(form, "role"), *spacing = attrs_first(form, "spacing", "layout_spacing");
  rect_t sz = size_attr(form), pad = {0}, mar = {0}; char form_id[128], titleq[ORIONC_STRING_SIZE], flags[256];
  if (!sz.w) { fprintf(stderr, "orionc_alt: form '%s' requires width=\n", nz(name, "")); return false; }
  const char *role_name = !role || !*role || eq(role, "normal") ? "WINDOW_ROLE_NORMAL" :
                          eq(role, "host") ? "WINDOW_ROLE_HOST" :
                          eq(role, "page") ? "WINDOW_ROLE_PAGE" : NULL;
  if (!role_name) {
    fprintf(stderr, "orionc: invalid role '%s' on form '%s'\n", role, nz(name, ""));
    free(name); free(title); free(flags_raw); free(role); free(spacing);
    return false;
  }
  ident(form_id, sizeof(form_id), name, false); cstr(titleq, sizeof(titleq), nz(title, name));
  xmlNodePtr toolbar = child(form, "toolbar"), pane_toolbar = child(form, "panetoolbar");
  int pane_toolbar_count = 0;
  EACH_ELEMENT(item, form) if (elem(item, "panetoolbar")) pane_toolbar_count++;
  if (pane_toolbar_count > 1 || (pane_toolbar && !eq(role, "page"))) {
    fprintf(stderr, "orionc: form '%s' allows one <PaneToolbar> and requires role=page\n", nz(name, ""));
    free(name); free(title); free(flags_raw); free(role); free(spacing); return false;
  }
  char pane_symbol[256]; snprintf(pane_symbol, sizeof(pane_symbol), "%s_%s_pane_toolbar", prefix, form_id);
  char toolbar_symbol[256]; snprintf(toolbar_symbol, sizeof(toolbar_symbol), "%s_%s_toolbar", prefix, form_id);
  int toolbar_count = 0;
  EACH_ELEMENT(item, form) if (elem(item, "toolbar")) toolbar_count++;
  if (toolbar_count > 1) {
    fprintf(stderr, "orionc: form '%s' has multiple <Toolbar> declarations\n", nz(name, ""));
    free(name); free(title); free(flags_raw); free(role); free(spacing);
    return false;
  }
  rect_attr(form, "padding", &pad) || rect_attr(form, "layout_padding", &pad); rect_attr(form, "margin", &mar) || rect_attr(form, "layout_margin", &mar);
  resolve_flags(flags, sizeof(flags), nz(flags_raw, "0"));
  emit_tableviews(f, form, form, database, form_id, prefix);
  emit_comboboxes(f, form, form_id);
  emit_toolbar(f, toolbar, toolbar_symbol, name);
  emit_toolbar(f, pane_toolbar, pane_symbol, name);
  emit_activitybars(f, form, form_id);
  OUT("static const form_ctrl_def_t %s_%s_children[] = {\n", prefix, form_id);
  int count = 0; bindings_t bindings = {0}; button_ids_t btn_ids = {0}; 
  emit_controls_ex(f, form, form_id, "0", &bindings, &count, &btn_ids); LINE("};\n\n");
  emit_bindings(f, prefix, form_id, &bindings, database);
  char initial[256];
  int activity_count = emit_page_bindings(f, form, prefix, form_id, initial, sizeof(initial));
  if (activity_count < 0) { free(name); free(title); free(flags_raw); free(role); free(spacing); return false; }
  OUT("static const form_def_t %s_%s_form = { .name = %s, .id = \"%s\", .width = %d, .height = %d, .flags = (%s)%s | WINDOW_AUTO_LAYOUT, .role = %s, .layout_spacing = %u, .padding = { %d, %d, %d, %d }, .margin = { %d, %d, %d, %d }, .children = %s_%s_children, .child_count = %d",
      prefix, form_id, titleq, form_id, sz.w, sz.h, flags,
      toolbar && !eq(role, "page") ? " | WINDOW_TOOLBAR" : "", role_name,
      byte_attr(spacing, ORIONC_DEFAULT_SPACING),
      pad.x, pad.y, pad.w, pad.h, mar.x, mar.y, mar.w, mar.h, prefix, form_id, count);
  if (toolbar) OUT(", .toolbar_items = %s, .toolbar_count = ARRAY_LEN(%s)", toolbar_symbol, toolbar_symbol);
  else LINE(", .toolbar_items = NULL, .toolbar_count = 0");
  if (pane_toolbar) OUT(", .pane_toolbar_items = %s, .pane_toolbar_count = ARRAY_LEN(%s)", pane_symbol, pane_symbol);
  char *pane_replace = pane_toolbar ? attr(pane_toolbar, "replace") : NULL;
  if (eq(pane_replace, "true") || eq(pane_replace, "1")) OUT(", .pane_toolbar_replace = true");
  free(pane_replace);
  if (activity_count) OUT(", .page_bindings = %s_%s_page_bindings, .page_binding_count = %d, .initial_page = %s", prefix, form_id, activity_count, initial);
  char *toolbar_style = attr(form, "toolbar-style");
  if (eq(toolbar_style, "labels")) LINE(", .toolbar_style = TOOLBAR_STYLE_SHOW_LABELS");
  else if (eq(toolbar_style, "list")) LINE(", .toolbar_style = TOOLBAR_STYLE_LIST");
  else if (toolbar_style) fprintf(stderr, "orionc: warning: form '%s': unknown toolbar-style '%s' (want labels or list)\n", nz(name, ""), toolbar_style);
  free(toolbar_style);
  const char *page_attrs[] = {"icon", "checked-icon", "tooltip", "label"};
  const char *page_fields[] = {"icon", "checked_icon", "tooltip", "label"};
  for (int i = 0; i < ARRAY_LEN(page_attrs); i++) {
    char *value = attr(form, page_attrs[i]), quoted[ORIONC_STRING_SIZE];
    if (value) { cstr(quoted, sizeof(quoted), value); OUT(", .%s = %s", page_fields[i], quoted); }
    free(value);
  }
  if (bindings.n) {
    char table_id[128], meta[128]; 
    ident(table_id, sizeof(table_id), bindings.table, true);
    ident(meta, sizeof(meta), bindings.table, false);
    int field_count = count_table_fields(database, bindings.table);
    char record_type[128]; binding_record_type(record_type, sizeof(record_type), bindings.table);
    OUT(", .bindings = %s_%s_bindings, .binding_count = %d, .ok_id = %s, .cancel_id = %s, .db_name = \"%s\", .db_table = \"%s\", .db_table_id = TABLE_%s, .db_fields = %s_fields, .db_field_count = %d, .db_record_size = sizeof(%s)",
        prefix, form_id, bindings.n, 
        btn_ids.ok_id[0] ? btn_ids.ok_id : "0",
        btn_ids.cancel_id[0] ? btn_ids.cancel_id : "0",
        bindings.db, bindings.table, table_id, meta, field_count, record_type);
  } else {
    OUT(", .bindings = NULL, .binding_count = 0, .ok_id = %s, .cancel_id = %s, .db_name = NULL, .db_table = NULL, .db_table_id = 0, .db_fields = NULL, .db_field_count = 0",
        btn_ids.ok_id[0] ? btn_ids.ok_id : "0",
        btn_ids.cancel_id[0] ? btn_ids.cancel_id : "0");
  }
  LINE(" };\n\n");
  free(name); free(title); free(flags_raw); free(role); free(spacing);
  return true;
}

static void usage(const char *argv0) { fprintf(stderr, "usage: %s --input file.orion --output forms.h --prefix name [--form id]\n"
                                                     "       %s --input file.orion --strings catalog.xml [--language ru]\n", argv0, argv0); }

// ── String catalog template (like lupdate) ──────────────────────────────────
// Lists every user-visible text once, noting where it appears. Existing
// translations in the output file are kept; sources no longer used are kept
// with obsolete="true" so no translator work is lost. Entries marked
// where="code" (strings the app passes to ui_tr) and <context> sections are
// hand-written and carried over unchanged.

typedef struct { char *source, *where, *text; bool used; } catalog_entry_t;
typedef struct { catalog_entry_t *v; int n, cap; } catalog_t;

static catalog_entry_t *catalog_find(catalog_t *c, const char *source) {
  for (int i = 0; i < c->n; i++) if (eq(c->v[i].source, source)) return &c->v[i];
  return NULL;
}

static void catalog_add(catalog_t *c, const char *source, const char *where) {
  if (!source || !*source) return;
  char label[ORIONC_STRING_SIZE]; snprintf(label, sizeof(label), "%s", source);
  char *tab = strchr(label, '\t'); if (tab) *tab = 0;  // shortcuts are not translated
  catalog_entry_t *e = catalog_find(c, label);
  if (!e) {
    if (c->n == c->cap) { c->cap = c->cap ? c->cap * 2 : 128; c->v = realloc(c->v, (size_t)c->cap * sizeof(*c->v)); }
    e = &c->v[c->n++]; *e = (catalog_entry_t){ strdup(label), strdup(""), NULL, false };
  }
  e->used = true;
  if (where && !strstr(e->where, where)) {
    size_t n = strlen(e->where) + strlen(where) + 2; char *w = malloc(n);
    snprintf(w, n, "%s%s%s", e->where, e->where[0] ? "," : "", where); free(e->where); e->where = w;
  }
}

static void catalog_collect(catalog_t *c, xmlNodePtr n, const char *where) {
  static const char *attrs[] = { "text", "title", "label", "tooltip", "placeholder", "totals-label" };
  EACH_ELEMENT(child, n) {
    const char *scope = where;
    char *name = attr(child, "name");
    if (elem(child, "form")) scope = name;
    if (elem(child, "menu") || elem(child, "contextmenu")) scope = "menu";
    for (size_t i = 0; i < ARRAY_LEN(attrs) && !elem(child, "database"); i++) {
      char *v = attr(child, attrs[i]);
      catalog_add(c, v, scope); free(v);
    }
    catalog_collect(c, child, scope);
    free(name);
  }
}

static int emit_strings(xmlNodePtr root, const char *path, const char *language) {
  catalog_t c = {0};
  FILE *existing = fopen(path, "rb");
  if (existing) fclose(existing);
  xmlDocPtr old = existing ? xmlReadFile(path, NULL, XML_PARSE_NONET) : NULL;
  xmlNodePtr old_root = old ? xmlDocGetRootElement(old) : NULL;
  EACH_ELEMENT(s, old_root) if (elem(s, "string")) {
    char *source = attr(s, "source"), *where = attr(s, "where"); xmlChar *text = xmlNodeGetContent(s);
    if (source && !catalog_find(&c, source)) {
      // where="code" marks strings the app translates with ui_tr(); they stay current.
      bool code = eq(where, "code");
      if (c.n == c.cap) { c.cap = c.cap ? c.cap * 2 : 128; c.v = realloc(c.v, (size_t)c.cap * sizeof(*c.v)); }
      c.v[c.n++] = (catalog_entry_t){ strdup(source), strdup(code ? "code" : ""), strdup(text ? (char *)text : ""), code };
    }
    free(source); free(where); if (text) xmlFree(text);
  }
  catalog_collect(&c, root, NULL);
  xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
  xmlNodePtr out = xmlNewNode(NULL, BAD_CAST "strings");
  xmlDocSetRootElement(doc, out);
  char *lang = old_root ? attr(old_root, "language") : NULL;
  xmlNewProp(out, BAD_CAST "language", BAD_CAST (lang ? lang : nz(language, "xx")));
  static const char *keep[] = { "decimal", "group", "date" };
  for (size_t i = 0; old_root && i < ARRAY_LEN(keep); i++) { char *v = attr(old_root, keep[i]); if (v) xmlNewProp(out, BAD_CAST keep[i], BAD_CAST v); free(v); }
  free(lang);
  int fresh = 0;
  for (int i = 0; i < c.n; i++) {
    xmlNodePtr s = xmlNewTextChild(out, NULL, BAD_CAST "string", BAD_CAST (c.v[i].text ? c.v[i].text : ""));
    xmlNewProp(s, BAD_CAST "source", BAD_CAST c.v[i].source);
    if (c.v[i].where[0]) xmlNewProp(s, BAD_CAST "where", BAD_CAST c.v[i].where);
    if (!c.v[i].used) xmlNewProp(s, BAD_CAST "obsolete", BAD_CAST "true");
    if (!c.v[i].text || !c.v[i].text[0]) fresh++;
    free(c.v[i].source); free(c.v[i].where); free(c.v[i].text);
  }
  EACH_ELEMENT(ctx, old_root) if (elem(ctx, "context")) xmlAddChild(out, xmlCopyNode(ctx, 1));
  free(c.v);
  int ok = xmlSaveFormatFileEnc(path, doc, "UTF-8", 1) >= 0;
  xmlFreeDoc(doc); if (old) xmlFreeDoc(old);
  if (!ok) { perror(path); return 1; }
  fprintf(stderr, "orionc: %s: %d strings, %d untranslated\n", path, c.n, fresh);
  return 0;
}

int main(int argc, char **argv) {
  const char *input = NULL, *output = NULL, *prefix = "orion", *only = NULL, *strings = NULL, *language = NULL;
  for (int i = 1; i < argc; i++) {
    if (eq(argv[i], "--input") && i + 1 < argc) input = argv[++i];
    else if (eq(argv[i], "--output") && i + 1 < argc) output = argv[++i];
    else if (eq(argv[i], "--prefix") && i + 1 < argc) prefix = argv[++i];
    else if (eq(argv[i], "--form") && i + 1 < argc) only = argv[++i];
    else if (eq(argv[i], "--strings") && i + 1 < argc) strings = argv[++i];
    else if (eq(argv[i], "--language") && i + 1 < argc) language = argv[++i];
    else { usage(argv[0]); return 2; }
  }
  if (!input || (!output && !strings)) { usage(argv[0]); return 2; }
  xmlDocPtr doc = xmlReadFile(input, NULL, XML_PARSE_NONET);
  xmlNodePtr root = doc ? xmlDocGetRootElement(doc) : NULL;
  if (!elem(root, "orion")) { fprintf(stderr, "orionc_alt: %s is not an <orion> document\n", input); if (doc) xmlFreeDoc(doc); return 1; }
  if (strings) { int rc = emit_strings(root, strings, language); xmlFreeDoc(doc); return rc; }
  FILE *f = fopen(output, "wb"); if (!f) { perror(output); xmlFreeDoc(doc); return 1; }
  char guard[256], pre[128]; ident(guard, sizeof(guard), output, true); ident(pre, sizeof(pre), prefix, false);
  xmlNodePtr menus = child(root, "menus"), contexts = child(root, "contextmenus"), forms = child(root, "forms"), databases = child(root, "databases"), database = databases ? child(databases, "database") : child(root, "database");
  if (child(root, "toolbars")) {
    fprintf(stderr, "orionc: top-level <toolbars> is invalid; use one root <toolbar> for the application or nest <Toolbar> inside its owning <form>\n");
    fclose(f); xmlFreeDoc(doc); return 1;
  }
  EACH_ELEMENT(form, forms) if (elem(form, "form")) {
    char *toolbar_ref = attr(form, "toolbar");
    if (toolbar_ref && *toolbar_ref) {
      char *form_name = attr(form, "name");
      fprintf(stderr, "orionc: toolbar= is invalid on form '%s'; nest <Toolbar> inside the form\n", nz(form_name, ""));
      free(form_name); free(toolbar_ref); fclose(f); xmlFreeDoc(doc); return 1;
    }
    free(toolbar_ref);
  }
  xmlNodePtr app_toolbar = child(root, "toolbar");
  int app_toolbar_count = 0;
  EACH_ELEMENT(node, root) if (elem(node, "toolbar")) app_toolbar_count++;
  char *presentation = attr(app_toolbar, "presentation");
  if (app_toolbar_count > 1 || (presentation && !eq(presentation, "normal") && !eq(presentation, "compact"))) {
    fprintf(stderr, "orionc: declare at most one application <toolbar> with presentation=\"normal\" or \"compact\"\n");
    free(presentation); fclose(f); xmlFreeDoc(doc); return 1;
  }
  ids_t commands = {0}, controls = {0}; cmd_refs_t refs = {0}; action_meta_list_t meta = {0};
  EACH_ELEMENT(m, menus) if (elem(m, "menu")) { char *name = attr(m, "name"), scope[128]; ident(scope, sizeof(scope), name, true); collect_menu_ids(&commands, &meta, m, scope, name); free(name); }
  EACH_ELEMENT(form, forms) if (elem(form, "form")) { char *name = attr(form, "name"); collect_toolbar_ids(&commands, &refs, child(form, "toolbar"), name); collect_toolbar_ids(&commands, &refs, child(form, "panetoolbar"), name); collect_activitybar_ids(&commands, &refs, form, name); free(name); }
  collect_toolbar_ids(&commands, &refs, app_toolbar, "application");
  collect_context_ids(&commands, &refs, contexts);
  EACH_ELEMENT(form, forms) if (elem(form, "form")) { char *name = attr(form, "name"), form_id[128]; if (activity_form_needed(form, forms, only)) { ident(form_id, sizeof(form_id), name, false); collect_control_ids(&controls, form, form_id); } free(name); }
  int errors = validate_actions(&commands, &refs, &meta);
  tpl(f, "/* Generated by orionc_alt from {{input}}. */\n#ifndef {{guard}}\n#define {{guard}}\n\n#include <orion/ui.h>\n\n",
      (kv_t[]){{"input", input}, {"guard", guard}, {NULL, NULL}});
  emit_defines(f, &commands, "ID_COMMAND_BASE"); emit_defines(f, &controls, "ID_CONTROL_BASE");
  emit_action_meta(f, pre, &meta); emit_accelerators(f, pre, &meta);
  emit_menus(f, menus); emit_context_menus(f, contexts);
  if (app_toolbar) {
    LINE("#include <orion/user/toolbar.h>\n");
    char symbol[256]; snprintf(symbol, sizeof(symbol), "%s_application_toolbar_items", pre);
    emit_toolbar(f, app_toolbar, symbol, "application");
    OUT("static const application_toolbar_t %s_application_toolbar = { .items = %s, .count = ARRAY_LEN(%s), .presentation = %s };\n\n",
        pre, symbol, symbol, eq(presentation, "compact") ? "TOOLBAR_PRESENTATION_COMPACT" : "TOOLBAR_PRESENTATION_NORMAL");
  }
  free(presentation);
  // Emit every <database> under <databases> (a lone top-level <database> with
  // no wrapper is also supported, for older .orion files). Only the first is
  // "primary" and keeps the original unqualified symbol names.
  if (databases) {
    bool first = true;
    EACH_ELEMENT(db, databases) if (elem(db, "database")) { emit_database(f, db, pre, first); first = false; }
  } else {
    emit_database(f, database, pre, true);
  }
  EACH_ELEMENT(form, forms) if (elem(form, "form") && activity_form_needed(form, forms, only)) {
    char *name = attr(form, "name"), id[128]; ident(id, sizeof(id), name, false);
    OUT("static const form_def_t %s_%s_form;\n", pre, id); free(name);
  }
  // X-macro over every form, for code that walks all of them (tests, editors).
  char pre_upper[128]; ident(pre_upper, sizeof(pre_upper), pre, true);
  OUT("#define %s_FORMS(X)", pre_upper);
  EACH_ELEMENT(form, forms) if (elem(form, "form") && activity_form_needed(form, forms, only)) {
    char *name = attr(form, "name"), id[128]; ident(id, sizeof(id), name, false);
    OUT(" X(%s)", id); free(name);
  }
  LINE("\n");
  EACH_ELEMENT(form, forms) if (elem(form, "form") && activity_form_needed(form, forms, only)) {
    if (!emit_form(f, form, pre, databases ? databases : database)) { fclose(f); xmlFreeDoc(doc); return 1; }
  }
  OUT("#endif /* %s */\n", guard);
  fclose(f); xmlFreeDoc(doc);
  return errors ? 1 : 0;
}
