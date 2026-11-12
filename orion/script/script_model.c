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
  int controller, writing_table, writing_key;
  bool validating, formatting, notifying, initializing, scope_control;
} script_model_t;
static script_model_t *script_models;

static bool model_function(script_model_t *model, const char *name) {
  lua_State *L = script_runtime_state(model->runtime);
  lua_rawgeti(L, LUA_REGISTRYINDEX, model->controller); lua_getfield(L, -1, name);
  if (!lua_isfunction(L, -1)) { lua_pop(L, 2); return false; }
  lua_insert(L, -2);
  return true;
}
static bool model_result(script_model_t *model, int arguments) {
  lua_State *L = script_runtime_state(model->runtime);
  char error[1024]; bool refused = false;
  bool ok = script_call(L, arguments, 2, error, sizeof(error), &refused);
  if (ok) {
    ok = lua_toboolean(L, -2);
    if (!ok) snprintf(model->database->error, sizeof(model->database->error), "%s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "model refused the write");
    lua_pop(L, 2);
  } else snprintf(model->database->error, sizeof(model->database->error), "%s", error);
  return ok;
}
static lresult_t model_reject(script_model_t *model, const db_table_schema_t *table, const char *message) {
  if (message) snprintf(model->database->error, sizeof(model->database->error), "%s", message);
  fprintf(stderr, "[model] %s.%s: %s\n", model->database->name, table ? table->name : "?", model->database->error);
  fflush(stderr); return 0;
}
static bool model_scope(script_model_t *model, uint32_t message) {
  model->scope_control = true;
  bool ok = send_db_message(model->database, message, 0, NULL) != 0;
  model->scope_control = false;
  return ok;
}
static lresult_t model_database(database_t *database, uint32_t msg, uint32_t wparam, void *lparam) {
  script_model_t *model = script_models;
  while (model && model->database != database) model = model->next;
  if (!model) return 0;
  const db_table_schema_t *table = db_schema_table(database->schema, LOWORD(wparam));
  lua_State *L = script_runtime_state(model->runtime);
  bool write = msg == dbInsert || msg == dbUpdate || msg == dbDelete;
  bool scope = msg == dbBeginChanges || msg == dbCommitChanges || msg == dbRevertChanges;
  if ((write || scope || msg == dbSave) && (model->validating || model->formatting))
    return model_reject(model, table, "writes are forbidden during model validation or computation");
  if (((scope && !model->scope_control) || msg == dbSave) && (model->notifying || model->initializing))
    return model_reject(model, table, "model hooks cannot save or control the enclosing transaction");
  if (table && write && !table->transient) {
    if (msg != dbDelete && !lparam) return model_reject(model, table, "missing record for model write");
    database->error[0] = 0;
    int key = msg == dbDelete ? (int)(intptr_t)lparam : 0;
    if (msg != dbDelete) for (int i = 0; i < table->field_count; i++)
      if (table->fields[i].primary_key) key = *(int *)((char *)lparam + table->meta[i].offset);
    if (model->notifying && table->table_id == model->writing_table && key == model->writing_key)
      return model_reject(model, table, "model hooks cannot mutate their originating record");
    const void *before = msg == dbInsert ? NULL : (const void *)model->original(database, dbFind, (uint32_t)table->table_id, (void *)(intptr_t)key);
    void *previous = before ? malloc(table->record_size) : NULL;
    if (before && !previous) return model_reject(model, table, "cannot allocate previous record snapshot");
    if (previous) memcpy(previous, before, table->record_size);
    bool ok = true;
    if (model_function(model, msg == dbDelete ? "validate_delete" : "validate")) {
      model->validating = true;
      lua_pushstring(L, table->name);
      if (msg != dbDelete) script_push_record(L, table, lparam);
      if (previous) script_push_record(L, table, previous); else lua_pushnil(L);
      ok = model_result(model, msg == dbDelete ? 3 : 4);
      model->validating = false;
    }
    if (!ok) { free(previous); return model_reject(model, table, NULL); }
    bool notify = !model->notifying && model_function(model, "after_write");
    if (notify) lua_pop(L, 2);
    if (notify && !model_scope(model, dbBeginChanges)) {
      free(previous); return model_reject(model, table, "cannot begin model change scope");
    }
    lresult_t result = model->original(database, msg, wparam, lparam);
    if (result && msg == dbInsert) for (int i = 0; i < table->field_count; i++)
      if (table->fields[i].primary_key) key = *(int *)((char *)(intptr_t)result + table->meta[i].offset);
    if (result && notify && model_function(model, "after_write")) {
      model->notifying = true; model->writing_table = table->table_id; model->writing_key = key;
      lua_pushstring(L, table->name);
      lua_pushstring(L, msg == dbInsert ? "insert" : msg == dbUpdate ? "update" : "delete");
      const void *current = msg == dbInsert ? (const void *)result : msg == dbUpdate ?
        (const void *)model->original(database, dbFind, (uint32_t)table->table_id, (void *)(intptr_t)key) : NULL;
      if (current) script_push_record(L, table, current); else lua_pushnil(L);
      if (previous) script_push_record(L, table, previous); else lua_pushnil(L);
      ok = model_result(model, 5);
      model->notifying = false;
    }
    if (result && ok && msg != dbDelete) {
      const void *stored = (const void *)model->original(database, dbFind, (uint32_t)table->table_id, (void *)(intptr_t)key);
      if (!stored) { ok = false; snprintf(database->error, sizeof(database->error), "model hook removed the written record"); }
      else if (msg == dbInsert) result = (lresult_t)stored;
    }
    free(previous);
    if (notify) {
      if (!result || !ok) {
        model_scope(model, dbRevertChanges);
        return model_reject(model, table, NULL);
      }
      if (!model_scope(model, dbCommitChanges)) {
        model_scope(model, dbRevertChanges);
        return model_reject(model, table, "cannot commit model change scope");
      }
    }
    return result;
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
  *model = (script_model_t){.next=script_models, .database=database, .original=database->proc, .runtime=runtime, .controller=controller};
  script_models = model; database->proc = model_database;
  return true;
}
bool script_model_initialize(database_t *database) {
  script_model_t *model = script_models;
  while (model && model->database != database) model = model->next;
  if (!model || !model_function(model, "initialize")) return true;
  lua_State *L = script_runtime_state(model->runtime);
  if (strcmp(database->class_name, "db_schema_xml")) {
    lua_pop(L, 2); model_reject(model, NULL, "model initialization requires the schema XML transaction contract"); return false;
  }
  if (!model_scope(model, dbBeginChanges)) {
    lua_pop(L, 2); model_reject(model, NULL, "cannot begin model initialization"); return false;
  }
  model->initializing = true;
  lua_pushstring(L, database->name);
  bool ok = model_result(model, 2);
  model->initializing = false;
  if (ok && database->dirty && database->source_path[0]) ok = model->original(database, dbSave, 0, NULL) != 0;
  if (ok) ok = model_scope(model, dbCommitChanges) != 0;
  if (!ok) {
    model_scope(model, dbRevertChanges);
    model_reject(model, NULL, database->error[0] ? NULL : "model initialization failed");
  }
  return ok;
}
void script_model_detach(database_t *database) {
  script_model_t **link = &script_models;
  while (*link && (*link)->database != database) link = &(*link)->next;
  if (*link) { script_model_t *model = *link; database->proc = model->original; *link = model->next; free(model); }
}
