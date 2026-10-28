// ledger_script — the configuration's Lua rules and plugins, so that a change
// in the law is a change to <configuration>/rules.lua, not to C.
//
// Loaded in order into one Lua state per configuration:
//   <config_dir>/rules.lua         the configuration's own rules
//   <share>/stdplugs/*.lua       standard Lua features
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
#include <limits.h>
#include <float.h>

static lua_State *g_lua;
static const ledger_post_t *g_post;  // the posting a hook runs for
static int g_handlers;               // registry ref: event name -> array of functions
static bool g_script_writable;
static bool g_script_running;
static int g_script_transactions;
static int g_executors;
static bool g_executor_active;
static bool g_readonly_hook;
static char g_hook_error[512];
int ledger_open_executor(lua_State *L);

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
    if (t->meta[field].type == DB_TYPE_INT) {
      luaL_argcheck(L, lua_isinteger(L, 4), 4, "integer filter expected");
      value = (void *)(intptr_t)lua_tointeger(L, 4);
    } else if (t->meta[field].type == DB_TYPE_STRING || t->meta[field].type == DB_TYPE_DATE) {
      luaL_checktype(L, 4, LUA_TSTRING);
      value = (void *)lua_tostring(L, 4);
    } else return luaL_error(L, "filter supports integer, string and date fields only");
  } else if (!lua_isnoneornil(L, 4)) {
    return luaL_error(L, "filter value requires a field");
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
  if (!g_post || g_executor_active || g_readonly_hook) return luaL_error(L, "ledger.entry outside a posting");
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

static bool automation_table(const char *name) {
  return !strcmp(name, "items") || !strcmp(name, "counterparties") || !strcmp(name, "rates");
}

static const char *lua_field_type(db_field_type_t type) {
  switch (type) {
    case DB_TYPE_INT: return "integer";
    case DB_TYPE_BOOL: return "boolean";
    case DB_TYPE_FLOAT: case DB_TYPE_DOUBLE: return "number";
    default: return "string";
  }
}

static int l_schema(lua_State *L) {
  database_t *db = db_named(L, 1);
  const db_table_schema_t *t = table_named(L, db, 2);
  lua_newtable(L);
  lua_pushstring(L, t->name); lua_setfield(L, -2, "name");
  lua_pushboolean(L, !strcmp(db->name, "ib") && automation_table(t->name)); lua_setfield(L, -2, "writable");
  lua_newtable(L);
  for (int i = 0; i < t->meta_count; i++) {
    const db_field_meta_t *f = &t->meta[i];
    lua_newtable(L);
    lua_pushstring(L, f->name); lua_setfield(L, -2, "name");
    lua_pushstring(L, lua_field_type(f->type)); lua_setfield(L, -2, "type");
    lua_pushinteger(L, f->length); lua_setfield(L, -2, "length");
    if (i < t->field_count && t->fields[i].relation_table) {
      lua_pushstring(L, t->fields[i].relation_table); lua_setfield(L, -2, "relation");
    }
    lua_rawseti(L, -2, i + 1);
  }
  lua_setfield(L, -2, "fields");
  return 1;
}

static bool script_iso_date(const char *s) {
  if (!s[0]) return true;
  int y, m, d, used = 0;
  if (strlen(s) != 10 || s[4] != '-' || s[7] != '-' || sscanf(s, "%4d-%2d-%2d%n", &y, &m, &d, &used) != 3 || used != 10) return false;
  for (int i = 0; i < 10; i++) if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9')) return false;
  static const int days[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
  return y > 0 && m > 0 && m <= 12 && d > 0 && d <= days[m] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
}

static void script_relation(lua_State *L, database_t *db, const db_table_schema_t *t, int field, int id) {
  if (!id || field >= t->field_count || !t->fields[field].relation_table) return;
  const char *relation = t->fields[field].relation_table, *dot = strchr(relation, '.');
  database_t *foreign_db = db;
  const char *table = relation;
  if (dot) {
    char name[64];
    snprintf(name, sizeof(name), "%.*s", (int)(dot - relation), relation);
    foreign_db = get_database_by_name(name);
    table = dot + 1;
  }
  const db_table_schema_t *foreign = foreign_db ? db_schema_table_named(foreign_db->schema, table) : NULL;
  if (!foreign || !send_db_message(foreign_db, dbFind, MAKEDWORD(foreign->table_id, 0), (void *)(intptr_t)id))
    luaL_error(L, "missing reference %s.%s=%d", t->name, t->meta[field].name, id);
}

static bool executor_field_allowed(const char *allowed, const char *field) {
  for (const char *p = allowed; p && *p; ) {
    size_t n = strcspn(p, ",");
    if (strlen(field) == n && !strncmp(p, field, n)) return true;
    p += n;
    if (*p) p++;
  }
  return false;
}

static int script_read_fields(lua_State *L, database_t *db, const db_table_schema_t *t,
                               int fields, void *record, const char *allowed) {
  const char *name = t->name;
  fields = lua_absindex(L, fields);
  luaL_checktype(L, fields, LUA_TTABLE);
  lua_pushnil(L);
  while (lua_next(L, fields)) {
    if (lua_type(L, -2) != LUA_TSTRING) return luaL_error(L, "field names must be strings");
    const char *key = lua_tostring(L, -2);
    int index = db_schema_field_index(t, key);
    if (index < 0 || !strcmp(key, "id") || (allowed && !executor_field_allowed(allowed, key))) return luaL_error(L, "unknown or protected field %s.%s", name, key);
    const db_field_meta_t *f = &t->meta[index];
    char *p = (char *)record + f->offset;
    switch (f->type) {
      case DB_TYPE_INT: {
        if (!lua_isinteger(L, -1)) return luaL_error(L, "%s requires an integer", key);
        lua_Integer v = lua_tointeger(L, -1);
        if (v < 0 || v > INT_MAX) return luaL_error(L, "%s integer out of range", key);
        script_relation(L, db, t, index, (int)v);
        *(int *)p = (int)v;
        break;
      }
      case DB_TYPE_BOOL:
        if (!lua_isboolean(L, -1)) return luaL_error(L, "%s requires a boolean", key);
        *(bool *)p = lua_toboolean(L, -1); break;
      case DB_TYPE_FLOAT: case DB_TYPE_DOUBLE: {
        if (lua_type(L, -1) != LUA_TNUMBER) return luaL_error(L, "%s requires a number", key);
        double v = lua_tonumber(L, -1);
        if (!isfinite(v) || fabs(v) > (f->type == DB_TYPE_FLOAT ? FLT_MAX : DBL_MAX)) return luaL_error(L, "%s number out of range", key);
        if (f->type == DB_TYPE_FLOAT) *(float *)p = (float)v; else *(double *)p = v;
        break;
      }
      default: {
        if (lua_type(L, -1) != LUA_TSTRING) return luaL_error(L, "%s requires a string", key);
        size_t len;
        const char *s = lua_tolstring(L, -1, &len);
        if (f->length <= 0 || len >= (size_t)f->length || strlen(s) != len) return luaL_error(L, "%s string too long or contains NUL", key);
        if (f->type == DB_TYPE_DATE && !script_iso_date(s)) return luaL_error(L, "%s requires an ISO date", key);
        memcpy(p, s, len + 1);
      }
    }
    lua_pop(L, 1);
  }
  return 0;
}

static int script_write(lua_State *L, bool insert, bool validate) {
  if (!validate && (!g_script_writable || !g_script_transactions || g_post))
    return luaL_error(L, "catalog writes require a writable script and ledger.transaction, outside posting");
  const char *name = luaL_checkstring(L, 1);
  if (!automation_table(name)) return luaL_error(L, "table '%s' is not an automation write target", name);
  database_t *db = get_database_by_name("ib");
  if (!db) return luaL_error(L, "infobase unavailable");
  const db_table_schema_t *t = db_schema_table_named(db->schema, name);
  if (!t) return luaL_error(L, "unknown table '%s'", name);
  int fields = validate || insert ? 2 : 3;
  luaL_checktype(L, fields, LUA_TTABLE);
  lua_Integer requested_id = insert ? 0 : luaL_checkinteger(L, validate ? 3 : 2);
  if (requested_id < 0 || requested_id > INT_MAX) return luaL_error(L, "record ID out of range");
  int id = (int)requested_id;
  const void *existing = insert ? NULL : (const void *)send_db_message(db, dbFind, MAKEDWORD(t->table_id, 0), (void *)(intptr_t)id);
  if (!insert && !existing) return luaL_error(L, "missing record %s:%d", name, id);
  void *record = lua_newuserdatauv(L, t->record_size, 0);
  memset(record, 0, t->record_size);
  if (existing) memcpy(record, existing, t->record_size);
  else send_db_message(db, dbInitRecord, t->table_id, record);
  script_read_fields(L, db, t, fields, record, NULL);
  if (!strcmp(name, "items") && ((db_item_t *)record)->price < 0) return luaL_error(L, "price must be nonnegative");
  if (!strcmp(name, "rates")) {
    const db_rate_t *r = record;
    if (r->rate <= 0 || !r->date[0] || r->currency_id <= 0) return luaL_error(L, "rate requires a currency, date and positive rate");
  }
  if (validate) { push_record(L, t, record); return 1; }
  lresult_t result = send_db_message(db, insert ? dbInsert : dbUpdate, t->table_id, record);
  if (!result) return luaL_error(L, "cannot write %s", name);
  push_record(L, t, insert ? (const void *)result : record);
  return 1;
}

static int l_insert(lua_State *L) { return script_write(L, true, false); }
static int l_update(lua_State *L) { return script_write(L, false, false); }

static int l_validate(lua_State *L) { return script_write(L, lua_isnoneornil(L, 3), true); }

static int l_transaction(lua_State *L) {
  if (!g_script_writable || g_post) return luaL_error(L, "transaction requires a writable script outside posting");
  database_t *db = get_database_by_name("ib");
  if (!db || !send_db_message(db, dbBeginChanges, 0, NULL)) return luaL_error(L, "cannot begin transaction");
  g_script_transactions++;
  lua_pushvalue(L, 1);
  int status = lua_pcall(L, 0, LUA_MULTRET, 0);
  g_script_transactions--;
  if (status != LUA_OK) {
    send_db_message(db, dbRevertChanges, 0, NULL);
    return lua_error(L);
  }
  if (!send_db_message(db, dbCommitChanges, 0, NULL)) {
    send_db_message(db, dbRevertChanges, 0, NULL);
    return luaL_error(L, "cannot commit transaction");
  }
  return lua_gettop(L) - 1;
}

#include "build/generated/apps/ledger/ledger_api.h"

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
  luaL_requiref(g_lua, "ledger.executor", ledger_open_executor, 0);
  lua_setfield(g_lua, -2, "executor");
  lua_setglobal(g_lua, "ledger");
  lua_newtable(g_lua);
  g_executors = luaL_ref(g_lua, LUA_REGISTRYINDEX);
  lua_newtable(g_lua);
  g_handlers = luaL_ref(g_lua, LUA_REGISTRYINDEX);
  char path[LEDGER_PATH_MAX * 2];
  snprintf(path, sizeof(path), "%s/rules.lua", config_dir);
  if (axPathExists(path)) run_file(path);
  // Standard and custom plugins use the same deterministic Lua loader.
  snprintf(path, sizeof(path), "%s/../../stdplugs", config_dir);
  if (axPathExists(path)) run_plugins(path);
  snprintf(path, sizeof(path), "%s/plugins", config_dir);
  if (axPathExists(path)) run_plugins(path);
  if (data_dir && data_dir[0]) {
    snprintf(path, sizeof(path), "%s/plugins", data_dir);
    if (axPathExists(path)) run_plugins(path);
  }
  return true;
}

bool ledger_script_run(const char *path, bool writable, char *error, size_t error_sz) {
  if (error && error_sz) error[0] = '\0';
  if (!g_lua || g_post || g_script_running || g_script_transactions) {
    if (error && error_sz) snprintf(error, error_sz, "script runtime unavailable or busy");
    return false;
  }
  database_t *db = get_database_by_name("ib");
  if (!db || !send_db_message(db, dbBeginChanges, 0, NULL)) return false;
  int top = lua_gettop(g_lua);
  g_script_writable = writable;
  g_script_running = true;
  bool ok = luaL_dofile(g_lua, path) == LUA_OK;
  g_script_writable = false;
  g_script_running = false;
  if (!ok) {
    if (error && error_sz) snprintf(error, error_sz, "%s", lua_tostring(g_lua, -1));
    send_db_message(db, dbRevertChanges, 0, NULL);
  } else if (!send_db_message(db, writable ? dbCommitChanges : dbRevertChanges, 0, NULL)) {
    if (error && error_sz) snprintf(error, error_sz, "cannot finish script transaction");
    ok = false;
  }
  lua_settop(g_lua, top);
  return ok;
}

void ledger_script_close(void) {
  if (g_lua) lua_close(g_lua);
  g_lua = NULL;
  g_post = NULL;
  g_executor_active = false;
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
    snprintf(g_hook_error, sizeof(g_hook_error), "Validation function '%s' was not found.", function);
    fprintf(stderr, "[ledger] script: no function '%s' in the configuration's rules\n", function);
    return false;
  }
  lua_insert(L, -2);  // function below the context
  const ledger_post_t *outer = g_post;
  g_post = p;
  bool ok = lua_pcall(L, 1, nresults, 0) == LUA_OK;
  g_post = outer;
  if (!ok) {
    snprintf(g_hook_error, sizeof(g_hook_error), "%s", lua_tostring(L, -1));
    fprintf(stderr, "[ledger] script %s: %s\n", function, g_hook_error);
    lua_pop(L, 1);
  }
  return ok;
}

