#ifndef __LEDGER_TEST_DATABASES_H__
#define __LEDGER_TEST_DATABASES_H__

#include <platform/platform.h>
#include <errno.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#define LEDGER_TEST_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define LEDGER_TEST_MKDIR(path) mkdir(path, 0700)
#endif

static bool ledger_test_directory(char *dir, size_t size) {
  axMkDir("build");
  for (unsigned n = 0; n < 10000; n++) {
    snprintf(dir, size, "build/ledger-fixture-%lu-%u", (unsigned long)time(NULL), n);
    if (LEDGER_TEST_MKDIR(dir) == 0) return true;
    if (errno != EEXIST) break;
  }
  return false;
}

static database_t *ledger_test_writable_seed(const char *name, const char *class_name, const db_schema_def_t *schema, const char *seed) {
  database_t *db = create_database_with_schema_readonly(name, class_name, schema, seed);
  char dir[128], path[160];
  if (!db || !ledger_test_directory(dir, sizeof(dir))) {
    fprintf(stderr, "[ledger-test] cannot open seed or create temporary directory: %s\n", seed);
    abort();
  }
  snprintf(path, sizeof(path), "%s/infobase.xml", dir);
  if (!db_schema_xml_seed_to(db, path)) abort();
  database_discard_on_destroy(db);
  return db;
}

#undef LEDGER_TEST_MKDIR

static database_t *ledger_test_infobase(const char *seed) {
  return ledger_test_writable_seed("ib", "ledger_ib", &ledger_ib_schema, seed);
}

#endif
