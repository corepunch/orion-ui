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
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <direct.h>
#define DBS_BINARY _O_BINARY
#else
#include <sys/file.h>
#include <unistd.h>
#define DBS_BINARY 0
#endif

#include "user.h"
#include "database.h"
#include "i18n.h"
#include <libxml/parser.h>
#include <libxml/tree.h>

#ifdef _WIN32
static bool dbs_resolve_directory(const char *directory, char *out, size_t size) {
  HANDLE handle = CreateFileA(directory, FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                              OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
  if (handle == INVALID_HANDLE_VALUE) { errno = EACCES; return false; }
  typedef DWORD (WINAPI *dbs_final_path_fn)(HANDLE, LPSTR, DWORD, DWORD);
  dbs_final_path_fn final_path = (dbs_final_path_fn)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetFinalPathNameByHandleA");
  char resolved[4096];
  DWORD length = final_path ? final_path(handle, resolved, sizeof(resolved), 0) : 0;
  CloseHandle(handle);
  if (!length || length >= sizeof(resolved)) { errno = EIO; return false; }
  const char *path = resolved;
  if (!strncmp(path, "\\\\?\\UNC\\", 8)) {
    if (snprintf(out, size, "\\\\%s", path + 8) >= (int)size) { errno = ENAMETOOLONG; return false; }
  } else {
    if (!strncmp(path, "\\\\?\\", 4)) path += 4;
    if (snprintf(out, size, "%s", path) >= (int)size) { errno = ENAMETOOLONG; return false; }
  }
  return true;
}
static bool dbs_lock_file(int fd) {
  OVERLAPPED offset = {0};
  if (LockFileEx((HANDLE)_get_osfhandle(fd), LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                 0, MAXDWORD, MAXDWORD, &offset)) return true;
  errno = EACCES; return false;
}
static int dbs_sync_file(int fd) { return _commit(fd); }
static int dbs_replace(const char *from, const char *to) {
  if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return 0;
  errno = EACCES; return -1;
}
static int dbs_link(const char *from, const char *to) {
  if (CreateHardLinkA(to, from, NULL)) return 0;
  errno = EACCES; return -1;
}
static int dbs_temporary(char *path, size_t size) {
  char directory[MAX_PATH], generated[MAX_PATH];
  if (strlen(path) >= sizeof(directory)) { errno = ENAMETOOLONG; return -1; }
  snprintf(directory, sizeof(directory), "%s", path);
  char *slash = strrchr(directory, '/'), *backslash = strrchr(directory, '\\');
  if (!slash || (backslash && backslash > slash)) slash = backslash;
  if (slash) *slash = 0; else strcpy(directory, ".");
  if (!GetTempFileNameA(directory, "ori", 0, generated)) { errno = EIO; return -1; }
  if (strlen(generated) >= size) { DeleteFileA(generated); errno = ENAMETOOLONG; return -1; }
  snprintf(path, size, "%s", generated);
  int fd = _open(path, _O_RDWR | _O_BINARY);
  if (fd < 0) DeleteFileA(path);
  return fd;
}
#else
static bool dbs_lock_file(int fd) { return flock(fd, LOCK_EX | LOCK_NB) == 0; }
static int dbs_sync_file(int fd) { return fsync(fd); }
static int dbs_replace(const char *from, const char *to) { return rename(from, to); }
static int dbs_link(const char *from, const char *to) { return link(from, to); }
static int dbs_temporary(char *path, size_t size) { (void)size; return mkstemp(path); }
#endif

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
  int lock_fd;
  unsigned delete_depth;
  char history[512];
  char locked_path[sizeof(((database_t *)0)->source_path)];
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
  fflush(stderr); return -1;
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

static int dbs_find_row(const db_table_schema_t *t, dbs_table_t *rows, int id);

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
      if (*key <= 0) {
        fprintf(stderr, "[db_schema_xml] key space exhausted db=%s table=%s\n", db->name, t->name);
        fflush(stderr); free(record); return NULL;
      }
    }
    if (dbs_find_row(t, rows, *key) >= 0) {
      fprintf(stderr, "[db_schema_xml] duplicate key db=%s table=%s id=%d\n", db->name, t->name, *key);
      fflush(stderr); free(record); return NULL;
    }
    if (rows->next_id > 0 && *key >= rows->next_id)
      rows->next_id = *key == INT_MAX ? 0 : *key + 1;
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
    case DB_TYPE_INT64:  return sizeof(intptr_t) >= sizeof(int64_t) && *(const int64_t *)p == (int64_t)(intptr_t)value;
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

