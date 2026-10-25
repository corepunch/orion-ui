// Localization: layered XML string catalogs. See i18n.h.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "user.h"
#include "i18n.h"
#include <libxml/parser.h>
#include <libxml/tree.h>

typedef struct {
  char *context;  // NULL = any context
  char *source;
  char *text;
} locale_string_t;

typedef struct locale_pooled_s {
  struct locale_pooled_s *next;
  char text[];
} locale_pooled_t;

static struct {
  char language[32];
  locale_string_t *strings;
  int count, capacity;
  locale_pooled_t *pool;       // composed results (shortcut labels, pseudo text)
  const char *context;
  char **scoped;               // add-on catalogs already loaded, by path
  int scoped_count;
  char decimal[8], group[8];
  char date[16];               // pattern of dd, MM and yyyy; empty = ISO
} g_locale = { .language = "en", .decimal = "." };

static void locale_clear(void) {
  for (int i = 0; i < g_locale.count; i++) {
    free(g_locale.strings[i].context);
    free(g_locale.strings[i].source);
    free(g_locale.strings[i].text);
  }
  free(g_locale.strings);
  g_locale.strings = NULL;
  g_locale.count = g_locale.capacity = 0;
  for (int i = 0; i < g_locale.scoped_count; i++) free(g_locale.scoped[i]);
  free(g_locale.scoped);
  g_locale.scoped = NULL;
  g_locale.scoped_count = 0;
  while (g_locale.pool) {
    locale_pooled_t *next = g_locale.pool->next;
    free(g_locale.pool);
    g_locale.pool = next;
  }
  snprintf(g_locale.decimal, sizeof(g_locale.decimal), ".");
  g_locale.group[0] = '\0';
  g_locale.date[0] = '\0';
}

static const char *locale_intern(const char *text, size_t len) {
  locale_pooled_t *p = malloc(sizeof(*p) + len + 1);
  if (!p) {
    fprintf(stderr, "[locale] string pool allocation failed len=%zu\n", len);
    return "";
  }
  memcpy(p->text, text, len);
  p->text[len] = '\0';
  p->next = g_locale.pool;
  g_locale.pool = p;
  return p->text;
}

static char *xml_dup(xmlNodePtr node, const char *attr) {
  xmlChar *v = attr ? xmlGetProp(node, (const xmlChar *)attr) : xmlNodeGetContent(node);
  char *s = v ? strdup((const char *)v) : NULL;
  if (v) xmlFree(v);
  return s;
}

static void locale_add(const char *context, xmlNodePtr node, const char *path) {
  char *source = xml_dup(node, "source"), *text = xml_dup(node, NULL);
  if (!source || !text) {
    fprintf(stderr, "[locale] %s:%d: <string> needs source= and text\n", path, (int)xmlGetLineNo(node));
    free(source); free(text);
    return;
  }
  if (!text[0]) { free(source); free(text); return; }  // untranslated template entry
  if (g_locale.count == g_locale.capacity) {
    int cap = g_locale.capacity ? g_locale.capacity * 2 : 256;
    locale_string_t *grown = realloc(g_locale.strings, (size_t)cap * sizeof(*grown));
    if (!grown) {
      fprintf(stderr, "[locale] catalog allocation failed count=%d\n", g_locale.count);
      free(source); free(text);
      return;
    }
    g_locale.strings = grown;
    g_locale.capacity = cap;
  }
  g_locale.strings[g_locale.count++] = (locale_string_t){
    context ? strdup(context) : NULL, source, text };
}

// A string of an add-on's catalog translates in each form its where= names;
// where="code" stands for scope, the context the add-on's code runs in.
static void locale_add_scoped(xmlNodePtr node, const char *path, const char *scope) {
  char *where = xml_dup(node, "where");
  const char *p = where ? where : "code";
  while (*p) {
    size_t n = strcspn(p, ",");
    char context[128];
    snprintf(context, sizeof(context), "%.*s", (int)n, p);
    locale_add(!strcmp(context, "code") ? scope : context, node, path);
    p += n + (p[n] == ',');
  }
  free(where);
}

