// Assistant integration checks use the repository's existing public demo XML.
// No customer records or accounting balances are embedded in this test.
#include "test_framework.h"
#include "test_env.h"
#include "apps/ledger/ledger.h"
#include <orion/script/script.h>
#include <orion/script/script_internal.h>
#include <lauxlib.h>
#include <orion/kernel/renderer.h>
#define SHARE "build/share/ledger"
#define ASSISTANT_SHARE SHARE "/addons/assistant"
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
  lua_State *L = script_state_new(ASSISTANT_SHARE, "assistant-spec");
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
    script_page_t *page = script_page_open(ASSISTANT_SHARE "/assistant", error, sizeof(error));
    ASSERT(page != NULL, error);
    ASSERT_NOT_NULL(script_page_create(page, NULL));
    ASSERT_STR_EQUAL(script_page_message(page), "");
    window_t *heading = script_page_control(page, "heading");
    window_t *summary = script_page_control(page, "summary");
    window_t *more = script_page_control(page, "more");
    window_t *choice[5];
    for (int c = 0; c < 5; c++) {
      char name[16];
      snprintf(name, sizeof(name), "choice%d", c + 1);
      choice[c] = script_page_control(page, name);
      ASSERT_NOT_NULL(choice[c]);
      ASSERT_TRUE(choice[c]->flags & BUTTON_COMMANDLINK);
    }
    ASSERT_NULL(script_page_control(page, "request"));
    ASSERT_NULL(script_page_control(page, "results"));
    window_t *command = script_page_control(page, "command");
    window_t *failure = script_page_control(page, "error");
    // Office 97's question balloon: heading, then the field; empty slots take no room.
    ASSERT_STR_EQUAL(heading->title, "What would you like to do?");
    ASSERT_STR_EQUAL(summary->title, "");
    ASSERT_EQUAL(summary->frame.h, 0);
    ASSERT_EQUAL(choice[0]->frame.h, 0);
    int greeting_height = get_root_window(summary)->frame.h;
    ASSERT_EQUAL(failure->frame.h, 0);
    ASSERT_EQUAL(command->frame.y, heading->frame.y + heading->frame.h + 6);
    set_text(page, "command", "покажи клиентов");
    ASSERT_TRUE(script_page_perform(page, "submit"));
    ASSERT_STR_EQUAL(script_page_message(page), "");
    int count = count_rows("assistant", 0);
    ASSERT_EQUAL(count, count_rows("ib", TABLE_COUNTERPARTIES));
    ASSERT(count > 0, "the demo has counterparties");
    ASSERT(strstr(summary->title, "Found:") == summary->title, "the balloon text reports the query");
    ASSERT(strncmp(choice[0]->title, "1. ", 3) == 0, "each result is a numbered balloon label");
    ASSERT(choice[0]->frame.h > 0 && choice[0]->frame.w > 0, "a filled label takes its row");
    for (int c = 0; c < 5; c++)
      ASSERT_EQUAL(choice[c]->title[0] != 0, c < count);
    ASSERT_EQUAL(more->title[0] != 0, count > 5);
    ASSERT(command->frame.y > choice[MIN(count, 5) - 1]->frame.y, "labels come before the field, as in Office 97");
    int answer_height = get_root_window(summary)->frame.h;
    ASSERT(answer_height > greeting_height, "the bubble grows for a real answer");
    // Clicking a label is the same as typing its row number.
    char typed[sizeof(command->title)];
    snprintf(typed, sizeof(typed), "unsent text");
    set_text(page, "command", typed);
    // No Ledger main window here, so the label reaches the open request and the
    // host declines it; the companion test below opens a record for real.
    ASSERT_TRUE(script_page_perform_from(page, "choose", "choice1"));
    ASSERT_STR_EQUAL(summary->title, "Could not open the record.");
    ASSERT_STR_EQUAL(command->title, typed);
    ASSERT_EQUAL(count_rows("assistant", 0), count);
    ASSERT_TRUE(script_page_perform(page, "help"));
    ASSERT(get_root_window(summary)->frame.h < answer_height, "the bubble shrinks again for a shorter message");
    ASSERT_STR_EQUAL(choice[0]->title, "show debtors");
    ASSERT_EQUAL(choice[3]->frame.h, 0);
    ASSERT_FALSE(script_page_perform_from(page, "choose", "choice4"));
    ASSERT_STR_EQUAL(script_page_message(page), "That row is not in the current list.");
    ASSERT_TRUE(script_page_perform_from(page, "choose", "choice2"));   // an example runs as a command
    ASSERT_EQUAL(count_rows("assistant", 0), count);
    ASSERT_STR_EQUAL(command->title, "");
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

