#include "user.h"
#include "display_group.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { database_t *source; const db_table_schema_t *table; int selected; } display_group_t;
static int display_key(const display_group_t *group, const void *record) {
  for (int i = 0; record && i < group->table->field_count; i++)
    if (group->table->fields[i].primary_key) return *(int *)((const char *)record + group->table->meta[i].offset);
  return 0;
}
lresult_t db_display_group(database_t *db, uint32_t msg, uint32_t wparam, void *lparam) {
  display_group_t *group = db->userdata;
  if (msg == dbCreate) {
    char source[512]; snprintf(source, sizeof(source), "%s", (const char *)lparam);
    char *dot = strchr(source, '.');
    if (!dot) return false;
    *dot++ = 0;
    database_t *database = get_database_by_name(source);
    const db_table_schema_t *table = database ? db_schema_table_named(database->schema, dot) : NULL;
    if (!table) return false;
    group = calloc(1, sizeof(*group));
    if (!group) return false;
    *group = (display_group_t){database, table, 0}; db->userdata = group;
    return true;
  }
  if (!group) return 0;
  int table = group->table->table_id;
  switch (msg) {
    case dbDestroy: free(group); db->userdata = NULL; return true;
    case dbLoad: case dbSave: return true;
    case dbGetSchema: return (lresult_t)db->schema;
    case dbGetDirty: return false;
    case dbSelectRecord: group->selected = (int)wparam; return true;
    case dbGetFieldText: return send_db_message(group->source, msg, (uint32_t)table, lparam);
    case dbFetch: {
      result_node_t *rows = (void *)send_db_message(group->source, msg, MAKEDWORD(table, HIWORD(wparam)), lparam);
      if (LOWORD(wparam) != 1) return (lresult_t)rows;
      result_node_t *selected = NULL;
      for (result_node_t *node = rows; node; node = node->next)
        if (display_key(group, *(void **)node->data) == group->selected) {
          selected = malloc(sizeof(*selected) + sizeof(void *));
          if (selected) { selected->next = NULL; memcpy(selected->data, node->data, sizeof(void *)); }
          break;
        }
      free_result_list(rows); return (lresult_t)selected;
    }
    case dbFind: case dbInsert: case dbUpdate: case dbDelete: case dbInitRecord:
    case dbCountReferrers: case dbGetFieldMeta: case dbGetObjectProc: case dbGetFieldBindings:
      return send_db_message(group->source, msg, MAKEDWORD(table, HIWORD(wparam)), lparam);
    default: return 0;
  }
}
