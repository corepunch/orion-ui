#define _POSIX_C_SOURCE 200809L
#include "test_framework.h"
#include "test_env.h"
#include <orion/script/application.h>
#include <orion/script/remote.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool wait_notice(script_page_t *page, const char *message) {
  for (int i = 0; i < 3000; i++) {
    repost_messages();
    window_t *notice = script_page_control(page, "notice");
    if (notice && strstr(notice->title, message) && window_is_enabled(script_page_control(page, "save_counterparty"))) return true;
    struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL);
  }
  return false;
}
static void set_text(script_page_t *page, const char *name, const char *value) {
  send_message(script_page_control(page, name), edSetText, 0, (void *)value);
}
static void test_ledger_remote_ui(void) {
  TEST("remote Ledger form creates, posts, reverses and closes a trading period");
  const char *country = getenv("ORION_TEST_COUNTRY");
  const char *expected_total = country && !strcmp(country, "RU") ? "12.20" : "11.90";
  char error[2048];
  ASSERT_TRUE(script_remote_configure_from_environment(error, sizeof(error)));
  test_env_init();
  script_application_t *app = script_application_open("apps/ledger/share/remote/remote.orion", NULL, error, sizeof(error));
  if (!app) fprintf(stderr, "%s\n", error);
  ASSERT_NOT_NULL(app); script_page_t *page = script_application_main(app); ASSERT_NOT_NULL(page);
  ASSERT_TRUE(script_page_perform(page, "connect")); ASSERT_TRUE(wait_notice(page, "Connected."));
  set_text(page, "name", "Remote Partner");
  ASSERT_TRUE(script_page_perform(page, "save_counterparty")); ASSERT_TRUE(wait_notice(page, "Counterparty 1 created."));
  ASSERT_TRUE(script_page_perform(page, "load_catalog_entry"));
  ASSERT(strstr(script_page_control(page, "catalog_edit_title")->title, "1"), "selected counterparty is identified before saving");
  set_text(page, "name", "Remote Partner Updated"); set_text(page, "tax_id", "TEST-TAX");
  ASSERT_TRUE(script_page_perform(page, "update_catalog_entry")); ASSERT_TRUE(wait_notice(page, "Catalog entry 1 updated."));
  set_text(page, "name", "Remote Goods"); set_text(page, "sku", "TEST-1"); set_text(page, "unit_id", "1"); set_text(page, "catalog_vat_id", "1");
  ASSERT_TRUE(script_page_perform(page, "save_item")); ASSERT_TRUE(wait_notice(page, "Item 1 created."));
  ASSERT_TRUE(script_page_perform(page, "load_catalog_entry"));
  set_text(page, "name", "Remote Goods Updated"); set_text(page, "sku", "TEST-2");
  ASSERT_TRUE(script_page_perform(page, "update_catalog_entry")); ASSERT_TRUE(wait_notice(page, "Catalog entry 1 updated."));
  set_text(page, "type_id", "1"); set_text(page, "draft_party_id", "1"); set_text(page, "draft_date", "2035-01-01");
  set_text(page, "item_id", "1"); set_text(page, "vat_id", "1"); set_text(page, "quantity", "1.000"); set_text(page, "price", "10.00");
  ASSERT_TRUE(script_page_perform(page, "add_line")); ASSERT_TRUE(script_page_perform(page, "create")); ASSERT_TRUE(wait_notice(page, "Draft 1 created."));
  set_text(page, "close_date", "2035-01-01"); ASSERT_TRUE(script_page_perform(page, "prepare_close"));
  ASSERT_TRUE(wait_notice(page, "Cannot close: 1 documents need attention; first document ID 1."));
  ASSERT(!window_is_enabled(script_page_control(page, "close_period")), "blocking draft prevents close");
  ASSERT_TRUE(script_page_perform(page, "load_draft"));
  ASSERT(strstr(script_page_control(page, "draft_edit_title")->title, "1"), "reviewed draft is identified before saving");
  set_text(page, "document_date", "2034-12-31"); set_text(page, "counterparty_id", "1");
  ASSERT_TRUE(script_page_perform(page, "update")); ASSERT_TRUE(wait_notice(page, "Draft 1 updated."));
  database_t *cache = get_database_by_name("ledger_remote_cache"); ASSERT_NOT_NULL(cache);
  const db_table_schema_t *documents = db_schema_table_named(cache->schema, "documents"); ASSERT_NOT_NULL(documents);
  void *draft = (void *)send_db_message(cache, dbFind, documents->table_id, (void *)(intptr_t)1); ASSERT_NOT_NULL(draft);
  char draft_date[32]; db_get_field_text(cache, documents->table_id, draft, "date", draft_date, sizeof(draft_date));
  ASSERT(!strcmp(draft_date, "2034-12-31"), "refreshed draft displays the saved date");
  ASSERT_TRUE(script_page_perform(page, "show_document_lines"));
  const db_table_schema_t *document_lines = db_schema_table_named(cache->schema, "document_lines"); ASSERT_NOT_NULL(document_lines);
  bool line_found = false;
  for (int i = 0; i < 500 && !line_found; i++) {
    repost_messages();
    void *row = (void *)send_db_message(cache, dbFind, document_lines->table_id, (void *)(intptr_t)1);
    if (row) {
      char quantity[32], price[32], total[32];
      db_get_field_text(cache, document_lines->table_id, row, "quantity", quantity, sizeof(quantity));
      db_get_field_text(cache, document_lines->table_id, row, "price", price, sizeof(price));
      db_get_field_text(cache, document_lines->table_id, row, "total", total, sizeof(total));
      line_found = !strcmp(quantity, "1.000") && !strcmp(price, "10.00") && !strcmp(total, expected_total);
    }
    if (!line_found) { struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL); }
  }
  ASSERT(line_found, "shared document lines display saved exact values");
  ASSERT_TRUE(script_page_perform(page, "prepare_copy_lines"));
  ASSERT(strstr(script_page_control(page, "copy_lines_title")->title, "1 saved"), "copy review identifies saved lines");
  ASSERT_TRUE(script_page_perform(page, "copy_lines"));
  ASSERT_TRUE(script_page_perform(page, "replace_lines")); ASSERT_TRUE(wait_notice(page, "Draft lines replaced."));
  ASSERT_TRUE(script_page_perform(page, "post")); ASSERT_TRUE(wait_notice(page, "Document posted."));
  ASSERT_TRUE(script_page_perform(page, "show_posting_history"));
  const db_table_schema_t *posting_history = db_schema_table_named(cache->schema, "posting_history"); ASSERT_NOT_NULL(posting_history);
  bool posting_found = false;
  for (int i = 0; i < 500 && !posting_found; i++) {
    repost_messages();
    void *row = (void *)send_db_message(cache, dbFind, posting_history->table_id, (void *)(intptr_t)1);
    if (row) {
      char actor[128], action[32], total[32];
      db_get_field_text(cache, posting_history->table_id, row, "actor", actor, sizeof(actor));
      db_get_field_text(cache, posting_history->table_id, row, "action", action, sizeof(action));
      db_get_field_text(cache, posting_history->table_id, row, "total", total, sizeof(total));
      posting_found = !strcmp(actor, "tester") && !strcmp(action, "post") && !strcmp(total, expected_total);
    }
    if (!posting_found) { struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL); }
  }
  ASSERT(posting_found, "shared posting history displays the selected document snapshot");
  ASSERT(strstr(script_page_control(page, "posting_history_title")->title, "1"), "history heading identifies the document");
  set_text(page, "history_document_id", "1");
  ASSERT_TRUE(script_page_perform(page, "find_posting_history"));
  for (int i = 0; i < 500 && !window_is_enabled(script_page_control(page, "show_revision_entries")); i++) {
    repost_messages(); struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL);
  }
  ASSERT(window_is_enabled(script_page_control(page, "show_revision_entries")), "history lookup completed");
  ASSERT_TRUE(script_page_perform(page, "show_revision_entries"));
  const db_table_schema_t *snapshot_entries = db_schema_table_named(cache->schema, "revision_entries"); ASSERT_NOT_NULL(snapshot_entries);
  bool entry_found = false;
  for (int i = 0; i < 500 && !entry_found; i++) {
    repost_messages();
    result_node_t *rows = (void *)send_db_message(cache, dbFetch, snapshot_entries->table_id, NULL);
    entry_found = count_result_list(rows) > 0; free_result_list(rows);
    if (!entry_found) { struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL); }
  }
  ASSERT(entry_found, "shared snapshot entries are displayed");
  ASSERT(strstr(script_page_control(page, "revision_entries_title")->title, "1"), "entry heading identifies the snapshot");
  set_text(page, "reversal_date", "2035-01-01"); set_text(page, "reason", "Test correction");
  ASSERT_TRUE(script_page_perform(page, "reverse")); ASSERT_TRUE(wait_notice(page, "Reversal 2 posted."));
  set_text(page, "close_date", "2035-01-01"); ASSERT_TRUE(script_page_perform(page, "prepare_close"));
  ASSERT_TRUE(wait_notice(page, "Review: close through"));
  ASSERT(window_is_enabled(script_page_control(page, "close_period")), "review enables period close");
  ASSERT_TRUE(script_page_perform(page, "connect")); ASSERT_TRUE(wait_notice(page, "Connected."));
  ASSERT(!window_is_enabled(script_page_control(page, "close_period")), "reconnecting invalidates close review");
  ASSERT_TRUE(script_page_perform(page, "prepare_close")); ASSERT_TRUE(wait_notice(page, "Review: close through"));
  ASSERT_TRUE(script_page_perform(page, "close_period")); ASSERT_TRUE(wait_notice(page, "Accounting period closed."));
  ASSERT_TRUE(script_page_perform(page, "show_history"));
  const db_table_schema_t *history = db_schema_table_named(cache->schema, "period_history"); ASSERT_NOT_NULL(history);
  bool found = false;
  for (int i = 0; i < 500 && !found; i++) {
    repost_messages();
    void *row = (void *)send_db_message(cache, dbFind, history->table_id, (void *)(intptr_t)1);
    if (row) {
      char actor[128], date[32];
      db_get_field_text(cache, history->table_id, row, "actor", actor, sizeof(actor));
      db_get_field_text(cache, history->table_id, row, "closed_through", date, sizeof(date));
      found = !strcmp(actor, "tester") && !strcmp(date, "2035-01-01");
    }
    if (!found) { struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL); }
  }
  ASSERT(found, "shared history displays the recorded close and actor");
  script_application_close(app); test_env_shutdown(); PASS();
}
int main(void) {
  TEST_START("Ledger remote UI");
  if (getenv("ORION_SERVER_URL") && getenv("ORION_SERVER_TOKEN")) test_ledger_remote_ui();
  else printf("SKIP: no disposable remote server configured\n");
  TEST_END();
}
