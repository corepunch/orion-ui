// launch.xml reader and command builder. See gc_launch.h for the format.

#include "gc_launch.h"
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

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

// ── guessed targets ────────────────────────────────────────────────────────────

static bool checkout_has(const char *checkout, const char *file) {
  char path[1024];
  if (snprintf(path, sizeof(path), "%s/%s", checkout, file) >= (int)sizeof(path)) return false;
  FILE *f = fopen(path, "rb");
  if (f) fclose(f);
  return f != NULL;
}

static char *read_checkout_file(const char *checkout, const char *file, size_t limit) {
  char path[1024];
  if (snprintf(path, sizeof(path), "%s/%s", checkout, file) >= (int)sizeof(path)) return NULL;
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  char *buf = malloc(limit + 1);
  size_t n = buf ? fread(buf, 1, limit, f) : 0;
  fclose(f);
  if (buf) buf[n] = 0;
  return buf;
}

static void guess(gc_launch_t *l, gc_target_kind_t kind, const char *command) {
  if (l->count >= GC_MAX_TARGETS) return;
  for (int i = 0; i < l->count; i++) if (!strcmp(l->targets[i].name, command)) return;
  gc_launch_target_t *t = &l->targets[l->count++];
  memset(t, 0, sizeof(*t));
  snprintf(t->name, sizeof(t->name), "%s", command);
  snprintf(t->run, sizeof(t->run), "%s", command);
  t->kind = kind;
}

// A rule "name:" at the start of a line; enough to tell whether make test exists.
static bool makefile_defines(const char *text, const char *rule) {
  size_t len = strlen(rule);
  for (const char *line = text; line; line = strchr(line, '\n'), line = line ? line + 1 : NULL)
    if (!strncmp(line, rule, len) && line[len] == ':' && line[len + 1] != '=') return true;
  return false;
}

// The keys of the top-level "scripts" object of a package.json.
static int package_scripts(const char *json, char names[][48], int max) {
  const char *p = strstr(json, "\"scripts\"");
  if (!p || !(p = strchr(p + 9, '{'))) return 0;
  int count = 0, depth = 0; bool key = true;
  for (p++; *p; p++) {
    if (*p == '"') {
      const char *start = ++p;
      while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; p++; }
      if (!*p) break;
      size_t len = (size_t)(p - start);
      if (depth == 0 && key && count < max && len > 0 && len < 48) {
        memcpy(names[count], start, len); names[count][len] = 0;
        bool plain = true;
        for (size_t i = 0; i < len; i++) if (!isalnum((unsigned char)start[i]) && !strchr(":-_.", start[i])) plain = false;
        if (plain) count++;
      }
    }
    else if (*p == ':' && depth == 0) key = false;
    else if (*p == ',' && depth == 0) key = true;
    else if (*p == '{' || *p == '[') depth++;
    else if (*p == '}' || *p == ']') { if (depth-- == 0) break; }
  }
  return count;
}

