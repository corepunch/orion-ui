// db_schema_xml — schema-driven XML database class.
//
// Every table, key, relation and record layout comes from the schema orionc
// generates for a <database> block, so an app declares its data in .orion and
// writes no adaptor code:
//
//   DB_CLASS(db_schema_xml);
//   database_t *db = create_database_with_schema("db", "db_schema_xml",
//                                                &myapp_db_schema, "data.xml");
//
// Records are individually allocated, so pointers returned by dbInsert,
// dbFind and dbFetch stay valid until the record is deleted. A missing
// source file loads as an empty database; dbSave writes it.
//
// Tables declared transient="true" hold derived data (report results): they
// are never written and changing them does not mark the database dirty.
//
// File format (same shape as SimpleXMLDatabase seeds):
//   <db><accounts><account id="1" code="01" .../></accounts>...</db>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "user.h"
#include "database.h"
#include <libxml/parser.h>
#include <libxml/tree.h>

typedef struct {
  void **rows;
  int count, capacity, next_id;
} dbs_table_t;

typedef struct {
  dbs_table_t *tables;  // parallel to db->schema->tables
} dbs_context_t;

static int dbs_table_index(database_t *db, int table_id) {
  for (int i = 0; db->schema && i < db->schema->table_count; i++)
    if (db->schema->tables[i].table_id == table_id) return i;
  fprintf(stderr, "[db_schema_xml] unknown table db=%s table_id=%d\n", db->name, table_id);
  return -1;
}

static int dbs_key_index(const db_table_schema_t *t) {
  for (int i = 0; i < t->field_count && i < t->meta_count; i++)
    if (t->fields[i].primary_key && t->meta[i].type == DB_TYPE_INT) return i;
  int id = db_schema_field_index(t, "id");
  return id >= 0 && t->meta[id].type == DB_TYPE_INT ? id : -1;
}

static int dbs_key(const db_table_schema_t *t, const void *record) {
  int k = dbs_key_index(t);
  return k >= 0 ? *(const int *)((const char *)record + t->meta[k].offset) : 0;
}

// Row element name: the singular of the table name (entries → entry).
static void dbs_row_tag(const char *table, char *out, size_t cap) {
  size_t n = strlen(table);
  static const char *es[] = { "xes", "ches", "shes", "sses", "zes" };  // taxes → tax, as orionc names the record type
  bool sibilant = false;
  for (size_t i = 0; i < sizeof(es) / sizeof(es[0]); i++) {
    size_t k = strlen(es[i]);
    if (n > k && !strcmp(table + n - k, es[i])) sibilant = true;
  }
  if (n > 3 && !strcmp(table + n - 3, "ies")) snprintf(out, cap, "%.*sy", (int)(n - 3), table);
  else if (sibilant) snprintf(out, cap, "%.*s", (int)(n - 2), table);
  else if (n > 1 && table[n - 1] == 's') snprintf(out, cap, "%.*s", (int)(n - 1), table);
  else snprintf(out, cap, "%s_row", table);
}

static void *dbs_append(database_t *db, const db_table_schema_t *t, dbs_table_t *rows, const void *src) {
  if (rows->count >= rows->capacity) {
    int cap = rows->capacity ? rows->capacity * 2 : 16;
    void **grown = realloc(rows->rows, (size_t)cap * sizeof(void *));
    if (!grown) {
      fprintf(stderr, "[db_schema_xml] row allocation failed db=%s table=%s count=%d\n",
              db->name, t->name, rows->count);
      return NULL;
    }
    rows->rows = grown; rows->capacity = cap;
  }
  void *record = malloc(t->record_size);
  if (!record) {
    fprintf(stderr, "[db_schema_xml] record allocation failed db=%s table=%s size=%zu\n",
            db->name, t->name, t->record_size);
    return NULL;
  }
  memcpy(record, src, t->record_size);
  int k = dbs_key_index(t);
  if (k >= 0) {
    int *key = (int *)((char *)record + t->meta[k].offset);
    if (*key <= 0) *key = rows->next_id;
    if (*key >= rows->next_id) rows->next_id = *key + 1;
  }
  rows->rows[rows->count++] = record;
  return record;
}

static int dbs_find_row(const db_table_schema_t *t, dbs_table_t *rows, int id) {
  for (int i = 0; i < rows->count; i++)
    if (dbs_key(t, rows->rows[i]) == id) return i;
  return -1;
}

