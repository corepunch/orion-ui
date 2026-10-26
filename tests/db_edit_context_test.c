#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>

typedef struct { int id, value; char name[32]; } edit_row_t;
static const db_field_meta_t edit_meta[] = {
  {"id", DB_TYPE_INT, offsetof(edit_row_t, id)},
  {"value", DB_TYPE_INT, offsetof(edit_row_t, value)},
  {"name", DB_TYPE_STRING, offsetof(edit_row_t, name), 32},
};
static const db_field_schema_t edit_fields[] = {
  {.name="id", .type=DB_TYPE_INT, .primary_key=true},
  {.name="value", .type=DB_TYPE_INT, .relation_table="records", .relation_field="id"},
  {.name="name", .type=DB_TYPE_STRING, .length=32},
};
static const db_table_schema_t edit_tables[] = {
  {.table_id=0,.name="records",.fields=edit_fields,.field_count=3,
   .meta=edit_meta,.meta_count=3,.record_size=sizeof(edit_row_t)},
};
static const db_schema_def_t edit_schema = {.name="book",.class_name="db_schema_xml",.tables=edit_tables,.table_count=1};
static lresult_t edit_reject(database_t *db, uint32_t msg, uint32_t wp, void *lp) {
  if (!db->edit_parent && msg == dbUpdate && lp && ((edit_row_t *)lp)->value == 999) return 0;
  return db_schema_xml(db, msg, wp, lp);
}
static edit_row_t *edit_find(database_t *db, int id) {
  return (edit_row_t *)send_db_message(db, dbFind, 0, (void *)(intptr_t)id);
}
static bool edit_value(database_t *db, int id, int value) {
  edit_row_t copy = *edit_find(db, id); copy.value = value;
  return send_db_message(db, dbUpdate, 0, &copy) != 0;
}
static database_t *edit_seed(void) {
  database_t *db = create_database_with_schema("book", "db_schema_xml", &edit_schema, "");
  edit_row_t a = {.id=1,.name="One"}, b = {.id=2,.name="Two"};
  send_db_message(db, dbInsert, 0, &a); send_db_message(db, dbInsert, 0, &b);
  db->dirty = false;
  return db;
}
static void test_independent_documents(void) {
  TEST("document contexts: out-of-order save and cancellation cannot revert another document");
  database_t *store = edit_seed(), *a = db_edit_context_create(store), *b = db_edit_context_create(store);
  ASSERT_NOT_NULL(a); ASSERT_NOT_NULL(b);
  ASSERT_TRUE(edit_value(a, 1, 10)); ASSERT_TRUE(edit_value(b, 2, 20));
  ASSERT_EQUAL(edit_find(store, 1)->value, 0);
  char error[256];
  ASSERT_TRUE(db_edit_context_save(b, error, sizeof(error)));
  destroy_database(a); destroy_database(b);
  ASSERT_EQUAL(edit_find(store, 1)->value, 0);
  ASSERT_EQUAL(edit_find(store, 2)->value, 20);
  store->dirty = false; destroy_database(store);
  PASS();
}
static void test_conflicts(void) {
  TEST("document contexts: conflicting save rejects the entire delta and retains the first save");
  database_t *store = edit_seed(), *a = db_edit_context_create(store), *b = db_edit_context_create(store);
  edit_value(a, 1, 10); edit_value(b, 1, 11); edit_value(b, 2, 22);
  char error[256];
  ASSERT_TRUE(db_edit_context_save(a, error, sizeof(error)));
  ASSERT_FALSE(db_edit_context_save(b, error, sizeof(error)));
  ASSERT(strstr(error, "changed") != NULL, "conflict is explained");
  ASSERT_EQUAL(edit_find(store, 1)->value, 10);
  ASSERT_EQUAL(edit_find(store, 2)->value, 0);
  destroy_database(a); destroy_database(b); store->dirty=false; destroy_database(store);
  PASS();
}
static void test_reserved_ids(void) {
  TEST("document contexts: concurrent new documents reserve unique keys without exposing drafts");
  database_t *store = edit_seed(), *a = db_edit_context_create(store), *b = db_edit_context_create(store);
  edit_row_t draft = {.name="Draft"};
  edit_row_t *one = (edit_row_t *)send_db_message(a, dbInsert, 0, &draft);
  edit_row_t *two = (edit_row_t *)send_db_message(b, dbInsert, 0, &draft);
  ASSERT_NOT_NULL(one); ASSERT_NOT_NULL(two);
  int first = one->id, second = two->id;
  ASSERT(first != second, "independent drafts reserve different keys");
  ASSERT_NULL(edit_find(store, first)); ASSERT_NULL(edit_find(store, second));
  char error[256]; ASSERT_TRUE(db_edit_context_save(b, error, sizeof(error)));
  destroy_database(a); destroy_database(b);
  ASSERT_NULL(edit_find(store, first)); ASSERT_NOT_NULL(edit_find(store, second));
  store->dirty=false; destroy_database(store);
  PASS();
}
static void test_failed_save_rolls_back(void) {
  TEST("document contexts: a late store-rule failure rolls back earlier applied records");
  database_t *store = edit_seed(); store->class_name="edit_reject"; store->proc=edit_reject;
  database_t *context = db_edit_context_create(store);
  ASSERT_NOT_NULL(context);
  ASSERT_TRUE(edit_value(context, 1, 100)); ASSERT_TRUE(edit_value(context, 2, 999));
  char error[256]; ASSERT_FALSE(db_edit_context_save(context, error, sizeof(error)));
  ASSERT_EQUAL(edit_find(store, 1)->value, 0); ASSERT_EQUAL(edit_find(store, 2)->value, 0);
  destroy_database(context); store->dirty=false; destroy_database(store);
  PASS();
}
static void test_deleted_reference(void) {
  TEST("document contexts: stale save cannot resurrect a deleted reference");
  database_t *store = edit_seed();
  ASSERT_TRUE(edit_value(store, 1, 2));
  database_t *context = db_edit_context_create(store);
  edit_row_t edited = *edit_find(context, 1);
  snprintf(edited.name, sizeof(edited.name), "Edited elsewhere");
  ASSERT_TRUE(send_db_message(context, dbUpdate, 0, &edited));
  ASSERT_TRUE(send_db_message(store, dbDelete, 0, (void *)(intptr_t)2));
  char error[256]; ASSERT_FALSE(db_edit_context_save(context, error, sizeof(error)));
  ASSERT_STR_EQUAL(edit_find(store, 1)->name, "One");
  ASSERT_NULL(edit_find(store, 2));
  destroy_database(context); store->dirty=false; destroy_database(store);
  PASS();
}
int main(void) {
  TEST_START("Document editing contexts"); test_env_init(); DB_CLASS(db_schema_xml); DB_CLASS(edit_reject);
  test_independent_documents(); test_conflicts(); test_reserved_ids(); test_failed_save_rolls_back(); test_deleted_reference();
  test_env_shutdown(); TEST_END();
}