static window_t *control_with_prefix(window_t *win, const char *prefix) {
  if (!strncmp(win->title, prefix, strlen(prefix))) return win;
  for (window_t *child = win->children; child; child = child->next) {
    window_t *found = control_with_prefix(child, prefix);
    if (found) return found;
  }
  return NULL;
}

static void click(window_t *win) {
  const uint32_t events[] = {kEventLeftButtonDown, kEventLeftButtonUp};
  for (size_t i = 0; i < ARRAY_LEN(events); i++) {
    ui_event_t event = {.message = events[i],
      .x = (window_screen_x(win) + win->frame.w / 2) * UI_WINDOW_SCALE,
      .y = (window_screen_y(win) + win->frame.h / 2) * UI_WINDOW_SCALE};
    dispatch_message(&event);
  }
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
  ASSERT(companion->frame.w <= 340 && companion->frame.h < 260, "greeting fits a compact three-row bubble and the mascot");
  ASSERT_NOT_NULL(control_with_text(companion, "Close"));
  window_t *bubble = companion->children;
  ASSERT_TRUE(window_is_class(bubble, "SpeechBubble"));
  ASSERT_TRUE(window_has_state(bubble, WINDOW_STATE_VISIBLE));
  ASSERT_EQUAL(companion->frame.x + companion->frame.w, 1280 - 12);
  ASSERT_EQUAL(companion->frame.y + companion->frame.h, 800 - 12);
  int old_x = companion->frame.x, old_y = companion->frame.y;
  move_window(main, main->frame.x + 30, main->frame.y + 20);
  ASSERT_EQUAL(companion->frame.x, old_x);
  ASSERT_EQUAL(companion->frame.y, old_y);
  resize_window(main, main->frame.w - 80, main->frame.h - 40);
  ASSERT_EQUAL(companion->frame.x, old_x);
  ASSERT_EQUAL(companion->frame.y, old_y);
  ui_update_screen_size(1320, 820);
  send_message(companion, evDisplayChange, 0, NULL);
  ASSERT_EQUAL(companion->frame.x, old_x + 40);
  ASSERT_EQUAL(companion->frame.y, old_y + 20);
  ui_update_screen_size(1280, 800);
  send_message(companion, evDisplayChange, 0, NULL);

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
  // A balloon label is clicked like any control and opens its row in Ledger.
  send_message(command, edSetText, 0, "show documents");
  send_message(companion, evCommand, MAKEDWORD(submit->id, btnClicked), submit);
  window_t *label = control_with_prefix(companion, "1. ");
  ASSERT_NOT_NULL(label);
  ASSERT_TRUE(label->flags & BUTTON_COMMANDLINK);
  window_t *client = get_window_item(main, ID_MAIN_WINDOW_DOCUMENTS_CLIENT);
  ASSERT_NOT_NULL(client);
  ASSERT_NULL(mdi_active_document(client));
  click(label);
  ASSERT_NOT_NULL(control_with_prefix(companion, "Opened record: "));
  window_t *opened = mdi_active_document(client);
  ASSERT(opened != NULL, "the row opens as a Ledger document");
  destroy_window(opened);
  set_focus(command);
  send_message(command, edSetText, 0, "show customers");
  send_message(companion, evCommand, MAKEDWORD(submit->id, btnClicked), submit);
  ASSERT_EQUAL(count_rows("assistant", 0), count);

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
  const uint32_t double_click[] = {kEventLeftButtonDown, kEventLeftButtonUp,
    kEventLeftButtonDown, kEventLeftDoubleClick, kEventLeftButtonUp};
  for (int collapsed = 0; collapsed < 2; collapsed++) {
    if (collapsed) send_message(companion, evCommand, MAKEDWORD(mascot->id, btnClicked), mascot);
    for (size_t i = 0; i < ARRAY_LEN(double_click); i++) {
      ui_event_t event = {.message = double_click[i],
        .x = (window_screen_x(mascot) + mascot->frame.w / 2) * UI_WINDOW_SCALE,
        .y = (window_screen_y(mascot) + mascot->frame.h / 2) * UI_WINDOW_SCALE};
      dispatch_message(&event);
    }
    ASSERT_TRUE(window_has_state(bubble, WINDOW_STATE_VISIBLE));
    ASSERT(g_ui_runtime.focused == command, "double-click opens the conversation and focuses input");
    send_message(command, edGetText, sizeof(text), text);
    ASSERT_STR_EQUAL(text, "over fifty thousand");
    ASSERT_EQUAL(count_rows("assistant", 0), count);
  }
  send_message(companion, evCommand, MAKEDWORD(submit->id, btnClicked), submit);
  ASSERT(count_rows("assistant", 0) <= count, "the follow-up still filters the existing customer scope");
  underneath = find_window(companion->frame.x + 30, companion->frame.y + companion->frame.h - 30);
  ASSERT(!underneath || get_root_window(underneath) != companion,
         "transparent space beside the mascot does not block the document");
  int filtered_count = count_rows("assistant", 0);
  send_message(companion, evClose, 0, NULL);
  ASSERT_FALSE(window_has_state(companion, WINDOW_STATE_VISIBLE));
  send_message(main, evCommand, MAKEDWORD(ID_HELP_ASSISTANT, kMenuBarNotificationItemClick), NULL);
  ASSERT_TRUE(window_has_state(companion, WINDOW_STATE_VISIBLE));
  ASSERT(assistant_window() == companion, "restoring a hidden assistant retains the same conversation");
  ASSERT_EQUAL(count_rows("assistant", 0), filtered_count);
  send_message(command, edSetText, 0, "show customers");
  database_t *assistant = get_database_by_name("assistant");
  for (int i = 0; i < 3; i++) {
    send_message(companion, evClose, 0, NULL);
    ASSERT_FALSE(window_has_state(companion, WINDOW_STATE_VISIBLE));
    ASSERT(g_ui_runtime.focused == main, "closing returns keyboard input to Ledger");
    send_message(main, evCommand, MAKEDWORD(ID_HELP_ASSISTANT, kAcceleratorNotification), NULL);
    ASSERT_TRUE(window_has_state(companion, WINDOW_STATE_VISIBLE));
    ASSERT_TRUE(window_has_state(bubble, WINDOW_STATE_VISIBLE));
    ASSERT(assistant_window() == companion, "closing and reopening reuse the companion");
    ASSERT(get_database_by_name("assistant") == assistant, "closing preserves the conversation model");
    send_message(command, edGetText, sizeof(text), text);
    ASSERT_STR_EQUAL(text, "show customers");
    ASSERT(g_ui_runtime.focused == command, "restoring focuses the input");
  }
  destroy_window(main);
  ASSERT_NULL(assistant_window());
  ASSERT_NULL(get_database_by_name("assistant"));
  PASS();
}