static bool dbs_lock_path(database_t *db, dbs_context_t *ctx, const char *path, bool must_be_new) {
  char directory[sizeof(db->source_path)], canonical[sizeof(db->source_path)];
  char resolved[4096], lock_path[sizeof(db->source_path) + 16];
  if (!path || !path[0] || strlen(path) >= sizeof(directory)) return false;
  snprintf(directory, sizeof(directory), "%s", path);
  char *slash = strrchr(directory, '/');
#ifdef _WIN32
  char *backslash = strrchr(directory, '\\');
  if (!slash || (backslash && backslash > slash)) slash = backslash;
#endif
  char name[sizeof(db->source_path)];
  snprintf(name, sizeof(name), "%s", slash ? slash + 1 : path);
  if (!name[0]) return false;
  if (slash) {
#ifdef _WIN32
    if (slash == directory + 2 && directory[1] == ':') slash[1] = 0;
    else
#endif
    if (slash == directory) slash[1] = 0; else *slash = 0;
  } else strcpy(directory, ".");
#ifdef _WIN32
  if (!dbs_resolve_directory(directory, resolved, sizeof(resolved)) || snprintf(canonical, sizeof(canonical), "%s/%s", resolved, name) >= (int)sizeof(canonical)) return false;
  DWORD attributes = GetFileAttributesA(canonical);
  if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) return false;
#else
  if (!realpath(directory, resolved) || snprintf(canonical, sizeof(canonical), "%s/%s", resolved, name) >= (int)sizeof(canonical)) return false;
  struct stat st;
  if (lstat(canonical, &st) == 0 && (S_ISLNK(st.st_mode) || !S_ISREG(st.st_mode))) return false;
#endif
  snprintf(lock_path, sizeof(lock_path), "%s.lock", canonical);
  int fd = open(lock_path, O_CREAT | O_RDWR, 0600);
  if (fd < 0 || !dbs_lock_file(fd)) { if (fd >= 0) close(fd); return false; }
  if (must_be_new) {
    struct stat target;
    if (stat(canonical, &target) == 0) { close(fd); errno = EEXIST; return false; }
    if (errno != ENOENT) { close(fd); return false; }
  }
  if (ctx->lock_fd >= 0) close(ctx->lock_fd);
  ctx->lock_fd = fd;
  snprintf(ctx->locked_path, sizeof(ctx->locked_path), "%s", canonical);
  snprintf(db->source_path, sizeof(db->source_path), "%s", canonical);
  return true;
}

bool db_schema_xml_seed_to(database_t *db, const char *path) {
  dbs_context_t *ctx = db ? db->userdata : NULL;
  if (!ctx || !dbs_lock_path(db, ctx, path, true)) {
    fprintf(stderr, "[db_schema_xml] cannot lock new destination db=%s path=%s: %s\n",
            db ? db->name : "(null)", path ? path : "(null)", strerror(errno));
    fflush(stderr);
    return false;
  }
  db->read_only = false;
  db->dirty = true;
  return true;
}

static bool dbs_sync_directory(const char *path) {
#ifdef _WIN32
  (void)path; return true;
#else
  char directory[sizeof(((database_t *)0)->source_path)];
  snprintf(directory, sizeof(directory), "%s", path);
  char *slash = strrchr(directory, '/');
  if (slash) { if (slash == directory) slash[1] = 0; else *slash = 0; }
  else strcpy(directory, ".");
  int fd = open(directory, O_RDONLY);
  if (fd < 0) return false;
  bool ok = fsync(fd) == 0;
  close(fd);
  return ok;
#endif
}

static bool dbs_save(database_t *db, dbs_context_t *ctx);

bool db_schema_xml_migrate_additive(void *root, unsigned from_version) {
  (void)root; (void)from_version;
  return true;
}

static void dbs_init_record(const db_table_schema_t *t, void *record);

