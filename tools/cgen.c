// cgen — Lua bindings generated from a .cgen module description, after
// orca's tools/codegen. The .cgen file declares each function a module
// exports: its arguments, their types and its results. cgen writes the
// wrappers that check the arguments and the luaopen_ function that registers
// them, so no binding is written by hand. The same file yields the module's
// Markdown reference.
//
//   cgen --input orion/script/db.cgen --output build/generated/orion/script/db_lua.h
//   cgen --input orion/script/db.cgen --markdown docs/lua/orion.db.md
//
// The header declares the bound C functions, so a signature that disagrees
// with the .cgen does not compile, and defines the wrappers as statics; the
// file implementing the module includes it at its end.
//
//   <module name="orion.db" prefix="script_db_">
//     <functions>
//       <function name="find" lua="true">          C: int script_db_find(lua_State *L, ...)
//         <summary>The first matching row, or nil.</summary>
//         <arg name="database" type="string"/>
//         <arg name="field" type="string" optional="true">nil for the key</arg>
//         <arg name="value" type="value"/>         a Lua stack index
//         <returns type="nresults"/>               the C function pushes its results
//       </function>
//       <function name="format_number" call="ui_format_number">   Lua: formatNumber
//         <arg name="value" type="number"/>
//         <arg name="decimals" type="int" default="2"/>
//         <arg name="buf" type="string" output="char[64]"/>       a local buffer, not a Lua argument
//         <arg name="size" type="size" bufsize="buf"/>            sizeof(buf)
//         <returns type="string"/>
//       </function>
//
// Types: int, number, bool, string, value, table, function (stack indices),
// size (bufsize only); results also nresults and void (no <returns>).

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ARGS      16
#define MAX_FUNCTIONS 96
#define MAX_INCLUDES  16

typedef struct {
  char *name, *type, *def, *output, *bufsize, *doc;
  bool optional;
  int index;  // Lua stack index; 0 for a local buffer or its size
} arg_t;

typedef struct {
  char *name, *lua_name, *call, *summary, *details, *ret, *ret_doc;
  bool with_state;
  arg_t args[MAX_ARGS];
  int argc, line;
} func_t;

typedef struct {
  const char *path;
  char *name, *prefix, *luaopen, *on_luaopen, *after_luaopen, *summary, *details;
  char *includes[MAX_INCLUDES];
  int include_count;
  func_t funcs[MAX_FUNCTIONS];
  int count;
} module_t;

typedef struct {
  const char *name, *c_type;
  bool stack;            // the argument is a Lua stack index
  const char *lua_type;  // its checked type; NULL for any value
} type_t;

static const type_t kTypes[] = {
  { "int",      "int",          false, NULL },
  { "number",   "double",       false, NULL },
  { "bool",     "bool",         false, NULL },
  { "string",   "const char *", false, NULL },
  { "value",    "int",          true,  NULL },
  { "table",    "int",          true,  "LUA_TTABLE" },
  { "function", "int",          true,  "LUA_TFUNCTION" },
  { "size",     "size_t",       false, NULL },
};

static const char *g_input;

static void fail_at(int line, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "cgen: %s:%d: ", g_input, line);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
  exit(3);
}

static const type_t *type_named(const char *name) {
  for (size_t i = 0; name && i < sizeof(kTypes) / sizeof(kTypes[0]); i++)
    if (!strcmp(kTypes[i].name, name)) return &kTypes[i];
  return NULL;
}

static char *copy(const char *s) {
  char *out = malloc(strlen(s) + 1);
  return strcpy(out, s);
}

static char *attr(xmlNodePtr n, const char *name) {
  xmlChar *v = xmlGetProp(n, BAD_CAST name);
  if (!v) return NULL;
  char *s = copy((const char *)v);
  xmlFree(v);
  return s;
}

static bool attr_true(xmlNodePtr n, const char *name) {
  char *v = attr(n, name);
  bool yes = v && !strcmp(v, "true");
  free(v);
  return yes;
}