// scope NULL: a catalog of the application, whose formats and strings apply
// everywhere. Otherwise an add-on's: its strings stay in their contexts and
// the host's formats are kept.
static bool load_catalog(const char *path, const char *scope) {
  if (!path || !axPathExists(path)) return false;
  xmlDocPtr doc = xmlReadFile(path, NULL, XML_PARSE_NONET);
  xmlNodePtr root = doc ? xmlDocGetRootElement(doc) : NULL;
  if (!root || xmlStrcmp(root->name, (const xmlChar *)"strings")) {
    fprintf(stderr, "[locale] %s is not a <strings> catalog\n", path);
    fflush(stderr);
    if (doc) xmlFreeDoc(doc);
    return false;
  }
  if (!scope) {
    char *decimal = xml_dup(root, "decimal"), *group = xml_dup(root, "group"), *date = xml_dup(root, "date");
    if (decimal) snprintf(g_locale.decimal, sizeof(g_locale.decimal), "%s", decimal);
    if (group) snprintf(g_locale.group, sizeof(g_locale.group), "%s", group);
    if (date) snprintf(g_locale.date, sizeof(g_locale.date), "%s", date);
    free(decimal); free(group); free(date);
  }
  for (xmlNodePtr n = root->children; n; n = n->next) {
    if (n->type != XML_ELEMENT_NODE) continue;
    if (!xmlStrcmp(n->name, (const xmlChar *)"string")) {
      if (scope) locale_add_scoped(n, path, scope); else locale_add(NULL, n, path);
      continue;
    }
    if (xmlStrcmp(n->name, (const xmlChar *)"context")) continue;
    char *context = xml_dup(n, "name");
    for (xmlNodePtr s = n->children; s; s = s->next)
      if (s->type == XML_ELEMENT_NODE && !xmlStrcmp(s->name, (const xmlChar *)"string"))
        locale_add(context, s, path);
    free(context);
  }
  xmlFreeDoc(doc);
  return true;
}

bool ui_load_string_catalog(const char *path) { return load_catalog(path, NULL); }

// Loads the neutral, then the regional catalog of the current language from dir.
static bool load_catalog_dir(const char *dir, const char *scope) {
  if (!dir || !strcmp(g_locale.language, UI_PSEUDO_LANGUAGE)) return false;
  char path[1024], neutral[32];
  snprintf(neutral, sizeof(neutral), "%s", g_locale.language);
  char *dash = strchr(neutral, '-');
  if (dash) *dash = '\0';
  snprintf(path, sizeof(path), "%s/%s.xml", dir, neutral);
  bool loaded = load_catalog(path, scope);
  if (dash) {
    snprintf(path, sizeof(path), "%s/%s.xml", dir, g_locale.language);
    loaded |= load_catalog(path, scope);
  }
  return loaded;
}

bool ui_add_scoped_string_catalog_dir(const char *dir, const char *scope) {
  if (!dir || !scope || !*scope) {
    fprintf(stderr, "[locale] ui_add_scoped_string_catalog_dir: dir and scope required\n");
    fflush(stderr);
    return false;
  }
  for (int i = 0; i < g_locale.scoped_count; i++)
    if (!strcmp(g_locale.scoped[i], dir)) return true;
  char **grown = realloc(g_locale.scoped, (size_t)(g_locale.scoped_count + 1) * sizeof(*grown));
  char *copy = strdup(dir);
  if (!grown || !copy) {
    fprintf(stderr, "[locale] add-on catalog allocation failed dir=%s\n", dir);
    fflush(stderr);
    if (grown) g_locale.scoped = grown;
    free(copy);
    return false;
  }
  g_locale.scoped = grown;
  g_locale.scoped[g_locale.scoped_count++] = copy;
  return load_catalog_dir(dir, scope);
}

bool ui_add_string_catalog_dir(const char *dir) { return load_catalog_dir(dir, NULL); }

