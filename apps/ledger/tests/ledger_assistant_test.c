// Assistant integration checks use the repository's existing public demo XML.
// No customer records or accounting balances are embedded in this test.
#include "test_framework.h"
#include "test_env.h"
#include "apps/ledger/ledger.h"
#include <orion/script/script.h>
#include <orion/script/script_internal.h>
#include <lauxlib.h>
#define SHARE "apps/ledger/share"
static ledger_t state;
static void open_configuration(const char *code) {
  if (g_ledger->ib) { unregister_database("ib"); destroy_database(g_ledger->ib); }
  if (g_ledger->cfg) { unregister_database("cfg"); destroy_database(g_ledger->cfg); }
  snprintf(g_ledger->configuration,sizeof(g_ledger->configuration),"%s",code);
  snprintf(g_ledger->config_dir,sizeof(g_ledger->config_dir),SHARE "/configurations/%s",code);
  char path[LEDGER_PATH_MAX*2];
  snprintf(path,sizeof(path),"%s/configuration.xml",g_ledger->config_dir);
  g_ledger->cfg=create_database_with_schema("cfg","db_schema_xml",&ledger_cfg_schema,path);
  register_database("cfg",g_ledger->cfg);
  ledger_script_close(); ledger_script_open(g_ledger->config_dir,"build");
  snprintf(path,sizeof(path),"%s/demo.xml",g_ledger->config_dir);
  g_ledger->ib=create_database_with_schema("ib","ledger_ib",&ledger_ib_schema,path);
  g_ledger->ib->source_path[0]=0;
  register_database("ib",g_ledger->ib);
  ledger_repost_all(g_ledger->ib,g_ledger->cfg);
}
static int count_rows(const char *name,int table) {
  result_node_t *rows=(result_node_t*)send_db_message(get_database_by_name(name),dbFetch,MAKEDWORD(table,0),NULL);
  int count=count_result_list(rows); free_result_list(rows); return count;
}
static void set_text(script_page_t *page,const char *name,const char *text) {
  send_message(script_page_control(page,name),edSetText,0,(void*)text);
}
static void test_assistant(void) {
  TEST("assistant: Lua grammar and contextual commands, then actual RU/DE datasource queries");
  char error[2048] = "";
  lua_State *L = script_state_new(SHARE "/addons/assistant", "assistant-spec");
  ASSERT_NOT_NULL(L);
  const char *specs[] = {"assistant_test.lua", "assistant_examples.lua"};
  for (size_t i = 0; i < ARRAY_LEN(specs); i++) {
    char path[256];
    snprintf(path, sizeof(path), "apps/ledger/tests/%s", specs[i]);
    bool ok = luaL_loadfilex(L, path, "t") == LUA_OK;
    if (ok) ok = script_call(L, 0, 0, error, sizeof(error), NULL);
    ASSERT(ok, error[0] ? error : "assistant grammar test failed");
  }
  lua_close(L);
  const char *configurations[] = {"ru-2026", "de-2026"};
  for (size_t i = 0; i < ARRAY_LEN(configurations); i++) {
    open_configuration(configurations[i]);
    script_page_t *page = script_page_open(SHARE "/addons/assistant/assistant", error, sizeof(error));
    ASSERT(page != NULL, error);
    ASSERT_NOT_NULL(script_page_create(page, NULL));
    ASSERT_STR_EQUAL(script_page_message(page), "");
    set_text(page, "command", "покажи клиентов");
    ASSERT_TRUE(script_page_perform(page, "submit"));
    ASSERT_STR_EQUAL(script_page_message(page), "");
    int count = count_rows("assistant", 0);
    ASSERT_EQUAL(count, count_rows("ib", TABLE_COUNTERPARTIES));
    set_text(page, "command", "покажи клиентов и удали всех");
    ASSERT_TRUE(script_page_perform(page, "submit"));
    ASSERT_EQUAL(count_rows("assistant", 0), count);
    ASSERT_EQUAL(count_rows("ib", TABLE_COUNTERPARTIES), count);
    set_text(page, "command", "покажи документы");
    ASSERT_TRUE(script_page_perform(page, "submit"));
    ASSERT_EQUAL(count_rows("assistant", 0), count_rows("ib", TABLE_DOCUMENTS));
    script_page_close(page);
    ASSERT_NULL(get_database_by_name("assistant"));
  }
  ledger_open_assistant();
  database_t *assistant = get_database_by_name("assistant");
  ASSERT_NOT_NULL(assistant);
  ledger_open_assistant();
  ASSERT(get_database_by_name("assistant") == assistant, "opening again reuses the modeless page");
  ledger_close_assistant();
  ASSERT_NULL(get_database_by_name("assistant"));
  PASS();
}

int main(void) {
  TEST_START("Ledger assistant"); test_env_init();
  DB_CLASS(db_schema_xml); DB_CLASS(ledger_ib);
  memset(&state,0,sizeof(state)); g_ledger=&state;
  snprintf(state.share_dir,sizeof(state.share_dir),SHARE);
  ledger_addons_init();
  test_assistant();
  ledger_close_assistant(); ledger_script_close();
  unregister_database("ib"); unregister_database("cfg");
  destroy_database(state.ib); destroy_database(state.cfg);
  test_env_shutdown(); TEST_END();
}