// Element text with runs of white space folded to one space.
static char *text_of(xmlNodePtr n) {
  xmlChar *raw = xmlNodeGetContent(n);
  const char *s = raw ? (const char *)raw : "";
  char *out = malloc(strlen(s) + 1), *o = out;
  bool space = false;
  for (; *s; s++) {
    if (isspace((unsigned char)*s)) { space = o != out; continue; }
    if (space) *o++ = ' ';
    space = false;
    *o++ = *s;
  }
  *o = 0;
  if (raw) xmlFree(raw);
  if (!*out) { free(out); return NULL; }
  return out;
}

static bool is_element(xmlNodePtr n, const char *name) {
  return n->type == XML_ELEMENT_NODE && !strcmp((const char *)n->name, name);
}

static bool is_identifier(const char *s) {
  if (!s || !*s || isdigit((unsigned char)*s)) return false;
  for (; *s; s++) if (!isalnum((unsigned char)*s) && *s != '_') return false;
  return true;
}

// format_number -> formatNumber
static char *camel_case(const char *s) {
  char *out = malloc(strlen(s) + 1), *o = out;
  for (bool up = false; *s; s++) {
    if (*s == '_') { up = o != out; continue; }
    *o++ = up ? (char)toupper((unsigned char)*s) : *s;
    up = false;
  }
  *o = 0;
  return out;
}

static void parse_arg(func_t *f, xmlNodePtr n, int *index) {
  int line = (int)xmlGetLineNo(n);
  if (f->argc >= MAX_ARGS) fail_at(line, "function '%s' has more than %d arguments", f->name, MAX_ARGS);
  arg_t *a = &f->args[f->argc++];
  a->name = attr(n, "name");
  a->type = attr(n, "type");
  a->def = attr(n, "default");
  a->output = attr(n, "output");
  a->bufsize = attr(n, "bufsize");
  a->optional = attr_true(n, "optional") || a->def;
  a->doc = text_of(n);
  if (!is_identifier(a->name)) fail_at(line, "function '%s': argument needs a C identifier as name", f->name);
  const type_t *t = type_named(a->type);
  if (!t) fail_at(line, "argument '%s': unknown type '%s'", a->name, a->type ? a->type : "");
  if (a->output && strcmp(a->type, "string")) fail_at(line, "argument '%s': output= needs type=\"string\"", a->name);
  if (a->bufsize) {
    bool found = false;
    for (int i = 0; i < f->argc - 1; i++) found |= !strcmp(f->args[i].name, a->bufsize) && f->args[i].output;
    if (!found) fail_at(line, "argument '%s': bufsize=\"%s\" names no earlier output argument", a->name, a->bufsize);
    if (strcmp(a->type, "size")) fail_at(line, "argument '%s': bufsize= needs type=\"size\"", a->name);
  } else if (!strcmp(a->type, "size")) {
    fail_at(line, "argument '%s': type=\"size\" is only for bufsize= arguments", a->name);
  }
  if (a->def && t->stack) fail_at(line, "argument '%s': a %s argument has no default", a->name, a->type);
  a->index = a->output || a->bufsize ? 0 : (*index)++;
}

