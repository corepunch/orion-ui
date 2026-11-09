// launch.xml reader and command builder. See gc_launch.h for the format.

#include "gc_launch.h"
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static const char *k_kind_names[] = {"run", "build", "test"};

const char *gc_launch_kind_name(gc_target_kind_t kind) {
  return (unsigned)kind < 3 ? k_kind_names[kind] : "run";
}

static bool copy_attr(xmlNodePtr node, const char *attr, char *out, size_t n) {
  xmlChar *v = xmlGetProp(node, BAD_CAST attr);
  if (!v) { out[0] = 0; return false; }
  size_t len = strlen((const char *)v);
  bool fits = len < n;
  snprintf(out, n, "%s", (const char *)v);
  xmlFree(v);
  return fits;
}

static bool attr_is(xmlNodePtr node, const char *name) {
  return node->type == XML_ELEMENT_NODE && !xmlStrcmp(node->name, BAD_CAST name);
}

static void reject(const char *path, xmlNodePtr node, const char *what) {
  fprintf(stderr, "[gc] %s:%d: %s\n", path, node ? (int)xmlGetLineNo(node) : 0, what);
  fflush(stderr);
}

static bool parse_target(const char *path, xmlNodePtr node, gc_launch_target_t *t) {
  memset(t, 0, sizeof(*t));
  char kind[16], flag[8];
  bool name_ok = copy_attr(node, "name", t->name, sizeof(t->name));
  bool run_ok = copy_attr(node, "run", t->run, sizeof(t->run));
  bool ok = name_ok && run_ok;
  if (!ok || !t->name[0] || !t->run[0]) { reject(path, node, "<target> needs name= and run= (run at most 511 bytes)"); return false; }
  if (xmlHasProp(node, BAD_CAST "build") && !copy_attr(node, "build", t->build, sizeof(t->build))) { reject(path, node, "build= is too long"); return false; }
  if (xmlHasProp(node, BAD_CAST "dir") && !copy_attr(node, "dir", t->dir, sizeof(t->dir))) { reject(path, node, "dir= is too long"); return false; }
  copy_attr(node, "kind", kind, sizeof(kind));
  t->kind = GC_TARGET_RUN;
  if (kind[0]) {
    int k = 0; while (k < 3 && strcmp(kind, k_kind_names[k])) k++;
    if (k == 3) { reject(path, node, "kind= must be run, build or test"); return false; }
    t->kind = (gc_target_kind_t)k;
  }
  copy_attr(node, "default", flag, sizeof(flag));
  t->is_default = !strcmp(flag, "true");
  for (xmlNodePtr c = node->children; c; c = c->next) {
    if (attr_is(c, "param")) {
      if (t->param_count >= GC_MAX_PARAMS) { reject(path, c, "more than 4 <param> in a target"); return false; }
      gc_launch_param_t *p = &t->params[t->param_count++];
      if (!copy_attr(c, "name", p->name, sizeof(p->name)) || !p->name[0]) { reject(path, c, "<param> needs a short name="); return false; }
      copy_attr(c, "choices", p->choices, sizeof(p->choices));
      if (!copy_attr(c, "default", p->def, sizeof(p->def)) && p->choices[0])
        sscanf(p->choices, "%127s", p->def);
    } else if (attr_is(c, "env")) {
      if (t->env_count >= GC_MAX_ENV) { reject(path, c, "more than 4 <env> in a target"); return false; }
      gc_launch_env_t *e = &t->env[t->env_count++];
      if (!copy_attr(c, "name", e->name, sizeof(e->name)) || !e->name[0]) { reject(path, c, "<env> needs name="); return false; }
      copy_attr(c, "value", e->value, sizeof(e->value));
    } else if (c->type == XML_ELEMENT_NODE) {
      reject(path, c, "unknown element in <target> (expected <param> or <env>)"); return false;
    }
  }
  return true;
}

