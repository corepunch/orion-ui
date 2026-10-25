// script_test.c — Lua add-ons: orion.model over a registered datasource
// (tests/fixtures/script/model_spec.lua), the sandbox, and a page whose form
// and scratch table come from a .orion file and whose behaviour is Lua:
// data() shown on the form, actions with typed inputs, refusals and script
// errors reported without losing the page.

#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/commctl/commctl.h>
#include <orion/script/script.h>
#include <orion/script/script_internal.h>
#include <lauxlib.h>

#define FIXTURES "tests/fixtures/script"

enum { TABLE_PEOPLE = 0, TABLE_PETS = 1 };

typedef struct { int id; char name[32]; int age; bool active; } person_t;
typedef struct { int id; int owner_id; char name[24]; } pet_t;

static const db_field_meta_t person_meta[] = {
  { "id", DB_TYPE_INT, offsetof(person_t, id), 0, 0 },   { "name", DB_TYPE_STRING, offsetof(person_t, name), 32, 0 },
  { "age", DB_TYPE_INT, offsetof(person_t, age), 0, 0 }, { "active", DB_TYPE_BOOL, offsetof(person_t, active), 0, 0 },
};
static const db_field_schema_t person_fields[] = {
  { "id", DB_TYPE_INT, 0, true, NULL, NULL },   { "name", DB_TYPE_STRING, 32, false, NULL, NULL },
  { "age", DB_TYPE_INT, 0, false, NULL, NULL }, { "active", DB_TYPE_BOOL, 0, false, NULL, NULL },
};
static const db_field_meta_t pet_meta[] = {
  { "id", DB_TYPE_INT, offsetof(pet_t, id), 0, 0 }, { "owner_id", DB_TYPE_INT, offsetof(pet_t, owner_id), 0, 0 },
  { "name", DB_TYPE_STRING, offsetof(pet_t, name), 24, 0 },
};
static const db_field_schema_t pet_fields[] = {
  { "id", DB_TYPE_INT, 0, true, NULL, NULL },
  { "owner_id", DB_TYPE_INT, 0, false, "people", "id", NULL, DB_DELETE_DENY },
  { "name", DB_TYPE_STRING, 24, false, NULL, NULL },
};
static const db_join_schema_t pet_joins[] = { { "owner", "owner_id", "people", "id" } };
static const db_table_schema_t tables[] = {
  { TABLE_PEOPLE, "people", NULL, person_fields, 4, NULL, 0, sizeof(person_t), person_meta, 4, false },
  { TABLE_PETS, "pets", NULL, pet_fields, 3, pet_joins, 1, sizeof(pet_t), pet_meta, 3, false },
};
static const db_schema_def_t schema = { "club", "db_schema_xml", NULL, tables, 2 };

static database_t *g_club;

static void seed(void) {
  g_club = create_database_with_schema("club", "db_schema_xml", &schema, "");
  register_database("club", g_club);
  send_db_message(g_club, dbInsert, TABLE_PEOPLE, &(person_t){ .name = "Ada", .age = 36, .active = true });
  send_db_message(g_club, dbInsert, TABLE_PEOPLE, &(person_t){ .name = "Bob", .age = 20 });
  send_db_message(g_club, dbInsert, TABLE_PEOPLE, &(person_t){ .name = "Cy", .age = 70 });
}

static int count_rows(database_t *db, int table) {
  int n = 0;
  result_node_t *list = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(table, 0), NULL);
  for (result_node_t *node = list; node; node = (result_node_t *)node->next) n++;
  free_result_list(list);
  return n;
}

static bool registered(const char *name) {
  for (int i = 0; get_database_at(i); i++) if (!strcmp(get_database_at(i)->name, name)) return true;
  return false;
}

static void test_model_spec(void) {
  TEST("orion.model: queries, writes, joins, transactions and the sandbox (model_spec.lua)");
  lua_State *L = script_state_new(FIXTURES, "model_spec.lua");
  ASSERT_NOT_NULL(L);
  ASSERT_EQUAL(luaL_loadfilex(L, FIXTURES "/model_spec.lua", "t"), LUA_OK);
  char msg[4096];
  bool refused = false, ok = script_call(L, 0, 0, msg, sizeof(msg), &refused);
  if (!ok) fprintf(stderr, "model_spec.lua:\n%s\n", msg);
  ASSERT(ok, "every expectation of model_spec.lua holds");
  lua_close(L);
  PASS();
}

