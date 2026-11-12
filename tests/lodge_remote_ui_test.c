#define _POSIX_C_SOURCE 200809L
#include "test_framework.h"
#include "test_env.h"
#include <orion/script/application.h>
#include <orion/script/remote.h>
#include <orion/script/remote_outbox.h>
#include <orion/kernel/http.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static bool wait_for(database_t *db, int table, int id, const char *field, const char *expected) {
  for (int i = 0; i < 500; i++) {
    repost_messages();
    void *row = (void *)send_db_message(db, dbFind, (uint32_t)table, (void *)(intptr_t)id);
    if (row) {
      char value[128]; db_get_field_text(db, table, row, field, value, sizeof(value));
      if (!strcmp(value, expected)) return true;
    }
    struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL);
  }
  fprintf(stderr, "wait failed: %s=%s\n", field, expected);
  return false;
}
static void test_recovered_request(void) {
  TEST("recovered payment requires an explicit new operation");
  const char *body = getenv("ORION_TEST_RECOVERY_BODY"), *company = getenv("ORION_TEST_COMPANY"), *kind = getenv("ORION_TEST_RECOVERY_KIND");
  ASSERT_NOT_NULL(body); ASSERT_NOT_NULL(company); ASSERT_NOT_NULL(kind);
  ASSERT_EQUAL(remote_outbox_save_at(getenv("ORION_REMOTE_PROFILE"), "lodge", getenv("ORION_SERVER_URL"), company, "tester", body), REMOTE_OUTBOX_OK);
  char error[2048], directory[128];
  ASSERT_TRUE(script_remote_configure_from_environment(error, sizeof(error)));
  snprintf(directory, sizeof(directory), "/tmp/orion-lodge-recovery-%ld", (long)getpid()); mkdir(directory, 0700);
  test_env_init();
  script_application_t *app = script_application_open("apps/lodge/share/remote/remote.orion", directory, error, sizeof(error));
  ASSERT_NOT_NULL(app); script_page_t *page = script_application_main(app); ASSERT_NOT_NULL(page);
  ASSERT_TRUE(script_page_perform(page, "connect"));
  for (int i = 0; i < 500 && !strstr(script_page_control(page, "notice")->title, "saved request needs retry"); i++) {
    struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL); repost_messages();
  }
  ASSERT_NOT_NULL(strstr(script_page_control(page, "notice")->title, "saved request needs retry"));
  ASSERT_TRUE(script_page_perform(page, "retry"));
  for (int i = 0; i < 500 && !strstr(script_page_control(page, "notice")->title, "Server data refreshed"); i++) {
    struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL); repost_messages();
  }
  ASSERT_NOT_NULL(strstr(script_page_control(page, "notice")->title, "Server data refreshed"));
  if (!strcmp(kind, "payment")) {
    ASSERT_FALSE(window_is_enabled(script_page_control(page, "pay")));
    ASSERT_FALSE(window_is_enabled(script_page_control(page, "refund")));
    ASSERT_FALSE(script_page_perform(page, "pay"));
    ASSERT_TRUE(window_is_enabled(script_page_control(page, "new_payment")));
    ASSERT_TRUE(script_page_perform(page, "new_payment"));
    ASSERT_TRUE(window_is_enabled(script_page_control(page, "pay")));
  } else {
    ASSERT_TRUE(window_is_enabled(script_page_control(page, "pay")));
    ASSERT_FALSE(window_is_enabled(script_page_control(page, "new_payment")));
  }
  script_application_close(app); test_env_shutdown(); rmdir(directory); PASS();
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
  for (int i = 0; i < 500 && !strstr(script_page_control(page, "notice")->title, "Server data refreshed"); i++) {
    struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL); repost_messages();
  }
  ASSERT_NOT_NULL(strstr(script_page_control(page, "notice")->title, "Server data refreshed"));
  send_message(script_page_control(page, "room_name"), edSetText, 0, "Remote 101");
  send_message(script_page_control(page, "capacity"), edSetText, 0, "2");
  send_message(script_page_control(page, "room_rate"), edSetText, 0, "100.00");
  ASSERT_TRUE(script_page_perform(page, "add_room")); ASSERT_TRUE(wait_for(db, rooms->table_id, 1, "name", "Remote 101"));
  send_message(script_page_control(page, "edit_room_choice"), cbSetCurrentValue, 1, NULL);
  ASSERT_TRUE(script_page_perform(page, "load_room"));
  ASSERT_STR_EQUAL(script_page_control(page, "room_name")->title, "Remote 101");
  send_message(script_page_control(page, "room_name"), edSetText, 0, "Updated 101");
  send_message(script_page_control(page, "room_category"), edSetText, 0, "Suite");
  send_message(script_page_control(page, "capacity"), edSetText, 0, "3");
  send_message(script_page_control(page, "room_rate"), edSetText, 0, "150.00");
  ASSERT_TRUE(script_page_perform(page, "save_room")); ASSERT_TRUE(wait_for(db, rooms->table_id, 1, "name", "Updated 101"));
  ASSERT_TRUE(script_page_perform(page, "load_room"));
  ASSERT_STR_EQUAL(script_page_control(page, "room_category")->title, "Suite");
  ASSERT_STR_EQUAL(script_page_control(page, "capacity")->title, "3");
  ASSERT_STR_EQUAL(script_page_control(page, "room_rate")->title, "150.00");
  send_message(script_page_control(page, "capacity"), edSetText, 0, "0");
  ASSERT_TRUE(script_page_perform(page, "save_room"));
  for (int i = 0; i < 500 && !strstr(script_page_control(page, "notice")->title, "Capacity must be a positive integer"); i++) {
    struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL); repost_messages();
  }
  ASSERT_NOT_NULL(strstr(script_page_control(page, "notice")->title, "Capacity must be a positive integer"));
  ASSERT_TRUE(script_page_perform(page, "load_room")); ASSERT_STR_EQUAL(script_page_control(page, "capacity")->title, "3");
  ASSERT_TRUE(script_page_perform(page, "connect"));
  for (int i = 0; i < 500 && !strstr(script_page_control(page, "room_editor")->title, "Load the room again"); i++) {
    struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL); repost_messages();
  }
  ASSERT_FALSE(window_is_enabled(script_page_control(page, "save_room")));
  for (int i = 0; i < 500 && !window_is_enabled(script_page_control(page, "load_room")); i++) {
    struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL); repost_messages();
  }
  ASSERT_TRUE(script_page_perform(page, "load_room")); ASSERT_TRUE(window_is_enabled(script_page_control(page, "save_room")));
  send_message(script_page_control(page, "guest_name"), edSetText, 0, "Remote guest");
  ASSERT_TRUE(script_page_perform(page, "add_guest")); ASSERT_TRUE(wait_for(db, guests->table_id, 1, "name", "Remote guest"));
  send_message(script_page_control(page, "edit_guest_choice"), cbSetCurrentValue, 1, NULL);
  ASSERT_TRUE(script_page_perform(page, "load_guest"));
  ASSERT_STR_EQUAL(script_page_control(page, "guest_name")->title, "Remote guest");
  send_message(script_page_control(page, "guest_name"), edSetText, 0, "Updated <guest>");
  send_message(script_page_control(page, "phone"), edSetText, 0, "+49 123");
  ASSERT_TRUE(script_page_perform(page, "save_guest")); ASSERT_TRUE(wait_for(db, guests->table_id, 1, "name", "Updated <guest>"));
  ASSERT_TRUE(script_page_perform(page, "load_guest")); ASSERT_STR_EQUAL(script_page_control(page, "phone")->title, "+49 123");
  ASSERT_TRUE(script_page_perform(page, "connect"));
  for (int i = 0; i < 500 && !strstr(script_page_control(page, "guest_editor")->title, "Load the guest again"); i++) {
    struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL); repost_messages();
  }
  ASSERT_FALSE(window_is_enabled(script_page_control(page, "save_guest")));
  for (int i = 0; i < 500 && !window_is_enabled(script_page_control(page, "load_guest")); i++) {
    struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL); repost_messages();
  }
  ASSERT_TRUE(script_page_perform(page, "load_guest")); ASSERT_TRUE(window_is_enabled(script_page_control(page, "save_guest")));
  send_message(script_page_control(page, "clean_room"), cbSetCurrentValue, 1, NULL);
  ASSERT_TRUE(script_page_perform(page, "mark_clean")); ASSERT_TRUE(wait_for(db, rooms->table_id, 1, "cleaning_state", "Clean"));
  ASSERT_TRUE(script_page_perform(page, "mark_dirty")); ASSERT_TRUE(wait_for(db, rooms->table_id, 1, "cleaning_state", "Dirty"));
  ASSERT_TRUE(script_page_perform(page, "mark_inspection")); ASSERT_TRUE(wait_for(db, rooms->table_id, 1, "cleaning_state", "Inspection needed"));
  ASSERT_TRUE(script_page_perform(page, "mark_clean")); ASSERT_TRUE(wait_for(db, rooms->table_id, 1, "cleaning_state", "Clean"));
  send_message(script_page_control(page, "room"), cbSetCurrentValue, 1, NULL);
  send_message(script_page_control(page, "guest"), cbSetCurrentValue, 1, NULL);
  send_message(script_page_control(page, "arrival"), edSetText, 0, "2035-01-01");
  send_message(script_page_control(page, "departure"), edSetText, 0, "2035-01-02");
  send_message(script_page_control(page, "persons"), edSetText, 0, "1");
  send_message(script_page_control(page, "rate"), edSetText, 0, "100.00");
  ASSERT_TRUE(script_page_perform(page, "reserve")); ASSERT_TRUE(wait_for(db, stays->table_id, 1, "status", "Reserved"));
  ASSERT_TRUE(script_page_perform(page, "edit_booking"));
  ASSERT_NOT_NULL(strstr(script_page_control(page, "editor")->title, "Edit reservation 1"));
  send_message(script_page_control(page, "rate"), edSetText, 0, "125.00");
  send_message(script_page_control(page, "notes"), edSetText, 0, "Late arrival");
  ASSERT_TRUE(script_page_perform(page, "reserve")); ASSERT_TRUE(wait_for(db, stays->table_id, 1, "balance", "125.00"));
  ASSERT_TRUE(script_page_perform(page, "edit_booking"));
  ASSERT_STR_EQUAL(script_page_control(page, "notes")->title, "Late arrival");
  ASSERT_TRUE(script_page_perform(page, "connect"));
  for (int i = 0; i < 500 && !strstr(script_page_control(page, "notice")->title, "Reload the selected reservation"); i++) {
    struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL); repost_messages();
  }
  ASSERT_NOT_NULL(strstr(script_page_control(page, "notice")->title, "Reload the selected reservation"));
  ASSERT_FALSE(window_is_enabled(script_page_control(page, "reserve")));
  ASSERT_TRUE(script_page_perform(page, "edit_booking"));
  ASSERT_TRUE(window_is_enabled(script_page_control(page, "reserve")));
  ASSERT_TRUE(script_page_perform(page, "new_booking"));
  ASSERT_STR_EQUAL(script_page_control(page, "editor")->title, "New reservation");
  ASSERT_TRUE(script_page_perform(page, "check_in")); ASSERT_TRUE(wait_for(db, stays->table_id, 1, "status", "Checked in"));
  send_message(script_page_control(page, "amount"), edSetText, 0, "125.00");
  ASSERT_TRUE(script_page_perform(page, "pay"));
  ASSERT_TRUE(wait_for(db, stays->table_id, 1, "balance", "0.00"));
  ASSERT_FALSE(window_is_enabled(script_page_control(page, "pay")));
  ASSERT_FALSE(window_is_enabled(script_page_control(page, "refund")));
  ASSERT_FALSE(script_page_perform(page, "pay"));
  ASSERT_TRUE(script_page_perform(page, "new_payment"));
  send_message(script_page_control(page, "amount"), edSetText, 0, "25.00");
  ASSERT_TRUE(script_page_perform(page, "refund")); ASSERT_TRUE(wait_for(db, stays->table_id, 1, "balance", "25.00"));
  ASSERT_FALSE(window_is_enabled(script_page_control(page, "refund")));
  ASSERT_TRUE(script_page_perform(page, "new_payment"));
  send_message(script_page_control(page, "amount"), edSetText, 0, "25.00");
  ASSERT_TRUE(script_page_perform(page, "pay")); ASSERT_TRUE(wait_for(db, stays->table_id, 1, "balance", "0.00"));
  ASSERT_TRUE(script_page_perform(page, "confirmation"));
  for (int i = 0; i < 500 && !strstr(script_page_control(page, "notice")->title, "Confirmation saved:"); i++) {
    struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL); repost_messages();
  }
  ASSERT_NOT_NULL(strstr(script_page_control(page, "notice")->title, "Confirmation saved:"));
  char confirmation_path[160], html[4096]; snprintf(confirmation_path, sizeof(confirmation_path), "%s/confirmation.html", directory);
  FILE *confirmation_file = fopen(confirmation_path, "rb"); ASSERT_NOT_NULL(confirmation_file);
  size_t html_size = fread(html, 1, sizeof(html) - 1, confirmation_file); fclose(confirmation_file); html[html_size] = 0;
  ASSERT_NOT_NULL(strstr(html, "Updated &lt;guest&gt;")); ASSERT_NULL(strstr(html, "Updated <guest>"));
  ASSERT_NOT_NULL(strstr(html, "Updated 101")); ASSERT_NOT_NULL(strstr(html, "125.00"));
  ASSERT_NOT_NULL(strstr(html, "EUR")); ASSERT_NOT_NULL(strstr(html, "Checked in"));
  char moved_directory[160]; snprintf(moved_directory, sizeof(moved_directory), "%s-moved", directory);
  ASSERT_EQUAL(rename(directory, moved_directory), 0);
  ASSERT_TRUE(script_page_perform(page, "confirmation"));
  for (int i = 0; i < 500 && !strstr(script_page_control(page, "notice")->title, "The document could not be written"); i++) {
    struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL); repost_messages();
  }
  ASSERT_NOT_NULL(strstr(script_page_control(page, "notice")->title, "The document could not be written"));
  ASSERT_EQUAL(rename(moved_directory, directory), 0);
  ASSERT_TRUE(script_page_perform(page, "check_out")); ASSERT_TRUE(wait_for(db, stays->table_id, 1, "status", "Checked out"));
  send_message(script_page_control(page, "room_name"), edSetText, 0, "Remote 102");
  ASSERT_TRUE(script_page_perform(page, "add_room")); ASSERT_TRUE(wait_for(db, rooms->table_id, 2, "name", "Remote 102"));
  send_message(script_page_control(page, "edit_room_choice"), cbSetCurrentValue, 2, NULL);
  ASSERT_FALSE(script_page_perform(page, "save_room"));
  send_message(script_page_control(page, "room_name"), edSetText, 0, "Remote 103");
  ASSERT_TRUE(script_page_perform(page, "add_room")); ASSERT_TRUE(wait_for(db, rooms->table_id, 3, "name", "Remote 103"));
  send_message(script_page_control(page, "sections"), tcSetSelection, 1, NULL);
  send_message(script_page_control(page, "group_name"), edSetText, 0, "Conference <team>");
  send_message(script_page_control(page, "group_guest"), cbSetCurrentValue, 1, NULL);
  send_message(script_page_control(page, "group_room1"), cbSetCurrentValue, 2, NULL);
  send_message(script_page_control(page, "group_room2"), cbSetCurrentValue, 3, NULL);
  send_message(script_page_control(page, "group_arrival"), edSetText, 0, "2035-02-01");
  send_message(script_page_control(page, "group_departure"), edSetText, 0, "2035-02-02");
  send_message(script_page_control(page, "group_persons1"), edSetText, 0, "1");
  send_message(script_page_control(page, "group_persons2"), edSetText, 0, "1");
  send_message(script_page_control(page, "group_rate1"), edSetText, 0, "50.00");
  send_message(script_page_control(page, "group_rate2"), edSetText, 0, "60.00");
  ASSERT_TRUE(script_page_perform(page, "group_reserve")); ASSERT_TRUE(wait_for(db, stays->table_id, 3, "status", "Reserved"));
  ASSERT_TRUE(script_page_perform(page, "group_reserve"));
  for (int i = 0; i < 500 && !strstr(script_page_control(page, "notice")->title, "already booked"); i++) {
    struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL); repost_messages();
  }
  ASSERT_NOT_NULL(strstr(script_page_control(page, "notice")->title, "already booked"));
  ASSERT_NULL((void *)send_db_message(db, dbFind, stays->table_id, (void *)(intptr_t)4));
  send_message(script_page_control(page, "sections"), tcSetSelection, 2, NULL);
  send_message(script_page_control(page, "night_booking"), cbSetCurrentValue, 2, NULL);
  send_message(script_page_control(page, "night"), edSetText, 0, "2035-02-01");
  send_message(script_page_control(page, "night_amount"), edSetText, 0, "75.00");
  ASSERT_TRUE(script_page_perform(page, "set_night"));
  bool price_set = wait_for(db, stays->table_id, 2, "balance", "75.00");
  if (!price_set) fprintf(stderr, "notice: %s; error: %s\n", script_page_control(page, "notice")->title, script_page_control(page, "error")->title);
  ASSERT_TRUE(price_set);
  send_message(script_page_control(page, "sections"), tcSetSelection, 1, NULL);
  send_message(script_page_control(page, "statement_group"), cbSetCurrentValue, 1, NULL);
  ASSERT_TRUE(script_page_perform(page, "group_statement"));
  for (int i = 0; i < 500 && !strstr(script_page_control(page, "notice")->title, "Group statement saved:"); i++) {
    struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL); repost_messages();
  }
  ASSERT_NOT_NULL(strstr(script_page_control(page, "notice")->title, "Group statement saved:"));
  char statement_path[160]; snprintf(statement_path, sizeof(statement_path), "%s/group-statement.html", directory);
  FILE *statement = fopen(statement_path, "rb"); ASSERT_NOT_NULL(statement);
  html_size = fread(html, 1, sizeof(html) - 1, statement); fclose(statement); html[html_size] = 0;
  ASSERT_NOT_NULL(strstr(html, "Conference &lt;team&gt;")); ASSERT_NOT_NULL(strstr(html, "Updated &lt;guest&gt;"));
  ASSERT_NOT_NULL(strstr(html, "Remote 102")); ASSERT_NOT_NULL(strstr(html, "Remote 103"));
  ASSERT_NOT_NULL(strstr(html, "75.00")); ASSERT_NOT_NULL(strstr(html, "60.00")); ASSERT_NOT_NULL(strstr(html, "135.00"));
  send_message(script_page_control(page, "sections"), tcSetSelection, 2, NULL);
  ASSERT_TRUE(script_page_perform(page, "reset_night")); ASSERT_TRUE(wait_for(db, stays->table_id, 2, "balance", "50.00"));
  send_message(script_page_control(page, "sections"), tcSetSelection, 3, NULL);
  ASSERT_TRUE(script_page_perform(page, "preview_day"));
  for (int i = 0; i < 500 && !strstr(script_page_control(page, "day_report")->title, "Overdue:"); i++) {
    struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL); repost_messages();
  }
  ASSERT_NOT_NULL(strstr(script_page_control(page, "day_report")->title, "Overdue: 0"));
  ASSERT_NOT_NULL(strstr(script_page_control(page, "cancelled_paid_report")->title, "Cancelled reservations with payments: 0"));
  ASSERT_TRUE(script_page_perform(page, "close_day"));
  for (int i = 0; i < 500 && !strstr(script_page_control(page, "notice")->title, "Server data refreshed"); i++) {
    struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL); repost_messages();
  }
  ASSERT_NOT_NULL(script_page_control(page, "day_history"));
  const db_table_schema_t *history = db_schema_table_named(db->schema, "day_closes"); ASSERT_NOT_NULL(history);
  ASSERT_TRUE(wait_for(db, history->table_id, 1, "cancelled_paid_display", "0.00"));
  ASSERT_TRUE(script_page_perform(page, "preview_day"));
  for (int i = 0; i < 500 && !strstr(script_page_control(page, "notice")->title, "Today is closed"); i++) {
    struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL); repost_messages();
  }
  ASSERT_NOT_NULL(strstr(script_page_control(page, "notice")->title, "Today is closed"));
  ASSERT_TRUE(script_page_perform(page, "close_day"));
  for (int i = 0; i < 500 && !strstr(script_page_control(page, "notice")->title, "already closed"); i++) {
    struct timespec delay = {.tv_nsec = 10000000}; nanosleep(&delay, NULL); repost_messages();
  }
  ASSERT_NOT_NULL(strstr(script_page_control(page, "notice")->title, "already closed"));
  script_application_close(app); test_env_shutdown(); unlink(statement_path); unlink(confirmation_path); rmdir(directory); PASS();
}
int main(void) {
  TEST_START("Lodge remote UI");
  if (getenv("ORION_TEST_RECOVERY_BODY")) test_recovered_request();
  else if (getenv("ORION_SERVER_URL") && getenv("ORION_SERVER_TOKEN")) test_lodge_remote_ui();
  else printf("SKIP: no disposable remote server configured\n");
  TEST_END();
}