static void parse_function(module_t *m, xmlNodePtr n) {
  int line = (int)xmlGetLineNo(n);
  if (m->count >= MAX_FUNCTIONS) fail_at(line, "more than %d functions", MAX_FUNCTIONS);
  func_t *f = &m->funcs[m->count++];
  f->line = line;
  f->name = attr(n, "name");
  if (!is_identifier(f->name)) fail_at(line, "a function needs a C identifier as name");
  char *export = attr(n, "export");
  f->lua_name = export ? export : camel_case(f->name);
  f->call = attr(n, "call");
  if (!f->call) {
    f->call = malloc(strlen(m->prefix ? m->prefix : "") + strlen(f->name) + 1);
    sprintf(f->call, "%s%s", m->prefix ? m->prefix : "", f->name);
  }
  f->with_state = attr_true(n, "lua");
  for (int i = 0; i < m->count - 1; i++)
    if (!strcmp(m->funcs[i].lua_name, f->lua_name)) fail_at(line, "function '%s' is exported twice", f->lua_name);
  int index = 1;
  for (xmlNodePtr c = n->children; c; c = c->next) {
    if (c->type != XML_ELEMENT_NODE) continue;
    if (is_element(c, "summary")) f->summary = text_of(c);
    else if (is_element(c, "details")) f->details = text_of(c);
    else if (is_element(c, "arg")) parse_arg(f, c, &index);
    else if (is_element(c, "returns")) {
      f->ret = attr(c, "type");
      f->ret_doc = text_of(c);
    } else fail_at((int)xmlGetLineNo(c), "function '%s': unknown element <%s>", f->name, c->name);
  }
  if (f->ret && strcmp(f->ret, "nresults") && strcmp(f->ret, "void")) {
    const type_t *t = type_named(f->ret);
    if (!t || t->stack || !strcmp(f->ret, "size")) fail_at(line, "function '%s': cannot return '%s'", f->name, f->ret);
  }
  if (f->ret && !strcmp(f->ret, "nresults") && !f->with_state)
    fail_at(line, "function '%s': returns nresults needs lua=\"true\"", f->name);
  for (int i = 0; i < f->argc; i++)
    if (type_named(f->args[i].type)->stack && !f->with_state)
      fail_at(line, "function '%s': argument '%s' is a stack index and needs lua=\"true\"", f->name, f->args[i].name);
}

static void parse_module(module_t *m, xmlNodePtr root) {
  if (!is_element(root, "module")) fail_at((int)xmlGetLineNo(root), "the root element must be <module>");
  m->name = attr(root, "name");
  m->prefix = attr(root, "prefix");
  m->luaopen = attr(root, "luaopen");
  m->on_luaopen = attr(root, "on-luaopen");
  m->after_luaopen = attr(root, "after-luaopen");
  if (!m->name || !*m->name) fail_at((int)xmlGetLineNo(root), "<module> needs a name");
  if (m->luaopen && !is_identifier(m->luaopen)) fail_at((int)xmlGetLineNo(root), "luaopen=\"%s\" is not a C identifier", m->luaopen);
  for (xmlNodePtr c = root->children; c; c = c->next) {
    if (c->type != XML_ELEMENT_NODE) continue;
    if (is_element(c, "summary")) m->summary = text_of(c);
    else if (is_element(c, "details")) m->details = text_of(c);
    else if (is_element(c, "includes")) {
      for (xmlNodePtr i = c->children; i; i = i->next) {
        if (!is_element(i, "include")) continue;
        if (m->include_count >= MAX_INCLUDES) fail_at((int)xmlGetLineNo(i), "more than %d includes", MAX_INCLUDES);
        m->includes[m->include_count++] = attr(i, "file");
      }
    } else if (is_element(c, "functions")) {
      for (xmlNodePtr f = c->children; f; f = f->next)
        if (is_element(f, "function")) parse_function(m, f);
        else if (f->type == XML_ELEMENT_NODE) fail_at((int)xmlGetLineNo(f), "unknown element <%s> in <functions>", f->name);
    } else {
      fail_at((int)xmlGetLineNo(c), "unknown element <%s> in <module>", c->name);
    }
  }
}

// ── C ───────────────────────────────────────────────────────────────────────

static void symbol_of(const char *name, char *out, size_t sz) {
  snprintf(out, sz, "%s", name);
  for (char *p = out; *p; p++) if (!isalnum((unsigned char)*p)) *p = '_';
}

static const char *c_result(const func_t *f) {
  if (!f->ret || !strcmp(f->ret, "void")) return "void";
  if (!strcmp(f->ret, "nresults")) return "int";
  return type_named(f->ret)->c_type;
}

