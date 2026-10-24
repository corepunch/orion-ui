#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "user.h"
#include "database.h"

// ── Database class registry (analogous to window class registry) ────────────

#define MAX_DATABASE_CLASSES 16

static db_class_desc_t g_database_classes[MAX_DATABASE_CLASSES];
static int g_database_class_count = 0;

static bool db_streq(const char *a, const char *b) {
  return a && b && strcmp(a, b) == 0;
}

bool register_database_class(const db_class_desc_t *desc) {
  if (!desc || !desc->class_name || !*desc->class_name || !desc->proc)
    return false;
  
  // Check if already registered (idempotent)
  for (int i = 0; i < g_database_class_count; i++) {
    if (db_streq(g_database_classes[i].class_name, desc->class_name))
      return true;
  }
  
  if (g_database_class_count >= MAX_DATABASE_CLASSES)
    return false;
  
  g_database_classes[g_database_class_count++] = *desc;
  return true;
}

dbproc_t find_database_class_proc(const char *class_name) {
  if (!class_name || !*class_name) return NULL;
  for (int i = 0; i < g_database_class_count; i++) {
    if (db_streq(g_database_classes[i].class_name, class_name))
      return g_database_classes[i].proc;
  }
  return NULL;
}

// ── Database instance registry ──────────────────────────────────────────────

#define MAX_DATABASE_INSTANCES 16

static struct {
  const char *name;
  database_t *db;
} g_database_instances[MAX_DATABASE_INSTANCES];
static int g_database_instance_count = 0;

bool register_database(const char *name, database_t *db) {
  if (!name || !*name || !db) return false;
  
  // Check if already registered (allow overwrite)
  for (int i = 0; i < g_database_instance_count; i++) {
    if (db_streq(g_database_instances[i].name, name)) {
      g_database_instances[i].db = db;
      return true;
    }
  }
  
  if (g_database_instance_count >= MAX_DATABASE_INSTANCES) {
    fprintf(stderr, "Too many database instances registered (max %d)\n",
            MAX_DATABASE_INSTANCES);
    return false;
  }
  
  g_database_instances[g_database_instance_count].name = name;
  g_database_instances[g_database_instance_count].db = db;
  g_database_instance_count++;
  return true;
}

database_t *get_database_by_name(const char *name) {
  if (!name || !*name) return NULL;
  for (int i = 0; i < g_database_instance_count; i++) {
    if (db_streq(g_database_instances[i].name, name))
      return g_database_instances[i].db;
  }
  fprintf(stderr, "Database '%s' not found in registry\n", name);
  return NULL;
}

database_t *create_database(const char *name, const char *class_name, const char *source_path) {
  return create_database_with_schema(name, class_name, NULL, source_path);
}

database_t *create_database_with_schema(const char *name, const char *class_name,
                                        const db_schema_def_t *schema,
                                        const char *source_path) {
  if (!name || !class_name) return NULL;
  
  dbproc_t proc = find_database_class_proc(class_name);
  if (!proc) {
    fprintf(stderr, "Database class '%s' not registered\n", class_name);
    return NULL;
  }
  
  database_t *db = calloc(1, sizeof(database_t));
  if (!db) return NULL;
  
  db->name = name;
  db->class_name = class_name;
  db->proc = proc;
  db->dirty = false;
  db->schema = schema;
  
  if (source_path)
    strncpy(db->source_path, source_path, sizeof(db->source_path) - 1);
  
  // Send dbCreate message (proc can allocate userdata, parse source_path, etc.)
  send_db_message(db, dbCreate, 0, (void *)source_path);
  
  // Send dbLoad message to load initial data
  send_db_message(db, dbLoad, 0, NULL);
  
  return db;
}

void destroy_database(database_t *db) {
  if (!db) return;
  
  // Save if dirty
  if (db->dirty)
    send_db_message(db, dbSave, 0, NULL);
  
  // Send destroy message (proc can free userdata)
  send_db_message(db, dbDestroy, 0, NULL);
  
  free(db);
}

lresult_t send_db_message(database_t *db, uint32_t msg, uint32_t wparam, void *lparam) {
  if (!db || !db->proc) return 0;
  return db->proc(db, msg, wparam, lparam);
}

// ── Declarative database API helpers (from .orion metadata) ─────────────────

const db_source_def_t *db_api_find_source(const db_api_def_t *api, const char *name) {
  if (!api || !name || !api->sources || api->source_count <= 0) return NULL;
  for (int i = 0; i < api->source_count; i++) {
    if (api->sources[i].name && strcmp(api->sources[i].name, name) == 0)
      return &api->sources[i];
  }
  return NULL;
}

