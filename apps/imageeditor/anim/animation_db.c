#include "animation_db.h"

static const db_field_msg_binding_t animation_bindings[] = {
  {"id", 1}, {"cel", 2}, {"delay", 3}, {"uses", 4}, {"name", 5}, {"note", 6}
};
static const db_field_meta_t animation_fields[] = {
  {"id", DB_TYPE_INT, offsetof(animation_row_t, id), 0},
  {"cel", DB_TYPE_INT, offsetof(animation_row_t, cel), 0},
  {"delay", DB_TYPE_INT, offsetof(animation_row_t, delay), 0},
  {"uses", DB_TYPE_INT, offsetof(animation_row_t, uses), 0},
  {"name", DB_TYPE_STRING, offsetof(animation_row_t, name), 32},
  {"note", DB_TYPE_STRING, offsetof(animation_row_t, note), 128}
};
static const db_field_schema_t animation_schema_fields[] = {
  {"id", DB_TYPE_INT, 0, true}, {"cel", DB_TYPE_INT}, {"delay", DB_TYPE_INT},
  {"uses", DB_TYPE_INT}, {"name", DB_TYPE_STRING, 32}, {"note", DB_TYPE_STRING, 128}
};
static const db_table_schema_t animation_tables[] = {
  {.table_id = TABLE_CEL_BANK, .name = "cel_bank", .fields = animation_schema_fields, .field_count = 6},
  {.table_id = TABLE_EXPOSURE_SHEET, .name = "exposure_sheet", .fields = animation_schema_fields, .field_count = 6}
};
static const db_schema_def_t animation_schema = {
  .name = "animation", .class_name = "animation_db", .tables = animation_tables, .table_count = 2
};

static result_t animation_object(const void *object, uint32_t msg, uint32_t wparam, void *lparam) {
  const animation_row_t *row = object;
  if (!row || !lparam || !HIWORD(wparam) || msg != dbObjGetFieldText) return false;
  char *buf = lparam; size_t size = HIWORD(wparam);
  switch (LOWORD(wparam)) {
    case 1: snprintf(buf, size, "%d", row->id); break;
    case 2: snprintf(buf, size, "%d", row->cel); break;
    case 3: snprintf(buf, size, "%d", row->delay); break;
    case 4: snprintf(buf, size, "%d", row->uses); break;
    case 5: snprintf(buf, size, "%s", row->name); break;
    case 6: snprintf(buf, size, "%s", row->note); break;
    default: return false;
  }
  return true;
}

static lresult_t animation_db_proc(database_t *db, uint32_t msg, uint32_t wparam, void *lparam) {
  animation_db_state_t *st = db->userdata;
  switch (msg) {
    case dbCreate: case dbLoad: return true;
    case dbDestroy:
      if (st) { free(st->bank); free(st->sheet); st->bank = st->sheet = NULL; }
      db->userdata = NULL;
      return true;
    case dbGetObjectProc: return (lresult_t)animation_object;
    case dbGetFieldBindings:
      if (lparam) *(int *)lparam = ARRAY_LEN(animation_bindings);
      return (lresult_t)animation_bindings;
    case dbGetFieldMeta:
      if (lparam) *(int *)lparam = ARRAY_LEN(animation_fields);
      return (lresult_t)animation_fields;
    case dbGetSchema: return (lresult_t)&animation_schema;
    case dbFetch: {
      if (!st || !st->doc || !st->doc->anim) return 0;
      bool bank = LOWORD(wparam) == TABLE_CEL_BANK;
      if (!bank && LOWORD(wparam) != TABLE_EXPOSURE_SHEET) return 0;
      anim_timeline_t *tl = st->doc->anim;
      int count = bank ? tl->cel_count : tl->frame_count;
      animation_row_t *rows = calloc(count, sizeof(*rows));
      if (!rows) return 0;
      result_node_t *head = NULL, **tail = &head;
      for (int i = 0; i < count; i++) {
        anim_frame_t *cel = bank ? tl->bank[i] : tl->frames[i].cel;
        rows[i].id = i + 1; rows[i].cel = cel->id; rows[i].uses = anim_cel_uses(tl, cel->id);
        snprintf(rows[i].name, sizeof(rows[i].name), "%s", cel->name);
        if (!bank) {
          rows[i].delay = tl->frames[i].delay_ms;
          snprintf(rows[i].note, sizeof(rows[i].note), "%s", tl->frames[i].note);
        }
        result_node_t *node = calloc(1, sizeof(*node) + sizeof(void *));
        if (!node) { free_result_list(head); free(rows); return 0; }
        *(void **)node->data = rows + i;
        *tail = node; tail = (result_node_t **)&node->next;
      }
      animation_row_t **owned = bank ? &st->bank : &st->sheet;
      free(*owned); *owned = rows;
      return (lresult_t)head;
    }
    default: return 0;
  }
}

// A registered instance has process lifetime; only its modal document binding is temporary.
static database_t animation_database = {.name = "animation", .class_name = "animation_db", .proc = animation_db_proc};

database_t *animation_db_bind(animation_db_state_t *state) {
  if (!state || animation_database.userdata || !register_database("animation", &animation_database)) return NULL;
  animation_database.userdata = state;
  return &animation_database;
}

void animation_db_unbind(database_t *db) {
  if (db) send_db_message(db, dbDestroy, 0, NULL);
}
