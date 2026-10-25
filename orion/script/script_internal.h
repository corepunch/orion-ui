#ifndef __ORION_SCRIPT_INTERNAL_H__
#define __ORION_SCRIPT_INTERNAL_H__

// Shared by the files of libscript; not part of the public API.

#include <stddef.h>
#include <stdbool.h>
#include <lua.h>
#include <lauxlib.h>

#include <orion/user/database.h>

#define SCRIPT_REFUSAL "orion.Refusal"

// A sandboxed state whose require() searches `dir` (the add-on's folder) and
// Orion's Lua modules. `name` prefixes its print() lines on stderr.
lua_State *script_state_new(const char *dir, const char *name);

// Calls the function under nargs arguments with a traceback handler. On
// failure writes the message to msg and sets *refused when the script raised
// a refusal (orion.fail) rather than a bug; the stack then holds nothing.
bool script_call(lua_State *L, int nargs, int nresults, char *msg, size_t msg_sz, bool *refused);

// Raises a refusal: a message for the user, not a bug.
int script_refuse(lua_State *L, const char *message);

// The "orion" and "orion.db" modules, generated from orion.cgen and db.cgen.
int luaopen_orion(lua_State *L);
int luaopen_orion_db(lua_State *L);

// Records as Lua tables, through a table's schema.
void script_push_record(lua_State *L, const db_table_schema_t *t, const void *record);
// Sets the fields of the table at idx into record; false with a message for
// the user when a value cannot be stored (a string too long, a bad date).
bool script_read_record(lua_State *L, int idx, const db_table_schema_t *t, void *record,
                        char *msg, size_t msg_sz);
// The schema of "db.table", or NULL with a Lua error raised.
const db_table_schema_t *script_table(lua_State *L, const char *db_name, const char *table, database_t **db);

#endif