const db_view_binding_t *db_api_find_binding(const db_api_def_t *api, const char *name) {
  if (!api || !name || !api->bindings || api->binding_count <= 0) return NULL;
  for (int i = 0; i < api->binding_count; i++) {
    if (api->bindings[i].name && strcmp(api->bindings[i].name, name) == 0)
      return &api->bindings[i];
  }
  return NULL;
}

const db_view_binding_t *db_api_find_binding_for_view(const db_api_def_t *api, const char *view) {
  if (!api || !view || !api->bindings || api->binding_count <= 0) return NULL;
  for (int i = 0; i < api->binding_count; i++) {
    if (api->bindings[i].view && strcmp(api->bindings[i].view, view) == 0)
      return &api->bindings[i];
  }
  return NULL;
}

const db_action_def_t *db_api_find_action(const db_api_def_t *api, const char *name) {
  if (!api || !name || !api->actions || api->action_count <= 0) return NULL;
  for (int i = 0; i < api->action_count; i++) {
    if (api->actions[i].name && strcmp(api->actions[i].name, name) == 0)
      return &api->actions[i];
  }
  return NULL;
}

const db_outlet_def_t *db_api_find_outlet(const db_api_def_t *api, const char *name) {
  if (!api || !name || !api->outlets || api->outlet_count <= 0) return NULL;
  for (int i = 0; i < api->outlet_count; i++) {
    if (api->outlets[i].name && strcmp(api->outlets[i].name, name) == 0)
      return &api->outlets[i];
  }
  return NULL;
}

bool db_object_get_field_text(const db_field_msg_binding_t *bindings, int binding_count,
                              db_object_proc_t proc, const void *object,
                              const char *field, char *buf, size_t buf_sz) {
  if (!bindings || binding_count <= 0 || !proc || !object || !field || !buf || buf_sz == 0)
    return false;
  // HIWORD(wparam) is 16-bit in the Action-Message DDX contract.
  if (buf_sz > 0xffffu)
    return false;
  
  for (int i = 0; i < binding_count; i++) {
    if (!bindings[i].field || strcmp(bindings[i].field, field) != 0)
      continue;
    
    buf[0] = '\0';
    uint32_t packed = MAKEDWORD(bindings[i].column_id, (uint16_t)buf_sz);
    result_t result = proc(object, dbObjGetFieldText, packed, buf);
    return result ? true : false;
  }
  
  return false;
}

// ── Schema-driven field formatting ──────────────────────────────────────────

const db_table_schema_t *db_schema_table(const db_schema_def_t *schema, int table_id) {
  for (int i = 0; schema && i < schema->table_count; i++)
    if (schema->tables[i].table_id == table_id) return &schema->tables[i];
  return NULL;
}

const db_table_schema_t *db_schema_table_named(const db_schema_def_t *schema, const char *name) {
  for (int i = 0; schema && name && i < schema->table_count; i++)
    if (db_streq(schema->tables[i].name, name)) return &schema->tables[i];
  return NULL;
}

int db_schema_field_index(const db_table_schema_t *table, const char *field) {
  for (int i = 0; table && field && i < table->meta_count; i++)
    if (db_streq(table->meta[i].name, field)) return i;
  return -1;
}

bool db_format_field(const db_field_meta_t *field, const void *record, char *buf, size_t buf_sz) {
  if (!field || !record || !buf || buf_sz == 0) return false;
  const char *p = (const char *)record + field->offset;
  int decimals = field->precision > 0 ? field->precision : 2;
  switch (field->type) {
    case DB_TYPE_INT:    snprintf(buf, buf_sz, "%d", *(const int *)p);                    return true;
    case DB_TYPE_STRING: snprintf(buf, buf_sz, "%s", p);                                  return true;
    case DB_TYPE_DATE:   ui_format_date(p, buf, buf_sz);                                  return true;
    case DB_TYPE_BOOL:   snprintf(buf, buf_sz, "%d", *(const bool *)p ? 1 : 0);           return true;
    case DB_TYPE_FLOAT:  ui_format_number(*(const float *)p, decimals, buf, buf_sz);       return true;
    case DB_TYPE_DOUBLE: ui_format_number(*(const double *)p, decimals, buf, buf_sz);      return true;
  }
  return false;
}