static void test_modeless_documents(void) {
  TEST("Ledger MDI: two real documents, independent Save/Cancel, drafts and assistant isolation");
  open_configuration("ru-2026");
  int ids[2] = {0}, count = 0;
  result_node_t *rows = (result_node_t *)send_db_message(g_ledger->ib, dbFetch, TABLE_DOCUMENTS, NULL);
  for (result_node_t *n = rows; n && count < 2; n = n->next) {
    const db_document_t *doc = *(db_document_t **)n->data;
    const db_doc_type_t *type = (const db_doc_type_t *)send_db_message(g_ledger->cfg, dbFind, TABLE_DOC_TYPES, (void *)(intptr_t)doc->doc_type_id);
    if (type && !strcmp(type->form, "document")) ids[count++] = doc->id;
  }
  free_result_list(rows); ASSERT_EQUAL(count, 2);
  window_t *frame = create_window_from_form(&ledger_main_window_form, 10, 30, NULL, ledger_main_proc, 0, NULL);
  ASSERT_NOT_NULL(frame);
  window_t *client = get_window_item(frame, ID_MAIN_WINDOW_DOCUMENTS_CLIENT);
  ASSERT_NOT_NULL(client);
  ASSERT_TRUE(ledger_open_record("documents", ids[0])); window_t *a = mdi_active_document(client);
  ASSERT_TRUE(ledger_open_record("documents", ids[1])); window_t *b = mdi_active_document(client);
  ASSERT_NOT_NULL(a); ASSERT_NOT_NULL(b); ASSERT(a != b, "documents have separate windows");
  ASSERT_TRUE(window_is_enabled(frame));
  ASSERT(a->mdi_client == client && b->mdi_client == client, "both documents belong to the common MDI client");
  ASSERT(db_dialog_database(a) != db_dialog_database(b), "each editor owns a separate working copy");
  ASSERT_TRUE(ledger_open_record("documents", ids[0])); ASSERT(mdi_active_document(client) == a, "opening again activates the existing document");
  send_message(get_window_item(a, ID_DOCUMENT_COMMENT), edSetText, 0, "Saved from document A");
  send_message(get_window_item(b, ID_DOCUMENT_COMMENT), edSetText, 0, "Discarded from document B");
  ASSERT_TRUE(db_form_save(a, false)); ASSERT_TRUE(is_window(a));
  db_document_t *stored = (db_document_t *)send_db_message(g_ledger->ib, dbFind, TABLE_DOCUMENTS, (void *)(intptr_t)ids[0]);
  ASSERT_STR_EQUAL(stored->comment, "Saved from document A");
  send_message(get_window_item(a, ID_DOCUMENT_COMMENT), edSetText, 0, "Saved again from document A");
  ASSERT_TRUE(db_form_save(a, false));
  ASSERT_STR_EQUAL(stored->comment, "Saved again from document A");
  window_t *cancel = get_window_item(b, ID_DOCUMENT_CANCEL);
  send_message(b, evCommand, MAKEDWORD(cancel->id, btnClicked), cancel);
  ASSERT_FALSE(is_window(b));
  ASSERT_STR_EQUAL(stored->comment, "Saved again from document A");
  ledger_open_assistant();
  window_t *assistant = assistant_window(); ASSERT_NOT_NULL(assistant);
  ASSERT(assistant->owner == frame && !assistant->mdi_client, "assistant is an owned tool window, not a document");
  ASSERT_TRUE(assistant->flags & WINDOW_ALWAYSONTOP);
  int before = count_rows("ib", TABLE_DOCUMENTS);
  ledger_command(ID_DOC_NEW);
  window_t *draft = mdi_active_document(client); ASSERT_NOT_NULL(draft); ASSERT(draft != a, "new document opens independently");
  ASSERT_EQUAL(count_rows("ib", TABLE_DOCUMENTS), before);
  cancel = get_window_item(draft, ID_DOCUMENT_CANCEL);
  send_message(draft, evCommand, MAKEDWORD(cancel->id, btnClicked), cancel);
  ASSERT_EQUAL(count_rows("ib", TABLE_DOCUMENTS), before);
  cancel = get_window_item(a, ID_DOCUMENT_CANCEL);
  send_message(a, evCommand, MAKEDWORD(cancel->id, btnClicked), cancel);
  ASSERT_FALSE(is_window(a));
  destroy_window(frame);
  PASS();
}