bool ui_set_language(const char *language) {
  if (!language || !*language || strlen(language) >= sizeof(g_locale.language)) {
    fprintf(stderr, "[locale] invalid language '%s'\n", language ? language : "(null)");
    fflush(stderr);
    return false;
  }
  locale_clear();
  snprintf(g_locale.language, sizeof(g_locale.language), "%s", language);
  char dir[1024];
  snprintf(dir, sizeof(dir), "%s/../share/orion/locale", ui_get_exe_dir());
  ui_add_string_catalog_dir(dir);
  return true;
}

const char *ui_get_language(void) { return g_locale.language; }

const char *ui_system_language(char *buf, size_t buf_sz) {
  const char *vars[] = { "LC_ALL", "LC_MESSAGES", "LANG" };
  const char *value = NULL;
  for (size_t i = 0; i < sizeof(vars) / sizeof(vars[0]) && !value; i++) {
    const char *v = getenv(vars[i]);
    if (v && *v) value = v;
  }
  if (!value || !strcmp(value, "C") || !strcmp(value, "POSIX") || !strncmp(value, "C.", 2)) value = "en";
  size_t n = 0;
  for (; value[n] && value[n] != '.' && value[n] != '@' && n + 1 < buf_sz; n++)
    buf[n] = value[n] == '_' ? '-' : value[n];
  buf[n] = '\0';
  return buf;
}

const char *ui_set_string_context(const char *context) {
  const char *previous = g_locale.context;
  g_locale.context = context;
  return previous;
}

static const char *locale_find(const char *context, const char *source, size_t len) {
  // Later catalogs override earlier ones; a context match beats a global one.
  for (int pass = context ? 0 : 1; pass < 2; pass++)
    for (int i = g_locale.count - 1; i >= 0; i--) {
      const locale_string_t *s = &g_locale.strings[i];
      if (pass == 0 ? !s->context || strcmp(s->context, context) : s->context != NULL) continue;
      if (strlen(s->source) == len && !strncmp(s->source, source, len)) return s->text;
    }
  return NULL;
}

// Accents ASCII letters and pads by ~40%: "[Šàvé ·····]".
static const char *locale_pseudo(const char *s, size_t len, const char *tail) {
  static const char *map[128] = {
    ['a'] = "à", ['e'] = "é", ['i'] = "ï", ['o'] = "ö", ['u'] = "ü", ['c'] = "ç", ['n'] = "ñ",
    ['s'] = "š", ['y'] = "ý", ['A'] = "Å", ['E'] = "É", ['I'] = "Ï", ['O'] = "Ö", ['U'] = "Ü",
    ['C'] = "Ç", ['N'] = "Ñ", ['S'] = "Š",
  };
  size_t cap = len * 3 + len / 2 + strlen(tail) + 16, n = 0;
  char *out = malloc(cap);
  if (!out) return "";
  out[n++] = '[';
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    const char *m = c < 128 ? map[c] : NULL;
    if (m) { memcpy(out + n, m, strlen(m)); n += strlen(m); }
    else out[n++] = (char)c;
  }
  out[n++] = ' ';
  for (size_t i = 0; i < len * 2 / 5; i++) { memcpy(out + n, "·", 2); n += 2; }
  out[n++] = ']';
  n += (size_t)snprintf(out + n, cap - n, "%s", tail);
  const char *result = locale_intern(out, n);
  free(out);
  return result;
}

const char *ui_trc(const char *context, const char *source) {
  if (!source || !*source) return source;
  const char *tab = strchr(source, '\t');
  size_t len = tab ? (size_t)(tab - source) : strlen(source);
  if (!strcmp(g_locale.language, UI_PSEUDO_LANGUAGE)) return locale_pseudo(source, len, tab ? tab : "");
  const char *text = g_locale.count ? locale_find(context, source, len) : NULL;
  if (!text) return source;
  if (!tab) return text;
  char joined[512];
  int n = snprintf(joined, sizeof(joined), "%s%s", text, tab);
  return n > 0 && (size_t)n < sizeof(joined) ? locale_intern(joined, (size_t)n) : source;
}

