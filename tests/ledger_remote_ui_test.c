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
  char error[2048];
  ASSERT_TRUE(script_remote_configure_from_environment(error, sizeof(error)));
  test_env_init();
  script_application_t *app = script_application_open("apps/ledger/share/remote/remote.orion", NULL, error, sizeof(error));
  if (!app) fprintf(stderr, "%s\n", error);
  ASSERT_NOT_NULL(app); script_page_t *page = script_application_main(app); ASSERT_NOT_NULL(page);
  ASSERT_TRUE(script_page_perform(page, "connect")); ASSERT_TRUE(wait_notice(page, "Connected."));
  set_text(page, "name", "Remote Partner");
  ASSERT_TRUE(script_page_perform(page, "save_counterparty")); ASSERT_TRUE(wait_notice(page, "Counterparty 1 created."));
  set_text(page, "name", "Remote Goods"); set_text(page, "sku", "TEST-1"); set_text(page, "unit_id", "1"); set_text(page, "catalog_vat_id", "1");
  ASSERT_TRUE(script_page_perform(page, "save_item")); ASSERT_TRUE(wait_notice(page, "Item 1 created."));
  set_text(page, "type_id", "1"); set_text(page, "draft_party_id", "1"); set_text(page, "draft_date", "2035-01-01");
  set_text(page, "item_id", "1"); set_text(page, "vat_id", "1"); set_text(page, "quantity", "1.000"); set_text(page, "price", "10.00");
  ASSERT_TRUE(script_page_perform(page, "add_line")); ASSERT_TRUE(script_page_perform(page, "create")); ASSERT_TRUE(wait_notice(page, "Draft 1 created."));
  ASSERT_TRUE(script_page_perform(page, "post")); ASSERT_TRUE(wait_notice(page, "Document posted."));
  set_text(page, "reversal_date", "2035-01-01"); set_text(page, "reason", "Test correction");
  ASSERT_TRUE(script_page_perform(page, "reverse")); ASSERT_TRUE(wait_notice(page, "Reversal 2 posted."));
  set_text(page, "close_date", "2035-01-01"); ASSERT_TRUE(script_page_perform(page, "prepare_close"));
  ASSERT_TRUE(script_page_perform(page, "close_period")); ASSERT_TRUE(wait_notice(page, "Accounting period closed."));
  script_application_close(app); test_env_shutdown(); PASS();
}
int main(void) {
  TEST_START("Ledger remote UI");
  if (getenv("ORION_SERVER_URL") && getenv("ORION_SERVER_TOKEN")) test_ledger_remote_ui();
  else printf("SKIP: no disposable remote server configured\n");
  TEST_END();
}
