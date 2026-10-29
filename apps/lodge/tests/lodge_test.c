#include "test_framework.h"
#include "test_env.h"
#include <orion/script/script_internal.h>
#include <orion/script/script.h>
#include <lauxlib.h>
#include <unistd.h>
#include "build/generated/apps/lodge/lodge.h"

static database_t *hotel;
static char hotel_path[128];
static bool fail_save;
static int save_test(lua_State *L) {
  if (fail_save) { lua_pushnil(L); lua_pushliteral(L, "Simulated save failure."); return 2; }
  lua_pushboolean(L, send_db_message(hotel, dbSave, 0, NULL)); return 1;
}
static int confirmation_test(lua_State *L) { lua_pushstring(L, "test confirmation"); return 1; }
static int test_store(lua_State *L) {
  lua_newtable(L);
  lua_pushcfunction(L, save_test); lua_setfield(L, -2, "save");
  lua_pushcfunction(L, confirmation_test); lua_setfield(L, -2, "confirmation");
  return 1;
}
static void test_hotel(void) {
  TEST("Lua hotel workflows: conflicts, capacity, settlement and transitions");
  lua_State *L = script_state_new("apps/lodge/share", "hotel test");
  ASSERT_NOT_NULL(L);
  ASSERT_EQUAL(luaL_loadfilex(L, "apps/lodge/tests/hotel_spec.lua", "t"), LUA_OK);
  char message[4096]; bool refused;
  bool ok = script_call(L, 0, 0, message, sizeof(message), &refused);
  if (!ok) fprintf(stderr, "%s\n", message);
  ASSERT_TRUE(ok);
  lua_close(L);
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
  script_register_module("lodge.store", test_store);
  char error[1024];
  script_page_t *page = script_page_open("apps/lodge/share/reception", error, sizeof(error));
  if (!page) fprintf(stderr, "%s\n", error);
  ASSERT_NOT_NULL(page);
  ASSERT_NOT_NULL(script_page_create(page, NULL));
  ASSERT_NOT_NULL(script_page_control(page, "bookings"));
  ASSERT_NOT_NULL(strstr(script_page_control(page, "summary")->title, "3"));
  ASSERT_TRUE(script_page_perform(page, "new_booking"));
  ASSERT_FALSE(script_page_perform(page, "reserve"));
  ASSERT_NOT_NULL(strstr(script_page_message(page), "room"));
  // Failed persistence rolls the action back, so retry cannot duplicate a room.
  send_message(script_page_control(page, "room_name"), edSetText, 0, "202");
  fail_save = true;
  ASSERT_FALSE(script_page_perform(page, "save_room"));
  ASSERT_STR_EQUAL(script_page_message(page), "Simulated save failure.");
  ASSERT_NULL((void *)send_db_message(hotel, dbFind, MAKEDWORD(TABLE_ROOMS, 0), (void *)(intptr_t)2));
  fail_save = false;
  ASSERT_TRUE(script_page_perform(page, "save_room"));
  ASSERT_NOT_NULL((void *)send_db_message(hotel, dbFind, MAKEDWORD(TABLE_ROOMS, 0), (void *)(intptr_t)2));
  // Create a reservation through actual typed controls and export its confirmation.
  send_message(script_page_control(page, "room"), cbSetCurrentValue, 2, NULL);
  send_message(script_page_control(page, "guest"), cbSetCurrentValue, 1, NULL);
  send_message(script_page_control(page, "arrival"), edSetText, 0, "2026-05-01");
  send_message(script_page_control(page, "departure"), edSetText, 0, "2026-05-03");
  ASSERT_TRUE(script_page_perform(page, "room_rate"));
  ASSERT_TRUE(script_page_perform(page, "reserve"));
  db_reservation_t *created = (void *)send_db_message(hotel, dbFind, MAKEDWORD(TABLE_RESERVATIONS, 0), (void *)(intptr_t)4);
  ASSERT_NOT_NULL(created); ASSERT_EQUAL(created->room_id, 2); ASSERT_EQUAL(created->rate, 100);
  ASSERT_TRUE(script_page_perform(page, "confirmation"));
  ASSERT_NOT_NULL(strstr(script_page_control(page, "notice")->title, "test confirmation"));
  script_page_close(page);
  // Both forms and code use the add-on's scoped translations.
  ui_set_language("ru");
  page = script_page_open("apps/lodge/share/reception", error, sizeof(error));
  ASSERT_NOT_NULL(page); ASSERT_NOT_NULL(script_page_create(page, NULL));
  ASSERT_STR_EQUAL(script_page_control(page, "heading")->title, "Ресепшен");
  ASSERT_FALSE(script_page_perform(page, "reserve"));
  ASSERT_STR_EQUAL(script_page_message(page), "Выберите номер и гостя.");
  script_page_close(page);
  ui_set_language(UI_PSEUDO_LANGUAGE);
  page = script_page_open("apps/lodge/share/reception", error, sizeof(error));
  ASSERT_NOT_NULL(page); ASSERT_NOT_NULL(script_page_create(page, NULL));
  ASSERT_STR_EQUAL(script_page_message(page), "");
  ASSERT_NOT_NULL(strstr(script_page_control(page, "summary")->title, "4"));
  script_page_close(page); ui_set_language("en");
  PASS();
}
int main(void) {
  TEST_START("Orion Lodge");
  test_env_init(); DB_CLASS(db_schema_xml);
  snprintf(hotel_path, sizeof(hotel_path), "/tmp/orion-lodge-%ld.xml", (long)getpid());
  hotel = create_database_with_schema("lodge", "db_schema_xml", &lodge_lodge_schema, hotel_path);
  register_database("lodge", hotel);
  test_hotel(); test_persistence_and_page();
  unregister_database("lodge"); destroy_database(hotel); unlink(hotel_path);
  test_env_shutdown(); TEST_END();
}