bool db_parse_field(const db_field_meta_t *field, void *record, const char *text) {
  if (!field || !record || !text) return false;
  char *p = (char *)record + field->offset;
  switch (field->type) {
    case DB_TYPE_INT:    *(int *)p = atoi(text);                                                    return true;
    case DB_TYPE_BOOL:   *(bool *)p = text[0] == '1' || text[0] == 't' || text[0] == 'Y';           return true;
    case DB_TYPE_FLOAT:  *(float *)p = (float)atof(text);                                           return true;
    case DB_TYPE_DOUBLE: *(double *)p = atof(text);                                                 return true;
    case DB_TYPE_DATE:
      if (!strcmp(text, "today")) {
        time_t now = time(NULL);
        if (field->length > 0) strftime(p, (size_t)field->length, "%Y-%m-%d", localtime(&now));
        return true;
      }
      // fall through: stored as ISO text
    case DB_TYPE_STRING: {
      const char *start = text, *end = text + strlen(text);
      while (*start && isspace((unsigned char)*start)) start++;
      while (end > start && isspace((unsigned char)*(end - 1))) end--;
      size_t len = (size_t)(end - start), cap = (size_t)field->length;
      if (cap == 0) return false;
      if (len >= cap) len = cap - 1;
      memcpy(p, start, len);
      p[len] = '\0';
      return true;
    }
  }
  return false;
}

static const db_join_schema_t *db_schema_join(const db_table_schema_t *table, const char *name, size_t len) {
  for (int i = 0; table && i < table->join_count; i++)
    if (strlen(table->joins[i].name) == len && !strncmp(table->joins[i].name, name, len))
      return &table->joins[i];
  return NULL;
}

bool db_schema_field_text(database_t *db, const db_table_schema_t *table,
                          const void *record, const char *field,
                          char *buf, size_t buf_sz) {
  if (!db || !table || !record || !field || !buf || buf_sz == 0) return false;
  buf[0] = '\0';
  const char *dot = strchr(field, '.');
  if (!dot) {
    int index = db_schema_field_index(table, field);
    return index >= 0 && db_format_field(&table->meta[index], record, buf, buf_sz);
  }
  const db_join_schema_t *join = db_schema_join(table, field, (size_t)(dot - field));
  int local = join ? db_schema_field_index(table, join->local_field) : -1;
  if (local < 0 || table->meta[local].type != DB_TYPE_INT) {
    fprintf(stderr, "[database] unknown join path db=%s table=%s field=%s\n",
            db->name, table->name, field);
    return false;
  }
  int key = *(const int *)((const char *)record + table->meta[local].offset);
  if (key == 0) return true;  // unset reference displays as empty
  // "db.table" relations cross into another registered database.
  database_t *foreign_db = db;
  const char *foreign_name = join->foreign_table;
  const char *qualified = foreign_name[0] == '{' ? NULL : strchr(foreign_name, '.');
  if (qualified) {
    char db_name[64];
    size_t n = (size_t)(qualified - foreign_name);
    if (n >= sizeof(db_name)) return false;
    memcpy(db_name, foreign_name, n); db_name[n] = '\0';
    foreign_db = get_database_by_name(db_name);
    foreign_name = qualified + 1;
  }
  // A polymorphic relation names its table by a path on the same record:
  // relation="ib.{debit.subconto_kind.table}.id".
  char target_table[64];
  if (foreign_name[0] == '{') {
    const char *end = strchr(foreign_name, '}');
    char path[128];
    if (!end || (size_t)(end - foreign_name - 1) >= sizeof(path)) {
      fprintf(stderr, "[database] malformed polymorphic relation db=%s table=%s relation=%s\n",
              db->name, table->name, join->foreign_table);
      return false;
    }
    snprintf(path, sizeof(path), "%.*s", (int)(end - foreign_name - 1), foreign_name + 1);
    if (!db_schema_field_text(db, table, record, path, target_table, sizeof(target_table))) return false;
    if (!target_table[0]) return true;  // the record names no table: nothing to show
    foreign_name = target_table;
  }
  const db_schema_def_t *schema = foreign_db
    ? (const db_schema_def_t *)send_db_message(foreign_db, dbGetSchema, 0, NULL) : NULL;
  const db_table_schema_t *foreign = db_schema_table_named(schema, foreign_name);
  int search = db_schema_field_index(foreign, join->foreign_field);
  if (!foreign || search < 0) {
    fprintf(stderr, "[database] join target missing db=%s relation=%s.%s\n",
            db->name, join->foreign_table, join->foreign_field);
    return false;
  }
  const void *target = (const void *)send_db_message(foreign_db, dbFind,
    MAKEDWORD(foreign->table_id, search), (void *)(intptr_t)key);
  return target ? db_get_field_text(foreign_db, foreign->table_id, target, dot + 1, buf, buf_sz)
                : true;
}

bool db_get_field_text(database_t *db, int table_id, const void *record,
                       const char *field, char *buf, size_t buf_sz) {
  if (!db || !record || !field || !buf || buf_sz == 0) return false;
  db_field_text_t req = { record, field, buf, buf_sz };
  if (send_db_message(db, dbGetFieldText, (uint32_t)table_id, &req)) return true;
  int count = 0;
  db_object_proc_t proc = (db_object_proc_t)send_db_message(db, dbGetObjectProc, (uint32_t)table_id, NULL);
  const db_field_msg_binding_t *bindings = (const db_field_msg_binding_t *)send_db_message(
    db, dbGetFieldBindings, (uint32_t)table_id, &count);
  return db_object_get_field_text(bindings, count, proc, record, field, buf, buf_sz);
}