static bool dbs_field_matches(const db_field_meta_t *f, const void *record, const void *value) {
  const char *p = (const char *)record + f->offset;
  switch (f->type) {
    case DB_TYPE_INT:    return *(const int *)p == (int)(intptr_t)value;
    case DB_TYPE_BOOL:   return *(const bool *)p == ((intptr_t)value != 0);
    case DB_TYPE_STRING:
    case DB_TYPE_DATE:   return value && !strcmp(p, (const char *)value);
    default:             return false;
  }
}

static void dbs_clear(database_t *db, dbs_context_t *ctx) {
  for (int i = 0; i < db->schema->table_count; i++) {
    dbs_table_t *rows = &ctx->tables[i];
    for (int r = 0; r < rows->count; r++) free(rows->rows[r]);
    free(rows->rows);
    *rows = (dbs_table_t){ .next_id = 1 };
  }
}

static bool dbs_load(database_t *db, dbs_context_t *ctx) {
  dbs_clear(db, ctx);
  if (!db->source_path[0] || !axPathExists(db->source_path)) return true;
  xmlDocPtr doc = xmlReadFile(db->source_path, NULL, XML_PARSE_NONET);
  xmlNodePtr root = doc ? xmlDocGetRootElement(doc) : NULL;
  if (!root) {
    fprintf(stderr, "[db_schema_xml] cannot parse db=%s path=%s\n", db->name, db->source_path);
    fflush(stderr);
    if (doc) xmlFreeDoc(doc);
    return false;
  }
  for (xmlNodePtr tn = root->children; tn; tn = tn->next) {
    if (tn->type != XML_ELEMENT_NODE) continue;
    const db_table_schema_t *t = db_schema_table_named(db->schema, (const char *)tn->name);
    if (!t) {
      fprintf(stderr, "[db_schema_xml] skipping unknown table db=%s table=%s\n", db->name, tn->name);
      continue;
    }
    int ti = (int)(t - db->schema->tables);
    void *record = calloc(1, t->record_size);
    if (!record) break;
    for (xmlNodePtr rn = tn->children; rn; rn = rn->next) {
      if (rn->type != XML_ELEMENT_NODE) continue;
      memset(record, 0, t->record_size);
      for (int f = 0; f < t->meta_count; f++) db_load_field_from_xml(rn, record, &t->meta[f]);
      dbs_append(db, t, &ctx->tables[ti], record);
    }
    free(record);
  }
  xmlFreeDoc(doc);
  db->dirty = false;
  return true;
}

static bool dbs_save(database_t *db, dbs_context_t *ctx) {
  if (!db->source_path[0]) return false;
  xmlDocPtr doc = xmlNewDoc((const xmlChar *)"1.0");
  xmlNodePtr root = xmlNewNode(NULL, (const xmlChar *)db->name);
  xmlDocSetRootElement(doc, root);
  for (int i = 0; i < db->schema->table_count; i++) {
    const db_table_schema_t *t = &db->schema->tables[i];
    if (t->transient) continue;
    char tag[128];
    dbs_row_tag(t->name, tag, sizeof(tag));
    xmlNodePtr tn = xmlNewChild(root, NULL, (const xmlChar *)t->name, NULL);
    for (int r = 0; r < ctx->tables[i].count; r++)
      db_save_record_to_xml(tn, tag, ctx->tables[i].rows[r], t->meta, t->meta_count);
  }
  int written = xmlSaveFormatFileEnc(db->source_path, doc, "UTF-8", 1);
  xmlFreeDoc(doc);
  if (written < 0) {
    fprintf(stderr, "[db_schema_xml] cannot write db=%s path=%s\n", db->name, db->source_path);
    fflush(stderr);
    return false;
  }
  db->dirty = false;
  return true;
}

static lresult_t dbs_fetch(database_t *db, dbs_context_t *ctx, int ti, int field, const void *value) {
  const db_table_schema_t *t = &db->schema->tables[ti];
  if (field < 0 || field >= t->meta_count) {
    fprintf(stderr, "[db_schema_xml] fetch filter out of range db=%s table=%s field=%d\n",
            db->name, t->name, field);
    return 0;
  }
  result_node_t *head = NULL, *tail = NULL;
  for (int r = 0; r < ctx->tables[ti].count; r++) {
    void *record = ctx->tables[ti].rows[r];
    if (field > 0 && !dbs_field_matches(&t->meta[field], record, value)) continue;
    result_node_t *node = malloc(sizeof(result_node_t) + sizeof(void *));
    if (!node) { free_result_list(head); return 0; }
    node->next = NULL;
    *(void **)node->data = record;
    if (tail) tail->next = node; else head = node;
    tail = node;
  }
  return (lresult_t)head;
}