const char *ui_get_string_context(void) { return g_locale.context; }

const char *ui_tr(const char *source) { return ui_trc(g_locale.context, source); }

const char *ui_format_date(const char *iso, char *buf, size_t buf_sz) {
  int y, m, d;
  if (!g_locale.date[0] || !iso || sscanf(iso, "%4d-%2d-%2d", &y, &m, &d) != 3) {
    snprintf(buf, buf_sz, "%s", iso ? iso : "");
    return buf;
  }
  size_t n = 0;
  for (const char *p = g_locale.date; *p && n + 1 < buf_sz; ) {
    if (!strncmp(p, "yyyy", 4)) { n += (size_t)snprintf(buf + n, buf_sz - n, "%04d", y); p += 4; }
    else if (!strncmp(p, "MM", 2)) { n += (size_t)snprintf(buf + n, buf_sz - n, "%02d", m); p += 2; }
    else if (!strncmp(p, "dd", 2)) { n += (size_t)snprintf(buf + n, buf_sz - n, "%02d", d); p += 2; }
    else buf[n++] = *p++;
  }
  buf[n < buf_sz ? n : buf_sz - 1] = '\0';
  return buf;
}

bool ui_parse_date(const char *text, char *iso, size_t iso_sz) {
  int y = 0, m = 0, d = 0, fields = 0;
  iso[0] = '\0';
  if (!text || !*text) return false;
  if (sscanf(text, "%4d-%2d-%2d", &y, &m, &d) == 3) fields = 3;
  for (const char *p = g_locale.date, *t = text; !fields && *p && *t; ) {
    int *slot = !strncmp(p, "yyyy", 4) ? &y : !strncmp(p, "MM", 2) ? &m : !strncmp(p, "dd", 2) ? &d : NULL;
    if (!slot) { p++; t++; continue; }
    char *end;
    long v = strtol(t, &end, 10);
    if (end == t) break;
    *slot = (int)v; t = end; p += slot == &y ? 4 : 2;
    if (!*p) fields = 3;
  }
  if (fields != 3 || y < 1000 || m < 1 || m > 12 || d < 1 || d > 31) return false;
  snprintf(iso, iso_sz, "%04d-%02d-%02d", y, m, d);
  return true;
}

double ui_parse_number(const char *text) {
  char clean[64];
  size_t n = 0, dl = strlen(g_locale.decimal), gl = strlen(g_locale.group);
  for (const char *p = text ? text : ""; *p && n + 1 < sizeof(clean); ) {
    if (gl && !strncmp(p, g_locale.group, gl) && strncmp(p, g_locale.decimal, dl)) { p += gl; continue; }
    if (dl && !strncmp(p, g_locale.decimal, dl)) { clean[n++] = '.'; p += dl; continue; }
    if (*p == ',' && g_locale.decimal[0] != '.') { clean[n++] = '.'; p++; continue; }
    if (*p != ' ') clean[n++] = *p;
    p++;
  }
  clean[n] = '\0';
  return strtod(clean, NULL);
}

const char *ui_format_number(double value, int decimals, char *buf, size_t buf_sz) {
  char raw[64];
  snprintf(raw, sizeof(raw), "%.*f", decimals, value);
  const char *digits = raw + (raw[0] == '-');
  const char *point = strchr(digits, '.');
  size_t int_len = point ? (size_t)(point - digits) : strlen(digits), n = 0;
  if (raw[0] == '-' && n + 1 < buf_sz) buf[n++] = '-';
  for (size_t i = 0; i < int_len && n + 1 < buf_sz; i++) {
    if (i && g_locale.group[0] && (int_len - i) % 3 == 0)
      n += (size_t)snprintf(buf + n, buf_sz - n, "%s", g_locale.group);
    if (n + 1 < buf_sz) buf[n++] = digits[i];
  }
  if (point) n += (size_t)snprintf(buf + n, n < buf_sz ? buf_sz - n : 0, "%s%s", g_locale.decimal, point + 1);
  buf[n < buf_sz ? n : buf_sz - 1] = '\0';
  return buf;
}
