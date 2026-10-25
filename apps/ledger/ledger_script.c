// ledger_script — the configuration's Lua rules and plugins, so that a change
// in the law is a change to <configuration>/rules.lua, not to C.
//
// Loaded in order into one Lua state per configuration:
//   <config_dir>/rules.lua         the configuration's own rules
//   <config_dir>/plugins/*.lua     plugins shipped with the configuration
//   <data_dir>/plugins/*.lua       the organization's own plugins
//
// What a script can define (each is optional):
//   function <name>(ctx)            posting of a document type whose doc_types
//                                   row has script="<name>" (1C: ОбработкаПроведения);
//                                   ctx.document, ctx.type; entries via ledger.entry
//   function <name>(ctx)            a payroll tax whose payroll_taxes row has
//                                   script="<name>": returns the amount; ctx.tax,
//                                   ctx.line, ctx.employee, ctx.document, ctx.ytd,
//                                   ctx.accruals, ctx.withheld, ctx.taxes[code]
//   function <name>(ctx)            a closing step: closing_rules kind="script" script="<name>"
//   ledger.on("post", fn)           fn(ctx) after any document posts (plugins)
//
// The ledger module:
//   ledger.find(db, table, id)              -> record table or nil      ("ib", "documents", 5)
//   ledger.fetch(db, table [, field, value]) -> array of record tables
//   ledger.field(db, table, id, path)        -> text of a field or join path ("counterparty.name")
//   ledger.account(code)                     -> cfg.accounts record or nil
//   ledger.entry{debit=, credit=, amount=, quantity=, content=, counterparty=, item=,
//                employee=, asset=, currency=, currency_amount=}   accounts by code or id
//   ledger.turnover(account [, subconto [, currency]]) -> {dr, cr, dr_qty, cr_qty, dr_cur, cr_cur}
//   ledger.rate(currency_id, date)           -> units of national currency, 0 when unknown
//   ledger.round2(x), ledger.log(text)
//
// Scripts read the databases and write entries; records are copies, so a
// script cannot corrupt the infobase. Errors are logged and the hook fails.

#include "ledger.h"
#include <platform/platform.h>
#include <math.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

static lua_State *g_lua;
static const ledger_post_t *g_post;  // the posting a hook runs for
static int g_handlers;               // registry ref: event name -> array of functions

static database_t *db_named(lua_State *L, int arg) {
  const char *name = luaL_checkstring(L, arg);
  database_t *db = get_database_by_name(name);
  if (!db) luaL_error(L, "unknown database '%s'", name);
  return db;
}

static const db_table_schema_t *table_named(lua_State *L, database_t *db, int arg) {
  const char *name = luaL_checkstring(L, arg);
  const db_table_schema_t *t = db_schema_table_named(db->schema, name);
  if (!t) luaL_error(L, "unknown table %s.%s", db->name, name);
  return t;
}

// A record as a Lua table of its fields, by the schema's reflection metadata.
static void push_record(lua_State *L, const db_table_schema_t *t, const void *record) {
  if (!record) { lua_pushnil(L); return; }
  lua_createtable(L, 0, t->meta_count);
  for (int i = 0; i < t->meta_count; i++) {
    const db_field_meta_t *f = &t->meta[i];
    const char *p = (const char *)record + f->offset;
    switch (f->type) {
      case DB_TYPE_INT:    lua_pushinteger(L, *(const int *)p); break;
      case DB_TYPE_BOOL:   lua_pushboolean(L, *(const bool *)p); break;
      case DB_TYPE_FLOAT:  lua_pushnumber(L, *(const float *)p); break;
      case DB_TYPE_DOUBLE: lua_pushnumber(L, *(const double *)p); break;
      default:             lua_pushstring(L, p); break;
    }
    lua_setfield(L, -2, f->name);
  }
}

static int l_find(lua_State *L) {
  database_t *db = db_named(L, 1);
  const db_table_schema_t *t = table_named(L, db, 2);
  const void *record = (const void *)send_db_message(db, dbFind, MAKEDWORD(t->table_id, 0), (void *)(intptr_t)luaL_checkinteger(L, 3));
  push_record(L, t, record);
  return 1;
}