lresult_t db_schema_xml(database_t *db, uint32_t msg, uint32_t wparam, void *lparam) {
  dbs_context_t *ctx = (dbs_context_t *)db->userdata;
  if (msg == dbCreate) {
    if (!db->schema || db->schema->table_count <= 0) {
      fprintf(stderr, "[db_schema_xml] db=%s created without a schema; use create_database_with_schema\n",
              db->name);
      fflush(stderr);
      return 0;
    }
    ctx = calloc(1, sizeof(*ctx));
    if (ctx) ctx->tables = calloc((size_t)db->schema->table_count, sizeof(dbs_table_t));
    if (!ctx || !ctx->tables) {
      fprintf(stderr, "[db_schema_xml] context allocation failed db=%s\n", db->name);
      free(ctx);
      return 0;
    }
    for (int i = 0; i < db->schema->table_count; i++) ctx->tables[i].next_id = 1;
    db->userdata = ctx;
    return 1;
  }
  if (!ctx) return 0;

  int ti = -1;
  switch (msg) {
    case dbInsert: case dbUpdate: case dbDelete: case dbFetch: case dbFind:
    case dbGetFieldMeta: case dbGetFieldText:
      if ((ti = dbs_table_index(db, LOWORD(wparam))) < 0) return 0;
      break;
  }
  const db_table_schema_t *t = ti >= 0 ? &db->schema->tables[ti] : NULL;

  switch (msg) {
    case dbDestroy:
      dbs_clear(db, ctx);
      free(ctx->tables);
      free(ctx);
      db->userdata = NULL;
      return 1;
    case dbLoad: return dbs_load(db, ctx);
    case dbSave: return db->dirty ? dbs_save(db, ctx) : 1;
    case dbGetDirty: return db->dirty;
    case dbGetSchema: return (lresult_t)db->schema;
    case dbInsert: {
      if (!lparam) return 0;
      void *record = dbs_append(db, t, &ctx->tables[ti], lparam);
      if (record && !t->transient) db->dirty = true;
      return (lresult_t)record;
    }
    case dbUpdate: {
      if (!lparam) return 0;
      int id = HIWORD(wparam) ? (int)HIWORD(wparam) : dbs_key(t, lparam);
      int row = dbs_find_row(t, &ctx->tables[ti], id);
      if (row < 0) {
        fprintf(stderr, "[db_schema_xml] update of missing record db=%s table=%s id=%d\n",
                db->name, t->name, id);
        return 0;
      }
      memmove(ctx->tables[ti].rows[row], lparam, t->record_size);
      db->dirty |= !t->transient;
      return 1;
    }
    case dbDelete: {
      dbs_table_t *rows = &ctx->tables[ti];
      int row = dbs_find_row(t, rows, (int)(intptr_t)lparam);
      if (row < 0) return 0;
      free(rows->rows[row]);
      memmove(&rows->rows[row], &rows->rows[row + 1], (size_t)(rows->count - row - 1) * sizeof(void *));
      rows->count--;
      db->dirty |= !t->transient;
      return 1;
    }
    case dbFetch: return dbs_fetch(db, ctx, ti, HIWORD(wparam), lparam);
    case dbFind: {
      int field = HIWORD(wparam) ? HIWORD(wparam) : dbs_key_index(t);
      if (field < 0 || field >= t->meta_count) return 0;
      for (int r = 0; r < ctx->tables[ti].count; r++)
        if (dbs_field_matches(&t->meta[field], ctx->tables[ti].rows[r], lparam))
          return (lresult_t)ctx->tables[ti].rows[r];
      return 0;
    }
    case dbGetFieldMeta:
      if (lparam) *(int *)lparam = t->meta_count;
      return (lresult_t)t->meta;
    case dbGetFieldText: {
      db_field_text_t *req = (db_field_text_t *)lparam;
      return req && db_schema_field_text(db, t, req->record, req->field, req->buf, req->buf_sz);
    }
  }
  return 0;
}