int gc_launch_guess(const char *checkout, gc_launch_t *out) {
  if (!checkout || !out) return -1;
  memset(out, 0, sizeof(*out)); out->default_index = -1; out->guessed = true;
  static const char *makefiles[] = {"GNUmakefile", "makefile", "Makefile"};
  for (int i = 0; i < 3; i++) {
    char *text = read_checkout_file(checkout, makefiles[i], 256 * 1024);
    if (!text) continue;
    guess(out, GC_TARGET_BUILD, "make");
    if (makefile_defines(text, "test"))       guess(out, GC_TARGET_TEST, "make test");
    else if (makefile_defines(text, "check")) guess(out, GC_TARGET_TEST, "make check");
    free(text);
    break;
  }
  if (checkout_has(checkout, "Package.swift")) {
    guess(out, GC_TARGET_RUN, "swift run"); guess(out, GC_TARGET_BUILD, "swift build"); guess(out, GC_TARGET_TEST, "swift test");
  }
  if (checkout_has(checkout, "Cargo.toml")) {
    guess(out, GC_TARGET_RUN, "cargo run"); guess(out, GC_TARGET_BUILD, "cargo build"); guess(out, GC_TARGET_TEST, "cargo test");
  }
  if (checkout_has(checkout, "go.mod")) {
    guess(out, GC_TARGET_RUN, "go run ."); guess(out, GC_TARGET_BUILD, "go build ./..."); guess(out, GC_TARGET_TEST, "go test ./...");
  }
  if (checkout_has(checkout, "CMakeLists.txt")) {
    guess(out, GC_TARGET_BUILD, "cmake -S . -B build && cmake --build build");
    guess(out, GC_TARGET_TEST, "ctest --test-dir build");
  }
  char *json = read_checkout_file(checkout, "package.json", 256 * 1024);
  if (json) {
    const char *npm = checkout_has(checkout, "pnpm-lock.yaml") ? "pnpm" : checkout_has(checkout, "yarn.lock") ? "yarn" : "npm";
    char names[GC_MAX_TARGETS][48];
    int n = package_scripts(json, names, GC_MAX_TARGETS);
    for (int i = 0; i < n; i++) {
      char command[96];
      snprintf(command, sizeof(command), "%s run %s", npm, names[i]);
      gc_target_kind_t kind = !strcmp(names[i], "test") || !strncmp(names[i], "test:", 5) ? GC_TARGET_TEST
                            : !strcmp(names[i], "build") || !strncmp(names[i], "build:", 6) ? GC_TARGET_BUILD : GC_TARGET_RUN;
      guess(out, kind, command);
      if (kind == GC_TARGET_RUN && (!strcmp(names[i], "start") || !strcmp(names[i], "dev")) && out->default_index < 0)
        out->default_index = out->count - 1;
    }
    free(json);
  }
  if (out->default_index < 0)
    for (int i = 0; i < out->count; i++) if (out->targets[i].kind == GC_TARGET_RUN) { out->default_index = i; break; }
  if (out->default_index >= 0) out->targets[out->default_index].is_default = true;
  return out->count;
}

// ── saving ─────────────────────────────────────────────────────────────────────

static void put_xml_attr(FILE *f, const char *name, const char *v) {
  fprintf(f, " %s=\"", name);
  for (; *v; v++)
    switch (*v) {
      case '&': fputs("&amp;", f);  break;
      case '<': fputs("&lt;", f);   break;
      case '>': fputs("&gt;", f);   break;
      case '"': fputs("&quot;", f); break;
      default:  fputc(*v, f);       break;
    }
  fputc('"', f);
}

bool gc_launch_save(const char *checkout, const gc_launch_t *l) {
  if (!checkout || !l) return false;
  char path[1024];
  if (snprintf(path, sizeof(path), "%s/%s", checkout, GC_LAUNCH_FILE) >= (int)sizeof(path)) return false;
  int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
  FILE *f = fd >= 0 ? fdopen(fd, "w") : NULL;
  if (!f) {
    int err = errno;
    if (fd >= 0) close(fd);
    fprintf(stderr, "[gc] %s: not written (%s)\n", path, strerror(err)); fflush(stderr);
    return false;
  }
  fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<launch", f);
  if (l->project[0]) put_xml_attr(f, "project", l->project);
  fputs(">\n", f);
  for (int i = 0; i < l->count; i++) {
    const gc_launch_target_t *t = &l->targets[i];
    fputs("    <target", f);
    put_xml_attr(f, "name", t->name);
    if (t->is_default) fputs(" default=\"true\"", f);
    if (t->kind != GC_TARGET_RUN) put_xml_attr(f, "kind", gc_launch_kind_name(t->kind));
    if (t->dir[0])   put_xml_attr(f, "dir", t->dir);
    if (t->build[0]) put_xml_attr(f, "build", t->build);
    put_xml_attr(f, "run", t->run);
    if (!t->param_count && !t->env_count) { fputs(" />\n", f); continue; }
    fputs(">\n", f);
    for (int p = 0; p < t->param_count; p++) {
      fputs("        <param", f); put_xml_attr(f, "name", t->params[p].name);
      if (t->params[p].choices[0]) put_xml_attr(f, "choices", t->params[p].choices);
      if (t->params[p].def[0])     put_xml_attr(f, "default", t->params[p].def);
      fputs(" />\n", f);
    }
    for (int e = 0; e < t->env_count; e++) {
      fputs("        <env", f); put_xml_attr(f, "name", t->env[e].name); put_xml_attr(f, "value", t->env[e].value); fputs(" />\n", f);
    }
    fputs("    </target>\n", f);
  }
  fputs("</launch>\n", f);
  bool ok = !ferror(f);
  if (fclose(f) != 0) ok = false;
  if (!ok) { fprintf(stderr, "[gc] %s: write failed\n", path); fflush(stderr); }
  return ok;
}