static bool dbs_read_history(dbs_context_t *ctx, xmlNodePtr root, unsigned version) {
  ctx->history[0] = 0;
  xmlChar *history = xmlGetProp(root, BAD_CAST "orion-history");
  if (!history) return true;
  const char *p = (const char *)history;
  unsigned long previous = 0;
  bool valid = *p && strlen(p) < sizeof(ctx->history);
  while (valid && *p) {
    char *end = NULL; errno = 0;
    unsigned long value = strtoul(p, &end, 10);
    if (errno || end == p || !value || value > UINT32_MAX || (previous && value != previous + 1)) { valid = false; break; }
    previous = value;
    if (!*end) { p = end; break; }
    if (*end != ',' || !end[1]) { valid = false; break; }
    p = end + 1;
  }
  valid = valid && previous == version;
  if (valid) snprintf(ctx->history, sizeof(ctx->history), "%s", (char *)history);
  xmlFree(history);
  return valid;
}

static bool dbs_append_history(dbs_context_t *ctx, unsigned version) {
  size_t length = strlen(ctx->history);
  int n = snprintf(ctx->history + length, sizeof(ctx->history) - length, "%s%u", length ? "," : "", version);
  return n >= 0 && (size_t)n < sizeof(ctx->history) - length;
}

static xmlChar *dbs_field_xml_value(xmlNodePtr row, const db_field_meta_t *field) {
  xmlChar *value = xmlGetProp(row, BAD_CAST field->name);
  for (xmlNodePtr child = row->children; child && !value; child = child->next)
    if (child->type == XML_ELEMENT_NODE && !xmlStrcmp(child->name, BAD_CAST field->name))
      value = xmlNodeGetContent(child);
  if (!value && (!strcmp(field->name, "body") || !strcmp(field->name, "text")))
    value = xmlNodeGetContent(row);
  return value;
}

