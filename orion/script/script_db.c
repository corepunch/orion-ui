// "orion.db": the datasource messages C code sends (dbFetch, dbFind, dbInsert,
// dbUpdate, dbDelete, change scopes), for orion.model to build on. Records
// cross as plain tables of field values; the database keeps its rules
// (defaults, numbering, delete rules) as it does for C callers. db.cgen
// declares the module; its bindings are generated (tools/cgen.c).

#include "script.h"
#include "script_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <orion/user/i18n.h>

static const char *const kTypeNames[] = {
  [DB_TYPE_INT] = "integer", [DB_TYPE_STRING] = "string", [DB_TYPE_BOOL] = "boolean",
  [DB_TYPE_FLOAT] = "float", [DB_TYPE_DOUBLE] = "double", [DB_TYPE_DATE] = "date",
};

static database_t *find_database(const char *name) {
  for (int i = 0; ; i++) {
    database_t *db = get_database_at(i);
    if (!db) return NULL;
    if (db->name && !strcmp(db->name, name)) return db;
  }
}

const db_table_schema_t *script_table(lua_State *L, const char *db_name, const char *table, database_t **out) {
  database_t *db = find_database(db_name);
  if (!db) { luaL_error(L, "no database '%s' is open", db_name); return NULL; }
  const db_schema_def_t *schema = db->schema ? db->schema : (const db_schema_def_t *)send_db_message(db, dbGetSchema, 0, NULL);
  const db_table_schema_t *t = schema ? db_schema_table_named(schema, table) : NULL;
  if (!t) { luaL_error(L, "database '%s' has no table '%s'", db_name, table); return NULL; }
  if (out) *out = db;
  return t;
}

static int key_index(const db_table_schema_t *t) {
  for (int i = 0; i < t->field_count; i++) if (t->fields[i].primary_key) return i;
  return 0;
}

// The field named, or the key when name is NULL.
static int field_named(lua_State *L, const db_table_schema_t *t, const char *name) {
  if (!name) return key_index(t);
  for (int i = 0; i < t->meta_count; i++) if (!strcmp(t->meta[i].name, name)) return i;
  return luaL_error(L, "table '%s' has no field '%s'", t->name, name);
}

void script_push_record(lua_State *L, const db_table_schema_t *t, const void *record) {
  lua_createtable(L, 0, t->meta_count);
  for (int i = 0; i < t->meta_count; i++) {
    const db_field_meta_t *m = &t->meta[i];
    const char *p = (const char *)record + m->offset;
    switch (m->type) {
      case DB_TYPE_INT:    lua_pushinteger(L, *(const int *)p); break;
      case DB_TYPE_BOOL:   lua_pushboolean(L, *(const bool *)p); break;
      case DB_TYPE_FLOAT:  lua_pushnumber(L, *(const float *)p); break;
      case DB_TYPE_DOUBLE: lua_pushnumber(L, *(const double *)p); break;
      case DB_TYPE_STRING:
      case DB_TYPE_DATE:   lua_pushlstring(L, p, strnlen(p, (size_t)m->length)); break;
    }
    lua_setfield(L, -2, m->name);
  }
}

static bool iso_date(const char *s) {
  if (!s || strlen(s) != 10 || s[4] != '-' || s[7] != '-') return false;
  for (int i = 0; i < 10; i++) if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9')) return false;
  return true;
}

