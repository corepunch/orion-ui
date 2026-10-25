#include "test_framework.h"
#include "apps/ledger/ledger.h"
#include <platform/platform.h>

static ledger_t automation_test_ledger;
static void test_local_automation(void) {
  TEST("Lua recipes and typed graphs use the real schema API, validate writes and roll back failures");
  DB_CLASS(db_schema_xml);
  DB_CLASS(ledger_ib);
  g_ledger = &automation_test_ledger;
  snprintf(g_ledger->config_dir, sizeof(g_ledger->config_dir), "apps/ledger/share/configurations/ru-2003");
  snprintf(g_ledger->data_dir, sizeof(g_ledger->data_dir), "build/test-ledger-automation");
  axMkDir(g_ledger->data_dir);
  g_ledger->cfg = create_database_with_schema("cfg", "db_schema_xml", &ledger_cfg_schema, "apps/ledger/share/configurations/ru-2003/configuration.xml");
  g_ledger->ib = create_database_with_schema("ib", "ledger_ib", &ledger_ib_schema, "apps/ledger/share/configurations/ru-2003/demo.xml");
  ASSERT_NOT_NULL(g_ledger->cfg);
  ASSERT_NOT_NULL(g_ledger->ib);
  register_database("cfg", g_ledger->cfg);
  register_database("ib", g_ledger->ib);
  ASSERT_TRUE(ledger_script_open(g_ledger->config_dir, g_ledger->data_dir));
  ledger_repost_all(g_ledger->ib, g_ledger->cfg);
  ASSERT_TRUE(send_db_message(g_ledger->ib, dbBeginChanges, 0, NULL));
  char error[1024];
  bool ok = ledger_script_run("apps/ledger/tests/automation_readonly.lua", false, error, sizeof(error));
  if (!ok) fprintf(stderr, "%s\n", error);
  ASSERT_TRUE(ok);
  ok = ledger_script_run("apps/ledger/tests/automation_failure.lua", true, error, sizeof(error));
  ASSERT_FALSE(ok);
  ASSERT(strstr(error, "deliberate script failure"), "Lua failure should be returned to the caller");
  result_node_t *rows = (result_node_t *)send_db_message(g_ledger->ib, dbFetch, MAKEDWORD(TABLE_ITEMS, ledger_field(g_ledger->ib, TABLE_ITEMS, "sku")), "OUTER-ROLLBACK");
  int count = count_result_list(rows);
  free_result_list(rows);
  ASSERT_EQUAL(count, 0);
  ok = ledger_script_run("apps/ledger/tests/automation.lua", true, error, sizeof(error));
  if (!ok) fprintf(stderr, "%s\n", error);
  ASSERT_TRUE(ok);
  ledger_script_close();
  const char *custom = "build/test-ledger-custom";
  axMkDir(custom);
  axMkDir("build/test-ledger-custom/plugins");
  FILE *entry = fopen("build/test-ledger-custom/plugins/20-custom.lua", "w");
  ASSERT_NOT_NULL(entry);
  fputs("local spec = assert(loadfile('build/generated/apps/ledger/custom_catalog.lua'))()\n"
        "ledger.automation.register(spec, {text=function(_,p) return p.value end}, {echo=function(p) return {p.value} end})\n", entry);
  fclose(entry);
  ASSERT_TRUE(ledger_script_open(g_ledger->config_dir, custom));
  ok = ledger_script_run("apps/ledger/tests/custom_plugin.lua", false, error, sizeof(error));
  if (!ok) fprintf(stderr, "%s\n", error);
  ASSERT_TRUE(ok);
  send_db_message(g_ledger->ib, dbRevertChanges, 0, NULL);
  g_ledger->ib->dirty = false;
  destroy_database(g_ledger->ib);
  destroy_database(g_ledger->cfg);
  ledger_script_close();
  PASS();
}

int main(void) {
  TEST_START("Ledger local Lua automation");
  test_local_automation();
  TEST_END();
}