bool ledger_script_post(const ledger_post_t *p, const char *function) {
  if (!g_lua) { fprintf(stderr, "[ledger] script: document type '%s' needs rules.lua, none loaded\n", p->type->code); return false; }
  push_context(p);
  return call(p, function, 0);
}

bool ledger_script_print(const ledger_post_t *p, const db_print_form_t *form, char *error, size_t error_sz) {
  if (!form->validate_script[0]) return true;
  if (!g_lua) {
    snprintf(error, error_sz, "%s", ui_tr("Print validation requires the configuration's script runtime."));
    return false;
  }
  push_context(p);
  push_record(g_lua, db_schema_table(p->cfg->schema, TABLE_PRINT_FORMS), form);
  lua_setfield(g_lua, -2, "print_form");
  bool outer = g_readonly_hook;
  g_readonly_hook = true;
  bool ok = call(p, form->validate_script, 0);
  g_readonly_hook = outer;
  if (!ok) snprintf(error, error_sz, "%s", g_hook_error);
  return ok;
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
  bool valid = lua_type(L, -1) == LUA_TNUMBER && isfinite(lua_tonumber(L, -1));
  if (valid) *amount = lua_tonumber(L, -1);
  else fprintf(stderr, "[ledger] payroll tax '%s' must return a finite number\n", tax->code);
  lua_pop(L, 1);
  return valid;
}