static void test_page_shows_data(void) {
  TEST("script page: data() fills values and the add-on's own list");
  char error[512];
  script_page_t *page = script_page_open(FIXTURES "/roster", error, sizeof(error));
  if (!page) fprintf(stderr, "open: %s\n", error);
  ASSERT_NOT_NULL(page);
  ASSERT_STR_EQUAL(script_page_title(page), "Club roster");
  ASSERT(registered("roster"), "the add-on's database is registered while it is open");
  window_t *win = script_page_create(page, NULL);
  ASSERT_NOT_NULL(win);
  ASSERT_STR_EQUAL(script_page_control(page, "summary")->title, "3 people");
  ASSERT_STR_EQUAL(script_page_control(page, "min_age")->title, "0");
  database_t *roster = get_database_by_name("roster");
  ASSERT_EQUAL(count_rows(roster, 0), 3);
  window_t *lines = script_page_control(page, "lines");
  const char *first = (const char *)(intptr_t)send_message(lines, tvGetRecord, 0, NULL) + sizeof(int);
  ASSERT_STR_EQUAL(first, "Cy");
  script_page_close(page);
  ASSERT(!registered("roster"), "closing the add-on removes its database");
  PASS();
}

static void test_actions(void) {
  TEST("script page: actions get typed inputs, then the page is asked again");
  script_page_t *page = script_page_open(FIXTURES "/roster.lua", NULL, 0);
  ASSERT_NOT_NULL(page);
  script_page_create(page, NULL);
  send_message(script_page_control(page, "min_age"), edSetText, 0, "30");
  send_message(script_page_control(page, "born"), edSetText, 0, "1815-12-10");
  ASSERT_TRUE(script_page_perform(page, "filter"));
  ASSERT_STR_EQUAL(script_page_message(page), "");
  ASSERT_STR_EQUAL(script_page_control(page, "summary")->title, "2 people since 1815-12-10");
  ASSERT_EQUAL(count_rows(get_database_by_name("roster"), 0), 2);

  send_message(script_page_control(page, "active_only"), btnSetCheck, 1, NULL);
  ASSERT_TRUE(script_page_perform(page, "filter"));
  ASSERT_STR_EQUAL(script_page_control(page, "summary")->title, "1 people since 1815-12-10");
  ASSERT(send_message(script_page_control(page, "active_only"), btnGetCheck, 0, NULL), "a boolean value checks its CheckBox");

  ASSERT_FALSE(script_page_perform(page, "add"));
  ASSERT_STR_EQUAL(script_page_message(page), "A person needs a name.");
  send_message(script_page_control(page, "new_name"), edSetText, 0, "Dee");
  ASSERT_TRUE(script_page_perform(page, "add"));
  ASSERT_EQUAL(count_rows(g_club, TABLE_PEOPLE), 4);
  script_page_close(page);
  PASS();
}

static void test_refusals_and_errors(void) {
  TEST("script page: refusals and script errors show a message and keep the page");
  script_page_t *page = script_page_open(FIXTURES "/roster.orion", NULL, 0);
  ASSERT_NOT_NULL(page);
  script_page_create(page, NULL);
  window_t *error = script_page_control(page, "error");

  ASSERT_FALSE(script_page_perform(page, "refuse"));
  ASSERT_STR_EQUAL(script_page_message(page), "Not today.");
  ASSERT_STR_EQUAL(error->title, "Not today.");

  ASSERT_FALSE(script_page_perform(page, "broken"));
  ASSERT(strstr(script_page_message(page), "stopped with an error") && strstr(script_page_message(page), "roster.lua"),
         "a script error names the add-on and where it failed");

  ASSERT_FALSE(script_page_perform(page, "nonexistent"));
  ASSERT(strstr(script_page_message(page), "nonexistent") != NULL, "an unknown action is reported");

  ASSERT_TRUE(script_page_perform(page, "filter"));
  ASSERT_STR_EQUAL(script_page_message(page), "");
  ASSERT_STR_EQUAL(error->title, "");
  ASSERT_TRUE(script_page_perform(page, "done"));
  script_page_close(page);
  PASS();
}

static void test_open_failures(void) {
  TEST("script_page_open: missing files and clashing databases fail with a reason");
  char error[512];
  ASSERT_NULL(script_page_open(FIXTURES "/nothing", error, sizeof(error)));
  ASSERT(strstr(error, "nothing.orion") != NULL, "a missing form is named");
  script_page_t *first = script_page_open(FIXTURES "/roster", NULL, 0);
  ASSERT_NOT_NULL(first);
  ASSERT_NULL(script_page_open(FIXTURES "/roster", error, sizeof(error)));
  ASSERT(strstr(error, "roster") && strstr(error, "clashes"), "a second copy cannot take the database name");
  script_page_close(first);
  PASS();
}

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  TEST_START("Lua add-ons");
  test_env_init();
  DB_CLASS(db_schema_xml);
  seed();
  test_model_spec();
  test_page_shows_data();
  test_actions();
  test_refusals_and_errors();
  test_open_failures();
  unregister_database("club");
  destroy_database(g_club);
  test_env_shutdown();
  TEST_END();
}
