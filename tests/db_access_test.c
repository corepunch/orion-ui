#include "test_framework.h"
#include <orion/ui.h>

typedef struct { int id, value; } access_row_t;
static const db_field_meta_t access_meta[] = {
  {"id", DB_TYPE_INT, offsetof(access_row_t, id)},
  {"value", DB_TYPE_INT, offsetof(access_row_t, value)},
};
static const db_field_schema_t access_fields[] = {
  {.name="id", .type=DB_TYPE_INT, .primary_key=true},
  {.name="value", .type=DB_TYPE_INT},
};
static const db_table_schema_t access_tables[] = {
  {.table_id=0, .name="public", .fields=access_fields, .field_count=2,
   .meta=access_meta, .meta_count=2, .record_size=sizeof(access_row_t)},
  {.table_id=1, .name="restricted", .fields=access_fields, .field_count=2,
   .meta=access_meta, .meta_count=2, .record_size=sizeof(access_row_t)},
};
static const db_schema_def_t access_schema = {.name="access", .class_name="db_schema_xml", .tables=access_tables, .table_count=2};
typedef struct { int denied, allowed; char actor[128]; } audit_t;
static bool access_policy(database_t *db, const char *actor, uint32_t msg, uint32_t wp, void *context) {
  (void)db; (void)actor; (void)context;
  return !((msg == dbFind || msg == dbFetch || msg == dbInsert) && LOWORD(wp) == 1);
}
static void access_audit(database_t *db, const char *actor, uint32_t msg, uint32_t wp,
                         bool allowed, bool success, void *context) {
  (void)db; (void)msg; (void)wp; (void)success;
  audit_t *audit = context;
  if (allowed) audit->allowed++; else audit->denied++;
  snprintf(audit->actor, sizeof(audit->actor), "%s", actor);
}
static void test_access(void) {
  TEST("database access: capabilities and table policy cover native messages and editing contexts");
  database_t *db = create_database_with_schema("access", "db_schema_xml", &access_schema, "");
  ASSERT_NOT_NULL(db); access_row_t row = {.id=1, .value=10};
  ASSERT_NOT_NULL((void *)send_db_message(db, dbInsert, 0, &row));
  ASSERT_NOT_NULL((void *)send_db_message(db, dbInsert, 1, &row));
  audit_t audit = {0};
  ASSERT_TRUE(database_set_access(db, "alice", DB_ACCESS_READ, access_policy, access_audit, &audit));
  ASSERT_NOT_NULL((void *)send_db_message(db, dbFind, 0, (void *)(intptr_t)1));
  ASSERT_NULL((void *)send_db_message(db, dbFind, 1, (void *)(intptr_t)1));
  row.value=20; ASSERT_FALSE(send_db_message(db, dbUpdate, 0, &row));
  ASSERT_FALSE(send_db_message(db, dbBeginChanges, 0, NULL));
  ASSERT_FALSE(send_db_message(db, dbUser + 1, 0, NULL));
  ASSERT_EQUAL(audit.denied, 4); ASSERT_STR_EQUAL(audit.actor, "alice");
  database_t *draft = db_edit_context_create(db); ASSERT_NOT_NULL(draft);
  ASSERT_FALSE(send_db_message(draft, dbUpdate, 0, &row)); destroy_database(draft);
  ASSERT_TRUE(database_set_access(db, "bob", DB_ACCESS_READ | DB_ACCESS_WRITE, access_policy, access_audit, &audit));
  ASSERT_TRUE(send_db_message(db, dbUpdate, 0, &row));
  ASSERT_TRUE(send_db_message(db, dbBeginChanges, 0, NULL));
  ASSERT_FALSE(database_set_access(db, "carol", DB_ACCESS_READ, NULL, NULL, NULL));
  ASSERT_TRUE(send_db_message(db, dbRevertChanges, 0, NULL));
  ASSERT_FALSE(send_db_message(db, dbInsert, 1, &row));
  ASSERT_STR_EQUAL(audit.actor, "bob"); ASSERT_TRUE(audit.allowed > 0);
  ASSERT_FALSE(database_set_access(db, "", DB_ACCESS_ADMIN, NULL, NULL, NULL));
  ASSERT_FALSE(send_db_message(db, dbUser + 1, 0, NULL));
  db->dirty = false; destroy_database(db); PASS();
}
static const db_field_schema_t cascade_fields[] = {
  {.name="id", .type=DB_TYPE_INT, .primary_key=true},
  {.name="value", .type=DB_TYPE_INT, .relation_table="parents", .relation_field="id", .delete_rule=DB_DELETE_CASCADE},
};
static const db_table_schema_t cascade_tables[] = {
  {.table_id=0, .name="parents", .fields=access_fields, .field_count=2, .meta=access_meta, .meta_count=2, .record_size=sizeof(access_row_t)},
  {.table_id=1, .name="children", .fields=cascade_fields, .field_count=2, .meta=access_meta, .meta_count=2, .record_size=sizeof(access_row_t)},
  {.table_id=2, .name="restricted", .fields=cascade_fields, .field_count=2, .meta=access_meta, .meta_count=2, .record_size=sizeof(access_row_t)},
};
static const db_schema_def_t cascade_schema = {.name="cascade", .class_name="db_schema_xml", .tables=cascade_tables, .table_count=3};
static bool cascade_policy(database_t *db, const char *actor, uint32_t msg, uint32_t wp, void *context) {
  (void)db; (void)actor; (void)context;
  return !(msg == dbDelete && LOWORD(wp) == 2);
}
static void test_cascade_denial(void) {
  TEST("database access: denied cascaded delete rolls back prior child deletions");
  database_t *db = create_database_with_schema("cascade", "db_schema_xml", &cascade_schema, "");
  ASSERT_NOT_NULL(db); access_row_t parent = {.id=1}, child = {.id=1,.value=1};
  ASSERT_TRUE(send_db_message(db, dbInsert, 0, &parent));
  ASSERT_TRUE(send_db_message(db, dbInsert, 1, &child));
  ASSERT_TRUE(send_db_message(db, dbInsert, 2, &child));
  ASSERT_TRUE(database_set_access(db, "operator", DB_ACCESS_READ | DB_ACCESS_WRITE, cascade_policy, NULL, NULL));
  ASSERT_FALSE(send_db_message(db, dbDelete, 0, (void *)(intptr_t)1));
  ASSERT_NOT_NULL((void *)send_db_message(db, dbFind, 0, (void *)(intptr_t)1));
  ASSERT_NOT_NULL((void *)send_db_message(db, dbFind, 1, (void *)(intptr_t)1));
  ASSERT_NOT_NULL((void *)send_db_message(db, dbFind, 2, (void *)(intptr_t)1));
  db->dirty = false; destroy_database(db); PASS();
}
int main(void) { TEST_START("Database access"); DB_CLASS(db_schema_xml); test_access(); test_cascade_denial(); TEST_END(); }