// ── Result list helpers ──────────────────────────────────────────────────────

void free_result_list(void *head) {
  result_node_t *node = (result_node_t *)head;
  while (node) {
    result_node_t *next = (result_node_t *)node->next;
    free(node);
    node = next;
  }
}

int count_result_list(void *head) {
  int count = 0;
  for (result_node_t *node = (result_node_t *)head; node; node = (result_node_t *)node->next)
    count++;
  return count;
}

// ── Reflection-based XML loading (uses generated field metadata) ────────────

#include <libxml/parser.h>
#include <libxml/tree.h>

bool db_load_field_from_xml(xmlNodePtr node, void *record_base,
                             const db_field_meta_t *field) {
  if (!node || !record_base || !field) return false;
  
  xmlChar *value = NULL;
  
  // Try attribute first
  value = xmlGetProp(node, (const xmlChar *)field->name);
  
  // If not found as attribute, try child element
  if (!value) {
    for (xmlNode *child = node->children; child; child = child->next) {
      if (child->type == XML_ELEMENT_NODE &&
          xmlStrcmp(child->name, (const xmlChar *)field->name) == 0) {
        value = xmlNodeGetContent(child);
        break;
      }
    }
  }
  
  // Special case: if field is "body" or "text" and we haven't found it yet,
  // use the node's direct text content
  if (!value && (strcmp(field->name, "body") == 0 || strcmp(field->name, "text") == 0)) {
    value = xmlNodeGetContent(node);
  }
  
  if (!value) return false;
  
  bool ok = db_parse_field(field, record_base, (const char *)value);
  xmlFree(value);
  return ok;
}

bool db_load_record_from_xml(xmlNodePtr node, void *record,
                              const db_field_meta_t *fields, int field_count) {
  if (!node || !record || !fields || field_count <= 0) return false;
  
  // Zero the record first
  memset(record, 0, fields[field_count - 1].offset +
         (fields[field_count - 1].type == DB_TYPE_STRING || fields[field_count - 1].type == DB_TYPE_DATE ?
          fields[field_count - 1].length : sizeof(int)));
  
  // Load each field
  for (int i = 0; i < field_count; i++) {
    db_load_field_from_xml(node, record, &fields[i]);
  }
  
  return true;
}

// ── Reflection-based XML saving (uses generated field metadata) ─────────────

bool db_save_field_to_xml(xmlNodePtr node, const void *record_base,
                           const db_field_meta_t *field) {
  if (!node || !record_base || !field) return false;
  
  const void *field_ptr = (const char *)record_base + field->offset;
  char buf[2048];
  
  switch (field->type) {
    case DB_TYPE_INT:
      snprintf(buf, sizeof(buf), "%d", *(const int *)field_ptr);
      break;
      
    case DB_TYPE_DATE:
      snprintf(buf, sizeof(buf), "%s", (const char *)field_ptr);
      break;

    case DB_TYPE_STRING: {
      const char *str = (const char *)field_ptr;
      // For body/text fields with potentially large content, use element content
      if (strcmp(field->name, "body") == 0 || strcmp(field->name, "text") == 0) {
        if (str[0] != '\0') {
          xmlNodeSetContent(node, (const xmlChar *)str);
        }
        return true;
      }
      snprintf(buf, sizeof(buf), "%s", str);
      break;
    }
      
    case DB_TYPE_BOOL:
      snprintf(buf, sizeof(buf), "%d", *(const bool *)field_ptr ? 1 : 0);
      break;
      
    case DB_TYPE_FLOAT:
      snprintf(buf, sizeof(buf), "%g", *(const float *)field_ptr);
      break;
      
    case DB_TYPE_DOUBLE:
      snprintf(buf, sizeof(buf), "%.15g", *(const double *)field_ptr);
      break;
      
    default:
      return false;
  }
  
  xmlSetProp(node, (const xmlChar *)field->name, (const xmlChar *)buf);
  return true;
}

xmlNodePtr db_save_record_to_xml(xmlNodePtr parent, const char *element_name,
                                   const void *record, const db_field_meta_t *fields,
                                   int field_count) {
  if (!parent || !element_name || !record || !fields || field_count <= 0)
    return NULL;
  
  xmlNodePtr node = xmlNewChild(parent, NULL, (const xmlChar *)element_name, NULL);
  if (!node) return NULL;
  
  // Save each field
  for (int i = 0; i < field_count; i++) {
    db_save_field_to_xml(node, record, &fields[i]);
  }
  
  return node;
}