static bool dbs_load(database_t *db, dbs_context_t *ctx) {
  dbs_clear(db, ctx);
  if (!db->source_path[0]) return true;
  struct stat st;
  if (stat(db->source_path, &st) != 0) {
    if (errno == ENOENT && !db->read_only) return true;
    fprintf(stderr, "[db_schema_xml] cannot inspect db=%s path=%s: %s\n", db->name, db->source_path, strerror(errno));
    fflush(stderr); db->open_failed = true; return false;
  }
  xmlDocPtr doc = xmlReadFile(db->source_path, NULL, XML_PARSE_NONET);
  xmlNodePtr root = doc ? xmlDocGetRootElement(doc) : NULL;
  xmlChar *version = root ? xmlGetProp(root, BAD_CAST "orion-version") : NULL;
  unsigned current = db->schema->version ? db->schema->version : 1, found = 1;
  bool compatible = true;
  if (version) {
    char *end = NULL; errno = 0; unsigned long parsed = strtoul((const char *)version, &end, 10);
    compatible = !errno && end && *end == 0 && end != (char *)version && parsed > 0 && parsed <= current;
    if (compatible) found = (unsigned)parsed;
    xmlFree(version);
  }
  if (!root || (xmlStrcmp(root->name, BAD_CAST db->name) &&
                (!db->schema->name || xmlStrcmp(root->name, BAD_CAST db->schema->name))) || !compatible ||
      (found < current && !db->schema->migrate_xml)) {
    fprintf(stderr, "[db_schema_xml] incompatible or invalid db=%s path=%s version=%u supported=%u\n",
            db->name, db->source_path, found, current);
    fflush(stderr);
    if (doc) xmlFreeDoc(doc);
    db->open_failed = true;
    return false;
  }
  if (!dbs_read_history(ctx, root, found)) {
    fprintf(stderr, "[db_schema_xml] invalid migration history db=%s path=%s\n", db->name, db->source_path);
    fflush(stderr); xmlFreeDoc(doc); db->open_failed = true; return false;
  }
  bool migrated = found < current;
  if (migrated && !ctx->history[0] && !dbs_append_history(ctx, found)) {
    xmlFreeDoc(doc); db->open_failed = true; return false;
  }
  while (found < current) {
    if (!db->schema->migrate_xml(root, found)) {
      fprintf(stderr, "[db_schema_xml] migration failed db=%s from=%u\n", db->name, found);
      fflush(stderr); xmlFreeDoc(doc); db->open_failed = true; return false;
    }
    found++;
    if (!dbs_append_history(ctx, found)) {
      fprintf(stderr, "[db_schema_xml] migration history too long db=%s\n", db->name);
      fflush(stderr); xmlFreeDoc(doc); db->open_failed = true; return false;
    }
  }
  for (xmlAttrPtr a = root->properties; a; a = a->next) {
    if (xmlStrcmp(a->name, BAD_CAST "orion-version") && xmlStrcmp(a->name, BAD_CAST "orion-history")) {
      fprintf(stderr, "[db_schema_xml] unknown root attribute db=%s attribute=%s\n", db->name, a->name);
      fflush(stderr); xmlFreeDoc(doc); db->open_failed = true; return false;
    }
  }
  for (xmlNodePtr tn = root->children; tn; tn = tn->next) {
    if (tn->type != XML_ELEMENT_NODE) continue;
    const db_table_schema_t *t = db_schema_table_named(db->schema, (const char *)tn->name);
    if (!t) {
      fprintf(stderr, "[db_schema_xml] unknown table db=%s table=%s\n", db->name, tn->name);
      fflush(stderr); xmlFreeDoc(doc); dbs_clear(db, ctx); db->open_failed = true; return false;
    }
    if (tn->properties) {
      fprintf(stderr, "[db_schema_xml] unknown table attribute db=%s table=%s attribute=%s\n",
              db->name, t->name, tn->properties->name);
      fflush(stderr); xmlFreeDoc(doc); dbs_clear(db, ctx); db->open_failed = true; return false;
    }
    int ti = (int)(t - db->schema->tables);
    void *record = calloc(1, t->record_size);
    if (!record) {
      fprintf(stderr, "[db_schema_xml] load allocation failed db=%s table=%s\n", db->name, t->name);
      fflush(stderr); xmlFreeDoc(doc); dbs_clear(db, ctx); db->open_failed = true; return false;
    }
    for (xmlNodePtr rn = tn->children; rn; rn = rn->next) {
      if (rn->type != XML_ELEMENT_NODE) continue;
      for (xmlAttrPtr a = rn->properties; a; a = a->next)
        if (db_schema_field_index(t, (const char *)a->name) < 0) {
          fprintf(stderr, "[db_schema_xml] unknown field db=%s table=%s field=%s\n", db->name, t->name, a->name);
          fflush(stderr); free(record); xmlFreeDoc(doc); dbs_clear(db, ctx); db->open_failed = true; return false;
        }
      for (xmlNodePtr child = rn->children; child; child = child->next) {
        if (child->type != XML_ELEMENT_NODE) continue;
        if (db_schema_field_index(t, (const char *)child->name) < 0) {
          fprintf(stderr, "[db_schema_xml] unknown field db=%s table=%s field=%s\n", db->name, t->name, child->name);
          fflush(stderr); free(record); xmlFreeDoc(doc); dbs_clear(db, ctx); db->open_failed = true; return false;
        }
        bool duplicate = xmlHasProp(rn, child->name) != NULL;
        for (xmlNodePtr earlier = rn->children; earlier != child && !duplicate; earlier = earlier->next)
          duplicate = earlier->type == XML_ELEMENT_NODE && !xmlStrcmp(earlier->name, child->name);
        if (duplicate) {
          fprintf(stderr, "[db_schema_xml] duplicate field db=%s table=%s field=%s\n", db->name, t->name, child->name);
          fflush(stderr); free(record); xmlFreeDoc(doc); dbs_clear(db, ctx); db->open_failed = true; return false;
        }
      }
      dbs_init_record(t, record);
      for (int f = 0; f < t->meta_count; f++) {
        if (t->meta[f].type == DB_TYPE_INT) {
          xmlChar *value = dbs_field_xml_value(rn, &t->meta[f]);
          if (value) {
            char *end = NULL; errno = 0;
            long parsed = strtol((const char *)value, &end, 10);
            bool valid = !errno && end != (char *)value && !*end && parsed >= INT_MIN && parsed <= INT_MAX;
            xmlFree(value);
            if (!valid) {
              fprintf(stderr, "[db_schema_xml] invalid int db=%s table=%s field=%s\n", db->name, t->name, t->meta[f].name);
              fflush(stderr); free(record); xmlFreeDoc(doc); dbs_clear(db, ctx); db->open_failed = true; return false;
            }
          }
        }
        if (t->meta[f].type == DB_TYPE_STRING || t->meta[f].type == DB_TYPE_DATE) {
          xmlChar *value = dbs_field_xml_value(rn, &t->meta[f]);
          if (value) {
            const char *start = (const char *)value, *end = start + strlen(start);
            while (*start && isspace((unsigned char)*start)) start++;
            while (end > start && isspace((unsigned char)end[-1])) end--;
            bool valid = t->meta[f].length > 0 && (size_t)(end - start) < (size_t)t->meta[f].length;
            xmlFree(value);
            if (!valid) {
              fprintf(stderr, "[db_schema_xml] text exceeds field capacity db=%s table=%s field=%s capacity=%d\n",
                      db->name, t->name, t->meta[f].name, t->meta[f].length);
              fflush(stderr); free(record); xmlFreeDoc(doc); dbs_clear(db, ctx); db->open_failed = true; return false;
            }
          }
        }
        bool loaded = db_load_field_from_xml(rn, record, &t->meta[f]);
        if (!loaded && t->meta[f].type == DB_TYPE_INT64) {
          bool present = xmlHasProp(rn, BAD_CAST t->meta[f].name) != NULL;
          for (xmlNodePtr child = rn->children; child && !present; child = child->next)
            present = child->type == XML_ELEMENT_NODE && !xmlStrcmp(child->name, BAD_CAST t->meta[f].name);
          if (present) {
            fprintf(stderr, "[db_schema_xml] invalid int64 db=%s table=%s field=%s\n", db->name, t->name, t->meta[f].name);
            fflush(stderr); free(record); xmlFreeDoc(doc); dbs_clear(db, ctx); db->open_failed = true; return false;
          }
        }
      }
      if (!dbs_append(db, t, &ctx->tables[ti], record)) {
        free(record); xmlFreeDoc(doc); dbs_clear(db, ctx); db->open_failed = true; return false;
      }
    }
    free(record);
  }
  xmlFreeDoc(doc);
  db->dirty = migrated && !db->read_only;
  return true;
}