bool ledger_script_event(const char *event, const ledger_post_t *p) {
  if (!g_lua) return true;
  lua_State *L = g_lua;
  lua_rawgeti(L, LUA_REGISTRYINDEX, g_handlers);
  lua_getfield(L, -1, event);
  if (!lua_istable(L, -1)) { lua_pop(L, 2); return true; }
  int n = (int)lua_rawlen(L, -1);
  for (int i = 1; i <= n; i++) {
    lua_rawgeti(L, -1, i);
    push_context(p);
    const ledger_post_t *outer = g_post;
    g_post = p;
    bool ok = lua_pcall(L, 1, 0, 0) == LUA_OK;
    g_post = outer;
    if (!ok) {
      fprintf(stderr, "[ledger] script on %s: %s\n", event, lua_tostring(L, -1));
      lua_pop(L, 3);
      return false;
    }
  }
  lua_pop(L, 2);
  return true;
}

// The host applies capability-limited plans; workflow policy is a standard plugin.
typedef struct {
  const char *name, *document_fields, *replace_table, *parent_field, *line_fields;
  bool recalculate;
} ledger_executor_spec_t;
#include "build/generated/apps/ledger/executor_contract.h"

static const ledger_executor_spec_t *g_executor_spec;
static bool g_executor_scope;