int gc_launch_parse(const char *xml_path, gc_launch_t *out) {
  if (!out) return -1;
  memset(out, 0, sizeof(*out)); out->default_index = -1;
  xmlDocPtr doc = xmlReadFile(xml_path, NULL, XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
  xmlNodePtr root = doc ? xmlDocGetRootElement(doc) : NULL;
  if (!root || xmlStrcmp(root->name, BAD_CAST "launch")) {
    reject(xml_path, root, doc ? "root element must be <launch>" : "not well-formed XML");
    if (doc) xmlFreeDoc(doc);
    return -1;
  }
  copy_attr(root, "project", out->project, sizeof(out->project));
  bool ok = true;
  for (xmlNodePtr n = root->children; n && ok; n = n->next) {
    if (!attr_is(n, "target")) {
      if (n->type == XML_ELEMENT_NODE) { reject(xml_path, n, "unknown element in <launch> (expected <target>)"); ok = false; }
      continue;
    }
    if (out->count >= GC_MAX_TARGETS) { reject(xml_path, n, "more than 32 targets"); ok = false; break; }
    gc_launch_target_t *t = &out->targets[out->count];
    if (!(ok = parse_target(xml_path, n, t))) break;
    for (int i = 0; i < out->count; i++)
      if (!strcmp(out->targets[i].name, t->name)) { reject(xml_path, n, "duplicate target name"); ok = false; }
    if (ok && t->is_default) {
      if (out->default_index >= 0) { reject(xml_path, n, "more than one default target"); ok = false; }
      else out->default_index = out->count;
    }
    out->count++;
  }
  xmlFreeDoc(doc);
  if (!ok) { memset(out, 0, sizeof(*out)); out->default_index = -1; return -1; }
  if (out->default_index < 0)
    for (int i = 0; i < out->count; i++) if (out->targets[i].kind == GC_TARGET_RUN) { out->default_index = i; break; }
  if (out->default_index >= 0) out->targets[out->default_index].is_default = true;
  return out->count;
}

int gc_launch_load(const char *checkout, gc_launch_t *out) {
  if (!checkout || !out) return -1;
  memset(out, 0, sizeof(*out)); out->default_index = -1;
  char path[1024];
  if (snprintf(path, sizeof(path), "%s/%s", checkout, GC_LAUNCH_FILE) >= (int)sizeof(path)) return -1;
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  fclose(f);
  return gc_launch_parse(path, out);
}

// ── command building ───────────────────────────────────────────────────────────

typedef struct { char *p; size_t left; bool ok; } sink_t;

static void put(sink_t *s, const char *text, size_t len) {
  if (!s->ok || len >= s->left) { s->ok = false; return; }
  memcpy(s->p, text, len); s->p += len; s->left -= len; *s->p = 0;
}
static void puts_(sink_t *s, const char *text) { put(s, text, strlen(text)); }

static bool shell_safe(const char *v) {
  if (!*v) return false;
  for (; *v; v++) if (!isalnum((unsigned char)*v) && !strchr("._/-:=,+@%", *v)) return false;
  return true;
}

static void put_quoted(sink_t *s, const char *v) {
  if (shell_safe(v)) { puts_(s, v); return; }
  puts_(s, "'");
  for (; *v; v++) if (*v == '\'') puts_(s, "'\\''"); else put(s, v, 1);
  puts_(s, "'");
}

static const char *param_value(const gc_launch_target_t *t, const char *const *values, const char *name, size_t len) {
  for (int i = 0; i < t->param_count; i++)
    if (strlen(t->params[i].name) == len && !strncmp(t->params[i].name, name, len))
      return values && values[i] ? values[i] : t->params[i].def;
  return NULL;
}

// Expands ${param}; quote=true shell-quotes each value (command text), false
// copies it raw (inside an env value that is quoted as a whole).
static bool expand(const gc_launch_target_t *t, const char *const *values, const char *text, bool quote, sink_t *s) {
  for (const char *c = text; *c && s->ok;) {
    const char *open = strstr(c, "${");
    if (!open) { puts_(s, c); break; }
    put(s, c, (size_t)(open - c));
    const char *close = strchr(open, '}');
    const char *v = close ? param_value(t, values, open + 2, (size_t)(close - open - 2)) : NULL;
    if (!v) {
      fprintf(stderr, "[gc] launch target '%s': unknown parameter in \"%s\"\n", t->name, text); fflush(stderr);
      return false;
    }
    if (quote) put_quoted(s, v); else puts_(s, v);
    c = close + 1;
  }
  return s->ok;
}

bool gc_launch_command(const gc_launch_target_t *t, const char *const *values, char *out, size_t n) {
  if (!t || !out || !n) return false;
  out[0] = 0;
  sink_t s = {out, n, true};
  if (t->dir[0]) { puts_(&s, "cd "); put_quoted(&s, t->dir); puts_(&s, " && "); }
  if (t->build[0]) { if (!expand(t, values, t->build, true, &s)) return false; puts_(&s, " && "); }
  for (int i = 0; i < t->env_count; i++) {
    char value[512]; sink_t v = {value, sizeof(value), true}; value[0] = 0;
    if (!expand(t, values, t->env[i].value, false, &v)) return false;
    puts_(&s, t->env[i].name); puts_(&s, "="); put_quoted(&s, value); puts_(&s, " ");
  }
  if (!expand(t, values, t->run, true, &s)) return false;
  if (!s.ok) { fprintf(stderr, "[gc] launch target '%s': command longer than %zu bytes\n", t->name, n - 1); fflush(stderr); }
  return s.ok;
}