static bool dbs_save(database_t *db, dbs_context_t *ctx) {
  if (!db->source_path[0]) return false;
  if (strcmp(db->source_path, ctx->locked_path)) {
    fprintf(stderr, "[db_schema_xml] unlocked destination db=%s path=%s\n", db->name, db->source_path);
    fflush(stderr);
    return false;
  }
  xmlDocPtr doc = xmlNewDoc((const xmlChar *)"1.0");
  xmlNodePtr root = xmlNewNode(NULL, (const xmlChar *)db->name);
  if (!doc || !root) {
    if (doc) xmlFreeDoc(doc);
    if (root) xmlFreeNode(root);
    fprintf(stderr, "[db_schema_xml] document allocation failed db=%s\n", db->name);
    fflush(stderr);
    return false;
  }
  xmlDocSetRootElement(doc, root);
  for (int i = 0; i < db->schema->table_count; i++) {
    const db_table_schema_t *t = &db->schema->tables[i];
    if (t->transient) continue;
    char tag[128];
    dbs_row_tag(t->name, tag, sizeof(tag));
    xmlNodePtr tn = xmlNewChild(root, NULL, (const xmlChar *)t->name, NULL);
    if (!tn) { xmlFreeDoc(doc); return false; }
    for (int r = 0; r < ctx->tables[i].count; r++) {
      xmlNodePtr row = db_save_record_to_xml(tn, tag, ctx->tables[i].rows[r], t->meta, t->meta_count);
      if (!row) { xmlFreeDoc(doc); return false; }
      for (int f = 0; f < t->field_count; f++)
        if (t->fields[f].computed && *t->fields[f].computed) xmlUnsetProp(row, BAD_CAST t->fields[f].name);
    }
  }
  char version[16];
  snprintf(version, sizeof(version), "%u", db->schema->version ? db->schema->version : 1);
  if (!xmlSetProp(root, BAD_CAST "orion-version", BAD_CAST version) ||
      (ctx->history[0] && !xmlSetProp(root, BAD_CAST "orion-history", BAD_CAST ctx->history))) {
    xmlFreeDoc(doc); return false;
  }
  char temporary[sizeof(db->source_path) + 32], backup[sizeof(db->source_path) + 32];
  snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX", db->source_path);
  snprintf(backup, sizeof(backup), "%s.bak", db->source_path);
  int fd = dbs_temporary(temporary, sizeof(temporary));
  bool ok = fd >= 0;
  xmlChar *data = NULL; int length = 0;
  xmlDocDumpFormatMemoryEnc(doc, &data, &length, "UTF-8", 1);
  if (!data || length <= 0) ok = false;
  for (int offset = 0; ok && offset < length;) {
    ssize_t n = write(fd, data + offset, (size_t)(length - offset));
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) ok = false; else offset += (int)n;
  }
  if (data) xmlFree(data);
  xmlFreeDoc(doc);
  if (ok && dbs_sync_file(fd) != 0) ok = false;
  if (fd >= 0 && close(fd) != 0) ok = false;
  // Keep the previous committed inode until the replacement is durable.
  char backup_tmp[sizeof(db->source_path) + 32];
  snprintf(backup_tmp, sizeof(backup_tmp), "%s.bak.tmp", db->source_path);
  if (ok && access(db->source_path, F_OK) == 0) {
    unlink(backup_tmp);
    if (dbs_link(db->source_path, backup_tmp) != 0 || dbs_replace(backup_tmp, backup) != 0 ||
        !dbs_sync_directory(db->source_path)) ok = false;
  }
  if (ok && dbs_replace(temporary, db->source_path) != 0) ok = false;
  if (ok && !dbs_sync_directory(db->source_path)) ok = false;
  if (!ok) {
    fprintf(stderr, "[db_schema_xml] cannot commit db=%s path=%s: %s\n", db->name, db->source_path, strerror(errno));
    fflush(stderr);
    unlink(temporary); unlink(backup_tmp);
    return false;
  }
  db->dirty = false;
  return true;
}