static const ledger_executor_spec_t *executor_spec(const char *name) {
  for (size_t i = 0; i < ARRAY_LEN(kExecutorSpecs); i++)
    if (!strcmp(kExecutorSpecs[i].name, name)) return &kExecutorSpecs[i];
  return NULL;
}

bool ledger_script_in_executor(void) { return g_executor_active; }

int ledger_exec_register(lua_State *L, const char *operation, int handler) {
  if (!executor_spec(operation)) return luaL_error(L, "unknown executor '%s'", operation);
  if (g_executor_active || g_post || g_script_running) return luaL_error(L, "executor registration is only available during startup");
  lua_rawgeti(L, LUA_REGISTRYINDEX, g_executors);
  lua_getfield(L, -1, operation);
  if (!lua_isnil(L, -1)) return luaL_error(L, "duplicate executor '%s'", operation);
  lua_pop(L, 1);
  lua_pushvalue(L, handler);
  lua_setfield(L, -2, operation);
  lua_pop(L, 1);
  return 0;
}

static void executor_keys(lua_State *L, int table, const char *allowed) {
  table = lua_absindex(L, table);
  luaL_checktype(L, table, LUA_TTABLE);
  lua_pushnil(L);
  while (lua_next(L, table)) {
    if (lua_type(L, -2) != LUA_TSTRING || !executor_field_allowed(allowed, lua_tostring(L, -2)))
      luaL_error(L, "unsupported executor plan field");
    lua_pop(L, 1);
  }
}

