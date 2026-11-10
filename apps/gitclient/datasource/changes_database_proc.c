// Changes-tab database — minimal in-memory table of working-tree files from git status.
// No commit-id filtering, no history data. Cleared and repopulated on every dbLoad.

#include "gitclient.h"
#include <platform/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Single-table schema: only working-tree files (no commit_id).
// Uses the same TABLE_FILES ID so tableviews bound to "source=db.files" work unchanged.
static const db_field_schema_t changes_files_schema[] = {
  { "id", DB_TYPE_INT, 0, true, NULL, NULL },
  { "commit_id", DB_TYPE_INT, 0, false, NULL, NULL },
  { "path", DB_TYPE_STRING, 512, false, NULL, NULL },
  { "status", DB_TYPE_STRING, 2, false, NULL, NULL },
  { "staged", DB_TYPE_BOOL, 0, false, NULL, NULL },
};

static db_table_schema_t changes_database_tables[] = {
  { TABLE_FILES, "files", NULL, changes_files_schema, 5, NULL, 0 },
};

static db_schema_def_t changes_database_schema = {
  .name = NULL, .class_name = NULL, .source_path = NULL,
  .tables = changes_database_tables, .table_count = 1,
};

// Object proc for files — reuses generated file_object_proc
static result_t changes_file_object_proc(const void *object, uint32_t msg,
                                          uint32_t wparam, void *lparam) {
  if (msg != dbObjGetFieldText || !object || !lparam) return false;
  const db_file_t *f = (const db_file_t *)object;
  int field_index = (int)LOWORD(wparam) - GC_COL_FILE_ID;
  size_t buf_sz = (size_t)HIWORD(wparam);
  char *buf = (char *)lparam;
  if (field_index < 0 || field_index >= 5 || buf_sz == 0) return false;
  switch (field_index) {
    case 0: snprintf(buf, buf_sz, "%d", f->id);          return true;
    case 1: snprintf(buf, buf_sz, "%d", f->commit_id);   return true;
    case 2: snprintf(buf, buf_sz, "%s", f->path);         return true;
    case 3: snprintf(buf, buf_sz, "%s", gc_status_label(f->status[0])); return true;
    case 4: snprintf(buf, buf_sz, "%d", f->staged ? 1 : 0); return true;
  }
  return false;
}

static const db_field_msg_binding_t changes_file_bindings[] = {
  { "id", GC_COL_FILE_ID }, { "commit_id", GC_COL_FILE_COMMIT_ID },
  { "path", GC_COL_FILE_PATH },
  { "status", GC_COL_FILE_STATUS }, { "staged", GC_COL_FILE_STAGED },
};

// Per-database state
typedef struct {
  git_repo_t *repo;
  db_file_t  *files;
  int         file_count;
  int         file_capacity;
  int         next_id;
} changes_db_ctx_t;

static void changes_clear_table(void **data, int *count, int *capacity, int *next_id) {
  if (*data) free(*data);
  *data = NULL; *count = 0; *capacity = 0;
  if (next_id) *next_id = 0;
}

static void *changes_append_row(void **data, int *count, int *capacity,
                                 size_t record_size, int *next_id, int chunk) {
  if (*count >= *capacity) {
    int new_cap = *capacity + chunk;
    void *grown = realloc(*data, (size_t)new_cap * record_size);
    if (!grown) return NULL;
    *data = grown; *capacity = new_cap;
  }
  void *row = (char *)(*data) + (size_t)(*count) * record_size;
  memset(row, 0, record_size);
  int *id_field = (int *)row;
  *id_field = (*next_id)++;
  (*count)++;
  return row;
}

static result_node_t *changes_fetch_all(db_file_t *data, int count, size_t rec_size) {
  if (!data || count == 0) return NULL;
  result_node_t *head = NULL, *tail = NULL;
  for (int i = 0; i < count; i++) {
    char *row = (char *)data + (size_t)i * rec_size;
    result_node_t *node = malloc(sizeof(result_node_t) + sizeof(void *));
    node->next = NULL;
    *(void **)node->data = row;
    if (tail) tail->next = node;
    else head = node;
    tail = node;
  }
  return head;
}

lresult_t changes_database_proc(database_t *db, uint32_t msg,
                                        uint32_t wparam, void *lparam) {
  changes_db_ctx_t *ctx = (changes_db_ctx_t *)db->userdata;

  switch (msg) {
    case dbCreate:
      ctx = calloc(1, sizeof(changes_db_ctx_t));
      if (!ctx) return 0;
      db->userdata = ctx;
      return 1;

    case dbDestroy:
      if (ctx) {
        changes_clear_table((void **)&ctx->files, &ctx->file_count,
                            &ctx->file_capacity, &ctx->next_id);
        free(ctx); db->userdata = NULL;
      }
      return 1;

    case dbLoad: {
      // wparam = GC_DB_LOAD_SNAPSHOT: lparam is a git_repo_snapshot_t read elsewhere (a worker thread); otherwise lparam is the repo.
      if (!ctx || !lparam) return 0;
      git_repo_snapshot_t *own = NULL, *snap = (git_repo_snapshot_t *)lparam;
      if (wparam != GC_DB_LOAD_SNAPSHOT) {
        git_repo_t *repo = (git_repo_t *)lparam;
        own = snap = git_repo_snapshot_read(git_repo_path(repo), GIT_SNAP_FILES, 0);
        if (!snap) return 0;
        snap->repo = repo;
      }
      ctx->repo = snap->repo;
      changes_clear_table((void **)&ctx->files, &ctx->file_count,
                          &ctx->file_capacity, &ctx->next_id);
      for (int i = 0; i < snap->file_count; i++) {
        db_file_t *rec = changes_append_row((void **)&ctx->files, &ctx->file_count,
                                             &ctx->file_capacity, sizeof(db_file_t),
                                             &ctx->next_id, 64);
        if (!rec) break;
        rec->commit_id = 0;
        strncpy(rec->path, snap->files[i].path, sizeof(rec->path) - 1);
        rec->status[0] = snap->files[i].status;
        rec->status[1] = '\0';
        rec->staged = snap->files[i].staged;
      }
      free(own);
      return 1;
    }

    case dbFetch: {
      if (!ctx) return (lresult_t)NULL;
      int table_id = LOWORD(wparam);
      if (table_id == TABLE_FILES)
        return (lresult_t)changes_fetch_all(ctx->files, ctx->file_count,
                                             sizeof(db_file_t));
      return (lresult_t)NULL;
    }

    case dbGetObjectProc:
      if (wparam == TABLE_FILES)
        return (lresult_t)changes_file_object_proc;
      return (lresult_t)NULL;

    case dbGetFieldBindings: {
      int *count_out = (int *)lparam;
      if (wparam == TABLE_FILES) {
        if (count_out) *count_out = 5;
        return (lresult_t)changes_file_bindings;
      }
      if (count_out) *count_out = 0;
      return (lresult_t)NULL;
    }

    case dbGetSchema:
      changes_database_schema.name = db->name;
      changes_database_schema.class_name = db->class_name;
      changes_database_schema.source_path = db->source_path;
      return (lresult_t)&changes_database_schema;

    case dbGetFieldMeta: {
      int *count_out = (int *)lparam;
      if (wparam == TABLE_FILES) {
        if (count_out) *count_out = ARRAY_LEN(changes_files_schema);
        return (lresult_t)changes_files_schema;
      }
      if (count_out) *count_out = 0;
      return (lresult_t)NULL;
    }

    default:
      return 0;
  }
  return 0;
}