bool script_read_record(lua_State *L, int idx, const db_table_schema_t *t, void *record, char *msg, size_t msg_sz) {
  idx = lua_absindex(L, idx);
  luaL_checktype(L, idx, LUA_TTABLE);
  lua_pushnil(L);
  while (lua_next(L, idx)) {
    if (lua_type(L, -2) != LUA_TSTRING) { lua_pop(L, 2); luaL_error(L, "record fields are named by strings"); }
    const char *name = lua_tostring(L, -2);
    const db_field_meta_t *m = NULL;
    for (int i = 0; i < t->meta_count && !m; i++) if (!strcmp(t->meta[i].name, name)) m = &t->meta[i];
    if (!m) luaL_error(L, "table '%s' has no field '%s'", t->name, name);
    char *p = (char *)record + m->offset;
    switch (m->type) {
      case DB_TYPE_INT: {
        int ok = 0;
        lua_Integer v = lua_tointegerx(L, -1, &ok);
        if (!ok) luaL_error(L, "field '%s' takes an integer, not %s", name, luaL_typename(L, -1));
        *(int *)p = (int)v;
        break;
      }
      case DB_TYPE_BOOL:
        if (!lua_isboolean(L, -1)) luaL_error(L, "field '%s' takes a boolean, not %s", name, luaL_typename(L, -1));
        *(bool *)p = lua_toboolean(L, -1);
        break;
      case DB_TYPE_FLOAT:
      case DB_TYPE_DOUBLE:
        if (!lua_isnumber(L, -1)) luaL_error(L, "field '%s' takes a number, not %s", name, luaL_typename(L, -1));
        if (m->type == DB_TYPE_FLOAT) *(float *)p = (float)lua_tonumber(L, -1);
        else *(double *)p = (double)lua_tonumber(L, -1);
        break;
      case DB_TYPE_STRING:
      case DB_TYPE_DATE: {
        if (lua_type(L, -1) != LUA_TSTRING) luaL_error(L, "field '%s' takes a string, not %s", name, luaL_typename(L, -1));
        size_t len = 0;
        const char *s = lua_tolstring(L, -1, &len);
        if (m->type == DB_TYPE_DATE && len && !iso_date(s)) {
          snprintf(msg, msg_sz, ui_tr("'%s' is not a date (YYYY-MM-DD)."), s);
          lua_pop(L, 2);
          return false;
        }
        if (len >= (size_t)m->length) {
          snprintf(msg, msg_sz, ui_tr("The field '%s' holds at most %d characters."), name, m->length - 1);
          lua_pop(L, 2);
          return false;
        }
        memcpy(p, s, len + 1);
        break;
      }
    }
    lua_pop(L, 1);
  }
  return true;
}

static void *lookup_value(lua_State *L, const db_field_meta_t *m, int arg) {
  if (m->type == DB_TYPE_STRING || m->type == DB_TYPE_DATE) return (void *)luaL_checkstring(L, arg);
  if (m->type == DB_TYPE_BOOL) return (void *)(intptr_t)lua_toboolean(L, arg);
  return (void *)(intptr_t)luaL_checkinteger(L, arg);
}

static int refuse_with(lua_State *L, const char *message) {
  lua_pushnil(L);
  lua_pushstring(L, message);
  return 2;
}

static int script_db_schema(lua_State *L, const char *database, const char *table) {
  const db_table_schema_t *t = script_table(L, database, table, NULL);
  lua_createtable(L, 0, 4);
  lua_pushstring(L, t->name); lua_setfield(L, -2, "name");
  lua_pushstring(L, t->meta[key_index(t)].name); lua_setfield(L, -2, "key");
  lua_createtable(L, t->field_count, 0);
  for (int i = 0; i < t->field_count; i++) {
    const db_field_schema_t *f = &t->fields[i];
    lua_createtable(L, 0, 6);
    lua_pushstring(L, f->name); lua_setfield(L, -2, "name");
    lua_pushstring(L, kTypeNames[f->type]); lua_setfield(L, -2, "type");
    lua_pushboolean(L, f->primary_key); lua_setfield(L, -2, "key");
    if (f->length) { lua_pushinteger(L, f->length); lua_setfield(L, -2, "length"); }
    if (f->relation_table) {
      lua_pushstring(L, f->relation_table); lua_setfield(L, -2, "relation");
      lua_pushstring(L, f->relation_field); lua_setfield(L, -2, "relationField");
    }
    lua_rawseti(L, -2, i + 1);
  }
  lua_setfield(L, -2, "fields");
  lua_createtable(L, t->join_count, 0);
  for (int i = 0; i < t->join_count; i++) {
    const db_join_schema_t *j = &t->joins[i];
    lua_createtable(L, 0, 4);
    lua_pushstring(L, j->name); lua_setfield(L, -2, "name");
    lua_pushstring(L, j->local_field); lua_setfield(L, -2, "field");
    lua_pushstring(L, j->foreign_table); lua_setfield(L, -2, "table");
    lua_pushstring(L, j->foreign_field); lua_setfield(L, -2, "foreignField");
    lua_rawseti(L, -2, i + 1);
  }
  lua_setfield(L, -2, "joins");
  return 1;
}

static int script_db_fetch(lua_State *L, const char *database, const char *table, const char *field, int value) {
  database_t *db = NULL;
  const db_table_schema_t *t = script_table(L, database, table, &db);
  int f = field ? field_named(L, t, field) : 0;
  if (field && !value) return luaL_error(L, "fetch: field '%s' needs a value to compare", field);
  void *key = field ? lookup_value(L, &t->meta[f], value) : NULL;
  lua_newtable(L);
  // dbFetch reads field 0 as "no filter"; the key filters through dbFind.
  if (field && f == 0) {
    const void *record = (const void *)send_db_message(db, dbFind, MAKEDWORD(t->table_id, 0), key);
    if (record) { script_push_record(L, t, record); lua_rawseti(L, -2, 1); }
    return 1;
  }
  result_node_t *list = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(t->table_id, f), key);
  int n = 0;
  for (result_node_t *node = list; node; node = (result_node_t *)node->next) {
    script_push_record(L, t, *(void **)node->data);
    lua_rawseti(L, -2, ++n);
  }
  free_result_list(list);
  return 1;
}

