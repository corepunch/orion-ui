#include "test_framework.h"
#ifndef _WIN32
#include <orion/ui.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <sys/file.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include "../orion/user/user.h"
#include "../orion/user/database.h"
#include "../orion/user/i18n.h"

// This translation unit compiles the adapter separately; the shipped library is unchanged.
// Number calls only while the child is saving or restoring, so every target must fire.
static int fault_event, fault_target, fault_after, fault_error;
static bool fault_point(int after) {
  if (!fault_target) return false;
  if (!after) fault_event++;
  if (fault_event != fault_target || after != fault_after) return false;
  if (!fault_error) kill(getpid(), SIGKILL);
  errno = fault_error; return true;
}
static ssize_t fault_write(int fd, const void *buf, size_t size) {
  if (fault_point(0)) return -1;
  if (fault_target && fault_event == fault_target && fault_after == 2) {
    if (write(fd, buf, size / 2) < 0) _exit(13);
    kill(getpid(), SIGKILL);
  }
  ssize_t result = write(fd, buf, size); fault_point(1); return result;
}
static int fault_fsync(int fd) {
  if (fault_point(0)) return -1;
  int result = fsync(fd); fault_point(1); return result;
}
static int fault_link(const char *from, const char *to) {
  if (fault_point(0)) return -1;
  int result = link(from, to); fault_point(1); return result;
}
static int fault_rename(const char *from, const char *to) {
  if (fault_point(0)) return -1;
  int result = rename(from, to); fault_point(1); return result;
}
#define write fault_write
#define fsync fault_fsync
#define link fault_link
#define rename fault_rename
#include "../orion/user/db_schema_xml.c"
#undef write
#undef fsync
#undef link
#undef rename

typedef struct { int id, value; } fault_row_t;
static const db_field_meta_t fault_meta[] = {
  {"id", DB_TYPE_INT, offsetof(fault_row_t, id)}, {"value", DB_TYPE_INT, offsetof(fault_row_t, value)},
};
static const db_field_schema_t fault_fields[] = {
  {.name="id", .type=DB_TYPE_INT, .primary_key=true}, {.name="value", .type=DB_TYPE_INT},
};
static const db_table_schema_t fault_tables[] = {
  {.table_id=0, .name="records", .fields=fault_fields, .field_count=2,
   .meta=fault_meta, .meta_count=2, .record_size=sizeof(fault_row_t)},
};
static const db_schema_def_t fault_schema = {.name="book", .class_name="db_schema_xml", .tables=fault_tables, .table_count=1};
static database_t *fault_open(const char *path) {
  return create_database_with_schema("book", "db_schema_xml", &fault_schema, path);
}
static bool fault_seed(const char *path, int value) {
  FILE *file = fopen(path, "w");
  if (!file) return false;
  bool ok = fprintf(file, "<book><records><record id=\"1\" value=\"%d\"/></records></book>", value) > 0;
  return fclose(file) == 0 && ok;
}
static int fault_read_value(const char *path) {
  database_t *db = create_database_with_schema_readonly("book", "db_schema_xml", &fault_schema, path);
  if (!db) return -1;
  fault_row_t *row = (void *)send_db_message(db, dbFind, 0, (void *)(intptr_t)1);
  int value = row ? row->value : -1;
  destroy_database(db); return value;
}
static void test_save_faults(void) {
  TEST("schema XML: interrupted save and restore preserve complete versions at each syscall");
  char directory[] = "/tmp/orion-save-fault-XXXXXX", path[512], backup[528], lock[528];
  ASSERT_NOT_NULL(mkdtemp(directory));
  snprintf(path, sizeof(path), "%s/book.xml", directory);
  snprintf(backup, sizeof(backup), "%s.bak", path); snprintf(lock, sizeof(lock), "%s.lock", path);
  // Save: write, file sync, link, backup rename, directory sync, primary rename, sync.
  // Restore: write, file sync, primary rename, directory sync.
  for (int restore = 0; restore <= 1; restore++) {
   for (int error_case = 0; error_case < 3; error_case++) {
    for (int event = 1; event <= (restore ? 4 : 7); event++) {
      for (int after = 0; after <= (error_case == 0 ? (event == 1 ? 2 : 1) : 0); after++) {
        unlink(path); unlink(backup);
        ASSERT_TRUE(fault_seed(path, 10)); ASSERT_TRUE(fault_seed(backup, 5));
        pid_t child = fork(); ASSERT(child >= 0, "fork succeeds");
        if (!child) {
          database_t *db = fault_open(path); if (!db) _exit(10);
          fault_row_t row = {.id=1, .value=20};
          if (!restore && !send_db_message(db, dbUpdate, 0, &row)) _exit(11);
          fault_event = 0; fault_target = event; fault_after = after;
          fault_error = error_case == 1 ? ENOSPC : error_case == 2 ? EIO : 0;
          bool saved = restore ? db_schema_xml_restore_backup(db) : send_db_message(db, dbSave, 0, NULL);
          if (error_case && !saved && fault_event >= event) _exit(0);
          _exit(12);
        }
        int status = 0; ASSERT_EQUAL(waitpid(child, &status, 0), child);
        if (!error_case) { ASSERT_TRUE(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL); }
        else { ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0); }
        int published = restore ? 3 : 6;
        int expected = event > published || (event == published && after) ? (restore ? 5 : 20) : 10;
        ASSERT_EQUAL(fault_read_value(path), expected);
        ASSERT_EQUAL(fault_read_value(backup), !restore && (event > 4 || (event == 4 && after)) ? 10 : 5);
      }
    }
   }
  }
  unlink(path); unlink(backup); unlink(lock);
  DIR *dir = opendir(directory); ASSERT_NOT_NULL(dir);
  struct dirent *entry;
  while ((entry = readdir(dir))) {
    if (strncmp(entry->d_name, "book.xml.tmp.", 13) && strncmp(entry->d_name, "book.xml.restore.", 17) &&
        strcmp(entry->d_name, "book.xml.bak.tmp")) continue;
    char leftover[1024]; snprintf(leftover, sizeof(leftover), "%s/%s", directory, entry->d_name); unlink(leftover);
  }
  closedir(dir); ASSERT_EQUAL(rmdir(directory), 0); PASS();
}
#endif
int main(void) {
  TEST_START("XML save faults");
#ifndef _WIN32
  DB_CLASS(db_schema_xml); test_save_faults();
#endif
  TEST_END();
}
