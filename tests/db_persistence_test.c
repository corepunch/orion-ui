#include "test_framework.h"
#include <orion/ui.h>
#include <sys/stat.h>
#include <libxml/tree.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <io.h>
#define access _access
#define unlink _unlink
#define rmdir _rmdir
#ifndef F_OK
#define F_OK 0
#endif
static bool persistence_tempdir(char *out, size_t size) {
  char base[MAX_PATH], name[MAX_PATH];
  DWORD length = GetTempPathA(sizeof(base), base);
  if (!length || length >= sizeof(base) || !GetTempFileNameA(base, "orp", 0, name)) return false;
  if (!DeleteFileA(name) || !CreateDirectoryA(name, NULL)) return false;
  if (strlen(name) >= size) { RemoveDirectoryA(name); return false; }
  snprintf(out, size, "%s", name); return true;
}
#define persistence_mkdir(path) _mkdir(path)
#else
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
static bool persistence_tempdir(char *out, size_t size) {
  if (snprintf(out, size, "/tmp/orion-persistence-XXXXXX") >= (int)size) return false;
  return mkdtemp(out) != NULL;
}
#define persistence_mkdir(path) mkdir(path, 0700)
#endif

typedef struct { int id, value; } persistence_row_t;
static const db_field_meta_t meta[] = {
  {"id", DB_TYPE_INT, offsetof(persistence_row_t, id)},
  {"value", DB_TYPE_INT, offsetof(persistence_row_t, value)},
};
static const db_field_schema_t fields[] = {
  {.name="id", .type=DB_TYPE_INT, .primary_key=true},
  {.name="value", .type=DB_TYPE_INT},
};
static const db_table_schema_t tables[] = {
  {.table_id=0, .name="records", .fields=fields, .field_count=2,
   .meta=meta, .meta_count=2, .record_size=sizeof(persistence_row_t)},
};
static const db_schema_def_t schema = {.name="book", .class_name="db_schema_xml", .tables=tables, .table_count=1};
static database_t *open_book(const char *path) {
  return create_database_with_schema("book", "db_schema_xml", &schema, path);
}
static bool migrate_fail(void *root, unsigned from) { (void)root; (void)from; return false; }
static bool migrate_ok(void *root, unsigned from) {
  if (from != 1) return false;
  xmlNodePtr table = ((xmlNodePtr)root)->children;
  while (table && table->type != XML_ELEMENT_NODE) table = table->next;
  xmlNodePtr row = table ? table->children : NULL;
  while (row && row->type != XML_ELEMENT_NODE) row = row->next;
  return row && xmlSetProp(row, BAD_CAST "value", BAD_CAST "77");
}
static void test_migrations(void) {
  TEST("schema XML: failed migration preserves source, successful migration creates backup");
  char dir[512], path[1024], extra[1040];
  ASSERT_TRUE(persistence_tempdir(dir, sizeof(dir))); snprintf(path, sizeof(path), "%s/book.xml", dir);
  database_t *db = open_book(path); ASSERT_NOT_NULL(db);
  persistence_row_t row = {.id=1, .value=10};
  ASSERT_TRUE(send_db_message(db, dbInsert, 0, &row)); ASSERT_TRUE(send_db_message(db, dbSave, 0, NULL));
  destroy_database(db);
  db_schema_def_t v2 = schema; v2.version = 2; v2.migrate_xml = migrate_fail;
  ASSERT_NULL(create_database_with_schema("book", "db_schema_xml", &v2, path));
  db = open_book(path); ASSERT_NOT_NULL(db);
  persistence_row_t *saved = (void *)send_db_message(db, dbFind, 0, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(saved); ASSERT_EQUAL(saved->value, 10); destroy_database(db);
  v2.migrate_xml = migrate_ok;
  db = create_database_with_schema_readonly("book", "db_schema_xml", &v2, path); ASSERT_NOT_NULL(db);
  saved = (void *)send_db_message(db, dbFind, 0, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(saved); ASSERT_EQUAL(saved->value, 77); destroy_database(db);
  db = open_book(path); ASSERT_NOT_NULL(db);
  saved = (void *)send_db_message(db, dbFind, 0, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(saved); ASSERT_EQUAL(saved->value, 10); destroy_database(db);
  snprintf(extra, sizeof(extra), "%s.bak", path); ASSERT_EQUAL(persistence_mkdir(extra), 0);
  db = create_database_with_schema("book", "db_schema_xml", &v2, path); ASSERT_NOT_NULL(db);
  ASSERT_FALSE(send_db_message(db, dbSave, 0, NULL)); database_discard_on_destroy(db); destroy_database(db);
  ASSERT_EQUAL(rmdir(extra), 0);
  db = open_book(path); ASSERT_NOT_NULL(db);
  saved = (void *)send_db_message(db, dbFind, 0, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(saved); ASSERT_EQUAL(saved->value, 10); destroy_database(db);
  db = create_database_with_schema("book", "db_schema_xml", &v2, path); ASSERT_NOT_NULL(db);
  saved = (void *)send_db_message(db, dbFind, 0, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(saved); ASSERT_EQUAL(saved->value, 77); ASSERT_TRUE(send_db_message(db, dbSave, 0, NULL)); destroy_database(db);
  ASSERT_NULL(open_book(path));
  FILE *history_file = fopen(path, "rb"); ASSERT_NOT_NULL(history_file);
  char history_bytes[4096]; size_t history_size = fread(history_bytes, 1, sizeof(history_bytes) - 1, history_file);
  history_bytes[history_size] = 0; fclose(history_file);
  ASSERT_NOT_NULL(strstr(history_bytes, "orion-history=\"1,2\""));
  FILE *f = fopen(path, "w"); ASSERT_NOT_NULL(f); fputs("broken", f); fclose(f);
  db = db_schema_xml_open_backup("book", "db_schema_xml", &schema, path); ASSERT_NOT_NULL(db);
  saved = (void *)send_db_message(db, dbFind, 0, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(saved); ASSERT_EQUAL(saved->value, 10); destroy_database(db);
  unlink(path);
  f = fopen(path, "w"); ASSERT_NOT_NULL(f);
  fputs("<book><records><record id=\"1\" removed=\"9\"/></records></book>", f); fclose(f);
  v2.migrate_xml = db_schema_xml_migrate_additive;
  ASSERT_NULL(create_database_with_schema("book", "db_schema_xml", &v2, path));
  f = fopen(path, "w"); ASSERT_NOT_NULL(f); fputs("<book/>", f); fclose(f);
  snprintf(extra, sizeof(extra), "%s.bak", path);
  FILE *invalid_history = fopen(extra, "w"); ASSERT_NOT_NULL(invalid_history);
  fputs("<book orion-version=\"1\" orion-history=\"1,3\"/>", invalid_history); fclose(invalid_history);
  ASSERT_NULL(db_schema_xml_open_backup("book", "db_schema_xml", &schema, path));
  db = open_book(path); ASSERT_NOT_NULL(db); destroy_database(db);
  unlink(path); unlink(extra);
  ASSERT_NULL(db_schema_xml_open_backup("book", "db_schema_xml", &schema, path));
  FILE *bad = fopen(extra, "w"); ASSERT_NOT_NULL(bad); fputs("broken", bad); fclose(bad);
  ASSERT_NULL(db_schema_xml_open_backup("book", "db_schema_xml", &schema, path)); unlink(extra);
  snprintf(extra, sizeof(extra), "%s.lock", path); unlink(extra); rmdir(dir); PASS();
}
#ifndef _WIN32
static int migration_crash_stage;
static bool migrate_with_crash(void *root, unsigned from) {
  xmlNodePtr table = ((xmlNodePtr)root)->children;
  while (table && table->type != XML_ELEMENT_NODE) table = table->next;
  xmlNodePtr row = table ? table->children : NULL;
  while (row && row->type != XML_ELEMENT_NODE) row = row->next;
  if (!row || (from != 1 && from != 2) ||
      !xmlSetProp(row, BAD_CAST "value", BAD_CAST (from == 1 ? "20" : "30"))) return false;
  if (migration_crash_stage == (int)from) kill(getpid(), SIGKILL);
  return true;
}
static void test_migration_crashes(void) {
  TEST("schema XML: killed migration preserves old or published new version");
  char dir[512], path[1024], backup[1040], lock[1040];
  ASSERT_TRUE(persistence_tempdir(dir, sizeof(dir)));
  snprintf(path, sizeof(path), "%s/book.xml", dir);
  snprintf(backup, sizeof(backup), "%s.bak", path);
  snprintf(lock, sizeof(lock), "%s.lock", path);
  db_schema_def_t v3 = schema; v3.version = 3; v3.migrate_xml = migrate_with_crash;
  for (int stage = 1; stage <= 4; stage++) {
    database_t *db = open_book(path); ASSERT_NOT_NULL(db);
    persistence_row_t row = {.id=1, .value=10};
    ASSERT_TRUE(send_db_message(db, dbInsert, 0, &row));
    ASSERT_TRUE(send_db_message(db, dbSave, 0, NULL)); destroy_database(db);
    pid_t child = fork(); ASSERT(child >= 0, "fork succeeds");
    if (!child) {
      migration_crash_stage = stage;
      db = create_database_with_schema("book", "db_schema_xml", &v3, path);
      if (!db) _exit(1);
      if (stage == 3) kill(getpid(), SIGKILL);
      if (!send_db_message(db, dbSave, 0, NULL)) _exit(2);
      kill(getpid(), SIGKILL);
      _exit(3);
    }
    int status = 0; ASSERT_EQUAL(waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    db = create_database_with_schema("book", "db_schema_xml", stage == 4 ? &v3 : &schema, path);
    ASSERT_NOT_NULL(db);
    persistence_row_t *saved = (void *)send_db_message(db, dbFind, 0, (void *)(intptr_t)1);
    ASSERT_NOT_NULL(saved); ASSERT_EQUAL(saved->value, stage == 4 ? 30 : 10);
    destroy_database(db);
    if (stage == 4) {
      db = create_database_with_schema_readonly("book", "db_schema_xml", &schema, backup);
      ASSERT_NOT_NULL(db);
      saved = (void *)send_db_message(db, dbFind, 0, (void *)(intptr_t)1);
      ASSERT_NOT_NULL(saved); ASSERT_EQUAL(saved->value, 10); destroy_database(db);
    }
    unlink(path); unlink(backup);
  }
  unlink(lock); rmdir(dir); PASS();
}
#endif
typedef struct { int id; int64_t amount; } recovery_row_t;
static const db_field_meta_t recovery_meta[] = {
  {"id", DB_TYPE_INT, offsetof(recovery_row_t, id)},
  {"amount", DB_TYPE_INT64, offsetof(recovery_row_t, amount)},
};
static const db_field_schema_t recovery_fields[] = {
  {.name="id", .type=DB_TYPE_INT, .primary_key=true},
  {.name="amount", .type=DB_TYPE_INT64},
};
static const db_table_schema_t recovery_tables[] = {
  {.table_id=0, .name="records", .fields=recovery_fields, .field_count=2,
   .meta=recovery_meta, .meta_count=2, .record_size=sizeof(recovery_row_t)},
};
static const db_schema_def_t recovery_schema = {
  .name="book", .class_name="db_schema_xml", .tables=recovery_tables, .table_count=1,
};
static void test_restore_rejects_unloadable_backup(void) {
  TEST("schema XML: failed backup load preserves primary and open records");
  char dir[512], path[1024], backup[1040], lock[1040];
  ASSERT_TRUE(persistence_tempdir(dir, sizeof(dir)));
  snprintf(path, sizeof(path), "%s/book.xml", dir);
  snprintf(backup, sizeof(backup), "%s.bak", path);
  snprintf(lock, sizeof(lock), "%s.lock", path);
  database_t *db = create_database_with_schema("book", "db_schema_xml", &recovery_schema, path);
  ASSERT_NOT_NULL(db);
  recovery_row_t row = {.id=1, .amount=42};
  ASSERT_TRUE(send_db_message(db, dbInsert, 0, &row));
  ASSERT_TRUE(send_db_message(db, dbSave, 0, NULL));
  FILE *f = fopen(backup, "w"); ASSERT_NOT_NULL(f);
  fputs("<book><records><record id=\"1\" amount=\"invalid\"/></records></book>", f); fclose(f);
  ASSERT_FALSE(db_schema_xml_restore_backup(db));
  recovery_row_t *saved = (void *)send_db_message(db, dbFind, 0, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(saved); ASSERT_EQUAL(saved->amount, 42);
  destroy_database(db);
  db = create_database_with_schema("book", "db_schema_xml", &recovery_schema, path);
  ASSERT_NOT_NULL(db);
  saved = (void *)send_db_message(db, dbFind, 0, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(saved); ASSERT_EQUAL(saved->amount, 42); destroy_database(db);
  unlink(path); unlink(backup); unlink(lock); rmdir(dir); PASS();
}
static void test_persistence(void) {
  TEST("schema XML: exclusive writer, atomic backup and version rejection");
  char dir[512], path[1024], backup[1040], lock[1040];
  ASSERT_TRUE(persistence_tempdir(dir, sizeof(dir)));
  snprintf(path, sizeof(path), "%s/book.xml", dir);
  snprintf(backup, sizeof(backup), "%s.bak", path);
  snprintf(lock, sizeof(lock), "%s.lock", path);
  FILE *seed = fopen(path, "w"); ASSERT_NOT_NULL(seed); fputs("<book/>", seed); fclose(seed);
  database_t *shared = create_database_with_schema_readonly("book", "db_schema_xml", &schema, path);
  database_t *shared2 = create_database_with_schema_readonly("alias", "db_schema_xml", &schema, path);
  ASSERT_NOT_NULL(shared); ASSERT_NOT_NULL(shared2); ASSERT_TRUE(access(lock, F_OK) != 0);
  destroy_database(shared); destroy_database(shared2);
  database_t *db = open_book(path); ASSERT_NOT_NULL(db);
  persistence_row_t row = {.id=1, .value=10};
  ASSERT_TRUE(send_db_message(db, dbInsert, 0, &row));
  ASSERT_TRUE(send_db_message(db, dbSave, 0, NULL));
#ifndef _WIN32
  pid_t child = fork(); ASSERT(child >= 0, "fork succeeds");
  if (!child) { database_t *other = open_book(path); _exit(other ? 1 : 0); }
  int status = 0; ASSERT_EQUAL(waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
#else
  ASSERT_NULL(open_book(path));
#endif
  database_t *readonly = create_database_with_schema_readonly("book", "db_schema_xml", &schema, path);
  ASSERT_NOT_NULL(readonly); ASSERT_FALSE(send_db_message(readonly, dbInsert, 0, &row)); destroy_database(readonly);
  row.value = 20; ASSERT_TRUE(send_db_message(db, dbUpdate, 0, &row));
  ASSERT_TRUE(send_db_message(db, dbSave, 0, NULL)); destroy_database(db);
  db = open_book(backup); ASSERT_NOT_NULL(db);
  persistence_row_t *old = (void *)send_db_message(db, dbFind, 0, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(old); ASSERT_EQUAL(old->value, 10); destroy_database(db);
  FILE *f = fopen(path, "w"); ASSERT_NOT_NULL(f);
  fputs("<book orion-version=\"999\"/>", f); fclose(f);
  ASSERT_NULL(open_book(path));
  unlink(path);
#ifndef _WIN32
  int ready[2]; ASSERT_EQUAL(pipe(ready), 0);
  child = fork(); ASSERT(child >= 0, "fork succeeds");
  if (!child) {
    close(ready[0]); database_t *writer = open_book(path);
    if (!writer) _exit(1);
    persistence_row_t committed = {.id=1, .value=42};
    if (!send_db_message(writer, dbInsert, 0, &committed) || !send_db_message(writer, dbSave, 0, NULL)) _exit(2);
    write(ready[1], "x", 1); for (;;) pause();
  }
  close(ready[1]); char signal_byte; ASSERT_EQUAL(read(ready[0], &signal_byte, 1), 1); close(ready[0]);
  ASSERT_EQUAL(kill(child, SIGKILL), 0); ASSERT_EQUAL(waitpid(child, &status, 0), child);
  db = open_book(path); ASSERT_NOT_NULL(db);
  old = (void *)send_db_message(db, dbFind, 0, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(old); ASSERT_EQUAL(old->value, 42); destroy_database(db);
#endif
  unlink(path); unlink(backup); unlink(lock);
  snprintf(lock, sizeof(lock), "%s.bak.lock", path); unlink(lock); rmdir(dir);
  PASS();
}
int main(void) {
  TEST_START("Database persistence"); DB_CLASS(db_schema_xml);
  test_persistence(); test_migrations(); test_restore_rejects_unloadable_backup();
#ifndef _WIN32
  test_migration_crashes();
#endif
  TEST_END();
}
