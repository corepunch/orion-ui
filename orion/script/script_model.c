#include "model.h"
#include "script_internal.h"
#include <orion/user/i18n.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct script_model_s {
  struct script_model_s *next;
  database_t *database;
  dbproc_t original;
  script_runtime_t *runtime;
  int controller;
  bool validating, formatting;
} script_model_t;
static script_model_t *script_models;

static bool model_function(script_model_t *model, const char *name) {
  lua_State *L = script_runtime_state(model->runtime);
  lua_rawgeti(L, LUA_REGISTRYINDEX, model->controller); lua_getfield(L, -1, name);
  if (!lua_isfunction(L, -1)) { lua_pop(L, 2); return false; }
  lua_insert(L, -2);
  return true;
}
static lresult_t model_database(database_t *database, uint32_t msg, uint32_t wparam, void *lparam) {
  script_model_t *model = script_models;
  while (model && model->database != database) model = model->next;
  if (!model) return 0;
  const db_table_schema_t *table = db_schema_table(database->schema, LOWORD(wparam));
  lua_State *L = script_runtime_state(model->runtime);
  if (table && (msg == dbInsert || msg == dbUpdate) && !table->transient && !model->validating && model_function(model, "validate")) {
    model->validating = true; database->error[0] = 0;
    lua_pushstring(L, table->name); script_push_record(L, table, lparam);
    int key = 0;
    for (int i = 0; i < table->field_count; i++) if (table->fields[i].primary_key) key = *(int *)((char *)lparam + table->meta[i].offset);
    const void *previous = (const void *)model->original(database, dbFind, (uint32_t)table->table_id, (void *)(intptr_t)key);
    if (previous) script_push_record(L, table, previous); else lua_pushnil(L);
    char error[1024]; bool refused = false;
    bool ok = script_call(L, 4, 2, error, sizeof(error), &refused);
    if (ok) {
      bool accepted = lua_toboolean(L, -2);
      if (!accepted) snprintf(database->error, sizeof(database->error), "%s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "model refused the write");
      lua_pop(L, 2); ok = accepted;
    } else snprintf(database->error, sizeof(database->error), "%s", error);
    model->validating = false;
    if (!ok) { fprintf(stderr, "[model] %s.%s: %s\n", database->name, table->name, database->error); fflush(stderr); return 0; }
  }
  if (table && msg == dbGetFieldText && !model->formatting) {
    db_field_text_t *request = lparam;
    int field = db_schema_field_index(table, request->field);
    const char *property = field >= 0 ? table->fields[field].computed : NULL;
    if (property && *property && model_function(model, "compute")) {
      model->formatting = true;
      lua_pushstring(L, table->name); script_push_record(L, table, request->record); lua_pushstring(L, property);
      char error[1024]; bool refused = false;
      bool ok = script_call(L, 4, 1, error, sizeof(error), &refused);
      if (ok) {
        if (lua_isnumber(L, -1)) ui_format_number(lua_tonumber(L, -1), table->meta[field].precision ? table->meta[field].precision : 2, request->buf, request->buf_sz);
        else snprintf(request->buf, request->buf_sz, "%s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
        lua_pop(L, 1);
      } else { fprintf(stderr, "[model] compute %s.%s: %s\n", database->name, property, error); fflush(stderr); request->buf[0] = 0; }
      model->formatting = false;
      return true;
    }
  }
  return model->original(database, msg, wparam, lparam);
}
bool script_model_attach(database_t *database, script_runtime_t *runtime, int controller) {
  if (!database || !runtime || controller == LUA_NOREF) return false;
  script_model_t *model = calloc(1, sizeof(*model));
  if (!model) return false;
  *model = (script_model_t){script_models, database, database->proc, runtime, controller, false, false};
  script_models = model; database->proc = model_database;
  return true;
}
void script_model_detach(database_t *database) {
  script_model_t **link = &script_models;
  while (*link && (*link)->database != database) link = &(*link)->next;
  if (*link) { script_model_t *model = *link; database->proc = model->original; *link = model->next; free(model); }
}