static int close_prompts;
static void count_close_prompt(window_t *win, uint32_t msg, uint32_t wp, void *lp, void *userdata) {
  (void)msg; (void)wp; (void)lp; (void)userdata;
  if (!win->parent && !strcmp(win->title, ui_tr("Save"))) close_prompts++;
}
static void test_untouched_document_close(void) {
  TEST("Ledger: every untouched new document closes without a save prompt or persisted draft");
  open_configuration("ru-2003");
  window_t *frame = create_window_from_form(&ledger_main_window_form, 10, 30, NULL, ledger_main_proc, 0, NULL);
  ASSERT_NOT_NULL(frame);
  window_t *client = get_window_item(frame, ID_MAIN_WINDOW_DOCUMENTS_CLIENT);
  ASSERT_NOT_NULL(client);
  register_window_hook(evCreate, count_close_prompt, NULL);
  close_prompts = 0;
  int before = count_rows("ib", TABLE_DOCUMENTS);
  const uint32_t commands[] = {ID_DOC_NEW, ID_DOC_NEW_OPERATION, ID_DOC_NEW_PAYROLL, ID_DOC_NEW_ASSETS, ID_DOC_CLOSE_MONTH};
  for (int i = 0; i < ARRAY_LEN(commands); i++) {
    ledger_command(commands[i]);
    window_t *draft = mdi_active_document(client);
    ASSERT_NOT_NULL(draft);
    ASSERT_TRUE(window_is_enabled(frame));
    send_message(draft, evClose, 0, NULL);
    ASSERT_FALSE(is_window(draft));
    ASSERT_EQUAL(close_prompts, 0);
    ASSERT_EQUAL(count_rows("ib", TABLE_DOCUMENTS), before);
  }
  deregister_window_hook(evCreate, count_close_prompt, NULL);
  destroy_window(frame);
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
  test_modeless_documents();
  test_untouched_document_close();
  ledger_close_assistant(); ledger_script_close();
  unregister_database("ib"); unregister_database("cfg");
  destroy_database(state.ib); destroy_database(state.cfg);
  test_env_shutdown(); TEST_END();
}