static void executor_read_record(lua_State *L, const db_table_schema_t *table, int fields, void *record,
                                 const char *allowed, bool required) {
  script_read_fields(L, g_post->ib, table, fields, record, allowed);
  if (!required) return;
  for (const char *p = allowed; p && *p; ) {
    size_t n = strcspn(p, ",");
    char name[128];
    snprintf(name, sizeof(name), "%.*s", (int)n, p);
    lua_getfield(L, fields, name);
    if (lua_isnil(L, -1)) luaL_error(L, "executor result requires '%s'", name);
    lua_pop(L, 1);
    int i = db_schema_field_index(table, name);
    if (i >= 0 && table->fields[i].relation_table && *(const int *)((const char *)record + table->meta[i].offset) <= 0)
      luaL_error(L, "executor result requires a nonempty reference '%s'", name);
    p += n;
    if (*p) p++;
  }
}

static int executor_plan(lua_State *L) {
  const ledger_executor_spec_t *spec = g_executor_spec;
  database_t *ib = g_post->ib;
  lua_rawgeti(L, LUA_REGISTRYINDEX, g_executors);
  lua_getfield(L, -1, spec->name);
  lua_remove(L, -2);
  if (!lua_isfunction(L, -1)) return luaL_error(L, "executor '%s' is not registered", spec->name);
  push_context(g_post);
  lua_call(L, 1, 1);
  executor_keys(L, 1, "document,replace");
  bool changed = false;
  lua_getfield(L, 1, "document");
  if (!lua_isnil(L, -1)) {
    const db_table_schema_t *table = db_schema_table(ib->schema, TABLE_DOCUMENTS);
    void *record = lua_newuserdatauv(L, table->record_size, 0);
    memcpy(record, g_post->doc, table->record_size);
    executor_read_record(L, table, 2, record, spec->document_fields, false);
    lua_pop(L, 1);
    lua_pushnil(L);
    if (lua_next(L, 2)) { changed = true; lua_pop(L, 2); }
  }
  lua_pop(L, 1);
  lua_getfield(L, 1, "replace");
  if (!lua_isnil(L, -1)) executor_keys(L, 2, spec->replace_table);
  const db_table_schema_t *child = spec->replace_table[0] ? db_schema_table_named(ib->schema, spec->replace_table) : NULL;
  int parent = child ? db_schema_field_index(child, spec->parent_field) : -1;
  if (child && !lua_isnil(L, 2)) lua_getfield(L, 2, spec->replace_table); else lua_pushnil(L);
  if (!lua_isnil(L, 3)) {
    luaL_checktype(L, 3, LUA_TTABLE);
    changed = true;
    size_t count = lua_rawlen(L, 3);
    lua_pushnil(L);
    while (lua_next(L, 3)) {
      if (!lua_isinteger(L, -2) || lua_tointeger(L, -2) < 1 || (lua_Unsigned)lua_tointeger(L, -2) > count)
        return luaL_error(L, "executor rows must be a dense array");
      lua_pop(L, 1);
    }
    for (size_t i = 1; i <= count; i++) {
      lua_rawgeti(L, 3, i);
      luaL_checktype(L, 4, LUA_TTABLE);
      void *record = lua_newuserdatauv(L, child->record_size, 0);
      send_db_message(ib, dbInitRecord, child->table_id, record);
      executor_read_record(L, child, 4, record, spec->line_fields, true);
      lua_pop(L, 2);
    }
  }
  lua_settop(L, 1);
  if (!changed) return 1;
  if (!send_db_message(ib, dbBeginChanges, 0, NULL)) return luaL_error(L, "cannot begin executor transaction");
  g_executor_scope = true;
  lua_getfield(L, 1, "document");
  if (!lua_isnil(L, 2)) {
    const db_table_schema_t *table = db_schema_table(ib->schema, TABLE_DOCUMENTS);
    void *record = lua_newuserdatauv(L, table->record_size, 0);
    memcpy(record, g_post->doc, table->record_size);
    executor_read_record(L, table, 2, record, spec->document_fields, false);
    if (!send_db_message(ib, dbUpdate, TABLE_DOCUMENTS, record)) return luaL_error(L, "cannot update executor document");
  }
  lua_settop(L, 1);
  lua_getfield(L, 1, "replace");
  if (child && !lua_isnil(L, 2)) lua_getfield(L, 2, spec->replace_table); else lua_pushnil(L);
  if (!lua_isnil(L, 3)) {
    result_node_t *old = (result_node_t *)send_db_message(ib, dbFetch, MAKEDWORD(child->table_id, parent), (void *)(intptr_t)g_post->doc->id);
    bool ok = true;
    for (result_node_t *n = old; n; n = n->next) {
      const void *row = *(void **)n->data;
      int key_field = db_schema_field_index(child, "id");
      int key = *(const int *)((const char *)row + child->meta[key_field].offset);
      if (!send_db_message(ib, dbDelete, child->table_id, (void *)(intptr_t)key)) { ok = false; break; }
    }
    free_result_list(old);
    if (!ok) return luaL_error(L, "cannot replace executor rows");
    for (size_t i = 1; i <= lua_rawlen(L, 3); i++) {
      lua_rawgeti(L, 3, i);
      void *record = lua_newuserdatauv(L, child->record_size, 0);
      send_db_message(ib, dbInitRecord, child->table_id, record);
      executor_read_record(L, child, 4, record, spec->line_fields, true);
      *(int *)((char *)record + child->meta[parent].offset) = g_post->doc->id;
      if (!send_db_message(ib, dbInsert, child->table_id, record)) return luaL_error(L, "cannot insert executor row");
      lua_pop(L, 2);
    }
  }
  if (spec->recalculate) ledger_recalc_document(ib, g_post->cfg, g_post->doc->id);
  lua_settop(L, 1);
  return 1;
}

