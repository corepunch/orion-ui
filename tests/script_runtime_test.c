#include "test_framework.h"
#include "test_env.h"
#include <orion/script/application.h>
#include <orion/user/display_group.h>
#include <orion/commctl/form_loader.h>
#include <lua.h>
#include <lauxlib.h>

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
int main(void) {
  TEST_START("Application runtime"); test_env_init();
  test_runtime(); test_browser(); test_env_shutdown(); TEST_END();
}