bool db_schema_xml_restore_backup(database_t *db) {
  dbs_context_t *ctx = db ? db->userdata : NULL;
  if (!ctx || db->read_only || db->dirty || ctx->scope || ctx->lock_fd < 0 ||
      strcmp(db->source_path, ctx->locked_path)) goto failed;
  char backup[sizeof(db->source_path) + 16], temporary[sizeof(db->source_path) + 32];
  snprintf(backup, sizeof(backup), "%s.bak", db->source_path);
  xmlDocPtr doc = xmlReadFile(backup, NULL, XML_PARSE_NONET);
  xmlNodePtr root = doc ? xmlDocGetRootElement(doc) : NULL;
  xmlChar *version = root ? xmlGetProp(root, BAD_CAST "orion-version") : NULL;
  char *end = NULL;
  unsigned long found = version ? strtoul((char *)version, &end, 10) : 1;
  unsigned current = db->schema->version ? db->schema->version : 1;
  bool valid = root && (!xmlStrcmp(root->name, BAD_CAST db->name) ||
                        (db->schema->name && !xmlStrcmp(root->name, BAD_CAST db->schema->name))) &&
               found > 0 && found <= current &&
               (!version || (end != (char *)version && *end == 0)) &&
               (found == current || db->schema->migrate_xml);
  dbs_context_t history_context = {0};
  if (valid && !dbs_read_history(&history_context, root, (unsigned)found)) valid = false;
  unsigned long checking = found;
  while (valid && checking < current) {
    if (!db->schema->migrate_xml(root, (unsigned)checking)) valid = false;
    checking++;
  }
  if (valid) {
    for (xmlNodePtr tn = root->children; valid && tn; tn = tn->next) {
      if (tn->type != XML_ELEMENT_NODE) continue;
      const db_table_schema_t *table = db_schema_table_named(db->schema, (const char *)tn->name);
      if (!table) { valid = false; break; }
      for (xmlNodePtr row = tn->children; valid && row; row = row->next) {
        if (row->type != XML_ELEMENT_NODE) continue;
        for (xmlAttrPtr a = row->properties; a; a = a->next)
          if (db_schema_field_index(table, (const char *)a->name) < 0) { valid = false; break; }
      }
    }
  }
  if (version) xmlFree(version);
  if (doc) xmlFreeDoc(doc);
  if (!valid) goto failed;
  snprintf(temporary, sizeof(temporary), "%s.restore.XXXXXX", db->source_path);
  int input = open(backup, O_RDONLY | DBS_BINARY), output = dbs_temporary(temporary, sizeof(temporary));
  bool ok = input >= 0 && output >= 0;
  char bytes[8192];
  while (ok) {
    ssize_t n = read(input, bytes, sizeof(bytes));
    if (n < 0 && errno == EINTR) continue;
    if (n < 0) { ok = false; break; }
    if (!n) break;
    for (ssize_t off = 0; ok && off < n;) {
      ssize_t written = write(output, bytes + off, (size_t)(n - off));
      if (written < 0 && errno == EINTR) continue;
      if (written <= 0) ok = false; else off += written;
    }
  }
  if (ok && dbs_sync_file(output) != 0) ok = false;
  if (input >= 0) close(input);
  if (output >= 0 && close(output) != 0) ok = false;
  database_t staged = *db;
  dbs_context_t loaded = {0};
  loaded.tables = calloc((size_t)db->schema->table_count, sizeof(*loaded.tables));
  if (!loaded.tables || snprintf(staged.source_path, sizeof(staged.source_path), "%s", temporary) >= (int)sizeof(staged.source_path)) ok = false;
  if (ok && !dbs_load(&staged, &loaded)) ok = false;
  if (ok && dbs_replace(temporary, db->source_path) != 0) ok = false;
  if (!ok) {
    if (loaded.tables) { dbs_clear(db, &loaded); free(loaded.tables); }
    unlink(temporary); goto failed;
  }
  dbs_clear(db, ctx);
  free(ctx->tables);
  ctx->tables = loaded.tables;
  memcpy(ctx->history, loaded.history, sizeof(ctx->history));
  db->dirty = staged.dirty;
  if (!dbs_sync_directory(db->source_path)) goto failed;
  return true;
failed:
  fprintf(stderr, "[db_schema_xml] backup restore failed db=%s path=%s\n",
          db ? db->name : "(null)", db ? db->source_path : "(null)");
  fflush(stderr);
  return false;
}