static void emit_prototype(FILE *out, const func_t *f) {
  fprintf(out, "%s%s%s(", c_result(f), strchr(c_result(f), '*') ? "" : " ", f->call);
  bool first = true;
  if (f->with_state) { fputs("lua_State *L", out); first = false; }
  for (int i = 0; i < f->argc; i++) {
    const arg_t *a = &f->args[i];
    const char *type = a->output ? "char *" : type_named(a->type)->c_type;
    fprintf(out, "%s%s%s%s", first ? "" : ", ", type, strchr(type, '*') ? "" : " ", a->name);
    first = false;
  }
  fprintf(out, "%s);\n", first ? "void" : "");
}

static void emit_check(FILE *out, const arg_t *a) {
  const type_t *t = type_named(a->type);
  int i = a->index;
  const char *def = a->def;
  if (a->output) {
    const char *bracket = strchr(a->output, '[');
    fprintf(out, "  %.*s %s%s = {0};\n", bracket ? (int)(bracket - a->output) : (int)strlen(a->output), a->output, a->name,
            bracket ? bracket : "");
    return;
  }
  if (a->bufsize) { fprintf(out, "  size_t %s = sizeof(%s);\n", a->name, a->bufsize); return; }
  fprintf(out, "  %s%s%s = ", t->c_type, strchr(t->c_type, '*') ? "" : " ", a->name);
  if (t->stack) {
    if (a->optional && t->lua_type)
      fprintf(out, "lua_isnoneornil(L, %d) ? 0 : (luaL_checktype(L, %d, %s), %d);\n", i, i, t->lua_type, i);
    else if (a->optional) fprintf(out, "lua_isnoneornil(L, %d) ? 0 : %d;\n", i, i);
    else if (t->lua_type) fprintf(out, "(luaL_checktype(L, %d, %s), %d);\n", i, t->lua_type, i);
    else fprintf(out, "(luaL_checkany(L, %d), %d);\n", i, i);
  } else if (!strcmp(a->type, "int")) {
    if (a->optional) fprintf(out, "(int)luaL_optinteger(L, %d, %s);\n", i, def ? def : "0");
    else fprintf(out, "(int)luaL_checkinteger(L, %d);\n", i);
  } else if (!strcmp(a->type, "number")) {
    if (a->optional) fprintf(out, "luaL_optnumber(L, %d, %s);\n", i, def ? def : "0");
    else fprintf(out, "luaL_checknumber(L, %d);\n", i);
  } else if (!strcmp(a->type, "bool")) {
    if (def) fprintf(out, "lua_isnoneornil(L, %d) ? %s : lua_toboolean(L, %d);\n", i, def, i);
    else fprintf(out, "lua_toboolean(L, %d);\n", i);
  } else {  // string
    if (a->optional && def) fprintf(out, "luaL_optstring(L, %d, \"%s\");\n", i, def);
    else if (a->optional) fprintf(out, "luaL_optstring(L, %d, NULL);\n", i);
    else fprintf(out, "luaL_checkstring(L, %d);\n", i);
  }
}

static void emit_call(FILE *out, const func_t *f) {
  fprintf(out, "%s(", f->call);
  bool first = true;
  if (f->with_state) { fputs("L", out); first = false; }
  for (int i = 0; i < f->argc; i++) { fprintf(out, "%s%s", first ? "" : ", ", f->args[i].name); first = false; }
  fputs(")", out);
}

static void append(char *out, size_t sz, size_t *n, const char *fmt, ...) {
  if (*n >= sz) return;
  va_list ap;
  va_start(ap, fmt);
  int k = vsnprintf(out + *n, sz - *n, fmt, ap);
  va_end(ap);
  if (k > 0) *n += (size_t)k;
}

