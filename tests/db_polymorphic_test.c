// db_polymorphic_test.c — polymorphic relations in schema-driven databases
// (the referenced table is named by a path on the same record) and ComboBox
// re-sourcing with cbSetSource.

#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/commctl/commctl.h>

enum { TABLE_KINDS = 1, TABLE_ACCOUNTS, TABLE_PEOPLE, TABLE_THINGS, TABLE_ENTRIES };

typedef struct { int id; char table[32]; } kind_t;
typedef struct { int id; int kind_id; char code[8]; } account_t;
typedef struct { int id; char name[32]; } named_t;
typedef struct { int id; int account_id; int subject_id; } entry_t;

static const db_field_meta_t kind_meta[]    = { { "id", DB_TYPE_INT, offsetof(kind_t, id), 0, 0 }, { "table", DB_TYPE_STRING, offsetof(kind_t, table), 32, 0 } };
static const db_field_meta_t account_meta[] = { { "id", DB_TYPE_INT, offsetof(account_t, id), 0, 0 }, { "kind_id", DB_TYPE_INT, offsetof(account_t, kind_id), 0, 0 },
                                                { "code", DB_TYPE_STRING, offsetof(account_t, code), 8, 0 } };
static const db_field_meta_t named_meta[]   = { { "id", DB_TYPE_INT, offsetof(named_t, id), 0, 0 }, { "name", DB_TYPE_STRING, offsetof(named_t, name), 32, 0 } };
static const db_field_meta_t entry_meta[]   = { { "id", DB_TYPE_INT, offsetof(entry_t, id), 0, 0 }, { "account_id", DB_TYPE_INT, offsetof(entry_t, account_id), 0, 0 },
                                                { "subject_id", DB_TYPE_INT, offsetof(entry_t, subject_id), 0, 0 } };

static const db_field_schema_t kind_fields[]    = { { "id", DB_TYPE_INT, 0, true, NULL, NULL }, { "table", DB_TYPE_STRING, 32, false, NULL, NULL } };
static const db_field_schema_t account_fields[] = { { "id", DB_TYPE_INT, 0, true, NULL, NULL }, { "kind_id", DB_TYPE_INT, 0, false, "kinds", "id" },
                                                    { "code", DB_TYPE_STRING, 8, false, NULL, NULL } };
static const db_field_schema_t named_fields[]   = { { "id", DB_TYPE_INT, 0, true, NULL, NULL }, { "name", DB_TYPE_STRING, 32, false, NULL, NULL } };
static const db_field_schema_t entry_fields[]   = { { "id", DB_TYPE_INT, 0, true, NULL, NULL }, { "account_id", DB_TYPE_INT, 0, false, "accounts", "id" },
                                                    { "subject_id", DB_TYPE_INT, 0, false, "{account.kind.table}", "id" } };

static const db_join_schema_t account_joins[] = { { "kind", "kind_id", "kinds", "id" } };
static const db_join_schema_t entry_joins[]   = { { "account", "account_id", "accounts", "id" },
                                                  { "subject", "subject_id", "{account.kind.table}", "id" } };

static const db_table_schema_t tables[] = {
  { TABLE_KINDS,    "kinds",    NULL, kind_fields,    2, NULL,          0, sizeof(kind_t),    kind_meta,    2, false },
  { TABLE_ACCOUNTS, "accounts", NULL, account_fields, 3, account_joins, 1, sizeof(account_t), account_meta, 3, false },
  { TABLE_PEOPLE,   "people",   NULL, named_fields,   2, NULL,          0, sizeof(named_t),   named_meta,   2, false },
  { TABLE_THINGS,   "things",   NULL, named_fields,   2, NULL,          0, sizeof(named_t),   named_meta,   2, false },
  { TABLE_ENTRIES,  "entries",  NULL, entry_fields,   3, entry_joins,   2, sizeof(entry_t),   entry_meta,   3, false },
};
static const db_schema_def_t schema = { "poly", "db_schema_xml", NULL, tables, 5 };

static database_t *g_db;

static void insert(int table, const void *record) { send_db_message(g_db, dbInsert, (uint32_t)table, (void *)record); }

static void setup(void) {
  DB_CLASS(db_schema_xml);
  g_db = create_database_with_schema("poly", "db_schema_xml", &schema, "");  // in memory: never saved
  register_database("poly", g_db);
  insert(TABLE_KINDS, &(kind_t){ .table = "people" });
  insert(TABLE_KINDS, &(kind_t){ .table = "things" });
  insert(TABLE_ACCOUNTS, &(account_t){ .kind_id = 1, .code = "70" });
  insert(TABLE_ACCOUNTS, &(account_t){ .kind_id = 2, .code = "01" });
  insert(TABLE_ACCOUNTS, &(account_t){ .code = "51" });
  insert(TABLE_PEOPLE, &(named_t){ .name = "Ivanov" });
  insert(TABLE_PEOPLE, &(named_t){ .name = "Petrova" });
  insert(TABLE_THINGS, &(named_t){ .name = "Computer" });
}

static void test_polymorphic_join(void) {
  TEST("db_schema_xml: a polymorphic relation reads its table from the record's path");
  entry_t to_person = { .account_id = 1, .subject_id = 2 }, to_thing = { .account_id = 2, .subject_id = 1 }, plain = { .account_id = 3, .subject_id = 1 };
  char buf[64];
  ASSERT_TRUE(db_get_field_text(g_db, TABLE_ENTRIES, &to_person, "subject.name", buf, sizeof(buf)));
  ASSERT_STR_EQUAL(buf, "Petrova");
  ASSERT_TRUE(db_get_field_text(g_db, TABLE_ENTRIES, &to_thing, "subject.name", buf, sizeof(buf)));
  ASSERT_STR_EQUAL(buf, "Computer");
  ASSERT_TRUE(db_get_field_text(g_db, TABLE_ENTRIES, &plain, "subject.name", buf, sizeof(buf)));
  ASSERT_STR_EQUAL(buf, "");  // the account names no table
  PASS();
}

static result_t parent_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)wparam; (void)lparam;
  return msg == evCreate || msg == evDestroy;
}

static void test_combobox_set_source(void) {
  TEST("ComboBox: cbSetSource lists another table and keeps value binding");
  test_env_init();
  window_t *root = test_env_create_window("Test", 0, 0, 300, 200, parent_proc, NULL);
  combobox_params_t params = { g_db, TABLE_PEOPLE, "name", "id", NULL, NULL };
  form_ctrl_def_t def = { .class_name = "ComboBox", .lparam = &params };
  window_t *cb = create_window("", 0, MAKERECT(10, 10, 120, 19), root, win_combobox, 0, &def);
  ASSERT_NOT_NULL(cb);
  ASSERT_EQUAL((int)cb->cursor_pos, 2);
  ASSERT_TRUE(send_message(cb, cbSetSource, TABLE_THINGS, NULL));
  ASSERT_EQUAL((int)cb->cursor_pos, 1);
  send_message(cb, cbSetCurrentValue, 1, NULL);
  ASSERT_STR_EQUAL(cb->title, "Computer");
  ASSERT_EQUAL((int)send_message(cb, cbGetCurrentValue, 0, NULL), 1);
  destroy_window(root);
  test_env_shutdown();
  PASS();
}

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  TEST_START("db_polymorphic");
  setup();
  test_polymorphic_join();
  test_combobox_set_source();
  destroy_database(g_db);
  TEST_END();
}