static int l_fetch(lua_State *L) {
  database_t *db = db_named(L, 1);
  const db_table_schema_t *t = table_named(L, db, 2);
  int field = 0;
  void *value = NULL;
  if (!lua_isnoneornil(L, 3)) {
    field = db_schema_field_index(t, luaL_checkstring(L, 3));
    if (field < 0) return luaL_error(L, "unknown field %s.%s", t->name, lua_tostring(L, 3));
    value = lua_isnumber(L, 4) ? (void *)(intptr_t)lua_tointeger(L, 4) : (void *)luaL_checkstring(L, 4);
  }
  result_node_t *rows = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(t->table_id, field), value);
  lua_newtable(L);
  int n = 0;
  for (result_node_t *r = rows; r; r = r->next) { push_record(L, t, *(void **)r->data); lua_rawseti(L, -2, ++n); }
  free_result_list(rows);
  return 1;
}

static int l_field(lua_State *L) {
  database_t *db = db_named(L, 1);
  const db_table_schema_t *t = table_named(L, db, 2);
  const void *record = (const void *)send_db_message(db, dbFind, MAKEDWORD(t->table_id, 0), (void *)(intptr_t)luaL_checkinteger(L, 3));
  char text[512] = "";
  if (record) db_get_field_text(db, t->table_id, record, luaL_checkstring(L, 4), text, sizeof(text));
  lua_pushstring(L, text);
  return 1;
}

static int account_id(lua_State *L, int arg) {
  if (lua_isnoneornil(L, arg)) return 0;
  if (lua_isinteger(L, arg)) return (int)lua_tointeger(L, arg);
  database_t *cfg = g_post ? g_post->cfg : get_database_by_name("cfg");
  const db_account_t *a = (const db_account_t *)send_db_message(cfg, dbFind,
    MAKEDWORD(TABLE_ACCOUNTS, ledger_field(cfg, TABLE_ACCOUNTS, "code")), (void *)luaL_checkstring(L, arg));
  if (!a) luaL_error(L, "unknown account '%s'", lua_tostring(L, arg));
  return a->id;
}

static int l_account(lua_State *L) {
  database_t *cfg = g_post ? g_post->cfg : get_database_by_name("cfg");
  const db_account_t *a = (const db_account_t *)send_db_message(cfg, dbFind,
    MAKEDWORD(TABLE_ACCOUNTS, ledger_field(cfg, TABLE_ACCOUNTS, "code")), (void *)luaL_checkstring(L, 1));
  push_record(L, db_schema_table(cfg->schema, TABLE_ACCOUNTS), a);
  return 1;
}

static double opt_number(lua_State *L, int t, const char *key, double dflt) {
  lua_getfield(L, t, key);
  double v = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : dflt;
  lua_pop(L, 1);
  return v;
}

// ledger.entry{...}: an entry of the document being posted.
static int l_entry(lua_State *L) {
  if (!g_post) return luaL_error(L, "ledger.entry outside a posting");
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_getfield(L, 1, "debit");  int debit = account_id(L, -1);  lua_pop(L, 1);
  lua_getfield(L, 1, "credit"); int credit = account_id(L, -1); lua_pop(L, 1);
  ledger_subjects_t s = { (int)opt_number(L, 1, "counterparty", g_post->doc->counterparty_id), (int)opt_number(L, 1, "item", 0),
                          (int)opt_number(L, 1, "employee", 0), (int)opt_number(L, 1, "asset", 0) };
  lua_getfield(L, 1, "content");
  const char *content = lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL;
  ledger_add_entry(g_post, debit, credit, opt_number(L, 1, "amount", 0), &s, opt_number(L, 1, "quantity", 0),
                   (int)opt_number(L, 1, "currency", 0), opt_number(L, 1, "currency_amount", 0), content);
  lua_pop(L, 1);
  return 0;
}

static int l_turnover(lua_State *L) {
  if (!g_post) return luaL_error(L, "ledger.turnover outside a posting");
  ledger_turnover_t t = ledger_turnover(g_post, account_id(L, 1), (int)luaL_optinteger(L, 2, -1), (int)luaL_optinteger(L, 3, -1));
  lua_createtable(L, 0, 6);
  lua_pushnumber(L, t.dr); lua_setfield(L, -2, "dr");         lua_pushnumber(L, t.cr); lua_setfield(L, -2, "cr");
  lua_pushnumber(L, t.dr_qty); lua_setfield(L, -2, "dr_qty"); lua_pushnumber(L, t.cr_qty); lua_setfield(L, -2, "cr_qty");
  lua_pushnumber(L, t.dr_cur); lua_setfield(L, -2, "dr_cur"); lua_pushnumber(L, t.cr_cur); lua_setfield(L, -2, "cr_cur");
  return 1;
}

