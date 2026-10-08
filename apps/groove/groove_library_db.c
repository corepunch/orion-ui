// Read-only database view over Groove's synthesized block catalogue.
#include "groove.h"

static const uint16_t kBlockFieldIds[] = { 0x7100, 0x7101, 0x7102, 0x7103, 0x7104, 0x7105 };
static const db_field_msg_binding_t kBlockBindings[] = {
  { "id", kBlockFieldIds[0] }, { "name", kBlockFieldIds[1] },
  { "category", kBlockFieldIds[2] }, { "genres", kBlockFieldIds[3] },
  { "bars", kBlockFieldIds[4] }, { "variant", kBlockFieldIds[5] },
};
static const db_field_schema_t kBlockSchema[] = {
  { "id", DB_TYPE_INT, 0, true, NULL, NULL },
  { "name", DB_TYPE_STRING, 64, false, NULL, NULL },
  { "category", DB_TYPE_INT, 0, false, NULL, NULL },
  { "genres", DB_TYPE_INT, 0, false, NULL, NULL },
  { "bars", DB_TYPE_INT, 0, false, NULL, NULL },
  { "variant", DB_TYPE_INT, 0, false, NULL, NULL },
};
static db_table_schema_t kTables[] = {
  { TABLE_BLOCKS, "blocks", "library_block_t", kBlockSchema, ARRAY_LEN(kBlockSchema), NULL, 0 },
};
static db_schema_def_t kSchema = {
  .name = "library", .class_name = "groove_library_db", .tables = kTables,
  .table_count = ARRAY_LEN(kTables),
};

static bool block_row_init(library_block_t *row, int id) {
  const block_t *block = block_get(id);
  if (!row || !block) return false;
  memset(row, 0, sizeof(*row));
  row->id = id;
  snprintf(row->name, sizeof(row->name), "%s", block->display_name ? block->display_name : block->name);
  row->category = block->cat;
  row->genres = block->genres;
  row->bars = block->bars;
  row->variant = block->variant;
  return true;
}

static result_node_t *block_fetch(void) {
  result_node_t *head = NULL, **tail = &head;
  for (int category = 0; category < CAT_COUNT; category++) {
    int ids[GR_MAX_BLOCKS];
    int count = blocks_in_category((category_t)category, ids, ARRAY_LEN(ids));
    for (int i = 0; i < count; i++) {
      result_node_t *node = calloc(1, sizeof(*node) + sizeof(library_block_t));
      if (!node) { free_result_list(head); return NULL; }
      if (!block_row_init((library_block_t *)node->data, ids[i])) { free(node); continue; }
      *tail = node;
      tail = (result_node_t **)&node->next;
    }
  }
  return head;
}

static result_t block_object_proc(const void *object, uint32_t msg, uint32_t wparam, void *lparam) {
  if (msg != dbObjGetFieldText || !object || !lparam) return false;
  const library_block_t *row = object;
  char *out = lparam;
  size_t cap = HIWORD(wparam);
  if (!cap) return false;
  switch (LOWORD(wparam)) {
    case 0x7100: snprintf(out, cap, "%d", row->id); return true;
    case 0x7101: snprintf(out, cap, "%s", row->name); return true;
    case 0x7102: snprintf(out, cap, "%s", row->category >= 0 && row->category < CAT_COUNT ? kCategoryName[row->category] : ""); return true;
    case 0x7103: snprintf(out, cap, "%u", row->genres); return true;
    case 0x7104: snprintf(out, cap, "%d", row->bars); return true;
    case 0x7105: snprintf(out, cap, "%u", row->variant); return true;
    default: return false;
  }
}

lresult_t groove_library_db(database_t *db, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case dbCreate: case dbLoad: case dbSave: return 1;
    case dbDestroy: return 1;
    case dbGetDirty: return 0;
    case dbFetch:
      if (LOWORD(wparam) == TABLE_BLOCKS && HIWORD(wparam) == 0) return (lresult_t)block_fetch();
      fprintf(stderr, "[groove-db] dbFetch rejected table=%u filter_field=%u value=%ld\n",
              (unsigned)LOWORD(wparam), (unsigned)HIWORD(wparam), (long)(intptr_t)lparam);
      fflush(stderr);
      return 0;
    case dbGetObjectProc:
      return wparam == TABLE_BLOCKS ? (lresult_t)block_object_proc : 0;
    case dbGetFieldBindings:
      if (lparam) *(int *)lparam = wparam == TABLE_BLOCKS ? ARRAY_LEN(kBlockBindings) : 0;
      return wparam == TABLE_BLOCKS ? (lresult_t)kBlockBindings : 0;
    case dbGetSchema:
      kSchema.name = db ? db->name : "library";
      kSchema.class_name = db ? db->class_name : "groove_library_db";
      return (lresult_t)&kSchema;
    case dbGetFieldMeta:
      if (lparam) *(int *)lparam = wparam == TABLE_BLOCKS ? ARRAY_LEN(library_block_t_fields) : 0;
      return wparam == TABLE_BLOCKS ? (lresult_t)library_block_t_fields : 0;
    case dbGetApi: return (lresult_t)&groove_database_api;
    case dbInsert: case dbUpdate: case dbDelete:
      fprintf(stderr, "[groove-db] rejected write message=%u table=%u (read-only catalogue)\n", (unsigned)msg, (unsigned)wparam);
      fflush(stderr);
      return 0;
    default: return 0;
  }
}