static bool executor_call(lua_State *L, const ledger_post_t *p, const char *operation) {
  if (g_executor_active || g_post) { lua_pushliteral(L, "nested executor/posting execution is not allowed"); return false; }
  const ledger_executor_spec_t *spec = executor_spec(operation);
  if (!spec) { lua_pushfstring(L, "unknown executor '%s'", operation); return false; }
  g_executor_spec = spec;
  g_executor_active = true;
  g_executor_scope = false;
  g_post = p;
  lua_pushcfunction(L, executor_plan);
  bool ok = lua_pcall(L, 0, 1, 0) == LUA_OK;
  g_post = NULL;
  g_executor_active = false;
  if (g_executor_scope) {
    if (!send_db_message(p->ib, ok ? dbCommitChanges : dbRevertChanges, 0, NULL)) {
      if (ok) {
        send_db_message(p->ib, dbRevertChanges, 0, NULL);
        lua_pop(L, 1); lua_pushliteral(L, "cannot commit executor transaction");
      }
      ok = false;
    }
  }
  g_executor_scope = false;
  return ok;
}

int ledger_exec_execute(lua_State *L, const char *operation, int document_id) {
  if (!g_script_writable) return luaL_error(L, "executor requires a writable script");
  if (document_id <= 0 || lua_tointeger(L, 2) != document_id) return luaL_error(L, "executor document ID out of range");
  database_t *ib = get_database_by_name("ib"), *cfg = get_database_by_name("cfg");
  db_document_t *doc = ib ? (db_document_t *)send_db_message(ib, dbFind, MAKEDWORD(TABLE_DOCUMENTS, 0), (void *)(intptr_t)document_id) : NULL;
  if (!doc || !cfg) return luaL_error(L, "executor document not found");
  const db_doc_type_t *type = (const db_doc_type_t *)send_db_message(cfg, dbFind, MAKEDWORD(TABLE_DOC_TYPES, 0), (void *)(intptr_t)doc->doc_type_id);
  ledger_post_t p = {ib, cfg, doc, type, doc->rate};
  if (!executor_call(L, &p, operation)) return lua_error(L);
  return 1;
}

bool ledger_execute_document(database_t *ib, database_t *cfg, const char *operation, int document_id) {
  const db_document_t *doc = (const db_document_t *)send_db_message(ib, dbFind, MAKEDWORD(TABLE_DOCUMENTS, 0), (void *)(intptr_t)document_id);
  if (!doc) return false;
  if (!g_lua) { fprintf(stderr, "[ledger] executor '%s': script runtime is not open\n", operation); return false; }
  const db_doc_type_t *type = (const db_doc_type_t *)send_db_message(cfg, dbFind, MAKEDWORD(TABLE_DOC_TYPES, 0), (void *)(intptr_t)doc->doc_type_id);
  ledger_post_t p = {ib, cfg, (db_document_t *)doc, type, doc->rate};
  int top = lua_gettop(g_lua);
  bool ok = executor_call(g_lua, &p, operation);
  if (!ok) { fprintf(stderr, "[ledger] executor '%s': %s\n", operation, lua_tostring(g_lua, -1)); fflush(stderr); }
  lua_settop(g_lua, top);
  return ok;
}

#include "build/generated/apps/ledger/executor_lua.h"