// fetch(database, table [, field [, value]])
static void lua_signature(const func_t *f, const char *owner, char *out, size_t sz) {
  size_t n = 0;
  int open = 0;
  bool first = true;
  append(out, sz, &n, "%s%s%s(", owner ? owner : "", owner ? "." : "", f->lua_name);
  for (int i = 0; i < f->argc; i++) {
    const arg_t *a = &f->args[i];
    if (!a->index) continue;
    bool trailing = a->optional;  // only optional arguments follow
    for (int k = i + 1; k < f->argc && trailing; k++) trailing = !f->args[k].index || f->args[k].optional;
    if (trailing) { append(out, sz, &n, first ? "[%s" : " [, %s", a->name); open++; }
    else if (a->optional) append(out, sz, &n, "%s[%s]", first ? "" : ", ", a->name);
    else append(out, sz, &n, "%s%s", first ? "" : ", ", a->name);
    first = false;
  }
  for (; open > 0; open--) append(out, sz, &n, "]");
  append(out, sz, &n, ")");
}

static void emit_c(FILE *out, const module_t *m) {
  char symbol[256], guard[300], signature[512];
  symbol_of(m->name, symbol, sizeof(symbol));
  snprintf(guard, sizeof(guard), "__CGEN_%s_H__", symbol);
  for (char *p = guard; *p; p++) *p = (char)toupper((unsigned char)*p);
  fprintf(out, "// Lua module \"%s\", generated by tools/cgen.c from %s. Do not edit.\n"
               "// Included at the end of the file that implements it.\n"
               "#ifndef %s\n#define %s\n\n#include <stdbool.h>\n#include <stddef.h>\n#include <lua.h>\n#include <lauxlib.h>\n",
          m->name, m->path, guard, guard);
  for (int i = 0; i < m->include_count; i++) fprintf(out, "#include <%s>\n", m->includes[i]);
  fputs("\n", out);
  for (int i = 0; i < m->count; i++) emit_prototype(out, &m->funcs[i]);
  for (int i = 0; i < m->count; i++) {
    const func_t *f = &m->funcs[i];
    lua_signature(f, NULL, signature, sizeof(signature));
    fprintf(out, "\n// %s%s%s\nstatic int cgen_%s_%s(lua_State *L) {\n", signature, f->summary ? ": " : "",
            f->summary ? f->summary : "", symbol, f->name);
    for (int k = 0; k < f->argc; k++) emit_check(out, &f->args[k]);
    if (!f->ret || !strcmp(f->ret, "void")) {
      fputs("  ", out); emit_call(out, f); fputs(";\n  return 0;\n", out);
    } else if (!strcmp(f->ret, "nresults")) {
      fputs("  return ", out); emit_call(out, f); fputs(";\n", out);
    } else {
      const char *push = !strcmp(f->ret, "int") ? "lua_pushinteger" : !strcmp(f->ret, "number") ? "lua_pushnumber"
                       : !strcmp(f->ret, "bool") ? "lua_pushboolean" : "lua_pushstring";
      fprintf(out, "  %s(L, ", push); emit_call(out, f); fputs(");\n  return 1;\n", out);
    }
    fputs("}\n", out);
  }
  if (m->luaopen) fprintf(out, "\nint %s(lua_State *L) {\n", m->luaopen);
  else fprintf(out, "\nint luaopen_%s(lua_State *L) {\n", symbol);
  fputs("  static const luaL_Reg functions[] = {\n", out);
  for (int i = 0; i < m->count; i++)
    fprintf(out, "    { \"%s\", cgen_%s_%s },\n", m->funcs[i].lua_name, symbol, m->funcs[i].name);
  fputs("    { NULL, NULL },\n  };\n", out);
  fputs("  luaL_newlib(L, functions);\n", out);
  if (m->on_luaopen) fprintf(out, "  void %s(lua_State *L);\n  %s(L);\n", m->on_luaopen, m->on_luaopen);
  if (m->after_luaopen) fprintf(out, "  void %s(lua_State *L);\n  %s(L);\n", m->after_luaopen, m->after_luaopen);
  fprintf(out, "  return 1;\n}\n\n#endif // %s\n", guard);
}

// ── Markdown ────────────────────────────────────────────────────────────────

static const char *lua_type_name(const char *type) {
  if (!strcmp(type, "int")) return "integer";
  if (!strcmp(type, "value")) return "any";
  return type;
}

