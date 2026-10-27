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
// The record lifecycle follows NeXTSTEP DBKit and EOF:
//   dbInitRecord      fills a new record with the schema's default="" values
//   dbDelete          applies the referring fields' on-delete rules: cascade,
//                     nullify or deny (dbCountReferrers counts the denials)
//   dbBeginChanges    nested change scopes, like editing contexts: commit
//   dbCommitChanges   keeps a scope's changes, revert restores every record
//   dbRevertChanges   as it was, keeping the pointers of surviving records
//
// File format (same shape as SimpleXMLDatabase seeds):
//   <db><accounts><account id="1" code="01" .../></accounts>...</db>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "user.h"
#include "database.h"
#include "i18n.h"
#include <libxml/parser.h>
#include <libxml/tree.h>

typedef struct {
  void **rows;
  int count, capacity, next_id;
} dbs_table_t;

// A change scope's snapshot of the stored tables.
typedef struct {
  void **copies;
  int count, next_id;
} dbs_snapshot_t;

typedef struct dbs_scope_s {
  dbs_snapshot_t *tables;  // parallel to db->schema->tables; transient ones empty
  bool dirty;
  struct dbs_scope_s *outer;
} dbs_scope_t;

typedef struct {
  dbs_table_t *tables;  // parallel to db->schema->tables
  dbs_scope_t *scope;   // innermost open change scope
  dbs_scope_t *baseline; // original rows of an independent document context
  dbs_scope_t *clean;    // opening draft, separate from the delta applied on save
} dbs_context_t;

static database_t *dbs_create_edit_context(database_t *store);

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
    if (*key <= 0) {
      database_t *store = db->edit_parent;
      while (store && store->edit_parent) store = store->edit_parent;
      *key = store ? (int)send_db_message(store, dbReserveKey, (uint32_t)t->table_id, NULL) : rows->next_id;
      if (*key <= 0) { free(record); return NULL; }
    }
    if (*key >= rows->next_id) rows->next_id = *key + 1;
  }
  rows->rows[rows->count++] = record;
  return record;
}

