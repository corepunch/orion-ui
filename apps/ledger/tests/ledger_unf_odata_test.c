// Fixture tests for the read-only 1C UNF connector and its catalog handoff.
#include "test_framework.h"
#include "apps/ledger/ledger.h"
#include "apps/ledger/tests/test_databases.h"
#include <lauxlib.h>
#include <lualib.h>

static void *unf_find_by(database_t *db, int table, const char *field, const char *value) {
  return (void *)send_db_message(db, dbFind, MAKEDWORD(table, ledger_field(db, table, field)), (void *)value);
}

static void test_local_workflow(const char *filename) {
  TEST(filename);
  lua_State *L = luaL_newstate();
  ASSERT_TRUE(L);
  luaL_openlibs(L);
  lua_createtable(L, 0, 1);
  lua_pushstring(L, filename);
  lua_seti(L, -2, 0);
  lua_setglobal(L, "arg");
  int result = luaL_dofile(L, filename);
  char err[512] = "";
  if (result != LUA_OK) snprintf(err, sizeof(err), "%s", lua_tostring(L, -1));
  lua_close(L);
  ASSERT(result == LUA_OK, err);
  PASS();
}

static void test_import_unf_catalogs(void) {
  TEST("1C UNF stage: inserts catalogs, preserves existing fields, refuses document payloads");
  db_item_t owned = { .sku = "UNF-LOCAL", .name = "Local UNF collision", .price = 123.45 };
  ASSERT_TRUE(send_db_message(g_ledger->ib, dbInsert, TABLE_ITEMS, &owned));
  ledger_stage_import_t r;
  const char *stage = "apps/ledger/tests/fixtures/unf/catalog-stage.xml";
  ASSERT_TRUE(ledger_file_is_stage(stage));
  ASSERT(ledger_import_stage(g_ledger->ib, g_ledger->cfg, stage, &r), r.error);
  db_item_t *item = unf_find_by(g_ledger->ib, TABLE_ITEMS, "sku", "00045");
  ASSERT(item && !strcmp(item->name, "UNF item"), "item with leading-zero SKU imported");
  db_counterparty_t *cp = unf_find_by(g_ledger->ib, TABLE_COUNTERPARTIES, "tax_id", "0000000001");
  ASSERT(cp && !strcmp(cp->kpp, "000001001"), "counterparty tax identifiers imported");
  ASSERT(unf_find_by(g_ledger->ib, TABLE_WAREHOUSES, "name", "Главный склад УНФ"), "warehouse imported");
  item = unf_find_by(g_ledger->ib, TABLE_ITEMS, "sku", "UNF-LOCAL");
  ASSERT(item && item->price == 123.45 && !strcmp(item->name, "Local UNF collision"), "SKU collision does not overwrite price or name");
  ASSERT(!unf_find_by(g_ledger->ib, TABLE_IMPORT_SOURCES, "guid", "aa000000-0000-0000-0000-000000000004"), "collision not adopted");
  item = unf_find_by(g_ledger->ib, TABLE_ITEMS, "sku", "00045");
  ASSERT_TRUE(item);
  const int id = item->id;
  item->price = 88.0;
  ASSERT(ledger_import_stage(g_ledger->ib, g_ledger->cfg, stage, &r), r.error);
  item = unf_find_by(g_ledger->ib, TABLE_ITEMS, "sku", "00045");
  ASSERT(item && item->id == id && item->price == 88.0, "re-import preserves locally edited imported item");
  for (int i = 0; i < r.count_types; i++) ASSERT(!r.counts[i].added && !r.counts[i].updated, "repeat is insert-only");
  ASSERT(!ledger_import_stage(g_ledger->ib, g_ledger->cfg, "apps/ledger/tests/fixtures/unf/invalid-stage.xml", &r),
         "tagged stock or posting stages must not enter the ledger");
  ASSERT(!unf_find_by(g_ledger->ib, TABLE_WAREHOUSES, "name", "Never imported"), "invalid tagged stage is atomic");
  PASS();
}

int main(void) {
  TEST_START("Ledger 1C UNF");
  DB_CLASS(db_schema_xml);
  DB_CLASS(ledger_ib);
  static ledger_t fixture;
  g_ledger = &fixture;
  fixture.cfg = ledger_test_writable_seed("cfg", "db_schema_xml", &ledger_cfg_schema,
      "apps/ledger/share/configurations/ru-2003/configuration.xml");
  fixture.ib = ledger_test_infobase("apps/ledger/share/configurations/ru-2003/demo.xml");
  register_database("cfg", fixture.cfg);
  register_database("ib", fixture.ib);
  test_local_workflow("apps/ledger/tests/unf_odata_test.lua");
  test_local_workflow("apps/ledger/tests/label_sheet_test.lua");
  test_local_workflow("apps/ledger/tests/product_card_test.lua");
  test_local_workflow("apps/ledger/tests/fulfillment_test.lua");
  test_local_workflow("apps/ledger/tests/bank_statement.lua");
  test_import_unf_catalogs();
  destroy_database(fixture.ib);
  destroy_database(fixture.cfg);
  TEST_END();
}
