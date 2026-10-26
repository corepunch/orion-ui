// Assistant integration checks use the repository's existing public demo XML.
// No customer records or accounting balances are embedded in this test.
#include "test_framework.h"
#include "test_env.h"
#include "apps/ledger/ledger.h"
#include <orion/script/script.h>
#include <orion/script/script_internal.h>
#include <lauxlib.h>
#include <orion/kernel/renderer.h>
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
static void test_document_relation(script_page_t *page) {
  result_node_t *docs = (result_node_t *)send_db_message(g_ledger->ib, dbFetch, MAKEDWORD(TABLE_DOCUMENTS, 0), NULL);
  int party = 0, expected = 0;
  for (result_node_t *n = docs; n; n = n->next) {
    const db_document_t *doc = *(db_document_t **)n->data;
    if (!party && doc->counterparty_id) party = doc->counterparty_id;
  }
  ASSERT(party != 0, "demo documents must reference a counterparty");
  for (result_node_t *n = docs; n; n = n->next) {
    const db_document_t *doc = *(db_document_t **)n->data;
    if (doc->counterparty_id == party && doc->posted) expected++;
  }
  free_result_list(docs);
  set_text(page, "command", "show customers");
  ASSERT_TRUE(script_page_perform(page, "submit"));
  database_t *assistant = get_database_by_name("assistant");
  result_node_t *choices = (result_node_t *)send_db_message(assistant, dbFetch, 0, NULL);
  int ordinal = 0, index = 0;
  for (result_node_t *n = choices; n; n = n->next) {
    char value[32] = "";
    db_get_field_text(assistant, 0, *(void **)n->data, "record_id", value, sizeof(value));
    index++;
    if (atoi(value) == party) ordinal = index;
  }
  free_result_list(choices);
  ASSERT(ordinal > 0, "related counterparty is available in the assistant scope");
  char command[128];
  snprintf(command, sizeof(command), "show posted documents for %d", ordinal);
  set_text(page, "command", command);
  ASSERT_TRUE(script_page_perform(page, "submit"));
  ASSERT_STR_EQUAL(script_page_message(page), "");
  ASSERT_EQUAL(count_rows("assistant", 0), expected);
  set_text(page, "command", "sort by amount descending");
  ASSERT_TRUE(script_page_perform(page, "submit"));
  ASSERT_EQUAL(count_rows("assistant", 0), expected);
}
static void test_assistant(void) {
  TEST("assistant: Lua grammar and contextual commands, then actual RU/DE datasource queries");
  char error[2048] = "";
  lua_State *L = script_state_new(SHARE "/addons/assistant", "assistant-spec");
  ASSERT_NOT_NULL(L);
  const char *specs[] = {"assistant_test.lua", "assistant_examples.lua", "assistant_dialogue_test.lua", "assistant_dictionary_test.lua"};
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
    test_document_relation(page);
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

static window_t *assistant_window(void) {
  for (window_t *win = g_ui_runtime.windows; win; win = win->next)
    if (!strcmp(win->title, "Ledger — Assistant")) return win;
  return NULL;
}

static window_t *control_with_text(window_t *win, const char *text) {
  if (!strcmp(win->title, text)) return win;
  for (window_t *child = win->children; child; child = child->next) {
    window_t *found = control_with_text(child, text);
    if (found) return found;
  }
  return NULL;
}

static void test_assistant_companion(void) {
  TEST("Clippy: greeting without focus theft, click-through, conversation and window lifecycle");
  ui_update_screen_size(1280, 800);
  window_t *main = create_window_from_form(&ledger_main_window_form, 20, 40, NULL, ledger_main_proc, 0, NULL);
  ASSERT_NOT_NULL(main);
  set_focus(main);
  ledger_show_assistant();
  window_t *companion = assistant_window();
  ASSERT_NOT_NULL(companion);
  ASSERT_TRUE(window_has_state(companion, WINDOW_STATE_VISIBLE));
  ASSERT(g_ui_runtime.focused == main, "automatic greeting keeps focus in Ledger");
  ASSERT_EQUAL(titlebar_height(companion), 0);
  window_t *bubble = companion->children;
  ASSERT_TRUE(window_is_class(bubble, "SpeechBubble"));
  ASSERT_TRUE(window_has_state(bubble, WINDOW_STATE_VISIBLE));
  ASSERT(companion->frame.x + companion->frame.w <= main->frame.x + main->frame.w,
         "companion stays at the trailing edge of Ledger");
  int old_x = companion->frame.x, old_y = companion->frame.y;
  move_window(main, main->frame.x + 30, main->frame.y + 20);
  ASSERT_EQUAL(companion->frame.x, old_x + 30);
  ASSERT_EQUAL(companion->frame.y, old_y + 20);
  resize_window(main, main->frame.w - 80, main->frame.h - 40);
  ASSERT_EQUAL(companion->frame.x, old_x - 50);
  ASSERT_EQUAL(companion->frame.y, old_y - 20);

  ledger_open_assistant();
  ASSERT(assistant_window() == companion, "menu and shortcut reuse the companion");
  window_t *command = g_ui_runtime.focused;
  send_message(command, edSetText, 0, "show customers");
  window_t *submit = find_default_button(companion);
  ASSERT_NOT_NULL(submit);
  send_message(companion, evCommand, MAKEDWORD(submit->id, btnClicked), submit);
  int count = count_rows("assistant", 0);
  ASSERT_EQUAL(count, count_rows("ib", TABLE_COUNTERPARTIES));
  ASSERT(g_ui_runtime.focused == command, "sending keeps the command field focused");

  // The bottom-right control is the mascot, below the speech bubble.
  window_t *mascot = find_window(companion->frame.x + companion->frame.w - 70,
                                 companion->frame.y + companion->frame.h - 45);
  ASSERT_NOT_NULL(mascot);
  ASSERT(get_root_window(mascot) == companion, "the mascot remains clickable");
  send_message(command, edSetText, 0, "over fifty thousand");
  send_message(companion, evCommand, MAKEDWORD(mascot->id, btnClicked), mascot);
  ASSERT_FALSE(window_has_state(bubble, WINDOW_STATE_VISIBLE));
  ASSERT(g_ui_runtime.focused == main, "collapsing returns keyboard input to Ledger");
  window_t *underneath = find_window(companion->frame.x + 50, companion->frame.y + 80);
  ASSERT(underneath && get_root_window(underneath) == main,
         "collapsed speech bubble does not intercept the Ledger workspace");
  ASSERT_EQUAL(count_rows("assistant", 0), count);
  send_message(companion, evCommand, MAKEDWORD(mascot->id, btnClicked), mascot);
  ASSERT_TRUE(window_has_state(bubble, WINDOW_STATE_VISIBLE));
  char text[128] = "";
  send_message(command, edGetText, sizeof(text), text);
  ASSERT_STR_EQUAL(text, "over fifty thousand");
  ASSERT(g_ui_runtime.focused == command, "reopening focuses the input after showing the bubble");
  send_message(companion, evCommand, MAKEDWORD(submit->id, btnClicked), submit);
  ASSERT(count_rows("assistant", 0) <= count, "the follow-up still filters the existing customer scope");
  underneath = find_window(companion->frame.x + 30, companion->frame.y + companion->frame.h - 30);
  ASSERT(underneath && get_root_window(underneath) == main,
         "transparent space beside the mascot does not block the document");
  window_t *hide = control_with_text(companion, "Hide assistant");
  ASSERT_NOT_NULL(hide);
  send_message(companion, evCommand, MAKEDWORD(hide->id, btnClicked), hide);
  ASSERT_FALSE(window_has_state(companion, WINDOW_STATE_VISIBLE));
  ledger_open_assistant();
  ASSERT_TRUE(window_has_state(companion, WINDOW_STATE_VISIBLE));
  ASSERT(assistant_window() == companion, "restoring a hidden assistant retains the same conversation");
  destroy_window(main);
  ASSERT_NULL(assistant_window());
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
  test_assistant_companion();
  ledger_close_assistant(); ledger_script_close();
  unregister_database("ib"); unregister_database("cfg");
  destroy_database(state.ib); destroy_database(state.cfg);
  test_env_shutdown(); TEST_END();
}
