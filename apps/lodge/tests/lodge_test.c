#include "test_framework.h"
#include "test_env.h"
#include <orion/script/script_internal.h>
#include <orion/script/script.h>
#include <lauxlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <orion/script/application.h>
#include <orion/user/display_group.h>
#include "build/generated/apps/lodge/lodge.h"

static database_t *hotel;
static char hotel_path[128];
static void test_hotel(void) {
  TEST("Lua hotel workflows: conflicts, capacity, settlement and transitions");
  lua_State *L = script_state_new("apps/lodge/share", "hotel test");
  ASSERT_NOT_NULL(L);
  ASSERT_EQUAL(luaL_loadfilex(L, "apps/lodge/tests/hotel_spec.lua", "t"), LUA_OK);
  char message[4096]; bool refused;
  bool ok = script_call(L, 0, 0, message, sizeof(message), &refused);
  if (!ok) fprintf(stderr, "%s\n", message);
  ASSERT_TRUE(ok);
  script_state_close(L);
  PASS();
}
static void test_persistence_and_page(void) {
  TEST("hotel XML round-trip and real reception form with shared plugins");
  ASSERT_TRUE(send_db_message(hotel, dbSave, 0, NULL));
  unregister_database("lodge"); destroy_database(hotel);
  hotel = create_database_with_schema("lodge", "db_schema_xml", &lodge_lodge_schema, hotel_path);
  ASSERT_NOT_NULL(hotel); register_database("lodge", hotel);
  db_reservation_t *row = (void *)send_db_message(hotel, dbFind, MAKEDWORD(TABLE_RESERVATIONS, 0), (void *)(intptr_t)1);
  ASSERT_NOT_NULL(row); ASSERT_STR_EQUAL(row->status, "checked_out");
  ASSERT_EQUAL(row->paid, 209.9);
  unregister_database("lodge"); destroy_database(hotel); hotel = NULL;
  char directory[128], target[160], error[2048];
  snprintf(directory, sizeof(directory), "/tmp/orion-lodge-app-%ld", (long)getpid());
  mkdir(directory, 0700);
  snprintf(target, sizeof(target), "%s/hotel.xml", directory);
  FILE *in = fopen(hotel_path, "rb"), *out = fopen(target, "wb");
  ASSERT_NOT_NULL(in); ASSERT_NOT_NULL(out);
  int byte; while ((byte = fgetc(in)) != EOF) fputc(byte, out);
  fclose(in); fclose(out);
  script_application_t *app = script_application_open("apps/lodge/share", directory, error, sizeof(error));
  if (!app) fprintf(stderr, "%s\n", error);
  ASSERT_NOT_NULL(app);
  hotel = get_database_by_name("lodge");
  script_page_t *page = script_application_main(app);
  ASSERT_NOT_NULL(script_page_control(page, "bookings"));
  ASSERT_NOT_NULL(strstr(script_page_control(page, "summary")->title, "3"));
  ASSERT_TRUE(script_page_perform(page, "new_booking"));
  ASSERT_FALSE(script_page_perform(page, "reserve"));
  ASSERT_NOT_NULL(strstr(script_page_message(page), "room"));
  // Native writes run the same Lua validator as scripted writes.
  db_room_t invalid = {.capacity = 0, .rate = 100};
  snprintf(invalid.name, sizeof(invalid.name), "Invalid");
  ASSERT_FALSE(send_db_message(hotel, dbInsert, TABLE_ROOMS, &invalid));
  ASSERT_NOT_NULL(strstr(hotel->error, "Capacity"));
  db_room_t room = {.capacity = 2, .rate = 100};
  snprintf(room.name, sizeof(room.name), "202");
  db_room_t *inserted = (void *)send_db_message(hotel, dbInsert, TABLE_ROOMS, &room);
  ASSERT_NOT_NULL(inserted); room.id = inserted->id;
  ASSERT_TRUE(script_runtime_action(script_application_runtime(app), "orion.application", "open", "rooms", error, sizeof(error)));
  send_message(script_page_control(page, "room"), cbSetCurrentValue, room.id, NULL);
  send_message(script_page_control(page, "guest"), cbSetCurrentValue, 1, NULL);
  send_message(script_page_control(page, "arrival"), edSetText, 0, "2026-05-01");
  send_message(script_page_control(page, "departure"), edSetText, 0, "2026-05-03");
  ASSERT_TRUE(script_page_perform(page, "room_rate"));
  ASSERT_TRUE(script_page_perform(page, "reserve"));
  db_reservation_t *created = (void *)send_db_message(hotel, dbFind, TABLE_RESERVATIONS, (void *)(intptr_t)4);
  ASSERT_NOT_NULL(created); ASSERT_EQUAL(created->room_id, room.id); ASSERT_EQUAL(created->rate, 100);
  char total[80]; db_get_field_text(hotel, TABLE_RESERVATIONS, created, "total", total, sizeof(total));
  ASSERT_NOT_NULL(strstr(total, "200"));
  database_t *group = get_database_by_name("bookings"); ASSERT_NOT_NULL(group);
  ASSERT_TRUE(send_db_message(group, dbSelectRecord, created->id, NULL));
  result_node_t *selection = (void *)send_db_message(group, dbFetch, 1, NULL);
  ASSERT_NOT_NULL(selection); ASSERT_EQUAL((*(db_reservation_t **)selection->data)->id, created->id);
  free_result_list(selection);
  // A real host save failure rolls back the entire reservation action.
  ASSERT_TRUE(script_page_perform(page, "new_booking"));
  send_message(script_page_control(page, "room"), cbSetCurrentValue, room.id, NULL);
  send_message(script_page_control(page, "guest"), cbSetCurrentValue, 1, NULL);
  send_message(script_page_control(page, "arrival"), edSetText, 0, "2026-06-01");
  send_message(script_page_control(page, "departure"), edSetText, 0, "2026-06-03");
  ASSERT_TRUE(script_page_perform(page, "room_rate"));
  char original[sizeof(hotel->source_path)]; snprintf(original, sizeof(original), "%s", hotel->source_path);
  snprintf(hotel->source_path, sizeof(hotel->source_path), "/nonexistent-orion-lodge-directory/hotel.xml");
  ASSERT_FALSE(script_page_perform(page, "reserve"));
  ASSERT_NOT_NULL(strstr(script_page_message(page), "saved"));
  ASSERT_NULL((void *)send_db_message(hotel, dbFind, TABLE_RESERVATIONS, (void *)(intptr_t)5));
  snprintf(hotel->source_path, sizeof(hotel->source_path), "%s", original);
  script_application_close(app); hotel = NULL;
  unlink(target); rmdir(directory);
  PASS();
}
int main(void) {
  TEST_START("Orion Lodge");
  test_env_init(); DB_CLASS(db_schema_xml);
  snprintf(hotel_path, sizeof(hotel_path), "/tmp/orion-lodge-%ld.xml", (long)getpid());
  hotel = create_database_with_schema("lodge", "db_schema_xml", &lodge_lodge_schema, hotel_path);
  register_database("lodge", hotel);
  test_hotel(); test_persistence_and_page();
  if (hotel) { unregister_database("lodge"); destroy_database(hotel); } unlink(hotel_path);
  test_env_shutdown(); TEST_END();
}
