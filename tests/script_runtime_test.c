#include "test_framework.h"
#include "test_env.h"
#include <orion/script/application.h>
#include <orion/user/display_group.h>
#include <orion/commctl/form_loader.h>
#include <lua.h>
#include <lauxlib.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#endif

static void test_runtime(void) {
  TEST("one VM: independent controller objects, isolated package names and lifetime checks");
  char error[1024];
  script_runtime_t *runtime = script_runtime_open("tests/fixtures/runtime", "runtime test");
  ASSERT_NOT_NULL(runtime);
  int first = script_runtime_controller(runtime, "tests/fixtures/runtime/one/controller.lua", error, sizeof(error));
  int second = script_runtime_controller(runtime, "tests/fixtures/runtime/two/controller.lua", error, sizeof(error));
  ASSERT(first != LUA_NOREF && second != LUA_NOREF, "controllers load");
  ASSERT_TRUE(script_runtime_invoke(runtime, first, "check", "one", error, sizeof(error)));
  ASSERT_TRUE(script_runtime_invoke(runtime, second, "check", "two", error, sizeof(error)));
  ASSERT_FALSE(script_runtime_invoke(runtime, first, "missing", NULL, error, sizeof(error)));
  ASSERT_NOT_NULL(strstr(error, "missing"));
  ASSERT_FALSE(script_runtime_invoke(runtime, first, "refuse", "Choose a record.", error, sizeof(error)));
  ASSERT_STR_EQUAL(error, "Choose a record.");
  ASSERT_FALSE(script_runtime_invoke(runtime, first, "check", "wrong", NULL, 0));
  ASSERT_FALSE(script_runtime_invoke(runtime, first, "leak", NULL, error, sizeof(error)));
  ASSERT_NOT_NULL(strstr(error, "undeclared global 'leaked'"));
  ASSERT_EQUAL(script_runtime_controller(runtime, "tests/fixtures/runtime/none.lua", NULL, 0), LUA_NOREF);
  ASSERT_FALSE(script_runtime_close(runtime));
  script_runtime_release(runtime, first); script_runtime_release(runtime, second);
  ASSERT_TRUE(script_runtime_close(runtime));
  PASS();
}
static int notifications;
static void changed(database_t *database, void *userdata) { notifications++; }
static void test_browser(void) {
  TEST("codeless object graph: selection to Label/ImageView and transactional notifications");
  char error[1024];
  script_application_t *app = script_application_open("tests/fixtures/runtime/browser.orion", "/tmp", error, sizeof(error));
  ASSERT_NOT_NULL(app);
  script_page_t *page = script_application_main(app);
  database_t *database = get_database_by_name("people"), *group = get_database_by_name("selection");
  const db_table_schema_t *table = db_schema_table(database->schema, 0);
  void *record = calloc(1, table->record_size);
  int field = db_schema_field_index(table, "name");
  snprintf((char *)record + table->meta[field].offset, table->meta[field].length, "Ada");
  int token = db_observe(database, changed, NULL);
  notifications = 0;
  ASSERT_TRUE(send_db_message(database, dbBeginChanges, 0, NULL));
  void *inserted = (void *)send_db_message(database, dbInsert, 0, record);
  ASSERT_NOT_NULL(inserted); int id = *(int *)inserted;
  ASSERT_EQUAL(notifications, 0);
  ASSERT_TRUE(send_db_message(database, dbCommitChanges, 0, NULL));
  ASSERT_EQUAL(notifications, 1);
  ASSERT_TRUE(send_db_message(group, dbSelectRecord, id, NULL));
  ASSERT_STR_EQUAL(script_page_control(page, "name")->title, "Ada");
  ASSERT_NOT_NULL(script_page_control(page, "photo"));
  ASSERT_TRUE(send_db_message(database, dbBeginChanges, 0, NULL));
  ASSERT_TRUE(send_db_message(database, dbDelete, 0, (void *)(intptr_t)id));
  ASSERT_TRUE(send_db_message(database, dbRevertChanges, 0, NULL));
  ASSERT_EQUAL(notifications, 1);
  ASSERT_NOT_NULL((void *)send_db_message(database, dbFind, 0, (void *)(intptr_t)id));
  db_unobserve(token); free(record);
  script_application_close(app);
  PASS();
}
static void test_sdk_application(void) {
  TEST("SDK starter: generic runtime, save and reopen without app-specific native code");
  char error[1024], path[512];
#ifdef _WIN32
  char directory[400]; bool created = false;
  const char *temporary = getenv("TEMP");
  for (int attempt = 0; attempt < 100 && !created; attempt++) {
    snprintf(directory, sizeof(directory), "%s/orion-sdk-%d-%d", temporary ? temporary : ".", _getpid(), attempt);
    created = _mkdir(directory) == 0;
  }
  ASSERT_TRUE(created);
#else
  char directory[] = "/tmp/orion-sdk-XXXXXX";
  ASSERT_NOT_NULL(mkdtemp(directory));
#endif
  script_application_t *app = script_application_open("tools/sdk/templates/application.orion", directory, error, sizeof(error));
  ASSERT_NOT_NULL(app);
  database_t *database = get_database_by_name("starter");
  ASSERT_NOT_NULL(database);
  const db_table_schema_t *table = db_schema_table(database->schema, 0);
  void *record = calloc(1, table->record_size);
  int field = db_schema_field_index(table, "name");
  snprintf((char *)record + table->meta[field].offset, table->meta[field].length, "External developer");
  ASSERT_NOT_NULL((void *)send_db_message(database, dbInsert, 0, record));
  ASSERT_TRUE(script_runtime_action(script_application_runtime(app), "orion.application", "save", "starter", error, sizeof(error)));
  free(record); script_application_close(app);
  app = script_application_open("tools/sdk/templates/application.orion", directory, error, sizeof(error));
  ASSERT_NOT_NULL(app);
  database = get_database_by_name("starter");
  record = (void *)send_db_message(database, dbFind, 0, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(record);
  char value[128]; db_get_field_text(database, 0, record, "name", value, sizeof(value));
  ASSERT_STR_EQUAL(value, "External developer");
  table = db_schema_table(database->schema, 0); field = db_schema_field_index(table, "name");
  void *update = malloc(table->record_size); memcpy(update, record, table->record_size);
  snprintf((char *)update + table->meta[field].offset, table->meta[field].length, "Second revision");
  ASSERT_TRUE(send_db_message(database, dbUpdate, 0, update)); free(update);
  ASSERT_TRUE(script_runtime_action(script_application_runtime(app), "orion.application", "save", "starter", error, sizeof(error)));
  script_application_close(app);
  snprintf(path, sizeof(path), "%s/records.xml", directory);
  FILE *damaged = fopen(path, "wb"); ASSERT_NOT_NULL(damaged); fputs("broken", damaged); fclose(damaged);
  app = script_application_open_recovery("tools/sdk/templates/application.orion", directory, error, sizeof(error));
  ASSERT_NOT_NULL(app); database = get_database_by_name("starter");
  record = (void *)send_db_message(database, dbFind, 0, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(record); db_get_field_text(database, 0, record, "name", value, sizeof(value));
  ASSERT_STR_EQUAL(value, "External developer"); script_application_close(app);
  unlink(path);
  snprintf(path, sizeof(path), "%s/records.xml.bak", directory); unlink(path);
  snprintf(path, sizeof(path), "%s/records.xml.lock", directory); unlink(path); rmdir(directory);
  PASS();
}
static void test_model_hooks(void) {
  TEST("model hooks protect native writes/deletes and roll back audit and original write together");
  char error[1024];
  script_application_t *app = script_application_open("tests/fixtures/runtime/audit.orion", "/tmp", error, sizeof(error));
  ASSERT_NOT_NULL(app);
  database_t *database = get_database_by_name("audit_test");
  int observer = db_observe(database, changed, NULL); notifications = 0;
  const db_table_schema_t *table = db_schema_table(database->schema, 0);
  void *record = calloc(1, table->record_size);
  int field = db_schema_field_index(table, "name");
  char *name = (char *)record + table->meta[field].offset;
  snprintf(name, table->meta[field].length, "locked");
  void *stored = (void *)send_db_message(database, dbInsert, 0, record);
  ASSERT_NOT_NULL(stored); ASSERT_EQUAL(notifications, 1);
  int id = *(int *)stored; *(int *)record = id;
  ASSERT_NOT_NULL((void *)send_db_message(database, dbFind, 1, (void *)(intptr_t)1));
  ASSERT_FALSE(send_db_message(database, dbDelete, 0, (void *)(intptr_t)id));
  ASSERT_FALSE(send_db_message(database, dbDelete, 1, (void *)(intptr_t)1));
  snprintf(name, table->meta[field].length, "fail_hook");
  ASSERT_FALSE(send_db_message(database, dbUpdate, 0, record));
  ASSERT_NULL((void *)send_db_message(database, dbFind, 1, (void *)(intptr_t)2));
  stored = (void *)send_db_message(database, dbFind, 0, (void *)(intptr_t)id);
  ASSERT_NOT_NULL(stored); ASSERT_STR_EQUAL((char *)stored + table->meta[field].offset, "locked");
  snprintf(name, table->meta[field].length, "mutate_validation");
  ASSERT_FALSE(send_db_message(database, dbUpdate, 0, record));
  ASSERT_NULL((void *)send_db_message(database, dbFind, 1, (void *)(intptr_t)2));
  ASSERT_EQUAL(notifications, 1);
  *(int *)record = 0; snprintf(name, table->meta[field].length, "remove_self");
  ASSERT_FALSE(send_db_message(database, dbInsert, 0, record));
  ASSERT_EQUAL(notifications, 1);
  *(int *)record = id;
  snprintf(name, table->meta[field].length, "updated");
  ASSERT_TRUE(send_db_message(database, dbUpdate, 0, record));
  const db_table_schema_t *events = db_schema_table(database->schema, 1);
  void *event = (void *)send_db_message(database, dbFind, 1, (void *)(intptr_t)2);
  ASSERT_NOT_NULL(event);
  ASSERT_STR_EQUAL((char *)event + events->meta[db_schema_field_index(events, "name")].offset, "update:locked");
  ASSERT_EQUAL(notifications, 2);
  db_unobserve(observer); free(record); script_application_close(app); PASS();
}
static void test_runtime_api(void) {
  TEST("unsupported API is rejected before opening data");
  char error[1024];
  ASSERT_TRUE(script_runtime_check_api("1", error, sizeof(error)));
  ASSERT_FALSE(script_runtime_check_api("01", error, sizeof(error)));
  script_application_t *app = script_application_open("tests/fixtures/runtime/future.orion", "/tmp", error, sizeof(error));
  ASSERT_NULL(app); ASSERT_NOT_NULL(strstr(error, "unsupported runtime API"));
  ASSERT_NULL(get_database_by_name("future_app")); PASS();
}
int main(void) {
  TEST_START("Application runtime"); test_env_init();
  test_runtime(); test_browser(); test_sdk_application(); test_model_hooks(); test_runtime_api(); test_env_shutdown(); TEST_END();
}