static int dbs_find_row(const db_table_schema_t *t, dbs_table_t *rows, int id) {
  for (int i = 0; i < rows->count; i++)
    if (rows->rows[i] && dbs_key(t, rows->rows[i]) == id) return i;  // a revert in progress leaves gaps
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

// ── Defaults and delete rules ──────────────────────────────────────────────

static void dbs_init_record(const db_table_schema_t *t, void *record) {
  memset(record, 0, t->record_size);
  for (int i = 0; i < t->field_count; i++) {
    const char *value = t->fields[i].default_value;
    int f = db_schema_field_index(t, t->fields[i].name);
    if (value && value[0] && f >= 0) db_parse_field(&t->meta[f], record, value);
  }
}

// Whether a relation ("documents", "ib.documents") names table t of db.
static bool dbs_relation_to(database_t *db, const char *relation, const db_table_schema_t *t) {
  if (!relation || relation[0] == '{') return false;  // polymorphic relations carry no rule
  const char *dot = strchr(relation, '.');
  if (dot) {
    size_t n = (size_t)(dot - relation);
    if (strlen(db->name) != n || strncmp(db->name, relation, n)) return false;
    relation = dot + 1;
  }
  return !strcmp(relation, t->name);
}

typedef bool (*dbs_referrer_fn)(database_t *db, dbs_context_t *ctx, int ti, int field, void *record, void *user);

// Calls fn for each record of this database whose relation field refers to
// record id of table t and whose delete rule is rule.
static void dbs_each_referrer(database_t *db, dbs_context_t *ctx, const db_table_schema_t *t, int id,
                              db_delete_rule_t rule, dbs_referrer_fn fn, void *user) {
  for (int ti = 0; ti < db->schema->table_count; ti++) {
    const db_table_schema_t *r = &db->schema->tables[ti];
    for (int i = 0; i < r->field_count; i++) {
      if (r->fields[i].delete_rule != rule || !dbs_relation_to(db, r->fields[i].relation_table, t)) continue;
      int f = db_schema_field_index(r, r->fields[i].name);
      if (f < 0 || r->meta[f].type != DB_TYPE_INT) continue;
      for (int row = 0; row < ctx->tables[ti].count; row++) {
        void *record = ctx->tables[ti].rows[row];
        if (*(int *)((char *)record + r->meta[f].offset) == id && !fn(db, ctx, ti, f, record, user)) return;
      }
    }
  }
}

static bool dbs_count(database_t *db, dbs_context_t *ctx, int ti, int field, void *record, void *user) {
  (void)db; (void)ctx; (void)ti; (void)field; (void)record;
  (*(int *)user)++;
  return true;
}

static bool dbs_nullify(database_t *db, dbs_context_t *ctx, int ti, int field, void *record, void *user) {
  (void)ctx; (void)user;
  *(int *)((char *)record + db->schema->tables[ti].meta[field].offset) = 0;
  db->dirty |= !db->schema->tables[ti].transient;
  return true;
}

typedef struct { int table_id[64], id[64], count; } dbs_cascade_t;

static bool dbs_collect(database_t *db, dbs_context_t *ctx, int ti, int field, void *record, void *user) {
  (void)ctx; (void)field;
  dbs_cascade_t *c = user;
  if (c->count >= (int)(sizeof(c->id) / sizeof(c->id[0]))) return false;
  c->table_id[c->count] = db->schema->tables[ti].table_id;
  c->id[c->count++] = dbs_key(&db->schema->tables[ti], record);
  return true;
}

static int dbs_count_referrers(database_t *db, dbs_context_t *ctx, const db_table_schema_t *t, int id) {
  int count = 0;
  dbs_each_referrer(db, ctx, t, id, DB_DELETE_DENY, dbs_count, &count);
  return count;
}

static bool dbs_delete(database_t *db, dbs_context_t *ctx, int ti, int id) {
  const db_table_schema_t *t = &db->schema->tables[ti];
  dbs_table_t *rows = &ctx->tables[ti];
  int row = dbs_find_row(t, rows, id);
  if (row < 0) return false;
  int denied = dbs_count_referrers(db, ctx, t, id);
  if (denied) {
    fprintf(stderr, "[db_schema_xml] delete denied db=%s table=%s id=%d: %d record(s) refer to it\n",
            db->name, t->name, id, denied);
    fflush(stderr);
    return false;
  }
  // Cascades go through the database proc, so a subclassing adaptor sees them.
  dbs_cascade_t cascade = { .count = 0 };
  do {
    cascade.count = 0;
    dbs_each_referrer(db, ctx, t, id, DB_DELETE_CASCADE, dbs_collect, &cascade);
    for (int i = 0; i < cascade.count; i++)
      if (!send_db_message(db, dbDelete, (uint32_t)cascade.table_id[i], (void *)(intptr_t)cascade.id[i])) return false;
  } while (cascade.count == (int)(sizeof(cascade.id) / sizeof(cascade.id[0])));
  dbs_each_referrer(db, ctx, t, id, DB_DELETE_NULLIFY, dbs_nullify, NULL);
  row = dbs_find_row(t, rows, id);
  if (row < 0) return true;
  free(rows->rows[row]);
  memmove(&rows->rows[row], &rows->rows[row + 1], (size_t)(rows->count - row - 1) * sizeof(void *));
  rows->count--;
  db->dirty |= !t->transient;
  return true;
}

// ── Change scopes ──────────────────────────────────────────────────────────

static void dbs_free_scope(database_t *db, dbs_scope_t *scope) {
  for (int i = 0; i < db->schema->table_count; i++) {
    for (int r = 0; r < scope->tables[i].count; r++) free(scope->tables[i].copies[r]);
    free(scope->tables[i].copies);
  }
  free(scope->tables);
  free(scope);
}

static bool dbs_begin(database_t *db, dbs_context_t *ctx) {
  dbs_scope_t *scope = calloc(1, sizeof(*scope));
  if (scope) scope->tables = calloc((size_t)db->schema->table_count, sizeof(dbs_snapshot_t));
  if (!scope || !scope->tables) {
    fprintf(stderr, "[db_schema_xml] change scope allocation failed db=%s\n", db->name);
    free(scope);
    return false;
  }
  for (int i = 0; i < db->schema->table_count; i++) {
    const db_table_schema_t *t = &db->schema->tables[i];
    dbs_table_t *rows = &ctx->tables[i];
    dbs_snapshot_t *snap = &scope->tables[i];
    snap->next_id = rows->next_id;
    if (t->transient || !rows->count) continue;
    snap->copies = malloc((size_t)rows->count * sizeof(void *));
    for (int r = 0; snap->copies && r < rows->count; r++) {
      void *copy = malloc(t->record_size);
      if (!copy) break;
      memcpy(copy, rows->rows[r], t->record_size);
      snap->copies[snap->count++] = copy;
    }
    if (snap->count != rows->count) {
      fprintf(stderr, "[db_schema_xml] change scope snapshot failed db=%s table=%s\n", db->name, t->name);
      dbs_free_scope(db, scope);
      return false;
    }
  }
  scope->dirty = db->dirty;
  scope->outer = ctx->scope;
  ctx->scope = scope;
  return true;
}

// Restores each stored table to the snapshot; records that survive keep
// their addresses, so pointers held by views and callers stay valid.
static void dbs_restore(database_t *db, dbs_context_t *ctx, dbs_scope_t *scope) {
  for (int i = 0; i < db->schema->table_count; i++) {
    const db_table_schema_t *t = &db->schema->tables[i];
    if (t->transient) continue;
    dbs_table_t *rows = &ctx->tables[i];
    dbs_snapshot_t *snap = &scope->tables[i];
    // The snapshot already owns an array of the right size. Reuse it so
    // rollback cannot fail while trying to allocate memory under pressure.
    void **restored = snap->copies;
    for (int r = 0; r < snap->count; r++) {
      int row = dbs_key_index(t) >= 0 ? dbs_find_row(t, rows, dbs_key(t, snap->copies[r])) : -1;
      if (row >= 0) {
        void *copy = snap->copies[r];
        restored[r] = rows->rows[row];
        rows->rows[row] = NULL;
        memcpy(restored[r], copy, t->record_size);
        free(copy);
      }
    }
    int restored_count = snap->count;
    snap->copies = NULL;
    snap->count = 0;
    for (int r = 0; r < rows->count; r++) free(rows->rows[r]);  // inserted in the scope
    free(rows->rows);
    rows->rows = restored;
    rows->count = rows->capacity = restored_count;
    rows->next_id = snap->next_id;
  }
  db->dirty = scope->dirty;
}

static bool dbs_end(database_t *db, dbs_context_t *ctx, bool revert) {
  dbs_scope_t *scope = ctx->scope;
  if (!scope) {
    fprintf(stderr, "[db_schema_xml] %s without an open change scope db=%s\n", revert ? "revert" : "commit", db->name);
    fflush(stderr);
    return false;
  }
  if (revert) dbs_restore(db, ctx, scope);
  ctx->scope = scope->outer;
  dbs_free_scope(db, scope);
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
    case dbGetFieldMeta: case dbGetFieldText: case dbInitRecord: case dbCountReferrers:
    case dbReserveKey:
      if ((ti = dbs_table_index(db, LOWORD(wparam))) < 0) return 0;
      break;
  }
  const db_table_schema_t *t = ti >= 0 ? &db->schema->tables[ti] : NULL;

  switch (msg) {
    case dbDestroy:
      while (ctx->scope) dbs_end(db, ctx, false);
      if (ctx->baseline) dbs_free_scope(db, ctx->baseline);
      if (ctx->clean) dbs_free_scope(db, ctx->clean);
      dbs_clear(db, ctx);
      free(ctx->tables);
      free(ctx);
      db->userdata = NULL;
      return 1;
    case dbLoad: return dbs_load(db, ctx);
    case dbSave: return db->dirty ? dbs_save(db, ctx) : 1;
    case dbGetDirty: return db->dirty;
    case dbReserveKey: return ctx->tables[ti].next_id++;
    case dbGetSchema: return (lresult_t)db->schema;
    case dbCreateEditContext: return (lresult_t)dbs_create_edit_context(db);
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
    case dbDelete:         return dbs_delete(db, ctx, ti, (int)(intptr_t)lparam);
    case dbCountReferrers: return dbs_count_referrers(db, ctx, t, (int)(intptr_t)lparam);
    case dbInitRecord:     if (lparam) dbs_init_record(t, lparam); return lparam != NULL;
    case dbBeginChanges:   return dbs_begin(db, ctx);
    case dbCommitChanges:  return dbs_end(db, ctx, false);
    case dbRevertChanges:  return dbs_end(db, ctx, true);
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

static database_t *dbs_create_edit_context(database_t *store) {
  // Delta matching and reserved draft IDs require integer record keys.
  for (int i = 0; i < store->schema->table_count; i++)
    if (!store->schema->tables[i].transient && dbs_key_index(&store->schema->tables[i]) < 0) {
      fprintf(stderr, "[database] document context requires integer keys db=%s table=%s\n",
              store->name, store->schema->tables[i].name);
      fflush(stderr);
      return NULL;
    }
  database_t *copy = create_database_with_schema(store->name, store->class_name, store->schema, "");
  if (!copy || !copy->userdata) { destroy_database(copy); return NULL; }
  dbs_context_t *source = store->userdata, *target = copy->userdata;
  for (int i = 0; i < store->schema->table_count; i++) {
    const db_table_schema_t *table = &store->schema->tables[i];
    for (int r = 0; r < source->tables[i].count; r++)
      if (!dbs_append(copy, table, &target->tables[i], source->tables[i].rows[r])) {
        destroy_database(copy);
        return NULL;
      }
    target->tables[i].next_id = source->tables[i].next_id;
  }
  if (!dbs_begin(copy, target)) { destroy_database(copy); return NULL; }
  target->baseline = target->scope;
  target->scope = NULL;
  copy->edit_parent = store;
  copy->dirty = false;
  return copy;
}

database_t *db_edit_context_create(database_t *store) {
  return (database_t *)send_db_message(store, dbCreateEditContext, 0, NULL);
}

bool db_edit_context_reset(database_t *context) {
  if (!context || !context->edit_parent) return false;
  database_t *fresh = db_edit_context_create(context->edit_parent);
  if (!fresh) return false;
  send_db_message(context, dbDestroy, 0, NULL);
  context->userdata = fresh->userdata;
  context->dirty = false;
  fresh->userdata = NULL;
  free(fresh);
  return true;
}

static void *dbs_original(const db_table_schema_t *table, const dbs_snapshot_t *snapshot, int id) {
  for (int i = 0; i < snapshot->count; i++)
    if (dbs_key(table, snapshot->copies[i]) == id) return snapshot->copies[i];
  return NULL;
}

bool db_edit_context_mark_clean(database_t *db) {
  dbs_context_t *ctx = db ? db->userdata : NULL;
  if (!db || !db->edit_parent || !ctx || !ctx->baseline || ctx->scope) {
    fprintf(stderr, "[database] invalid document clean checkpoint db=%s\n", db ? db->name : "(null)");
    fflush(stderr);
    return false;
  }
  if (!dbs_begin(db, ctx)) return false;
  if (ctx->clean) dbs_free_scope(db, ctx->clean);
  ctx->clean = ctx->scope;
  ctx->scope = NULL;
  return true;
}

bool db_edit_context_has_changes(database_t *db) {
  dbs_context_t *ctx = db ? db->userdata : NULL;
  if (!db || !db->edit_parent || !ctx || !ctx->baseline) {
    fprintf(stderr, "[database] invalid document change query db=%s\n", db ? db->name : "(null)");
    fflush(stderr);
    return true;
  }
  dbs_scope_t *clean = ctx->clean ? ctx->clean : ctx->baseline;
  for (int i = 0; i < db->schema->table_count; i++) {
    const db_table_schema_t *table = &db->schema->tables[i];
    if (table->transient) continue;
    dbs_snapshot_t *original = &clean->tables[i];
    dbs_table_t *rows = &ctx->tables[i];
    if (rows->count != original->count) return true;
    for (int r = 0; r < rows->count; r++) {
      int id = dbs_key(table, rows->rows[r]);
      const void *before = dbs_key(table, original->copies[r]) == id ? original->copies[r] : dbs_original(table, original, id);
      if (!before || !db_record_equal(table->meta, table->meta_count, before, rows->rows[r])) return true;
    }
  }
  return false;
}

static bool dbs_edit_relations_unchanged(database_t *db, dbs_context_t *ctx,
                                        const db_table_schema_t *table, const void *record) {
  // A stale editor must not resurrect a reference whose target was deleted.
  for (int f = 0; f < table->field_count; f++) {
    int field = db_schema_field_index(table, table->fields[f].name);
    if (field < 0 || table->meta[field].type != DB_TYPE_INT) continue;
    int id = *(const int *)((const char *)record + table->meta[field].offset);
    if (id <= 0) continue;
    for (int i = 0; i < db->schema->table_count; i++) {
      const db_table_schema_t *target = &db->schema->tables[i];
      int key = dbs_key_index(target);
      if (target->transient || key < 0 || !dbs_relation_to(db, table->fields[f].relation_table, target)) continue;
      const char *foreign = table->fields[f].relation_field;
      if (foreign && strcmp(foreign, target->meta[key].name)) continue;
      if (dbs_original(target, &ctx->baseline->tables[i], id) &&
          !send_db_message(db->edit_parent, dbFind, (uint32_t)target->table_id, (void *)(intptr_t)id)) return false;
    }
  }
  return true;
}

bool db_edit_context_save(database_t *db, char *error, size_t error_size) {
  if (error && error_size) error[0] = 0;
  dbs_context_t *ctx = db ? db->userdata : NULL;
  if (!db || !db->edit_parent || !ctx || !ctx->baseline || ctx->scope) {
    fprintf(stderr, "[database] invalid document context save db=%s\n", db ? db->name : "(null)");
    fflush(stderr);
    return false;
  }
  database_t *store = db->edit_parent;
  // Validate every changed/deleted row before touching the committed store.
  for (int i = 0; i < db->schema->table_count; i++) {
    const db_table_schema_t *table = &db->schema->tables[i];
    if (table->transient) continue;
    dbs_snapshot_t *original = &ctx->baseline->tables[i];
    dbs_table_t *rows = &ctx->tables[i];
    for (int r = 0; r < original->count; r++) {
      int id = dbs_key(table, original->copies[r]);
      int local = dbs_find_row(table, rows, id);
      if (local >= 0 && !memcmp(rows->rows[local], original->copies[r], table->record_size)) continue;
      const void *current = (const void *)send_db_message(store, dbFind, (uint32_t)table->table_id, (void *)(intptr_t)id);
      if (!current || memcmp(current, original->copies[r], table->record_size)) goto conflict;
    }
    for (int r = 0; r < rows->count; r++) {
      int id = dbs_key(table, rows->rows[r]);
      void *before = dbs_original(table, original, id);
      if (before && !memcmp(before, rows->rows[r], table->record_size)) continue;
      if (!dbs_edit_relations_unchanged(db, ctx, table, rows->rows[r])) goto conflict;
      if (!before &&
          send_db_message(store, dbFind, (uint32_t)table->table_id, (void *)(intptr_t)id)) goto conflict;
    }
  }
  if (!send_db_message(store, dbBeginChanges, 0, NULL)) goto failed;
  database_t *previous = db_set_message_context(store);
  bool ok = true;
  // Insert masters before their detail tables, following schema order.
  for (int i = 0; ok && i < db->schema->table_count; i++) {
    const db_table_schema_t *table = &db->schema->tables[i];
    if (table->transient) continue;
    dbs_snapshot_t *original = &ctx->baseline->tables[i];
    dbs_table_t *rows = &ctx->tables[i];
    for (int r = 0; ok && r < rows->count; r++) {
      void *before = dbs_original(table, original, dbs_key(table, rows->rows[r]));
      if (before && !memcmp(before, rows->rows[r], table->record_size)) continue;
      ok = send_db_message(store, before ? dbUpdate : dbInsert, (uint32_t)table->table_id, rows->rows[r]) != 0;
    }
  }
  // Delete details first; an earlier cascade may have already removed one.
  for (int i = db->schema->table_count - 1; ok && i >= 0; i--) {
    const db_table_schema_t *table = &db->schema->tables[i];
    if (table->transient) continue;
    dbs_snapshot_t *original = &ctx->baseline->tables[i];
    for (int r = 0; ok && r < original->count; r++) {
      int id = dbs_key(table, original->copies[r]);
      if (dbs_find_row(table, &ctx->tables[i], id) >= 0) continue;
      if (send_db_message(store, dbFind, (uint32_t)table->table_id, (void *)(intptr_t)id))
        ok = send_db_message(store, dbDelete, (uint32_t)table->table_id, (void *)(intptr_t)id) != 0;
    }
  }
  if (ok) ok = send_db_message(store, dbCommitChanges, 0, NULL) != 0;
  if (!ok) send_db_message(store, dbRevertChanges, 0, NULL);
  db_set_message_context(previous);
  if (!ok) goto failed;
  return true;
conflict:
  if (error && error_size) snprintf(error, error_size, "%s", ui_tr("The record was changed in another window. Close and reopen this document."));
  return false;
failed:
  if (error && error_size) snprintf(error, error_size, "%s", ui_tr("The document could not be saved. No changes were applied."));
  return false;
}
