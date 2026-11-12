#include "test_framework.h"
#include "test_env.h"
#include <orion/script/script_internal.h>
#include <orion/script/script.h>
#include <lauxlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <orion/script/application.h>
#include <orion/user/display_group.h>
#include <orion/commctl/form_loader.h>
#include "build/generated/apps/lodge/lodge.h"

static database_t *hotel;
static char hotel_path[128];
static bool hotel_script(script_application_t *app, const char *source) {
  lua_State *L = script_runtime_state(script_application_runtime(app));
  int base = lua_gettop(L);
  lua_getfield(L, LUA_REGISTRYINDEX, "orion.runtime.scopes");
  lua_getfield(L, -1, "apps/lodge/share");
  if (!lua_istable(L, -1) || luaL_loadstring(L, source) != LUA_OK) { lua_settop(L, base); return false; }
  lua_pushvalue(L, -2); lua_setupvalue(L, -2, 1);
  char message[1024]; bool refused;
  bool ok = script_call(L, 0, 0, message, sizeof(message), &refused);
  if (!ok) fprintf(stderr, "%s\n", message);
  lua_settop(L, base); return ok;
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
  ASSERT_TRUE(hotel_script(app, "assert(require('hotel').configure_hotel('DE','EUR'))"));
  db_payment_event_t *opening = (void *)send_db_message(hotel, dbFind, TABLE_PAYMENT_EVENTS, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(opening); ASSERT_STR_EQUAL(opening->kind, "opening_balance"); ASSERT_STR_EQUAL(opening->actor, "migration"); ASSERT_EQUAL(opening->amount_minor, 20990);
  script_page_t *page = script_application_main(app);
  ASSERT_NOT_NULL(script_page_control(page, "bookings"));
  ASSERT_NOT_NULL(strstr(script_page_control(page, "summary")->title, "3"));
  ASSERT_TRUE(script_page_perform(page, "confirmation"));
  char confirmation_path[160]; snprintf(confirmation_path, sizeof(confirmation_path), "%s/confirmation.html", directory);
  FILE *confirmation_file = fopen(confirmation_path, "rb"); ASSERT_NOT_NULL(confirmation_file); fclose(confirmation_file);
  ASSERT_TRUE(script_page_perform(page, "new_booking"));
  ASSERT_FALSE(script_page_perform(page, "reserve"));
  ASSERT_NOT_NULL(strstr(script_page_message(page), "room"));
  // Native writes run the same Lua validator as scripted writes.
  db_room_t invalid = {.capacity = 0, .rate = 100};
  snprintf(invalid.name, sizeof(invalid.name), "Invalid");
  ASSERT_FALSE(send_db_message(hotel, dbInsert, TABLE_ROOMS, &invalid));
  ASSERT_NOT_NULL(strstr(hotel->error, "Capacity"));
  invalid.capacity = 2; invalid.rate = 1.005;
  ASSERT_FALSE(send_db_message(hotel, dbInsert, TABLE_ROOMS, &invalid));
  ASSERT_NOT_NULL(strstr(hotel->error, "decimal"));
  db_room_t room = {.capacity = 2, .rate = 100};
  snprintf(room.name, sizeof(room.name), "202");
  snprintf(room.cleaning_state, sizeof(room.cleaning_state), "inspection_needed");
  db_room_t *inserted = (void *)send_db_message(hotel, dbInsert, TABLE_ROOMS, &room);
  ASSERT_NOT_NULL(inserted); room.id = inserted->id;
  ASSERT_TRUE(script_runtime_action(script_application_runtime(app), "orion.application", "open", "rooms", error, sizeof(error)));
  ASSERT_TRUE(script_runtime_action(script_application_runtime(app), "orion.application", "open", "housekeeping", error, sizeof(error)));
  send_message(script_page_control(page, "room"), cbSetCurrentValue, room.id, NULL);
  send_message(script_page_control(page, "guest"), cbSetCurrentValue, 1, NULL);
  send_message(script_page_control(page, "arrival"), edSetText, 0, "2026-05-01");
  send_message(script_page_control(page, "departure"), edSetText, 0, "2026-05-03");
  ASSERT_TRUE(script_page_perform(page, "room_rate"));
  ASSERT_TRUE(script_page_perform(page, "reserve"));
  db_reservation_t *created = (void *)send_db_message(hotel, dbFind, TABLE_RESERVATIONS, (void *)(intptr_t)4);
  ASSERT_NOT_NULL(created); ASSERT_EQUAL(created->room_id, room.id); ASSERT_EQUAL(created->rate, 100);
  db_reservation_history_t *history = (void *)send_db_message(hotel, dbFind, TABLE_RESERVATION_HISTORY, (void *)(intptr_t)4);
  ASSERT_NOT_NULL(history); ASSERT_EQUAL(history->reservation_id, 4); ASSERT_STR_EQUAL(history->operation, "insert");
  ASSERT_STR_EQUAL(history->actor, "local-owner");
  ASSERT_FALSE(send_db_message(hotel, dbDelete, TABLE_RESERVATION_HISTORY, (void *)(intptr_t)4));
  ASSERT_FALSE(send_db_message(hotel, dbDelete, TABLE_RESERVATIONS, (void *)(intptr_t)4));
  db_reservation_t fractional = *created; fractional.extras = 0.001;
  ASSERT_FALSE(send_db_message(hotel, dbUpdate, TABLE_RESERVATIONS, &fractional));
  ASSERT_NOT_NULL(strstr(hotel->error, "decimal"));
  db_reservation_t paid = *created; paid.paid = 25.50;
  ASSERT_TRUE(send_db_message(hotel, dbUpdate, TABLE_RESERVATIONS, &paid));
  db_payment_event_t *event = (void *)send_db_message(hotel, dbFind, TABLE_PAYMENT_EVENTS, (void *)(intptr_t)4);
  ASSERT_NOT_NULL(event); ASSERT_EQUAL(event->reservation_id, 4); ASSERT_EQUAL(event->amount_minor, 2550);
  db_payment_event_t changed = *event; changed.amount_minor = 1;
  ASSERT_FALSE(send_db_message(hotel, dbUpdate, TABLE_PAYMENT_EVENTS, &changed));
  ASSERT_FALSE(send_db_message(hotel, dbDelete, TABLE_PAYMENT_EVENTS, (void *)(intptr_t)4));
  ASSERT_TRUE(hotel_script(app, "local h=require('hotel'); assert(h.payment(4, 500, 'test-payment')); assert(h.payment(4, 500, 'test-payment')); assert(not h.payment(4, 501, 'test-payment'))"));
  event = (void *)send_db_message(hotel, dbFind, TABLE_PAYMENT_EVENTS, (void *)(intptr_t)5);
  ASSERT_NOT_NULL(event); ASSERT_EQUAL(event->amount_minor, 500); ASSERT_STR_EQUAL(event->request_key, "test-payment");
  ASSERT_NULL((void *)send_db_message(hotel, dbFind, TABLE_PAYMENT_EVENTS, (void *)(intptr_t)6));
  ASSERT_TRUE(hotel_script(app, "local h=require('hotel'); local r=h.reservations.model:find(4); assert(h.paid(r)==30.5 and h.balance(r)==169.5)"));
  ASSERT_TRUE(hotel_script(app, "local h=require('hotel'); assert(h.payment(4, -200, 'test-refund')); assert(h.payment(4, -200, 'test-refund'))"));
  event = (void *)send_db_message(hotel, dbFind, TABLE_PAYMENT_EVENTS, (void *)(intptr_t)6);
  ASSERT_NOT_NULL(event); ASSERT_EQUAL(event->amount_minor, -200); ASSERT_STR_EQUAL(event->kind, "refund");
  ASSERT_TRUE(send_db_message(hotel, dbSave, 0, NULL));
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
  app = script_application_open("apps/lodge/share", directory, error, sizeof(error));
  ASSERT_NOT_NULL(app); hotel = get_database_by_name("lodge");
  event = (void *)send_db_message(hotel, dbFind, TABLE_PAYMENT_EVENTS, (void *)(intptr_t)4);
  ASSERT_NOT_NULL(event); ASSERT_EQUAL(event->amount_minor, 2550);
  event = (void *)send_db_message(hotel, dbFind, TABLE_PAYMENT_EVENTS, (void *)(intptr_t)5);
  ASSERT_NOT_NULL(event); ASSERT_STR_EQUAL(event->request_key, "test-payment");
  ASSERT_TRUE(hotel_script(app, "assert(require('hotel').payment(4, 500, 'test-payment'))"));
  ASSERT_NOT_NULL((void *)send_db_message(hotel, dbFind, TABLE_PAYMENT_EVENTS, (void *)(intptr_t)6));
  ASSERT_NULL((void *)send_db_message(hotel, dbFind, TABLE_PAYMENT_EVENTS, (void *)(intptr_t)7));
  history = (void *)send_db_message(hotel, dbFind, TABLE_RESERVATION_HISTORY, (void *)(intptr_t)5);
  ASSERT_NOT_NULL(history); ASSERT_STR_EQUAL(history->operation, "update");
  created = (void *)send_db_message(hotel, dbFind, TABLE_RESERVATIONS, (void *)(intptr_t)4);
  db_reservation_t checking_in = *created; snprintf(checking_in.status, sizeof(checking_in.status), "checked_in");
  ASSERT_FALSE(send_db_message(hotel, dbUpdate, TABLE_RESERVATIONS, &checking_in));
  ASSERT_NOT_NULL(strstr(hotel->error, "clean"));
  db_room_t *room_row = (void *)send_db_message(hotel, dbFind, TABLE_ROOMS, (void *)(intptr_t)room.id);
  ASSERT_NOT_NULL(room_row); ASSERT_STR_EQUAL(room_row->cleaning_state, "inspection_needed");
  db_room_t clean = *room_row; snprintf(clean.cleaning_state, sizeof(clean.cleaning_state), "clean");
  ASSERT_TRUE(send_db_message(hotel, dbUpdate, TABLE_ROOMS, &clean));
  ASSERT_TRUE(send_db_message(hotel, dbUpdate, TABLE_RESERVATIONS, &checking_in));
  ASSERT_TRUE(hotel_script(app, "assert(require('hotel').payment(4, 17150, 'settle-room'))"));
  ASSERT_TRUE(hotel_script(app, "assert(require('hotel').transition(4, 'checked_out'))"));
  room_row = (void *)send_db_message(hotel, dbFind, TABLE_ROOMS, (void *)(intptr_t)room.id);
  ASSERT_STR_EQUAL(room_row->cleaning_state, "dirty");
  ASSERT_TRUE(send_db_message(hotel, dbSave, 0, NULL));
  script_application_close(app); hotel = NULL;
  app = script_application_open("apps/lodge/share", directory, error, sizeof(error)); ASSERT_NOT_NULL(app);
  hotel = get_database_by_name("lodge");
  room_row = (void *)send_db_message(hotel, dbFind, TABLE_ROOMS, (void *)(intptr_t)room.id);
  ASSERT_NOT_NULL(room_row); ASSERT_STR_EQUAL(room_row->cleaning_state, "dirty");
  script_application_close(app); hotel = NULL;
  unlink(confirmation_path); unlink(target); rmdir(directory);
  PASS();
}
static void test_refused_migration(void) {
  TEST("legacy fractional-cent balance refuses migration without changing the original");
  char directory[128], target[160], error[2048], contents[1024];
  snprintf(directory, sizeof(directory), "/tmp/orion-lodge-old-%ld", (long)getpid()); mkdir(directory, 0700);
  snprintf(target, sizeof(target), "%s/hotel.xml", directory);
  const char *xml = "<lodge><rooms><room id=\"1\" name=\"101\" capacity=\"2\" rate=\"10\"/></rooms>"
    "<guests><guest id=\"1\" name=\"Guest\"/></guests><reservations>"
    "<reservation id=\"1\" room_id=\"1\" guest_id=\"1\" arrival=\"2026-01-01\" departure=\"2026-01-02\" persons=\"1\" rate=\"10\" paid=\"1.005\" status=\"reserved\"/>"
    "</reservations></lodge>";
  FILE *file = fopen(target, "wb"); ASSERT_NOT_NULL(file); ASSERT_EQUAL(fputs(xml, file) >= 0, true); fclose(file);
  script_application_t *app = script_application_open("apps/lodge/share", directory, error, sizeof(error));
  ASSERT_NULL(app); ASSERT_NOT_NULL(strstr(error, "fractional cents"));
  file = fopen(target, "rb"); ASSERT_NOT_NULL(file);
  size_t size = fread(contents, 1, sizeof(contents) - 1, file); fclose(file); contents[size] = 0;
  ASSERT_STR_EQUAL(contents, xml);
  const char *future = "<lodge orion-version=\"5\"><reservations/></lodge>";
  file = fopen(target, "wb"); ASSERT_NOT_NULL(file); ASSERT_EQUAL(fputs(future, file) >= 0, true); fclose(file);
  app = script_application_open("apps/lodge/share", directory, error, sizeof(error));
  ASSERT_NULL(app);
  file = fopen(target, "rb"); ASSERT_NOT_NULL(file);
  size = fread(contents, 1, sizeof(contents) - 1, file); fclose(file); contents[size] = 0;
  ASSERT_STR_EQUAL(contents, future);
  snprintf(contents, sizeof(contents), "%s", xml);
  char *amount = strstr(contents, "1.005"); ASSERT_NOT_NULL(amount); memcpy(amount, "1.000", 5);
  file = fopen(target, "wb"); ASSERT_NOT_NULL(file); ASSERT_TRUE(fputs(contents, file) >= 0); fclose(file);
  app = script_application_open("apps/lodge/share", directory, error, sizeof(error));
  ASSERT_NOT_NULL(app);
  database_t *db = get_database_by_name("lodge");
  db_room_t *old_room = (void *)send_db_message(db, dbFind, TABLE_ROOMS, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(old_room); ASSERT_STR_EQUAL(old_room->cleaning_state, "inspection_needed");
  db_payment_event_t *opening = (void *)send_db_message(db, dbFind, TABLE_PAYMENT_EVENTS, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(opening); ASSERT_STR_EQUAL(opening->kind, "opening_balance"); ASSERT_EQUAL(opening->amount_minor, 100);
  script_application_close(app);
  app = script_application_open("apps/lodge/share", directory, error, sizeof(error)); ASSERT_NOT_NULL(app);
  db = get_database_by_name("lodge");
  ASSERT_NULL((void *)send_db_message(db, dbFind, TABLE_PAYMENT_EVENTS, (void *)(intptr_t)2));
  script_application_close(app);
  char backup[168]; snprintf(backup, sizeof(backup), "%s.bak", target);
  unlink(backup); unlink(target); rmdir(directory); PASS();
}
static void test_payment_form(void) {
  TEST("payment form retries one request and refunds a cancelled reservation");
  char directory[128], target[160], error[2048];
  snprintf(directory, sizeof(directory), "/tmp/orion-lodge-payment-%ld", (long)getpid()); mkdir(directory, 0700);
  snprintf(target, sizeof(target), "%s/hotel.xml", directory);
  FILE *in = fopen("apps/lodge/share/samples/demo.xml", "rb"), *out = fopen(target, "wb");
  ASSERT_NOT_NULL(in); ASSERT_NOT_NULL(out);
  int byte; while ((byte = fgetc(in)) != EOF) fputc(byte, out);
  fclose(in); fclose(out);
  script_application_t *app = script_application_open("apps/lodge/share", directory, error, sizeof(error));
  ASSERT_NOT_NULL(app); hotel = get_database_by_name("lodge");
  ASSERT_TRUE(hotel_script(app, "assert(require('hotel').configure_hotel('DE','EUR'))"));
  database_t *group = get_database_by_name("bookings"); ASSERT_NOT_NULL(group);
  ASSERT_TRUE(send_db_message(group, dbSelectRecord, 2, NULL));
  script_page_t *reception = script_application_main(app);
  ASSERT_TRUE(script_page_perform(reception, "edit"));
  ASSERT_TRUE(hotel_script(app, "require('hotel').pending_payment_id=2"));
  form_library_t *library = form_library_load("apps/lodge/share/application.orion"); ASSERT_NOT_NULL(library);
  script_page_t *page = script_page_open_library(script_application_runtime(app), library, "payment", "apps/lodge/share/payment.lua", error, sizeof(error));
  ASSERT_NOT_NULL(page); ASSERT_NOT_NULL(script_page_create(page, NULL));
  send_message(script_page_control(page, "amount"), edSetText, 0, "10.00");
  char original[sizeof(hotel->source_path)]; snprintf(original, sizeof(original), "%s", hotel->source_path);
  snprintf(hotel->source_path, sizeof(hotel->source_path), "/nonexistent-orion-lodge-directory/hotel.xml");
  ASSERT_FALSE(script_page_perform(page, "pay"));
  ASSERT_NULL((void *)send_db_message(hotel, dbFind, TABLE_PAYMENT_EVENTS, (void *)(intptr_t)4));
  snprintf(hotel->source_path, sizeof(hotel->source_path), "%s", original);
  send_message(script_page_control(page, "amount"), edSetText, 0, "11.00");
  ASSERT_FALSE(script_page_perform(page, "pay"));
  send_message(script_page_control(page, "amount"), edSetText, 0, "10.00");
  ASSERT_TRUE(script_page_perform(page, "pay"));
  ASSERT_FALSE(script_page_perform(page, "pay"));
  db_payment_event_t *event = (void *)send_db_message(hotel, dbFind, TABLE_PAYMENT_EVENTS, (void *)(intptr_t)4);
  ASSERT_NOT_NULL(event); ASSERT_EQUAL(event->amount_minor, 1000); ASSERT_NOT_NULL(strstr(event->request_key, "reception:"));
  ASSERT_TRUE(script_page_perform(reception, "reserve"));
  db_reservation_t *saved = (void *)send_db_message(hotel, dbFind, TABLE_RESERVATIONS, (void *)(intptr_t)2);
  ASSERT_NOT_NULL(saved); ASSERT_EQUAL(saved->paid, 95);
  script_page_close(page);
  ASSERT_TRUE(hotel_script(app, "assert(require('hotel').transition(2,'cancelled'))"));
  page = script_page_open_library(script_application_runtime(app), library, "payment", "apps/lodge/share/payment.lua", error, sizeof(error));
  ASSERT_NOT_NULL(page); ASSERT_NOT_NULL(script_page_create(page, NULL));
  send_message(script_page_control(page, "amount"), edSetText, 0, "2.50");
  ASSERT_TRUE(script_page_perform(page, "refund"));
  event = (void *)send_db_message(hotel, dbFind, TABLE_PAYMENT_EVENTS, (void *)(intptr_t)5);
  ASSERT_NOT_NULL(event); ASSERT_EQUAL(event->amount_minor, -250); ASSERT_STR_EQUAL(event->kind, "refund");
  script_page_close(page); form_library_free(library);
  script_application_close(app); hotel = NULL;
  app = script_application_open("apps/lodge/share", directory, error, sizeof(error)); ASSERT_NOT_NULL(app);
  hotel = get_database_by_name("lodge");
  db_reservation_t *row = (void *)send_db_message(hotel, dbFind, TABLE_RESERVATIONS, (void *)(intptr_t)2);
  ASSERT_NOT_NULL(row); ASSERT_STR_EQUAL(row->status, "cancelled"); ASSERT_EQUAL(row->paid, 92.5);
  ASSERT_NULL((void *)send_db_message(hotel, dbFind, TABLE_PAYMENT_EVENTS, (void *)(intptr_t)6));
  script_application_close(app); hotel = NULL;
  char backup[168], lock[168]; snprintf(backup, sizeof(backup), "%s.bak", target); snprintf(lock, sizeof(lock), "%s.lock", target);
  unlink(backup); unlink(lock); unlink(target); rmdir(directory); PASS();
}
static void test_nightly_prices(void) {
  TEST("nightly prices validate, journal, save and reject closed edits");
  char directory[128], target[160], error[2048];
  snprintf(directory, sizeof(directory), "/tmp/orion-lodge-night-%ld", (long)getpid()); mkdir(directory, 0700);
  snprintf(target, sizeof(target), "%s/hotel.xml", directory);
  FILE *in = fopen("apps/lodge/share/samples/demo.xml", "rb"), *out = fopen(target, "wb");
  ASSERT_NOT_NULL(in); ASSERT_NOT_NULL(out);
  int byte; while ((byte = fgetc(in)) != EOF) fputc(byte, out);
  fclose(in); fclose(out);
  script_application_t *app = script_application_open("apps/lodge/share", directory, error, sizeof(error));
  ASSERT_NOT_NULL(app); hotel = get_database_by_name("lodge");
  ASSERT_TRUE(hotel_script(app, "assert(require('hotel').configure_hotel('DE','EUR'))"));
  ASSERT_TRUE(hotel_script(app, "require('hotel').pending_night_id=2"));
  form_library_t *library = form_library_load("apps/lodge/share/application.orion"); ASSERT_NOT_NULL(library);
  script_page_t *page = script_page_open_library(script_application_runtime(app), library, "night_prices", "apps/lodge/share/night_prices.lua", error, sizeof(error));
  ASSERT_NOT_NULL(page); ASSERT_NOT_NULL(script_page_create(page, NULL));
  send_message(script_page_control(page, "night"), edSetText, 0, "2026-10-29");
  send_message(script_page_control(page, "amount"), edSetText, 0, "120.25");
  ASSERT_TRUE(script_page_perform(page, "set_price"));
  db_night_rate_t *night = (void *)send_db_message(hotel, dbFind, TABLE_NIGHT_RATES, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(night); ASSERT_EQUAL(night->reservation_id, 2); ASSERT_EQUAL(night->amount_minor, 12025);
  ASSERT_TRUE(hotel_script(app, "local h=require('hotel'); local r=h.reservations.model:find(2); assert(h.total(r)==290.25 and h.balance(r)==205.25)"));
  db_night_rate_t duplicate = *night; duplicate.id = 0;
  ASSERT_FALSE(send_db_message(hotel, dbInsert, TABLE_NIGHT_RATES, &duplicate));
  ASSERT_NOT_NULL(strstr(hotel->error, "already exists"));
  db_reservation_t *reservation = (void *)send_db_message(hotel, dbFind, TABLE_RESERVATIONS, (void *)(intptr_t)2);
  db_reservation_t shortened = *reservation; snprintf(shortened.arrival, sizeof(shortened.arrival), "2026-10-30");
  ASSERT_FALSE(send_db_message(hotel, dbUpdate, TABLE_RESERVATIONS, &shortened));
  ASSERT_NOT_NULL(strstr(hotel->error, "nightly prices"));
  ASSERT_TRUE(hotel_script(app, "assert(require('hotel').transition(2,'cancelled'))"));
  ASSERT_FALSE(send_db_message(hotel, dbDelete, TABLE_NIGHT_RATES, (void *)(intptr_t)1));
  ASSERT_TRUE(send_db_message(hotel, dbSave, 0, NULL));
  script_page_close(page); form_library_free(library); script_application_close(app); hotel = NULL;
  app = script_application_open("apps/lodge/share", directory, error, sizeof(error)); ASSERT_NOT_NULL(app);
  hotel = get_database_by_name("lodge");
  night = (void *)send_db_message(hotel, dbFind, TABLE_NIGHT_RATES, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(night); ASSERT_EQUAL(night->amount_minor, 12025);
  ASSERT_TRUE(hotel_script(app, "local h=require('hotel'); assert(h.total(h.reservations.model:find(2))==290.25)"));
  script_application_close(app); hotel = NULL;
  char backup[168], lock[168]; snprintf(backup, sizeof(backup), "%s.bak", target); snprintf(lock, sizeof(lock), "%s.lock", target);
  unlink(backup); unlink(lock); unlink(target); rmdir(directory); PASS();
}
static void test_service(void) {
  TEST("service enforces roles, redacts housekeeping and runs booking workflows");
  char directory[128], target[160], error[2048];
  snprintf(directory, sizeof(directory), "/tmp/orion-lodge-service-%ld", (long)getpid()); mkdir(directory, 0700);
  snprintf(target, sizeof(target), "%s/hotel.xml", directory);
  script_application_t *app = script_application_open("apps/lodge/share", directory, error, sizeof(error)); ASSERT_NOT_NULL(app);
  hotel = get_database_by_name("lodge");
  char *response = script_runtime_request(script_application_runtime(app), "service",
    "{\"command\":\"close_day\",\"args\":{},\"role\":\"admin\",\"actor\":\"test\"}", error, sizeof(error));
  ASSERT_NOT_NULL(response); ASSERT_NOT_NULL(strstr(response, "\"ok\":false")); free(response);
  response = script_runtime_request(script_application_runtime(app), "service",
    "{\"command\":\"configure_hotel\",\"args\":{\"country\":\"DE\",\"currency\":\"EUR\"},\"role\":\"admin\",\"actor\":\"test\"}", error, sizeof(error));
  ASSERT_NOT_NULL(response); ASSERT_NOT_NULL(strstr(response, "\"ok\":true")); free(response);
  ASSERT_TRUE(hotel_script(app,
    "local j=require('orion.json'); local s=require('service'); "
    "local cal=require('orion.plugins').use('calendar',1); local today=require('orion').today(); "
    "local old=cal.add(today,-10); local nextday=cal.add(today,-9); local later=cal.add(today,-8); local later_end=cal.add(today,-7); "
    "local function call(c,a,r) return j.decode(s.handle(j.encode({command=c,args=a or {},role=r or 'reception',actor='test'}))) end; "
    "assert(not call('configure_hotel',{country='DE',currency='EUR'},'reception').ok); "
    "assert(not call('configure_hotel',{country='DE',currency='RUB'},'admin').ok); "
    "assert(call('configure_hotel',{country='DE',currency='EUR'},'admin').ok); "
    "assert(not call('configure_hotel',{country='RU',currency='RUB'},'admin').ok); "
    "assert(not call('save_guest',{name='No'},'housekeeping').ok); "
    "local room=assert(call('save_room',{name='R',capacity=2,rate=100}).result).id; "
    "assert(call('save_room',{id=room,name='R edited',category='Suite',capacity=3,rate=150}).ok); "
    "assert(not call('save_room',{id=room,capacity=0}).ok); "
    "assert(not call('save_room',{id=room,name=''},'reception').ok); "
    "local saved_room=call('snapshot').result.rooms[1]; assert(saved_room.name=='R edited' and saved_room.category=='Suite' and saved_room.capacity==3 and saved_room.rate==150); "
    "local guest=assert(call('save_guest',{name='Guest'}).result).id; "
    "assert(call('save_guest',{id=guest,name='Updated guest',phone='+49 123'}).ok); "
    "assert(not call('save_guest',{id=guest,name=''},'reception').ok); "
    "local saved_guest=call('snapshot').result.guests[1]; assert(saved_guest.id==guest and saved_guest.name=='Updated guest' and saved_guest.phone=='+49 123'); "
    "local hs=call('snapshot',{},'housekeeping'); assert(hs.ok and hs.result.guests==nil and hs.result.payment_events==nil and #hs.result.rooms==1); "
    "assert(call('set_room_cleaning',{id=room,state='clean'},'housekeeping').ok); "
    "assert(not call('set_room_cleaning',{id=room,state='dirty'},'reception').ok); "
    "assert(call('set_room_cleaning',{id=room,state='dirty'},'housekeeping').ok); "
    "assert(call('set_room_cleaning',{id=room,state='inspection_needed'},'housekeeping').ok); "
    "assert(not call('set_room_cleaning',{id=room,state='invalid'},'housekeeping').ok); "
    "assert(call('snapshot',{},'housekeeping').result.rooms[1].cleaning_state=='inspection_needed'); "
    "assert(call('set_room_cleaning',{id=room,state='clean'},'housekeeping').ok); "
    "local r=assert(call('reserve',{room_id=room,guest_id=guest,arrival=old,departure=nextday,persons=1,rate=100,extras=0,notes=''}).result).id; "
    "assert(call('reserve',{id=r,room_id=room,guest_id=guest,arrival=old,departure=nextday,persons=1,rate=125,extras=0,notes='Late arrival'}).ok); "
    "local edited=call('snapshot').result.reservations[1]; assert(edited.id==r and edited.rate==125 and edited.notes=='Late arrival'); "
    "assert(call('set_night',{id=r,night=old,amount_minor=15000}).ok); "
    "assert(call('check_in',{id=r}).ok); "
    "local room2=assert(call('save_room',{name='R2',capacity=2,rate=100}).result).id; "
    "local h=require('hotel'); assert(not h.save(r,{room_id=room2})); "
    "assert(h.rooms:save(room2,{cleaning_state='clean'})); assert(h.save(r,{room_id=room2})); "
    "assert(h.rooms.model:find(room).cleaning_state=='dirty'); assert(h.rooms:save(room,{cleaning_state='clean'})); "
    "assert(h.save(r,{room_id=room})); assert(h.rooms.model:find(room2).cleaning_state=='dirty'); "
    "assert(call('payment',{id=r,amount_minor=15000,request_key='server-test'}).ok); "
    "assert(call('payment',{id=r,amount_minor=15000,request_key='server-test'}).ok); "
    "assert(call('check_out',{id=r}).ok); local snap=call('snapshot'); "
    "assert(snap.ok and #snap.result.payment_events==1 and snap.result.reservations[1].balance==0 and snap.result.rooms[1].cleaning_state=='dirty'); "
    "assert(not call('reserve',{id=r,notes='Changed after checkout'}).ok); "
    "assert(not call('reset_night',{id=r,night=old}).ok); "
    "assert(not call('save_room',{id=room2,name='R edited'}).ok); "
    "assert(call('snapshot').result.rooms[2].name=='R2'); "
    "local function stay(id,a,d) return {room_id=id,guest_id=guest,arrival=a,departure=d,persons=1,rate=100,extras=0,notes=''} end; "
    "assert(not call('group_reserve',{name='Group',stays={stay(room2,old,nextday),stay(room,old,nextday)}}).ok); "
    "local before=call('snapshot').result; assert(#before.reservations==1 and #before.reservation_groups==0); "
    "local g=call('group_reserve',{name='Group',stays={stay(room,later,later_end),stay(room2,later,later_end)}}); "
    "assert(g.ok and #g.result.reservations==2); local after=call('snapshot').result; assert(#after.reservations==3 and #after.reservation_groups==1); "
    "assert(not call('reserve',{id=g.result.reservations[2],room_id=room}).ok); "
    "assert(call('snapshot').result.reservations[3].room_id==room2); "
    "assert(not call('close_day',{},'reception').ok); assert(not call('close_day',{},'admin').ok); "
    "assert(call('cancel',{id=g.result.reservations[1]}).ok and call('cancel',{id=g.result.reservations[2]}).ok); "
    "assert(call('day_preview').result.net_minor==15000); assert(call('close_day',{},'admin').ok); "
    "assert(not call('close_day',{},'admin').ok); assert(not call('reserve',stay(room,'2027-08-01','2027-08-02')).ok); "
    "assert(not j.decode(s.handle('not json')).ok)"));
  db_day_close_t *close = (void *)send_db_message(hotel, dbFind, TABLE_DAY_CLOSES, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(close); ASSERT_STR_EQUAL(close->net_minor, "15000"); ASSERT_STR_EQUAL(close->actor, "local-owner");
  db_day_close_t changed = *close; snprintf(changed.net_minor, sizeof(changed.net_minor), "0");
  ASSERT_FALSE(send_db_message(hotel, dbUpdate, TABLE_DAY_CLOSES, &changed));
  ASSERT_FALSE(send_db_message(hotel, dbDelete, TABLE_DAY_CLOSES, (void *)(intptr_t)1));
  script_application_close(app); hotel = NULL;
  app = script_application_open("apps/lodge/share", directory, error, sizeof(error)); ASSERT_NOT_NULL(app);
  hotel = get_database_by_name("lodge");
  close = (void *)send_db_message(hotel, dbFind, TABLE_DAY_CLOSES, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(close); ASSERT_STR_EQUAL(close->net_minor, "15000");
  ASSERT_TRUE(hotel_script(app, "local h=require('hotel'); assert(h.day_report().closed); assert(not h.close_today())"));
  script_application_close(app); hotel = NULL;
  char backup[168], lock[168]; snprintf(backup, sizeof(backup), "%s.bak", target); snprintf(lock, sizeof(lock), "%s.lock", target);
  unlink(backup); unlink(lock); unlink(target); rmdir(directory); PASS();
}
static void test_currency_setup(void) {
  TEST("Russia/RUB setup is explicit, immutable and survives reopening");
  char directory[128], target[160], error[2048];
  snprintf(directory, sizeof(directory), "/tmp/orion-lodge-currency-%ld", (long)getpid()); mkdir(directory, 0700);
  snprintf(target, sizeof(target), "%s/hotel.xml", directory);
  script_application_t *app = script_application_open("apps/lodge/share", directory, error, sizeof(error)); ASSERT_NOT_NULL(app);
  hotel = get_database_by_name("lodge");
  ASSERT_TRUE(hotel_script(app, "local h=require('hotel'); assert(h.configure_hotel('RU','RUB')); assert(not h.configure_hotel('DE','EUR'))"));
  db_hotel_setting_t *setting = (void *)send_db_message(hotel, dbFind, TABLE_HOTEL_SETTINGS, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(setting); ASSERT_STR_EQUAL(setting->country, "RU"); ASSERT_STR_EQUAL(setting->currency, "RUB");
  db_hotel_setting_t changed = *setting; snprintf(changed.currency, sizeof(changed.currency), "EUR");
  ASSERT_FALSE(send_db_message(hotel, dbUpdate, TABLE_HOTEL_SETTINGS, &changed));
  ASSERT_FALSE(send_db_message(hotel, dbDelete, TABLE_HOTEL_SETTINGS, (void *)(intptr_t)1));
  script_application_close(app); hotel = NULL;
  app = script_application_open("apps/lodge/share", directory, error, sizeof(error)); ASSERT_NOT_NULL(app);
  hotel = get_database_by_name("lodge");
  setting = (void *)send_db_message(hotel, dbFind, TABLE_HOTEL_SETTINGS, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(setting); ASSERT_STR_EQUAL(setting->currency, "RUB");
  script_application_close(app); hotel = NULL;
  char backup[168], lock[168]; snprintf(backup, sizeof(backup), "%s.bak", target); snprintf(lock, sizeof(lock), "%s.lock", target);
  unlink(backup); unlink(lock); unlink(target); rmdir(directory); PASS();
}
static void test_group_form(void) {
  TEST("group form creates both room reservations together");
  char directory[128], target[160], error[2048];
  snprintf(directory, sizeof(directory), "/tmp/orion-lodge-group-%ld", (long)getpid()); mkdir(directory, 0700);
  snprintf(target, sizeof(target), "%s/hotel.xml", directory);
  FILE *in = fopen("apps/lodge/share/samples/demo.xml", "rb"), *out = fopen(target, "wb");
  ASSERT_NOT_NULL(in); ASSERT_NOT_NULL(out);
  int byte; while ((byte = fgetc(in)) != EOF) fputc(byte, out);
  fclose(in); fclose(out);
  script_application_t *app = script_application_open("apps/lodge/share", directory, error, sizeof(error)); ASSERT_NOT_NULL(app);
  hotel = get_database_by_name("lodge");
  ASSERT_TRUE(hotel_script(app, "assert(require('hotel').configure_hotel('DE','EUR'))"));
  form_library_t *library = form_library_load("apps/lodge/share/application.orion"); ASSERT_NOT_NULL(library);
  script_page_t *page = script_page_open_library(script_application_runtime(app), library, "group_reservation", "apps/lodge/share/group_reservation.lua", error, sizeof(error));
  ASSERT_NOT_NULL(page); ASSERT_NOT_NULL(script_page_create(page, NULL));
  send_message(script_page_control(page, "name"), edSetText, 0, "Conference");
  send_message(script_page_control(page, "guest"), cbSetCurrentValue, 1, NULL);
  send_message(script_page_control(page, "arrival"), edSetText, 0, "2035-01-01");
  send_message(script_page_control(page, "departure"), edSetText, 0, "2035-01-03");
  send_message(script_page_control(page, "room1"), cbSetCurrentValue, 1, NULL);
  send_message(script_page_control(page, "room2"), cbSetCurrentValue, 2, NULL);
  ASSERT_TRUE(script_page_perform(page, "create_group"));
  ASSERT_FALSE(script_page_perform(page, "create_group"));
  db_reservation_group_t *group = (void *)send_db_message(hotel, dbFind, TABLE_RESERVATION_GROUPS, (void *)(intptr_t)1);
  db_reservation_t *first = (void *)send_db_message(hotel, dbFind, TABLE_RESERVATIONS, (void *)(intptr_t)4);
  db_reservation_t *second = (void *)send_db_message(hotel, dbFind, TABLE_RESERVATIONS, (void *)(intptr_t)5);
  ASSERT_NOT_NULL(group); ASSERT_NOT_NULL(first); ASSERT_NOT_NULL(second);
  ASSERT_EQUAL(first->group_id, group->id); ASSERT_EQUAL(second->group_id, group->id);
  window_t *booking_table = script_page_control(script_application_main(app), "bookings"); ASSERT_NOT_NULL(booking_table);
  send_message(booking_table, tvRefresh, 0, NULL);
  send_message(booking_table, RVM_SETSELECTION, 3, NULL);
  ASSERT_TRUE(script_page_perform(script_application_main(app), "group_statement"));
  char statement_path[160], html[4096]; snprintf(statement_path, sizeof(statement_path), "%s/group-statement.html", directory);
  FILE *statement = fopen(statement_path, "rb"); ASSERT_NOT_NULL(statement);
  size_t html_size = fread(html, 1, sizeof(html) - 1, statement); fclose(statement); html[html_size] = 0;
  ASSERT_NOT_NULL(strstr(html, "Conference")); ASSERT_NOT_NULL(strstr(html, "EUR"));
  ASSERT_TRUE(hotel_script(app,
    "local render=require('remote.group_statement'); local rows={"
    "{id=2,room='B',guest='Guest',arrival='2035-01-01',departure='2035-01-02',status='cancelled',total=0.20,paid=0.30,balance=-0.10},"
    "{id=1,room='A',guest='Guest',arrival='2035-01-01',departure='2035-01-02',status='reserved',total=0.10,paid=0,balance=0.10}}; "
    "local html=render('Team <A>',rows,'EUR'); assert(html:find('Team &lt;A&gt;',1,true) and html:find('Cancelled',1,true)); "
    "assert(html:find('Total 0.30',1,true) and html:find('Paid 0.30',1,true) and html:find('Balance 0.00',1,true)); "
    "assert(html:find('<td>A</td>',1,true)<html:find('<td>B</td>',1,true))"));
  script_page_close(page); form_library_free(library); script_application_close(app); hotel = NULL;
  app = script_application_open("apps/lodge/share", directory, error, sizeof(error)); ASSERT_NOT_NULL(app);
  hotel = get_database_by_name("lodge");
  second = (void *)send_db_message(hotel, dbFind, TABLE_RESERVATIONS, (void *)(intptr_t)5);
  ASSERT_NOT_NULL(second); ASSERT_EQUAL(second->group_id, 1);
  script_application_close(app); hotel = NULL;
  char backup[168], lock[168]; snprintf(backup, sizeof(backup), "%s.bak", target); snprintf(lock, sizeof(lock), "%s.lock", target);
  unlink(statement_path); unlink(backup); unlink(lock); unlink(target); rmdir(directory); PASS();
}
int main(void) {
  TEST_START("Orion Lodge");
  test_env_init(); DB_CLASS(db_schema_xml);
  snprintf(hotel_path, sizeof(hotel_path), "/tmp/orion-lodge-%ld.xml", (long)getpid());
  hotel = create_database_with_schema("lodge", "db_schema_xml", &lodge_lodge_schema, hotel_path);
  register_database("lodge", hotel);
  test_hotel(); test_persistence_and_page(); test_refused_migration(); test_payment_form(); test_nightly_prices(); test_service(); test_group_form(); test_currency_setup();
  if (hotel) { unregister_database("lodge"); destroy_database(hotel); } unlink(hotel_path);
  test_env_shutdown(); TEST_END();
}