database_t *db_schema_xml_open_backup(const char *name, const char *class_name,
                                      const db_schema_def_t *schema, const char *path) {
  database_t *db = create_database_with_schema(name, class_name, schema, "");
  dbs_context_t *ctx = db ? db->userdata : NULL;
  if (!ctx || !dbs_lock_path(db, ctx, path, false) || !db_schema_xml_restore_backup(db)) {
    destroy_database(db);
    return NULL;
  }
  return db;
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
    snprintf(db->error, sizeof(db->error), "%s", ui_tr("Could not read database records."));
    fprintf(stderr, "[db_schema_xml] fetch filter out of range db=%s table=%s field=%d\n",
            db->name, t->name, field);
    fflush(stderr); return 0;
  }
  result_node_t *head = NULL, *tail = NULL;
  for (int r = 0; r < ctx->tables[ti].count; r++) {
    void *record = ctx->tables[ti].rows[r];
    if (field > 0 && !dbs_field_matches(&t->meta[field], record, value)) continue;
    result_node_t *node = malloc(sizeof(result_node_t) + sizeof(void *));
    if (!node) {
      free_result_list(head); snprintf(db->error, sizeof(db->error), "%s", ui_tr("Could not read database records."));
      fprintf(stderr, "[db_schema_xml] fetch allocation failed db=%s table=%s field=%d\n", db->name, t->name, field);
      fflush(stderr); return 0;
    }
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
      db->open_failed = true;
      return 0;
    }
    for (int i = 0; i < db->schema->table_count; i++) {
      const db_table_schema_t *table = &db->schema->tables[i];
      for (int f = 0; f < table->field_count && f < table->meta_count; f++)
        if (table->fields[f].primary_key && table->meta[f].type == DB_TYPE_INT64) {
          fprintf(stderr, "[db_schema_xml] int64 key unsupported db=%s table=%s field=%s\n", db->name, table->name, table->fields[f].name);
          fflush(stderr); db->open_failed = true; return 0;
        }
      for (int f = 0; f < table->field_count && f < table->meta_count; f++)
        if (table->meta[f].type == DB_TYPE_INT64 && table->fields[f].default_value && table->fields[f].default_value[0]) {
          db_field_meta_t field = table->meta[f]; int64_t value;
          field.offset = 0;
          if (!db_parse_field(&field, &value, table->fields[f].default_value)) {
            fprintf(stderr, "[db_schema_xml] invalid int64 default db=%s table=%s field=%s\n", db->name, table->name, field.name);
            fflush(stderr); db->open_failed = true; return 0;
          }
        }
    }
    ctx = calloc(1, sizeof(*ctx));
    if (ctx) ctx->tables = calloc((size_t)db->schema->table_count, sizeof(dbs_table_t));
    if (!ctx || !ctx->tables) {
      fprintf(stderr, "[db_schema_xml] context allocation failed db=%s\n", db->name);
      free(ctx);
      db->open_failed = true;
      return 0;
    }
    ctx->lock_fd = -1;
    if (!db->read_only && db->source_path[0] && !dbs_lock_path(db, ctx, db->source_path, false)) {
      fprintf(stderr, "[db_schema_xml] cannot lock db=%s path=%s: %s\n", db->name, db->source_path, strerror(errno));
      fflush(stderr);
      free(ctx->tables); free(ctx); db->open_failed = true; return 0;
    }
    for (int i = 0; i < db->schema->table_count; i++) ctx->tables[i].next_id = 1;
    db->userdata = ctx;
    return 1;
  }
  if (!ctx) {
    if (msg == dbFetch) snprintf(db->error, sizeof(db->error), "%s", ui_tr("Could not read database records."));
    return 0;
  }

  if (db->read_only && (msg == dbInsert || msg == dbUpdate || msg == dbDelete || msg == dbSave)) {
    fprintf(stderr, "[db_schema_xml] write rejected for read-only db=%s message=%u\n", db->name, msg);
    fflush(stderr);
    return 0;
  }
  int ti = -1;
  if (msg == dbFetch) db->error[0] = '\0';
  switch (msg) {
    case dbInsert: case dbUpdate: case dbDelete: case dbFetch: case dbFind:
    case dbGetFieldMeta: case dbGetFieldText: case dbInitRecord: case dbCountReferrers:
    case dbReserveKey:
      if ((ti = dbs_table_index(db, LOWORD(wparam))) < 0) {
        if (msg == dbFetch) snprintf(db->error, sizeof(db->error), "%s", ui_tr("Could not read database records."));
        return 0;
      }
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
      if (ctx->lock_fd >= 0) close(ctx->lock_fd);
      free(ctx);
      db->userdata = NULL;
      return 1;
    case dbLoad: return dbs_load(db, ctx);
    case dbSave: return db->dirty ? dbs_save(db, ctx) : 1;
    case dbGetDirty: return db->dirty;
    case dbReserveKey: {
      int id = ctx->tables[ti].next_id;
      if (id <= 0) {
        fprintf(stderr, "[db_schema_xml] key space exhausted db=%s table=%s\n", db->name, t->name);
        fflush(stderr); return 0;
      }
      ctx->tables[ti].next_id = id == INT_MAX ? 0 : id + 1;
      return id;
    }
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
      if (dbs_key_index(t) >= 0 && dbs_key(t, lparam) != id) {
        fprintf(stderr, "[db_schema_xml] key change rejected db=%s table=%s id=%d new_id=%d\n",
                db->name, t->name, id, dbs_key(t, lparam));
        fflush(stderr); return 0;
      }
      memmove(ctx->tables[ti].rows[row], lparam, t->record_size);
      db->dirty |= !t->transient;
      return 1;
    }
    case dbDelete: {
      if (!db->access_enabled || ctx->delete_depth) return dbs_delete(db, ctx, ti, (int)(intptr_t)lparam);
      if (!dbs_begin(db, ctx)) return 0;
      ctx->delete_depth++;
      bool deleted = dbs_delete(db, ctx, ti, (int)(intptr_t)lparam);
      ctx->delete_depth--;
      dbs_end(db, ctx, !deleted);
      return deleted;
    }
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
  if (store->access_enabled && !database_set_access(copy, store->access_actor, store->access_capabilities,
                                                    store->access_policy, store->access_audit, store->access_context)) {
    destroy_database(copy); return NULL;
  }
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
  if (error && error_size) snprintf(error, error_size, "%s", store->error[0] ? store->error : ui_tr("The document could not be saved. No changes were applied."));
  return false;
}