static int script_db_find(lua_State *L, const char *database, const char *table, const char *field, int value) {
  database_t *db = NULL;
  const db_table_schema_t *t = script_table(L, database, table, &db);
  int f = field_named(L, t, field);
  const void *record = (const void *)send_db_message(db, dbFind, MAKEDWORD(t->table_id, f == key_index(t) ? 0 : f),
                                                     lookup_value(L, &t->meta[f], value));
  if (record) script_push_record(L, t, record);
  else lua_pushnil(L);
  return 1;
}

static int script_db_insert(lua_State *L, const char *database, const char *table, int fields) {
  database_t *db = NULL;
  const db_table_schema_t *t = script_table(L, database, table, &db);
  void *buf = lua_newuserdatauv(L, t->record_size, 0);
  memset(buf, 0, t->record_size);
  send_db_message(db, dbInitRecord, (uint32_t)t->table_id, buf);
  char msg[256];
  if (!script_read_record(L, fields, t, buf, msg, sizeof(msg))) return refuse_with(L, msg);
  const void *record = (const void *)send_db_message(db, dbInsert, (uint32_t)t->table_id, buf);
  if (!record) return refuse_with(L, ui_tr("The record could not be saved."));
  script_push_record(L, t, record);
  return 1;
}

static int script_db_update(lua_State *L, const char *database, const char *table, int key, int fields) {
  database_t *db = NULL;
  const db_table_schema_t *t = script_table(L, database, table, &db);
  const void *current = (const void *)send_db_message(db, dbFind, MAKEDWORD(t->table_id, 0), (void *)(intptr_t)key);
  if (!current) return refuse_with(L, ui_tr("The record no longer exists."));
  void *buf = lua_newuserdatauv(L, t->record_size, 0);
  memcpy(buf, current, t->record_size);
  char msg[256];
  if (!script_read_record(L, fields, t, buf, msg, sizeof(msg))) return refuse_with(L, msg);
  if (!send_db_message(db, dbUpdate, (uint32_t)t->table_id, buf)) return refuse_with(L, ui_tr("The record could not be saved."));
  script_push_record(L, t, (const void *)send_db_message(db, dbFind, MAKEDWORD(t->table_id, 0), (void *)(intptr_t)key));
  return 1;
}

static int script_db_delete(lua_State *L, const char *database, const char *table, int key) {
  database_t *db = NULL;
  const db_table_schema_t *t = script_table(L, database, table, &db);
  if (send_db_message(db, dbCountReferrers, (uint32_t)t->table_id, (void *)(intptr_t)key) > 0)
    return refuse_with(L, ui_tr("The record is in use and cannot be deleted."));
  if (!send_db_message(db, dbDelete, (uint32_t)t->table_id, (void *)(intptr_t)key))
    return refuse_with(L, ui_tr("The record could not be deleted."));
  lua_pushboolean(L, 1);
  return 1;
}

static int script_db_text(lua_State *L, const char *database, const char *table, int key, const char *path) {
  database_t *db = NULL;
  const db_table_schema_t *t = script_table(L, database, table, &db);
  const void *record = (const void *)send_db_message(db, dbFind, MAKEDWORD(t->table_id, 0), (void *)(intptr_t)key);
  char buf[512];
  if (record && db_get_field_text(db, t->table_id, record, path, buf, sizeof(buf))) lua_pushstring(L, buf);
  else lua_pushnil(L);
  return 1;
}

static bool change_scope(lua_State *L, const char *database, uint32_t msg) {
  database_t *db = find_database(database);
  if (!db) return luaL_error(L, "no database '%s' is open", database);
  return send_db_message(db, msg, 0, NULL) != 0;
}

static bool script_db_begin(lua_State *L, const char *database)  { return change_scope(L, database, dbBeginChanges); }
static bool script_db_commit(lua_State *L, const char *database) { return change_scope(L, database, dbCommitChanges); }
static bool script_db_revert(lua_State *L, const char *database) { return change_scope(L, database, dbRevertChanges); }

#include "build/generated/orion/script/db_lua.h"