static void emit_markdown(FILE *out, const module_t *m, const char *parent) {
  char signature[512];
  const char *dot = strrchr(m->name, '.');
  const char *local = dot ? dot + 1 : m->name;
  fprintf(out, "---\nlayout: default\ntitle: %s\n%s%s%s---\n\n", m->name, parent ? "parent: " : "", parent ? parent : "", parent ? "\n" : "");
  // Lua tables are written with braces; Jekyll must not read them as Liquid.
  fprintf(out, "{%% raw %%}\n# %s\n\n<!-- Generated by tools/cgen.c from %s. Do not edit. -->\n\n", m->name, m->path);
  fprintf(out, "```lua\nlocal %s = require(\"%s\")\n```\n\n", local, m->name);
  if (m->summary) fprintf(out, "%s\n\n", m->summary);
  if (m->details) fprintf(out, "%s\n\n", m->details);
  for (int i = 0; i < m->count; i++) {
    const func_t *f = &m->funcs[i];
    lua_signature(f, local, signature, sizeof(signature));
    fprintf(out, "## `%s`\n\n", signature);
    if (f->summary) fprintf(out, "%s\n\n", f->summary);
    if (f->details) fprintf(out, "%s\n\n", f->details);
    bool any = false;
    for (int k = 0; k < f->argc; k++) any |= f->args[k].index > 0;
    if (any) {
      fputs("| Argument | Type | |\n|---|---|---|\n", out);
      for (int k = 0; k < f->argc; k++) {
        const arg_t *a = &f->args[k];
        if (!a->index) continue;
        fprintf(out, "| `%s` | %s%s | %s", a->name, lua_type_name(a->type), a->optional ? ", optional" : "", a->doc ? a->doc : "");
        if (a->def) fprintf(out, "%sDefault %s.", a->doc ? " " : "", a->def);
        fputs(" |\n", out);
      }
      fputs("\n", out);
    }
    if (f->ret_doc) fprintf(out, "Returns %s%s\n\n", f->ret_doc, f->ret_doc[strlen(f->ret_doc) - 1] == '.' ? "" : ".");
  }
  fputs("{% endraw %}\n", out);
}

int main(int argc, char **argv) {
  const char *output = NULL, *markdown = NULL, *parent = NULL;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--input") && i + 1 < argc) g_input = argv[++i];
    else if (!strcmp(argv[i], "--output") && i + 1 < argc) output = argv[++i];
    else if (!strcmp(argv[i], "--markdown") && i + 1 < argc) markdown = argv[++i];
    else if (!strcmp(argv[i], "--parent") && i + 1 < argc) parent = argv[++i];
    else {
      fprintf(stderr, "usage: cgen --input module.cgen [--output module_lua.h] [--markdown module.md [--parent title]]\n");
      return 2;
    }
  }
  if (!g_input || (!output && !markdown)) {
    fprintf(stderr, "usage: cgen --input module.cgen [--output module_lua.h] [--markdown module.md [--parent title]]\n");
    return 2;
  }
  xmlDocPtr doc = xmlReadFile(g_input, NULL, XML_PARSE_NONET);
  if (!doc) { fprintf(stderr, "cgen: %s: cannot be read\n", g_input); return 3; }
  static module_t m;
  m.path = g_input;
  parse_module(&m, xmlDocGetRootElement(doc));
  const char *paths[2] = { output, markdown };
  for (int k = 0; k < 2; k++) {
    if (!paths[k]) continue;
    FILE *out = fopen(paths[k], "w");
    if (!out) { fprintf(stderr, "cgen: %s: cannot be written\n", paths[k]); return 7; }
    if (k == 0) emit_c(out, &m); else emit_markdown(out, &m, parent);
    if (fclose(out)) { fprintf(stderr, "cgen: %s: cannot be written\n", paths[k]); return 7; }
  }
  xmlFreeDoc(doc);
  return 0;
}