// Adds a record to memory only. A read-only store (a shared configuration)
// takes it for this process's lifetime; nothing reaches the file.
void *db_schema_xml_overlay_insert(database_t *db, uint32_t table_id, const void *record) {
  dbs_context_t *ctx = db ? (dbs_context_t *)db->userdata : NULL;
  int ti = ctx && record ? dbs_table_index(db, table_id) : -1;
  if (ti < 0) {
    fprintf(stderr, "[db_schema_xml] overlay insert rejected db=%s table=%u\n", db ? db->name : "(null)", table_id);
    fflush(stderr); return NULL;
  }
  return dbs_append(db, &db->schema->tables[ti], &ctx->tables[ti], record);
}

// Removes a record that db_schema_xml_overlay_insert() added, again in memory only.
bool db_schema_xml_overlay_remove(database_t *db, uint32_t table_id, int id) {
  dbs_context_t *ctx = db ? (dbs_context_t *)db->userdata : NULL;
  int ti = ctx ? dbs_table_index(db, table_id) : -1;
  if (ti < 0) {
    fprintf(stderr, "[db_schema_xml] overlay remove rejected db=%s table=%u id=%d\n", db ? db->name : "(null)", table_id, id);
    fflush(stderr); return false;
  }
  return dbs_delete(db, ctx, ti, id) != 0;
}