static int l_rate(lua_State *L) {
  database_t *ib = g_post ? g_post->ib : get_database_by_name("ib");
  lua_pushnumber(L, ledger_rate_on(ib, (int)luaL_checkinteger(L, 1), luaL_checkstring(L, 2)));
  return 1;
}

static int l_round2(lua_State *L) {
  double v = luaL_checknumber(L, 1);
  lua_pushnumber(L, v < 0 ? -floor(-v * 100.0 + 0.5) / 100.0 : floor(v * 100.0 + 0.5) / 100.0);
  return 1;
}

static int l_log(lua_State *L) {
  fprintf(stderr, "[ledger] script: %s\n", luaL_checkstring(L, 1));
  return 0;
}

// ledger.on(event, fn): plugins subscribe to "post".
static int l_on(lua_State *L) {
  const char *event = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_rawgeti(L, LUA_REGISTRYINDEX, g_handlers);
  lua_getfield(L, -1, event);
  if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_newtable(L); lua_pushvalue(L, -1); lua_setfield(L, -3, event); }
  lua_pushvalue(L, 2);
  lua_rawseti(L, -2, (lua_Integer)lua_rawlen(L, -2) + 1);
  lua_pop(L, 2);
  return 0;
}

static const luaL_Reg kLedgerApi[] = {
  { "find", l_find }, { "fetch", l_fetch }, { "field", l_field }, { "account", l_account }, { "entry", l_entry },
  { "turnover", l_turnover }, { "rate", l_rate }, { "round2", l_round2 }, { "log", l_log }, { "on", l_on }, { NULL, NULL },
};

static void run_file(const char *path) {
  if (luaL_dofile(g_lua, path) != LUA_OK) {
    fprintf(stderr, "[ledger] script %s: %s\n", path, lua_tostring(g_lua, -1));
    lua_pop(g_lua, 1);
  }
}

typedef struct { char (*names)[256]; int count, cap; } plugin_list_t;

static bool_t collect_lua(AXdirent const *entry, void *userdata) {
  plugin_list_t *list = userdata;
  const char *ext = strrchr(entry->name, '.');
  if (entry->is_directory || !ext || strcmp(ext, ".lua")) return TRUE;
  if (list->count == list->cap) {
    int cap = list->cap ? list->cap * 2 : 16;
    void *grown = realloc(list->names, (size_t)cap * sizeof(*list->names));
    if (!grown) return FALSE;
    list->names = grown; list->cap = cap;
  }
  snprintf(list->names[list->count++], sizeof(*list->names), "%s", entry->name);
  return TRUE;
}

static int by_name(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

// Plugins load in name order, so a configuration can number them.
static void run_plugins(const char *dir) {
  plugin_list_t list = {0};
  axListDir(dir, collect_lua, &list);
  if (list.count) qsort(list.names, (size_t)list.count, sizeof(*list.names), by_name);
  for (int i = 0; i < list.count; i++) {
    char path[LEDGER_PATH_MAX * 2];
    snprintf(path, sizeof(path), "%s/%s", dir, list.names[i]);
    run_file(path);
  }
  free(list.names);
}

bool ledger_script_open(const char *config_dir, const char *data_dir) {
  ledger_script_close();
  g_lua = luaL_newstate();
  if (!g_lua) { fprintf(stderr, "[ledger] script: cannot create the Lua state\n"); return false; }
  luaL_openlibs(g_lua);
  luaL_newlib(g_lua, kLedgerApi);
  lua_setglobal(g_lua, "ledger");
  lua_newtable(g_lua);
  g_handlers = luaL_ref(g_lua, LUA_REGISTRYINDEX);
  char path[LEDGER_PATH_MAX * 2];
  snprintf(path, sizeof(path), "%s/rules.lua", config_dir);
  if (axPathExists(path)) run_file(path);
  snprintf(path, sizeof(path), "%s/plugins", config_dir);
  if (axPathExists(path)) run_plugins(path);
  if (data_dir && data_dir[0]) {
    snprintf(path, sizeof(path), "%s/plugins", data_dir);
    if (axPathExists(path)) run_plugins(path);
  }
  return true;
}

void ledger_script_close(void) {
  if (g_lua) lua_close(g_lua);
  g_lua = NULL;
  g_post = NULL;
}

bool ledger_script_has(const char *function) {
  if (!g_lua || !function || !function[0]) return false;
  lua_getglobal(g_lua, function);
  bool has = lua_isfunction(g_lua, -1);
  lua_pop(g_lua, 1);
  return has;
}

// Pushes the common context: the document being posted and its type.
static void push_context(const ledger_post_t *p) {
  lua_State *L = g_lua;
  lua_newtable(L);
  push_record(L, db_schema_table(p->ib->schema, TABLE_DOCUMENTS), p->doc);   lua_setfield(L, -2, "document");
  push_record(L, db_schema_table(p->cfg->schema, TABLE_DOC_TYPES), p->type); lua_setfield(L, -2, "type");
  lua_pushnumber(L, p->rate); lua_setfield(L, -2, "rate");
}

// Calls function(ctx) with the context on top of the stack; nresults results stay.
static bool call(const ledger_post_t *p, const char *function, int nresults) {
  lua_State *L = g_lua;
  lua_getglobal(L, function);
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 2);
    fprintf(stderr, "[ledger] script: no function '%s' in the configuration's rules\n", function);
    return false;
  }
  lua_insert(L, -2);  // function below the context
  const ledger_post_t *outer = g_post;
  g_post = p;
  bool ok = lua_pcall(L, 1, nresults, 0) == LUA_OK;
  g_post = outer;
  if (!ok) {
    fprintf(stderr, "[ledger] script %s: %s\n", function, lua_tostring(L, -1));
    lua_pop(L, 1);
  }
  return ok;
}

