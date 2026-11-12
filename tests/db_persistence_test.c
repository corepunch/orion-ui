#include "test_framework.h"
#include <orion/ui.h>
#include <sys/stat.h>
#include <limits.h>
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
static void test_unknown_fields_preserved(void) {
  TEST("schema XML: unknown fields reject opening without changing the source");
  char dir[512], path[1024], lock[1040], bytes[512];
  const char *sources[] = {
    "<book orion-version=\"1\"><records><record id=\"1\" value=\"10\" legacy=\"42\"/></records></book>",
    "<book orion-version=\"1\"><records><record id=\"1\" value=\"10\"><legacy>42</legacy></record></records></book>",
  };
  ASSERT_TRUE(persistence_tempdir(dir, sizeof(dir)));
  snprintf(path, sizeof(path), "%s/book.xml", dir);
  snprintf(lock, sizeof(lock), "%s.lock", path);
  for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
    FILE *f = fopen(path, "wb"); ASSERT_NOT_NULL(f);
    ASSERT_EQUAL(fwrite(sources[i], 1, strlen(sources[i]), f), strlen(sources[i])); ASSERT_EQUAL(fclose(f), 0);
    ASSERT_NULL(open_book(path));
    f = fopen(path, "rb"); ASSERT_NOT_NULL(f);
    size_t n = fread(bytes, 1, sizeof(bytes) - 1, f); ASSERT_FALSE(ferror(f)); ASSERT_EQUAL(fclose(f), 0);
    bytes[n] = 0; ASSERT_TRUE(!strcmp(bytes, sources[i]));
  }
  unlink(path); unlink(lock); rmdir(dir); PASS();
}
static void test_unknown_container_attributes_preserved(void) {
  TEST("schema XML: unknown container attributes preserve primary and backup");
  char dir[512], path[1024], backup[1040], lock[1040], bytes[512];
  const char *sources[] = {
    "<book legacy=\"42\"><records><record id=\"1\" value=\"10\"/></records></book>",
    "<book><records legacy=\"42\"><record id=\"1\" value=\"10\"/></records></book>",
  };
  ASSERT_TRUE(persistence_tempdir(dir, sizeof(dir)));
  snprintf(path, sizeof(path), "%s/book.xml", dir); snprintf(backup, sizeof(backup), "%s.bak", path);
  snprintf(lock, sizeof(lock), "%s.lock", path);
  for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
    FILE *f = fopen(path, "wb"); ASSERT_NOT_NULL(f);
    ASSERT_EQUAL(fwrite(sources[i], 1, strlen(sources[i]), f), strlen(sources[i])); ASSERT_EQUAL(fclose(f), 0);
    ASSERT_NULL(open_book(path));
    f = fopen(path, "rb"); ASSERT_NOT_NULL(f);
    size_t n = fread(bytes, 1, sizeof(bytes) - 1, f); ASSERT_FALSE(ferror(f)); ASSERT_EQUAL(fclose(f), 0);
    bytes[n] = 0; ASSERT_TRUE(!strcmp(bytes, sources[i]));
    f = fopen(backup, "wb"); ASSERT_NOT_NULL(f);
    ASSERT_EQUAL(fwrite(sources[i], 1, strlen(sources[i]), f), strlen(sources[i])); ASSERT_EQUAL(fclose(f), 0);
    f = fopen(path, "wb"); ASSERT_NOT_NULL(f); fputs("<book/>", f); ASSERT_EQUAL(fclose(f), 0);
    database_t *db = open_book(path); ASSERT_NOT_NULL(db);
    ASSERT_FALSE(db_schema_xml_restore_backup(db)); destroy_database(db);
    f = fopen(path, "rb"); ASSERT_NOT_NULL(f);
    n = fread(bytes, 1, sizeof(bytes) - 1, f); ASSERT_FALSE(ferror(f)); ASSERT_EQUAL(fclose(f), 0);
    bytes[n] = 0; ASSERT_TRUE(!strcmp(bytes, "<book/>"));
  }
  unlink(path); unlink(backup); unlink(lock); rmdir(dir); PASS();
}
static void test_duplicate_fields_preserved(void) {
  TEST("schema XML: duplicate fields reject opening without changing the source");
  char dir[512], path[1024], backup[1040], lock[1040], bytes[512];
  const char *sources[] = {
    "<book><records><record id=\"1\" value=\"10\"><value>20</value></record></records></book>",
    "<book><records><record id=\"1\"><value>10</value><value>20</value></record></records></book>",
  };
  ASSERT_TRUE(persistence_tempdir(dir, sizeof(dir)));
  snprintf(path, sizeof(path), "%s/book.xml", dir); snprintf(backup, sizeof(backup), "%s.bak", path);
  snprintf(lock, sizeof(lock), "%s.lock", path);
  for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
    FILE *f = fopen(path, "wb"); ASSERT_NOT_NULL(f);
    ASSERT_EQUAL(fwrite(sources[i], 1, strlen(sources[i]), f), strlen(sources[i])); ASSERT_EQUAL(fclose(f), 0);
    ASSERT_NULL(open_book(path));
    f = fopen(path, "rb"); ASSERT_NOT_NULL(f);
    size_t n = fread(bytes, 1, sizeof(bytes) - 1, f); ASSERT_FALSE(ferror(f)); ASSERT_EQUAL(fclose(f), 0);
    bytes[n] = 0; ASSERT_TRUE(!strcmp(bytes, sources[i]));
  }
  FILE *f = fopen(path, "wb"); ASSERT_NOT_NULL(f);
  fputs("<book><records><record id=\"1\" value=\"42\"/></records></book>", f); ASSERT_EQUAL(fclose(f), 0);
  f = fopen(backup, "wb"); ASSERT_NOT_NULL(f); fputs(sources[0], f); ASSERT_EQUAL(fclose(f), 0);
  database_t *db = open_book(path); ASSERT_NOT_NULL(db);
  ASSERT_FALSE(db_schema_xml_restore_backup(db));
  persistence_row_t *row = (void *)send_db_message(db, dbFind, 0, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(row); ASSERT_EQUAL(row->value, 42); destroy_database(db);
  db = open_book(path); ASSERT_NOT_NULL(db);
  row = (void *)send_db_message(db, dbFind, 0, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(row); ASSERT_EQUAL(row->value, 42); destroy_database(db);
  unlink(path); unlink(backup); unlink(lock); rmdir(dir); PASS();
}
static void test_invalid_integer_preserved(void) {
  TEST("schema XML: invalid integers reject opening without changing the source");
  char dir[512], path[1024], lock[1040], bytes[512];
  const char *sources[] = {
    "<book><records><record id=\"1\" value=\"broken\"/></records></book>",
    "<book><records><record id=\"1\"><value>999999999999999999999999</value></record></records></book>",
  };
  ASSERT_TRUE(persistence_tempdir(dir, sizeof(dir)));
  snprintf(path, sizeof(path), "%s/book.xml", dir);
  snprintf(lock, sizeof(lock), "%s.lock", path);
  for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
    FILE *f = fopen(path, "wb"); ASSERT_NOT_NULL(f);
    ASSERT_EQUAL(fwrite(sources[i], 1, strlen(sources[i]), f), strlen(sources[i])); ASSERT_EQUAL(fclose(f), 0);
    ASSERT_NULL(open_book(path));
    f = fopen(path, "rb"); ASSERT_NOT_NULL(f);
    size_t n = fread(bytes, 1, sizeof(bytes) - 1, f); ASSERT_FALSE(ferror(f)); ASSERT_EQUAL(fclose(f), 0);
    bytes[n] = 0; ASSERT_TRUE(!strcmp(bytes, sources[i]));
  }
  unlink(path); unlink(lock); rmdir(dir); PASS();
}
typedef struct { int id; char name[6]; } short_text_row_t;
static const db_field_meta_t short_text_meta[] = {
  {"id", DB_TYPE_INT, offsetof(short_text_row_t, id)},
  {"name", DB_TYPE_STRING, offsetof(short_text_row_t, name), sizeof(((short_text_row_t *)0)->name)},
};
static const db_field_schema_t short_text_fields[] = {
  {.name="id", .type=DB_TYPE_INT, .primary_key=true}, {.name="name", .type=DB_TYPE_STRING},
};
static const db_table_schema_t short_text_tables[] = {
  {.table_id=0, .name="records", .fields=short_text_fields, .field_count=2,
   .meta=short_text_meta, .meta_count=2, .record_size=sizeof(short_text_row_t)},
};
static const db_schema_def_t short_text_schema = {
  .name="book", .class_name="db_schema_xml", .tables=short_text_tables, .table_count=1,
};
static void test_oversized_text_preserved(void) {
  TEST("schema XML: oversized text rejects opening without truncating source");
  char dir[512], path[1024], lock[1040], bytes[512];
  const char *sources[] = {
    "<book><records><record id=\"1\" name=\"abcdef\"/></records></book>",
    "<book><records><record id=\"1\"><name>abcdef</name></record></records></book>",
  };
  ASSERT_TRUE(persistence_tempdir(dir, sizeof(dir)));
  snprintf(path, sizeof(path), "%s/book.xml", dir); snprintf(lock, sizeof(lock), "%s.lock", path);
  for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
    FILE *f = fopen(path, "wb"); ASSERT_NOT_NULL(f);
    ASSERT_EQUAL(fwrite(sources[i], 1, strlen(sources[i]), f), strlen(sources[i])); ASSERT_EQUAL(fclose(f), 0);
    ASSERT_NULL(create_database_with_schema("book", "db_schema_xml", &short_text_schema, path));
    f = fopen(path, "rb"); ASSERT_NOT_NULL(f);
    size_t n = fread(bytes, 1, sizeof(bytes) - 1, f); ASSERT_FALSE(ferror(f)); ASSERT_EQUAL(fclose(f), 0);
    bytes[n] = 0; ASSERT_TRUE(!strcmp(bytes, sources[i]));
  }
  FILE *f = fopen(path, "wb"); ASSERT_NOT_NULL(f);
  fputs("<book><records><record id=\"1\" name=\" abcde \"/></records></book>", f); ASSERT_EQUAL(fclose(f), 0);
  database_t *db = create_database_with_schema("book", "db_schema_xml", &short_text_schema, path); ASSERT_NOT_NULL(db);
  short_text_row_t *row = (void *)send_db_message(db, dbFind, 0, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(row); ASSERT_TRUE(!strcmp(row->name, "abcde")); destroy_database(db);
  unlink(path); unlink(lock); rmdir(dir); PASS();
}
static void test_backup_in_separate_directory(void) {
  TEST("schema XML: backup restores independently in a separate directory");
  char source_dir[512], target_dir[512], source[1024], target[1024], source_backup[1040], target_backup[1040], lock[1040];
  ASSERT_TRUE(persistence_tempdir(source_dir, sizeof(source_dir)));
  ASSERT_TRUE(persistence_tempdir(target_dir, sizeof(target_dir)));
  snprintf(source, sizeof(source), "%s/book.xml", source_dir);
  snprintf(target, sizeof(target), "%s/book.xml", target_dir);
  snprintf(source_backup, sizeof(source_backup), "%s.bak", source);
  snprintf(target_backup, sizeof(target_backup), "%s.bak", target);
  database_t *original = open_book(source); ASSERT_NOT_NULL(original);
  persistence_row_t first = {.id=1, .value=10}, second = {.id=2, .value=20};
  ASSERT_TRUE(send_db_message(original, dbInsert, 0, &first));
  ASSERT_TRUE(send_db_message(original, dbInsert, 0, &second));
  ASSERT_TRUE(send_db_message(original, dbSave, 0, NULL));
  first.value = 11; ASSERT_TRUE(send_db_message(original, dbUpdate, 0, &first));
  ASSERT_TRUE(send_db_message(original, dbSave, 0, NULL));
  FILE *in = fopen(source_backup, "rb"), *out = fopen(target_backup, "wb");
  ASSERT_NOT_NULL(in); ASSERT_NOT_NULL(out);
  char bytes[1024]; size_t count;
  while ((count = fread(bytes, 1, sizeof(bytes), in)) > 0) ASSERT_EQUAL(fwrite(bytes, 1, count, out), count);
  ASSERT_FALSE(ferror(in)); ASSERT_EQUAL(fclose(in), 0); ASSERT_EQUAL(fclose(out), 0);
  database_t *restored = db_schema_xml_open_backup("book", "db_schema_xml", &schema, target);
  ASSERT_NOT_NULL(restored);
  persistence_row_t *saved = (void *)send_db_message(restored, dbFind, 0, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(saved); ASSERT_EQUAL(saved->value, 10);
  saved = (void *)send_db_message(restored, dbFind, 0, (void *)(intptr_t)2);
  ASSERT_NOT_NULL(saved); ASSERT_EQUAL(saved->value, 20);
  first.value = 12; ASSERT_TRUE(send_db_message(restored, dbUpdate, 0, &first));
  ASSERT_TRUE(send_db_message(restored, dbSave, 0, NULL)); destroy_database(restored);
  saved = (void *)send_db_message(original, dbFind, 0, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(saved); ASSERT_EQUAL(saved->value, 11); destroy_database(original);
  restored = open_book(target); ASSERT_NOT_NULL(restored);
  saved = (void *)send_db_message(restored, dbFind, 0, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(saved); ASSERT_EQUAL(saved->value, 12);
  saved = (void *)send_db_message(restored, dbFind, 0, (void *)(intptr_t)2);
  ASSERT_NOT_NULL(saved); ASSERT_EQUAL(saved->value, 20); destroy_database(restored);
  unlink(source); unlink(source_backup); snprintf(lock, sizeof(lock), "%s.lock", source); unlink(lock); rmdir(source_dir);
  unlink(target); unlink(target_backup); snprintf(lock, sizeof(lock), "%s.lock", target); unlink(lock); rmdir(target_dir);
  PASS();
}
static void test_key_integrity(void) {
  TEST("schema XML: duplicate keys and exhausted key space preserve records");
  char dir[512], path[1024], backup[1040], lock[1040], bytes[512];
  const char *duplicate = "<book><records><record id=\"1\" value=\"10\"/><record id=\"1\" value=\"20\"/></records></book>";
  ASSERT_TRUE(persistence_tempdir(dir, sizeof(dir)));
  snprintf(path, sizeof(path), "%s/book.xml", dir); snprintf(backup, sizeof(backup), "%s.bak", path);
  snprintf(lock, sizeof(lock), "%s.lock", path);
  FILE *f = fopen(path, "wb"); ASSERT_NOT_NULL(f); fputs(duplicate, f); ASSERT_EQUAL(fclose(f), 0);
  ASSERT_NULL(open_book(path));
  f = fopen(path, "rb"); ASSERT_NOT_NULL(f); size_t n = fread(bytes, 1, sizeof(bytes) - 1, f);
  ASSERT_FALSE(ferror(f)); ASSERT_EQUAL(fclose(f), 0); bytes[n] = 0; ASSERT_TRUE(!strcmp(bytes, duplicate));
  f = fopen(path, "wb"); ASSERT_NOT_NULL(f); fputs("<book/>", f); ASSERT_EQUAL(fclose(f), 0);
  database_t *db = open_book(path); ASSERT_NOT_NULL(db);
  f = fopen(backup, "wb"); ASSERT_NOT_NULL(f); fputs(duplicate, f); ASSERT_EQUAL(fclose(f), 0);
  ASSERT_FALSE(db_schema_xml_restore_backup(db));
  persistence_row_t first = {.id=1, .value=10}, second = {.id=2, .value=20};
  ASSERT_TRUE(send_db_message(db, dbInsert, 0, &first));
  ASSERT_FALSE(send_db_message(db, dbInsert, 0, &first));
  ASSERT_TRUE(send_db_message(db, dbInsert, 0, &second));
  ASSERT_FALSE(send_db_message(db, dbUpdate, MAKEDWORD(0, 1), &second));
  first.id = INT_MAX; ASSERT_TRUE(send_db_message(db, dbInsert, 0, &first));
  persistence_row_t automatic = {.value=30};
  ASSERT_FALSE(send_db_message(db, dbInsert, 0, &automatic));
  ASSERT_FALSE(send_db_message(db, dbReserveKey, 0, NULL));
  ASSERT_TRUE(send_db_message(db, dbSave, 0, NULL)); destroy_database(db);
  db = open_book(path); ASSERT_NOT_NULL(db);
  persistence_row_t *saved = (void *)send_db_message(db, dbFind, 0, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(saved); ASSERT_EQUAL(saved->value, 10);
  saved = (void *)send_db_message(db, dbFind, 0, (void *)(intptr_t)INT_MAX);
  ASSERT_NOT_NULL(saved); ASSERT_EQUAL(saved->value, 10);
  ASSERT_FALSE(send_db_message(db, dbInsert, 0, &automatic)); destroy_database(db);
  unlink(path); unlink(backup); unlink(lock); rmdir(dir); PASS();
}
static void test_fetch_errors(void) {
  TEST("schema XML: failed fetch is distinct from an empty result");
  database_t *db = create_database_with_schema("book", "db_schema_xml", &schema, NULL); ASSERT_NOT_NULL(db);
  ASSERT_FALSE(send_db_message(db, dbFetch, MAKEDWORD(0, 0), NULL));
  ASSERT(!db->error[0], "empty fetch has no error");
  ASSERT_FALSE(send_db_message(db, dbFetch, MAKEDWORD(0, 99), NULL));
  ASSERT(db->error[0], "invalid filter reports an error");
  ASSERT_FALSE(send_db_message(db, dbFetch, MAKEDWORD(0, 0), NULL));
  ASSERT(!db->error[0], "next successful empty fetch clears the error");
  ASSERT_FALSE(send_db_message(db, dbFetch, MAKEDWORD(99, 0), NULL));
  ASSERT(db->error[0], "invalid table reports an error");
  destroy_database(db); PASS();
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
  test_key_integrity(); test_fetch_errors(); test_persistence(); test_migrations(); test_restore_rejects_unloadable_backup(); test_unknown_fields_preserved(); test_unknown_container_attributes_preserved(); test_duplicate_fields_preserved(); test_invalid_integer_preserved(); test_oversized_text_preserved(); test_backup_in_separate_directory();
#ifndef _WIN32
  test_migration_crashes();
#endif
  TEST_END();
}
