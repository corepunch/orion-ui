#define _POSIX_C_SOURCE 200809L
#include "test_framework.h"
#include "test_env.h"
#include <orion/script/application.h>
#include <orion/script/remote.h>
#include <orion/kernel/http.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static bool wait_for(database_t *db, int table, int id, const char *field, const char *expected) {
  for (int i = 0; i < 3000; i++) {
    repost_messages();
    void *row = (void *)send_db_message(db, dbFind, (uint32_t)table, (void *)(intptr_t)id);
    if (row) {
      char value[128]; db_get_field_text(db, table, row, field, value, sizeof(value));
      if (!strcmp(value, expected)) return true;
    }
    struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL);
  }
  return false;
}
static void test_lodge_remote_ui(void) {
  TEST("remote Lodge form drives a shared-server stay");
  char error[2048], directory[128];
  ASSERT_TRUE(script_remote_configure_from_environment(error, sizeof(error)));
  snprintf(directory, sizeof(directory), "/tmp/orion-lodge-remote-ui-%ld", (long)getpid()); mkdir(directory, 0700);
  test_env_init();
  script_application_t *app = script_application_open("apps/lodge/share/remote/remote.orion", directory, error, sizeof(error));
  if (!app) fprintf(stderr, "%s\n", error);
  ASSERT_NOT_NULL(app); script_page_t *page = script_application_main(app); ASSERT_NOT_NULL(page);
  database_t *db = get_database_by_name("lodge_remote_cache"); ASSERT_NOT_NULL(db);
  const db_table_schema_t *rooms = db_schema_table_named(db->schema, "rooms"), *guests = db_schema_table_named(db->schema, "guests"), *stays = db_schema_table_named(db->schema, "reservations");
  ASSERT_NOT_NULL(rooms); ASSERT_NOT_NULL(guests); ASSERT_NOT_NULL(stays);
  ASSERT_TRUE(script_page_perform(page, "connect"));
  for (int i = 0; i < 3000 && !strstr(script_page_control(page, "notice")->title, "Server data refreshed"); i++) {
    struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL); repost_messages();
  }
  ASSERT_NOT_NULL(strstr(script_page_control(page, "notice")->title, "Server data refreshed"));
  send_message(script_page_control(page, "room_name"), edSetText, 0, "Remote 101");
  send_message(script_page_control(page, "capacity"), edSetText, 0, "2");
  send_message(script_page_control(page, "room_rate"), edSetText, 0, "100.00");
  ASSERT_TRUE(script_page_perform(page, "add_room")); ASSERT_TRUE(wait_for(db, rooms->table_id, 1, "name", "Remote 101"));
  send_message(script_page_control(page, "guest_name"), edSetText, 0, "Remote guest");
  ASSERT_TRUE(script_page_perform(page, "add_guest")); ASSERT_TRUE(wait_for(db, guests->table_id, 1, "name", "Remote guest"));
  send_message(script_page_control(page, "clean_room"), cbSetCurrentValue, 1, NULL);
  ASSERT_TRUE(script_page_perform(page, "mark_clean")); ASSERT_TRUE(wait_for(db, rooms->table_id, 1, "cleaning_state", "Clean"));
  send_message(script_page_control(page, "room"), cbSetCurrentValue, 1, NULL);
  send_message(script_page_control(page, "guest"), cbSetCurrentValue, 1, NULL);
  send_message(script_page_control(page, "arrival"), edSetText, 0, "2035-01-01");
  send_message(script_page_control(page, "departure"), edSetText, 0, "2035-01-02");
  send_message(script_page_control(page, "persons"), edSetText, 0, "1");
  send_message(script_page_control(page, "rate"), edSetText, 0, "100.00");
  ASSERT_TRUE(script_page_perform(page, "reserve")); ASSERT_TRUE(wait_for(db, stays->table_id, 1, "status", "Reserved"));
  ASSERT_TRUE(script_page_perform(page, "check_in")); ASSERT_TRUE(wait_for(db, stays->table_id, 1, "status", "Checked in"));
  send_message(script_page_control(page, "amount"), edSetText, 0, "100.00");
  ASSERT_TRUE(script_page_perform(page, "pay"));
  ASSERT_TRUE(wait_for(db, stays->table_id, 1, "balance", "0.00"));
  ASSERT_TRUE(script_page_perform(page, "check_out")); ASSERT_TRUE(wait_for(db, stays->table_id, 1, "status", "Checked out"));
  script_application_close(app); test_env_shutdown(); rmdir(directory); PASS();
}
int main(void) {
  TEST_START("Lodge remote UI");
  if (getenv("ORION_SERVER_URL") && getenv("ORION_SERVER_TOKEN")) test_lodge_remote_ui();
  else printf("SKIP: no disposable remote server configured\n");
  TEST_END();
}