bool ledger_script_post(const ledger_post_t *p, const char *function) {
  if (!g_lua) { fprintf(stderr, "[ledger] script: document type '%s' needs rules.lua, none loaded\n", p->type->code); return false; }
  push_context(p);
  return call(p, function, 0);
}

bool ledger_script_closing(const ledger_post_t *p, const db_closing_rule_t *rule) {
  if (!g_lua || !rule->script[0]) { fprintf(stderr, "[ledger] closing rule %d: kind=script needs script= and rules.lua\n", rule->id); return false; }
  push_context(p);
  push_record(g_lua, db_schema_table(p->cfg->schema, TABLE_CLOSING_RULES), rule);
  lua_setfield(g_lua, -2, "rule");
  return call(p, rule->script, 0);
}

bool ledger_script_tax(const ledger_post_t *p, const db_payroll_tax_t *tax, const db_payroll_line_t *line, double ytd, int accruals,
                       double withheld, const ledger_tax_amount_t *so_far, int so_far_count, double *amount) {
  *amount = 0;
  if (!g_lua) { fprintf(stderr, "[ledger] payroll tax '%s' needs rules.lua, none loaded\n", tax->code); return false; }
  lua_State *L = g_lua;
  push_context(p);
  push_record(L, db_schema_table(p->cfg->schema, TABLE_PAYROLL_TAXES), tax);  lua_setfield(L, -2, "tax");
  push_record(L, db_schema_table(p->ib->schema, TABLE_PAYROLL_LINES), line);  lua_setfield(L, -2, "line");
  const void *emp = (const void *)send_db_message(p->ib, dbFind, MAKEDWORD(TABLE_EMPLOYEES, 0), (void *)(intptr_t)line->employee_id);
  push_record(L, db_schema_table(p->ib->schema, TABLE_EMPLOYEES), emp);       lua_setfield(L, -2, "employee");
  lua_pushnumber(L, ytd);       lua_setfield(L, -2, "ytd");
  lua_pushinteger(L, accruals); lua_setfield(L, -2, "accruals");
  lua_pushnumber(L, withheld);  lua_setfield(L, -2, "withheld");
  lua_newtable(L);  // the taxes computed before this one, by code
  for (int i = 0; i < so_far_count; i++) { lua_pushnumber(L, so_far[i].amount); lua_setfield(L, -2, so_far[i].tax->code); }
  lua_setfield(L, -2, "taxes");
  if (!call(p, tax->script, 1)) return false;
  *amount = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 0;
  lua_pop(L, 1);
  return true;
}

void ledger_script_event(const char *event, const ledger_post_t *p) {
  if (!g_lua) return;
  lua_State *L = g_lua;
  lua_rawgeti(L, LUA_REGISTRYINDEX, g_handlers);
  lua_getfield(L, -1, event);
  if (!lua_istable(L, -1)) { lua_pop(L, 2); return; }
  int n = (int)lua_rawlen(L, -1);
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(L, -1, i);
    push_context(p);
    const ledger_post_t *outer = g_post;
    g_post = p;
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) { fprintf(stderr, "[ledger] script on %s: %s\n", event, lua_tostring(L, -1)); lua_pop(L, 1); }
    g_post = outer;
  }
  lua_pop(L, 2);
}
