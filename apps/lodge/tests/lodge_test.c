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
  test_mkdir(directory, 0700);
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
  script_page_t *page = script_application_page(app, "reception");
  ASSERT_NOT_NULL(script_page_control(page, "bookings"));
  ASSERT_NOT_NULL(strstr(script_page_control(page, "summary")->title, "3"));
  ASSERT_TRUE(script_page_perform(page, "confirmation"));
  char confirmation_path[160]; snprintf(confirmation_path, sizeof(confirmation_path), "%s/confirmation.html", directory);
  FILE *confirmation_file = fopen(confirmation_path, "rb"); ASSERT_NOT_NULL(confirmation_file);
  char confirmation_html[4096]; size_t confirmation_size = fread(confirmation_html, 1, sizeof(confirmation_html) - 1, confirmation_file);
  fclose(confirmation_file); confirmation_html[confirmation_size] = 0;
  ASSERT_NOT_NULL(strstr(confirmation_html, "EUR")); ASSERT_NOT_NULL(strstr(confirmation_html, "Checked out"));
  ASSERT_TRUE(hotel_script(app,
    "local render=require('remote.confirmation'); "
    "for _,currency in ipairs({'EUR','RUB'}) do "
    "local html=render({guest='<Guest>',room='101',arrival='2035-01-01',departure='2035-01-02',status='cancelled',currency=currency,total=100,paid=0,balance=100}); "
    "assert(html:find(currency,1,true) and html:find('Cancelled',1,true) and html:find('&lt;Guest&gt;',1,true)); end"));
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
  ASSERT_TRUE(script_runtime_action(script_application_runtime(app), "orion.application", "open", "guests", error, sizeof(error)));
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
  ASSERT_FALSE(send_db_message(hotel, dbUpdate, TABLE_RESERVATIONS, &paid));
  ASSERT_NOT_NULL(strstr(hotel->error, "payment action"));
  paid.id = 0; snprintf(paid.arrival, sizeof(paid.arrival), "2026-08-01"); snprintf(paid.departure, sizeof(paid.departure), "2026-08-02");
  ASSERT_FALSE(send_db_message(hotel, dbInsert, TABLE_RESERVATIONS, &paid));
  ASSERT_NULL((void *)send_db_message(hotel, dbFind, TABLE_PAYMENT_EVENTS, (void *)(intptr_t)4));
  ASSERT_TRUE(hotel_script(app, "assert(require('hotel').payment(4, 2550, 'initial-payment'))"));
  db_payment_event_t *event = (void *)send_db_message(hotel, dbFind, TABLE_PAYMENT_EVENTS, (void *)(intptr_t)4);
  ASSERT_NOT_NULL(event); ASSERT_EQUAL(event->reservation_id, 4); ASSERT_EQUAL(event->amount_minor, 2550); ASSERT_STR_EQUAL(event->request_key, "initial-payment");
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
  char occupancy[80]; db_get_field_text(hotel, TABLE_ROOMS, room_row, "occupancy_label", occupancy, sizeof(occupancy));
  ASSERT_STR_EQUAL(occupancy, "Guest checked in");
  script_page_t *housekeeping = script_application_page(app, "rooms");
  ASSERT_NOT_NULL(housekeeping); ASSERT_NOT_NULL(script_page_control(housekeeping, "rooms")); ASSERT_NOT_NULL(script_page_control(housekeeping, "blocks"));
  ASSERT_TRUE(hotel_script(app, "assert(require('hotel').payment(4, 17150, 'settle-room'))"));
  ASSERT_TRUE(hotel_script(app, "assert(require('hotel').transition(4, 'checked_out'))"));
  room_row = (void *)send_db_message(hotel, dbFind, TABLE_ROOMS, (void *)(intptr_t)room.id);
  ASSERT_STR_EQUAL(room_row->cleaning_state, "dirty");
  db_get_field_text(hotel, TABLE_ROOMS, room_row, "occupancy_label", occupancy, sizeof(occupancy)); ASSERT_STR_EQUAL(occupancy, "No guest checked in");
  ASSERT_TRUE(script_page_perform(housekeeping, "refresh"));
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
  snprintf(directory, sizeof(directory), "/tmp/orion-lodge-old-%ld", (long)getpid()); test_mkdir(directory, 0700);
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
  const char *future = "<lodge orion-version=\"9\"><reservations/></lodge>";
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
  const char *v4 = "<lodge orion-version=\"4\"><day_closes><day_close id=\"1\" day=\"2026-01-01\" actor=\"old\" net_minor=\"100\" outstanding_minor=\"0\" reservations=\"1\"/></day_closes></lodge>";
  file = fopen(target, "wb"); ASSERT_NOT_NULL(file); ASSERT_TRUE(fputs(v4, file) >= 0); fclose(file);
  app = script_application_open("apps/lodge/share", directory, error, sizeof(error)); ASSERT_NOT_NULL(app);
  db = get_database_by_name("lodge");
  db_day_close_t *old_close = (void *)send_db_message(db, dbFind, TABLE_DAY_CLOSES, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(old_close); ASSERT_EQUAL(old_close->cancelled_paid_count, -1); ASSERT_STR_EQUAL(old_close->cancelled_paid_minor, "");
  ASSERT_TRUE(hotel_script(app, "local h=require('hotel'); local d=require('remote.close_display'); local r=h.day_closes.model:find(1); assert(d(r,'cancelled_count_display')=='Unknown' and d(r,'cancelled_paid_display')=='Unknown')"));
  script_application_close(app);
  app = script_application_open("apps/lodge/share", directory, error, sizeof(error)); ASSERT_NOT_NULL(app);
  db = get_database_by_name("lodge"); old_close = (void *)send_db_message(db, dbFind, TABLE_DAY_CLOSES, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(old_close); ASSERT_EQUAL(old_close->cancelled_paid_count, -1); ASSERT_STR_EQUAL(old_close->net_minor, "100");
  script_application_close(app);
  const char *v5 = "<lodge orion-version=\"5\"><reservation_groups><reservation_group id=\"1\" name=\"Existing\"/></reservation_groups></lodge>";
  file = fopen(target, "wb"); ASSERT_NOT_NULL(file); ASSERT_TRUE(fputs(v5, file) >= 0); fclose(file);
  app = script_application_open("apps/lodge/share", directory, error, sizeof(error)); ASSERT_NOT_NULL(app);
  db = get_database_by_name("lodge");
  ASSERT_NOT_NULL((void *)send_db_message(db, dbFind, TABLE_RESERVATION_GROUPS, (void *)(intptr_t)1));
  ASSERT_NULL((void *)send_db_message(db, dbFind, TABLE_GROUP_SETTLEMENTS, (void *)(intptr_t)1));
  ASSERT_TRUE(send_db_message(db, dbSave, 0, NULL)); script_application_close(app);
  app = script_application_open("apps/lodge/share", directory, error, sizeof(error)); ASSERT_NOT_NULL(app);
  db = get_database_by_name("lodge");
  ASSERT_NOT_NULL((void *)send_db_message(db, dbFind, TABLE_RESERVATION_GROUPS, (void *)(intptr_t)1));
  ASSERT_NULL((void *)send_db_message(db, dbFind, TABLE_GROUP_SETTLEMENTS, (void *)(intptr_t)1));
  script_application_close(app);
  char backup[168]; snprintf(backup, sizeof(backup), "%s.bak", target);
  unlink(backup); unlink(target); rmdir(directory); PASS();
}
static void test_payment_form(void) {
  TEST("payment form retries one request and refunds a cancelled reservation");
  char directory[128], target[160], error[2048];
  snprintf(directory, sizeof(directory), "/tmp/orion-lodge-payment-%ld", (long)getpid()); test_mkdir(directory, 0700);
  snprintf(target, sizeof(target), "%s/hotel.xml", directory);
  FILE *in = fopen("apps/lodge/tests/fixtures/legacy-hotel.xml", "rb"), *out = fopen(target, "wb");
  ASSERT_NOT_NULL(in); ASSERT_NOT_NULL(out);
  int byte; while ((byte = fgetc(in)) != EOF) fputc(byte, out);
  fclose(in); fclose(out);
  script_application_t *app = script_application_open("apps/lodge/share", directory, error, sizeof(error));
  ASSERT_NOT_NULL(app); hotel = get_database_by_name("lodge");
  ASSERT_TRUE(hotel_script(app, "assert(require('hotel').configure_hotel('DE','EUR'))"));
  database_t *group = get_database_by_name("bookings"); ASSERT_NOT_NULL(group);
  ASSERT_TRUE(send_db_message(group, dbSelectRecord, 2, NULL));
  script_page_t *reception = script_application_page(app, "reception");
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
  ASSERT_TRUE(hotel_script(app, "local p=require('reception'); local h=require('hotel'); p:init(); local v={bookings={id=2}}; "
    "p:preview_cancel(v); assert(p.cancel_notice:find('95.00',1,true)); "
    "assert(h.payment(2,100,'changed-after-preview')); assert(not pcall(function() p:confirm_cancel(v) end)); "
    "assert(h.payment(2,-100,'restore-after-preview')); p:preview_cancel(v); p:confirm_cancel(v); "
    "assert(h.reservations.model:find(2).status=='cancelled')"));
  page = script_page_open_library(script_application_runtime(app), library, "payment", "apps/lodge/share/payment.lua", error, sizeof(error));
  ASSERT_NOT_NULL(page); ASSERT_NOT_NULL(script_page_create(page, NULL));
  send_message(script_page_control(page, "amount"), edSetText, 0, "2.50");
  ASSERT_TRUE(script_page_perform(page, "refund"));
  event = (void *)send_db_message(hotel, dbFind, TABLE_PAYMENT_EVENTS, (void *)(intptr_t)7);
  ASSERT_NOT_NULL(event); ASSERT_EQUAL(event->amount_minor, -250); ASSERT_STR_EQUAL(event->kind, "refund");
  script_page_close(page); form_library_free(library);
  script_application_close(app); hotel = NULL;
  app = script_application_open("apps/lodge/share", directory, error, sizeof(error)); ASSERT_NOT_NULL(app);
  hotel = get_database_by_name("lodge");
  db_reservation_t *row = (void *)send_db_message(hotel, dbFind, TABLE_RESERVATIONS, (void *)(intptr_t)2);
  ASSERT_NOT_NULL(row); ASSERT_STR_EQUAL(row->status, "cancelled"); ASSERT_EQUAL(row->paid, 92.5);
  ASSERT_NULL((void *)send_db_message(hotel, dbFind, TABLE_PAYMENT_EVENTS, (void *)(intptr_t)8));
  script_application_close(app); hotel = NULL;
  char backup[168], lock[168]; snprintf(backup, sizeof(backup), "%s.bak", target); snprintf(lock, sizeof(lock), "%s.lock", target);
  unlink(backup); unlink(lock); unlink(target); rmdir(directory); PASS();
}
static void test_nightly_prices(void) {
  TEST("nightly prices validate, journal, save and reject closed edits");
  char directory[128], target[160], error[2048];
  snprintf(directory, sizeof(directory), "/tmp/orion-lodge-night-%ld", (long)getpid()); test_mkdir(directory, 0700);
  snprintf(target, sizeof(target), "%s/hotel.xml", directory);
  FILE *in = fopen("apps/lodge/tests/fixtures/legacy-hotel.xml", "rb"), *out = fopen(target, "wb");
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
  snprintf(directory, sizeof(directory), "/tmp/orion-lodge-service-%ld", (long)getpid()); test_mkdir(directory, 0700);
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
    "assert(call('snapshot').result.today==today and call('snapshot',{},'housekeeping').result.today==today and call('day_preview').result.day==today); "
    "assert(cal.add('2024-02-28',1)=='2024-02-29' and cal.add('2024-02-29',1)=='2024-03-01'); "
    "assert(cal.add('2024-03-31',1)=='2024-04-01' and cal.add('2024-12-31',1)=='2025-01-01'); "
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
    "local hs=call('snapshot',{},'housekeeping'); assert(hs.ok and hs.result.guests==nil and hs.result.reservations==nil and hs.result.payment_events==nil and #hs.result.rooms==1 and hs.result.rooms[1].occupied==false); "
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
    "assert(call('snapshot',{},'housekeeping').result.rooms[1].occupied==true); "
    "local room2=assert(call('save_room',{name='R2',capacity=2,rate=100}).result).id; "
    "local h=require('hotel'); assert(not h.save(r,{room_id=room2})); "
    "assert(h.rooms:save(room2,{cleaning_state='clean'})); assert(h.save(r,{room_id=room2})); "
    "local moved=call('snapshot',{},'housekeeping').result.rooms; assert(not moved[1].occupied and moved[2].occupied); "
    "assert(h.rooms.model:find(room).cleaning_state=='dirty'); assert(h.rooms:save(room,{cleaning_state='clean'})); "
    "assert(h.save(r,{room_id=room})); assert(h.rooms.model:find(room2).cleaning_state=='dirty'); "
    "assert(call('payment',{id=r,amount_minor=15000,request_key='server-test'}).ok); "
    "assert(call('payment',{id=r,amount_minor=15000,request_key='server-test'}).ok); "
    "assert(call('check_out',{id=r}).ok); local snap=call('snapshot'); "
    "assert(call('snapshot',{},'housekeeping').result.rooms[1].occupied==false); "
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
    "local h=require('hotel'); local unchanged=h.day_report(); assert(call('reserve',{id=g.result.reservations[1],notes='Updated'}).ok); "
    "assert(not h.close_today(unchanged)); unchanged.closed=nil; assert(not call('close_day',unchanged,'admin').ok); "
    "assert(call('cancel',{id=g.result.reservations[1]}).ok and call('cancel',{id=g.result.reservations[2]}).ok); "
    "assert(call('payment',{id=g.result.reservations[1],amount_minor=1000,request_key='cancel-deposit'}).ok); "
    "assert(call('payment',{id=g.result.reservations[1],amount_minor=-500,request_key='cancel-refund'}).ok); "
    "local preview=call('day_preview').result; assert(preview.net_minor==15500 and preview.cancelled_paid_count==1 and preview.cancelled_paid_minor==500); "
    "local expected={day=preview.day,net_minor=preview.net_minor,outstanding_minor=preview.outstanding_minor,reservations=preview.reservations,pending=preview.pending,cancelled_paid_minor=preview.cancelled_paid_minor,cancelled_paid_count=preview.cancelled_paid_count,history_revision=preview.history_revision}; "
    "local wrong={}; for k,v in pairs(expected) do wrong[k]=v end; wrong.day='2000-01-01'; assert(not call('close_day',wrong,'admin').ok); "
    "wrong.day=expected.day; wrong.net_minor=wrong.net_minor+1; assert(not call('close_day',wrong,'admin').ok); assert(call('close_day',expected,'admin').ok); "
    "assert(not call('close_day',{},'admin').ok); assert(not call('reserve',stay(room,'2027-08-01','2027-08-02')).ok); "
    "assert(not j.decode(s.handle('not json')).ok)"));
  form_library_t *library = form_library_load("apps/lodge/share/application.orion"); ASSERT_NOT_NULL(library);
  script_page_t *day_page = script_page_open_library(script_application_runtime(app), library, "day_close", "apps/lodge/share/day_close.lua", error, sizeof(error));
  if (!day_page) fprintf(stderr, "%s\n", error);
  ASSERT_NOT_NULL(day_page);
  ASSERT_NOT_NULL(script_page_create(day_page, NULL));
  ASSERT_NOT_NULL(script_page_control(day_page, "day_history"));
  ASSERT_NOT_NULL(script_page_control(day_page, "checklist"));
  ASSERT_NOT_NULL(strstr(script_page_control(day_page, "status")->title, "Closed"));
  ASSERT_TRUE(hotel_script(app, "local d,h=require('day_close'),require('hotel'); local found=false; "
    "for _,row in ipairs(d:checklist(h.day_report())) do if row.title=='Cancelled reservations still holding payments' then found=row.detail:find('^1 ') end end; assert(found)"));
  ASSERT_TRUE(hotel_script(app, "local f=require('remote.overdue'); local h=f('2036-03-01',{{id=9,room='A<&',guest='Guest <one>',arrival='2036-02-29',departure='2036-03-02',status='reserved'},{id=10,room='B',guest='Leaving',arrival='2036-02-27',departure='2036-03-01',status='checked_in'},{id=11,room='C',guest='Future',arrival='2036-03-02',departure='2036-03-03',status='reserved'},{id=12,room='D',guest='Done',arrival='2036-02-27',departure='2036-03-01',status='checked_out'}}); assert(h:find('A&lt;&amp;',1,true) and h:find('Guest &lt;one&gt;',1,true) and h:find('Overdue arrival',1,true) and h:find('Overdue departure',1,true) and not h:find('Future',1,true) and not h:find('Done',1,true))"));
  ASSERT_TRUE(script_page_perform(day_page, "export_overdue"));
  ASSERT_NOT_NULL(strstr(script_page_control(day_page, "notice")->title, "overdue-stays.html"));
  char overdue_path[192]; snprintf(overdue_path, sizeof(overdue_path), "%s/overdue-stays.html", directory); ASSERT_EQUAL(access(overdue_path, F_OK), 0); unlink(overdue_path);

  script_page_close(day_page); form_library_free(library);
  db_day_close_t *close = (void *)send_db_message(hotel, dbFind, TABLE_DAY_CLOSES, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(close); ASSERT_STR_EQUAL(close->net_minor, "15500"); ASSERT_STR_EQUAL(close->actor, "local-owner");
  ASSERT_STR_EQUAL(close->cancelled_paid_minor, "500"); ASSERT_EQUAL(close->cancelled_paid_count, 1);
  char display[80]; db_get_field_text(hotel, TABLE_DAY_CLOSES, close, "cancelled_paid_display", display, sizeof(display));
  ASSERT_STR_EQUAL(display, "5.00");
  db_day_close_t changed = *close; snprintf(changed.net_minor, sizeof(changed.net_minor), "0");
  ASSERT_FALSE(send_db_message(hotel, dbUpdate, TABLE_DAY_CLOSES, &changed));
  ASSERT_FALSE(send_db_message(hotel, dbDelete, TABLE_DAY_CLOSES, (void *)(intptr_t)1));
  script_application_close(app); hotel = NULL;
  app = script_application_open("apps/lodge/share", directory, error, sizeof(error)); ASSERT_NOT_NULL(app);
  hotel = get_database_by_name("lodge");
  close = (void *)send_db_message(hotel, dbFind, TABLE_DAY_CLOSES, (void *)(intptr_t)1);
  ASSERT_NOT_NULL(close); ASSERT_STR_EQUAL(close->net_minor, "15500");
  ASSERT_STR_EQUAL(close->cancelled_paid_minor, "500"); ASSERT_EQUAL(close->cancelled_paid_count, 1);
  ASSERT_TRUE(hotel_script(app, "local h=require('hotel'); local r=h.day_report(); assert(r.closed and r.cancelled_paid_count==1 and r.cancelled_paid_minor==500); assert(not h.close_today())"));
  script_application_close(app); hotel = NULL;
  char backup[168], lock[168]; snprintf(backup, sizeof(backup), "%s.bak", target); snprintf(lock, sizeof(lock), "%s.lock", target);
  unlink(backup); unlink(lock); unlink(target); rmdir(directory); PASS();
}
static void test_currency_setup(void) {
  TEST("Russia/RUB setup is explicit, immutable and survives reopening");
  char directory[128], target[160], error[2048];
  snprintf(directory, sizeof(directory), "/tmp/orion-lodge-currency-%ld", (long)getpid()); test_mkdir(directory, 0700);
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
static void test_group_form(const char *country, const char *currency) {
  TEST("group form creates both room reservations together");
  char directory[128], target[160], error[2048];
  snprintf(directory, sizeof(directory), "/tmp/orion-lodge-group-%ld-%s", (long)getpid(), country); test_mkdir(directory, 0700);
  snprintf(target, sizeof(target), "%s/hotel.xml", directory);
  FILE *in = fopen("apps/lodge/tests/fixtures/legacy-hotel.xml", "rb"), *out = fopen(target, "wb");
  ASSERT_NOT_NULL(in); ASSERT_NOT_NULL(out);
  int byte; while ((byte = fgetc(in)) != EOF) fputc(byte, out);
  fclose(in); fclose(out);
  script_application_t *app = script_application_open("apps/lodge/share", directory, error, sizeof(error)); ASSERT_NOT_NULL(app);
  hotel = get_database_by_name("lodge");
  char configure[128]; snprintf(configure, sizeof(configure), "assert(require('hotel').configure_hotel('%s','%s'))", country, currency);
  ASSERT_TRUE(hotel_script(app, configure));
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
  window_t *booking_table = script_page_control(script_application_page(app, "reception"), "bookings"); ASSERT_NOT_NULL(booking_table);
  send_message(booking_table, tvRefresh, 0, NULL);
  send_message(booking_table, RVM_SETSELECTION, 3, NULL);
  ASSERT_TRUE(script_page_perform(script_application_page(app, "reception"), "group_statement"));
  char statement_path[160], html[4096]; snprintf(statement_path, sizeof(statement_path), "%s/group-statement.html", directory);
  FILE *statement = fopen(statement_path, "rb"); ASSERT_NOT_NULL(statement);
  size_t html_size = fread(html, 1, sizeof(html) - 1, statement); fclose(statement); html[html_size] = 0;
  ASSERT_NOT_NULL(strstr(html, "Conference")); ASSERT_NOT_NULL(strstr(html, currency));
  ASSERT_TRUE(hotel_script(app,
    "local render=require('remote.group_statement'); local rows={"
    "{id=2,room='B',guest='Guest',arrival='2035-01-01',departure='2035-01-02',status='cancelled',total=0.20,paid=0.30,balance=-0.10},"
    "{id=1,room='A',guest='Guest',arrival='2035-01-01',departure='2035-01-02',status='reserved',total=0.10,paid=0,balance=0.10}}; "
    "local html=render('Team <A>',rows,'EUR'); assert(html:find('Team &lt;A&gt;',1,true) and html:find('Cancelled',1,true)); "
    "assert(html:find('Total 0.30',1,true) and html:find('Paid 0.30',1,true) and html:find('Balance 0.00',1,true)); "
    "assert(html:find('<td>A</td>',1,true)<html:find('<td>B</td>',1,true))"));
  ASSERT_TRUE(hotel_script(app, "local h=require('hotel'); h.payment_request='group:legacy:4'; assert(h.reservations:save(4,{paid=25})); h.payment_request=nil; assert(h.payment(4,2500,'group:legacy:4'))"));
  ASSERT_TRUE(hotel_script(app, "local h=require('hotel'); local r=h.reservations.model:find(1); h.payment_request='group:local-1:1'; assert(h.reservations:save(1,{paid=r.paid+1})); h.payment_request=nil"));
  ASSERT_TRUE(script_page_perform(script_application_page(app, "reception"), "preview_group_settlement"));
  ASSERT_NOT_NULL(strstr(script_page_control(script_application_page(app, "reception"), "settlement_notice")->title, "2 reservations"));
  char original_settle[sizeof(hotel->source_path)]; snprintf(original_settle, sizeof(original_settle), "%s", hotel->source_path);
  snprintf(hotel->source_path, sizeof(hotel->source_path), "/nonexistent-orion-lodge-directory/hotel.xml");
  ASSERT_FALSE(script_page_perform(script_application_page(app, "reception"), "confirm_group_settlement"));
  first = (void *)send_db_message(hotel, dbFind, TABLE_RESERVATIONS, (void *)(intptr_t)4);
  second = (void *)send_db_message(hotel, dbFind, TABLE_RESERVATIONS, (void *)(intptr_t)5);
  ASSERT_EQUAL(first->paid, 25); ASSERT_EQUAL(second->paid, 0);
  ASSERT_NULL((void *)send_db_message(hotel, dbFind, TABLE_GROUP_SETTLEMENTS, (void *)(intptr_t)1));
  snprintf(hotel->source_path, sizeof(hotel->source_path), "%s", original_settle);
  ASSERT_TRUE(script_page_perform(script_application_page(app, "reception"), "confirm_group_settlement"));
  ASSERT_TRUE(hotel_script(app, "local h=require('hotel'); assert(h.balance(h.reservations.model:find(4))==0 and h.balance(h.reservations.model:find(5))==0)"));
  ASSERT_TRUE(hotel_script(app, "assert(require('hotel').payment(1,100,'group:local-1:1'))"));
  ASSERT_TRUE(hotel_script(app, "local h=require('hotel'); local e=require('orion.model'):extend('lodge.payment_events'); for _,p in ipairs(e:select({reservation_id=4})) do if p.request_key=='group:local-1:4' then assert(not h.payment(4,p.amount_minor,p.request_key)); return end end; error('missing group payment')"));
  ASSERT_TRUE(hotel_script(app, "local h=require('hotel'); assert(not h.group_settlements:save(1,{allocation='changed'})); assert(not h.group_settlements:delete(1))"));
  ASSERT_TRUE(hotel_script(app, "local h=require('hotel'); assert(not h.settle_group(nil,{{id=4,amount_minor=1}},'invalid')); assert(not h.settle_group(1,{{id=2147483648,amount_minor=1}},'invalid'))"));
  ASSERT_TRUE(script_page_perform(script_application_page(app, "reception"), "preview_group_cancel"));
  ASSERT_NOT_NULL(strstr(script_page_control(script_application_page(app, "reception"), "group_cancel_notice")->title, "2 reservations"));
  char original[sizeof(hotel->source_path)]; snprintf(original, sizeof(original), "%s", hotel->source_path);
  snprintf(hotel->source_path, sizeof(hotel->source_path), "/nonexistent-orion-lodge-directory/hotel.xml");
  ASSERT_FALSE(script_page_perform(script_application_page(app, "reception"), "confirm_group_cancel"));
  first = (void *)send_db_message(hotel, dbFind, TABLE_RESERVATIONS, (void *)(intptr_t)4);
  second = (void *)send_db_message(hotel, dbFind, TABLE_RESERVATIONS, (void *)(intptr_t)5);
  ASSERT_STR_EQUAL(first->status, "reserved"); ASSERT_STR_EQUAL(second->status, "reserved");
  snprintf(hotel->source_path, sizeof(hotel->source_path), "%s", original);
  ASSERT_TRUE(script_page_perform(script_application_page(app, "reception"), "confirm_group_cancel"));
  first = (void *)send_db_message(hotel, dbFind, TABLE_RESERVATIONS, (void *)(intptr_t)4);
  second = (void *)send_db_message(hotel, dbFind, TABLE_RESERVATIONS, (void *)(intptr_t)5);
  ASSERT_STR_EQUAL(first->status, "cancelled"); ASSERT_STR_EQUAL(second->status, "cancelled");
  ASSERT_FALSE(script_page_perform(script_application_page(app, "reception"), "confirm_group_cancel"));
  script_page_close(page); form_library_free(library); script_application_close(app); hotel = NULL;
  app = script_application_open("apps/lodge/share", directory, error, sizeof(error)); ASSERT_NOT_NULL(app);
  hotel = get_database_by_name("lodge");
  second = (void *)send_db_message(hotel, dbFind, TABLE_RESERVATIONS, (void *)(intptr_t)5);
  ASSERT_NOT_NULL(second); ASSERT_EQUAL(second->group_id, 1); ASSERT_STR_EQUAL(second->status, "cancelled");
  ASSERT_TRUE(hotel_script(app,
    "local h=require('hotel'); local stays={}; "
    "for i=1,100 do local r=assert(h.rooms:save(nil,{name='Batch'..i,capacity=1,rate=1})); "
    "stays[i]={room_id=r.id,guest_id=1,arrival='2040-01-01',departure='2040-01-02',persons=1,rate=1,extras=0,notes=''} end; "
    "local g=assert(h.create_group('Large group',stays)); local a={}; "
    "for i,id in ipairs(g.reservations) do a[i]={id=id,amount_minor=100} end; "
    "assert(h.settle_group(g.id,a,'large-group')); assert(h.settle_group(g.id,a,'large-group')); "
    "for _,id in ipairs(g.reservations) do assert(h.balance(h.reservations.model:find(id))==0) end"));
  script_application_close(app); hotel = NULL;
  char backup[168], lock[168]; snprintf(backup, sizeof(backup), "%s.bak", target); snprintf(lock, sizeof(lock), "%s.lock", target);
  unlink(statement_path); unlink(backup); unlink(lock); unlink(target); rmdir(directory); PASS();
}
static bool file_contains(const char *directory, const char *name, const char *text) {
  char path[256], contents[16384]; snprintf(path, sizeof(path), "%s/%s", directory, name);
  FILE *file = fopen(path, "rb"); if (!file) return false;
  size_t size = fread(contents, 1, sizeof(contents) - 1, file); fclose(file); contents[size] = 0; unlink(path);
  return strstr(contents, text) != NULL;
}
static void test_front_desk(void) {
  TEST("front desk: today's movements, free rooms, out of service, payment methods, documents and reports");
  char directory[128], error[2048];
  snprintf(directory, sizeof(directory), "/tmp/orion-lodge-desk-%ld", (long)getpid()); test_mkdir(directory, 0700);
  script_application_t *app = script_application_open("apps/lodge/share", directory, error, sizeof(error)); ASSERT_NOT_NULL(app);
  hotel = get_database_by_name("lodge");
  ASSERT_NOT_NULL(script_application_page(app, "today"));
  ASSERT_TRUE(hotel_script(app,
    "local orion,h=require('orion'),require('hotel'); local cal=require('orion.plugins').use('calendar',1); assert(h.configure_hotel('DE','EUR'),'step1'); "
    "assert(#h.booking_sources.model:all()==6 and #h.payment_methods.model:all()==3,'step2'); "
    "assert(not h.guests:save(nil,{name='Bad',email='not-an-email'}),'step3'); assert(not h.guests:save(nil,{name='Unborn',birth_date=cal.add(orion.today(),1)}),'step4'); "
    "assert(h.guests:save(nil,{name='Ada Lovelace',email='ada@example.org',birth_date='1990-12-10',nationality='British',address='1 St James Sq, London',document='P123'}),'step5'); "
    "assert(h.guests:save(nil,{name='Bo Larsen'}),'step6'); "
    "for _,r in ipairs({{'A',2,100},{'B',4,150},{'C',2,80}}) do assert(h.rooms:save(nil,{name=r[1],capacity=r[2],rate=r[3]}),'step7') end; "
    "for id=1,3 do assert(h.rooms:save(id,{cleaning_state='clean'}),'step8') end; "
    "local t=orion.today(); local phone=h.booking_sources.model:find({name='Phone'}).id; "
    "assert(h.save(nil,{room_id=1,guest_id=1,arrival=t,departure=cal.add(t,2),persons=2,rate=100,extras=0,notes='',source_id=phone}),'step9'); "
    "assert(h.save(nil,{room_id=2,guest_id=2,arrival=cal.add(t,-1),departure=t,persons=1,rate=150,extras=12.5,notes=''}),'step10'); "
    "assert(h.transition(2,'checked_in'),'step11'); "
    "assert(h.room_blocks:save(nil,{room_id=3,blocked_from=cal.add(t,5),blocked_until=cal.add(t,7),reason='Paint'}),'step12'); "
    "assert(not h.room_blocks:save(nil,{room_id=1,blocked_from=t,blocked_until=cal.add(t,1),reason='Leak'}),'step13'); "
    "assert(not h.save(nil,{room_id=3,guest_id=2,arrival=cal.add(t,6),departure=cal.add(t,8),persons=1,rate=80,extras=0,notes=''}),'step14'); "
    "local fd=require('front_desk'); local free=fd.free_rooms(cal.add(t,5),cal.add(t,6),1); assert(#free==2 and free[1].name=='A' and free[2].name=='B','step15'); "
    "free=fd.free_rooms(cal.add(t,5),cal.add(t,6),3); assert(#free==1 and free[1].name=='B','step16'); "
    "assert(#fd.free_rooms(t,cal.add(t,1),1)==2,'step17')"));
  script_page_t *today = script_application_page(app, "today"); ASSERT_NOT_NULL(today);
  script_page_update(today);
  ASSERT_STR_EQUAL(script_page_control(today, "arrivals_count")->title, "1");
  ASSERT_STR_EQUAL(script_page_control(today, "departures_count")->title, "1");
  ASSERT_STR_EQUAL(script_page_control(today, "in_house_count")->title, "1");
  send_message(script_page_control(today, "arrivals"), RVM_SETSELECTION, 0, NULL);
  ASSERT_TRUE(script_page_perform(today, "registration"));
  ASSERT_TRUE(file_contains(directory, "registration.html", "P123"));
  ASSERT_TRUE(script_page_perform(today, "check_in"));
  db_reservation_t *row = (void *)send_db_message(hotel, dbFind, TABLE_RESERVATIONS, (void *)(intptr_t)1);
  ASSERT_STR_EQUAL(row->status, "checked_in");
  ASSERT_STR_EQUAL(script_page_control(today, "in_house_count")->title, "2");
  send_message(script_page_control(today, "departures"), RVM_SETSELECTION, 0, NULL);
  ASSERT_FALSE(script_page_perform(today, "check_out"));
  ASSERT_NOT_NULL(strstr(script_page_message(today), "Settle"));
  ASSERT_TRUE(hotel_script(app, "local h=require('hotel'); local card=h.payment_methods.model:find({name='Card'}).id; "
    "assert(not h.payment(2,16250,'desk-1',999)); assert(h.payment(2,16250,'desk-1',card)); "
    "local e=require('orion.model'):extend('lodge.payment_events'):find({request_key='desk-1'}); assert(e.method_id==card)"));
  send_message(script_page_control(today, "departures"), RVM_SETSELECTION, 0, NULL);
  ASSERT_TRUE(script_page_perform(today, "invoice"));
  ASSERT_TRUE(file_contains(directory, "bill.html", "Card"));
  send_message(script_page_control(today, "departures"), RVM_SETSELECTION, 0, NULL);
  ASSERT_TRUE(script_page_perform(today, "check_out"));
  row = (void *)send_db_message(hotel, dbFind, TABLE_RESERVATIONS, (void *)(intptr_t)2);
  ASSERT_STR_EQUAL(row->status, "checked_out");
  ASSERT_STR_EQUAL(script_page_control(today, "departures_count")->title, "0");
  ASSERT_TRUE(hotel_script(app, "local orion,fd=require('orion'),require('front_desk'); local cal=require('orion.plugins').use('calendar',1); local t=orion.today(); "
    "local r=fd.period(cal.add(t,-1),cal.add(t,2)); assert(r.available==9 and r.sold==3 and r.guest_nights==5, r.sold); "
    "assert(r.room_revenue==35000 and r.extras==1250 and r.net==16250); assert(r.methods['Card']==16250); assert(r.sources['Phone']==1); "
    "local found=false; for _,row in ipairs(fd.period_rows(r)) do if row.label=='Average daily rate (ADR)' then found=row.value end end; assert(found=='116.67', found)"));
  script_page_t *reports = script_application_page(app, "reports"); ASSERT_NOT_NULL(reports);
  ASSERT_TRUE(script_page_perform(reports, "export"));
  ASSERT_TRUE(file_contains(directory, "period-report.html", "RevPAR"));
  script_page_t *rooms = script_application_page(app, "rooms"); ASSERT_NOT_NULL(rooms);
  ASSERT_TRUE(script_page_perform(rooms, "cleaning_list"));
  ASSERT_TRUE(file_contains(directory, "cleaning-list.html", "Stay-over: tidy"));
  script_page_t *availability = script_application_page(app, "availability"); ASSERT_NOT_NULL(availability);
  ASSERT_FALSE(script_page_perform(availability, "reserve"));
  ASSERT_TRUE(script_page_perform(availability, "search"));
  ASSERT_NOT_NULL(strstr(script_page_control(availability, "summary")->title, "2 rooms free"));
  send_message(script_page_control(availability, "free"), RVM_SETSELECTION, 0, NULL);
  send_message(script_page_control(availability, "guest"), cbSetCurrentValue, 2, NULL);
  ASSERT_TRUE(script_page_perform(availability, "reserve"));
  ASSERT_NOT_NULL(strstr(script_page_control(availability, "notice")->title, "Bo Larsen"));
  ASSERT_NOT_NULL(strstr(script_page_control(availability, "summary")->title, "1 rooms free"));
  script_application_close(app); hotel = NULL;
  char path[192];
  const char *files[] = {"hotel.xml", "hotel.xml.bak", "hotel.xml.lock"};
  for (int i = 0; i < 3; i++) { snprintf(path, sizeof(path), "%s/%s", directory, files[i]); unlink(path); }
  rmdir(directory);
  PASS();
}
static void test_demo(void) {
  TEST("the shipped demo hotel opens, migrates and shows its reservations");
  char directory[128], target[160], error[2048];
  snprintf(directory, sizeof(directory), "/tmp/orion-lodge-demo-%ld", (long)getpid()); test_mkdir(directory, 0700);
  snprintf(target, sizeof(target), "%s/hotel.xml", directory);
  FILE *in = fopen("apps/lodge/share/samples/demo.xml", "rb"), *out = fopen(target, "wb");
  ASSERT_NOT_NULL(in); ASSERT_NOT_NULL(out);
  int byte; while ((byte = fgetc(in)) != EOF) fputc(byte, out);
  fclose(in); fclose(out);
  script_application_t *app = script_application_open("apps/lodge/share", directory, error, sizeof(error));
  if (!app) fprintf(stderr, "%s\n", error);
  ASSERT_NOT_NULL(app); hotel = get_database_by_name("lodge");
  ASSERT_TRUE(hotel_script(app, "local h=require('hotel'); assert(h.configure_hotel('DE','EUR')); assert(#h.reservations.model:all()==10 and #h.room_blocks.model:all()==1)"));
  ASSERT_NOT_NULL(strstr(script_page_control(script_application_page(app, "reception"), "summary")->title, "10 reservations"));
  script_application_close(app); hotel = NULL;
  char path[192]; const char *files[] = {"hotel.xml", "hotel.xml.bak", "hotel.xml.lock"};
  for (int i = 0; i < 3; i++) { snprintf(path, sizeof(path), "%s/%s", directory, files[i]); unlink(path); }
  rmdir(directory); PASS();
}
static bool run_spec(script_application_t *app, const char *path) {
  FILE *file = fopen(path, "rb"); if (!file) return false;
  static char source[65536]; size_t size = fread(source, 1, sizeof(source) - 1, file); fclose(file); source[size] = 0;
  return hotel_script(app, source);
}
static script_application_t *open_sample(const char *name, char *directory, size_t size) {
  char error[2048];
  snprintf(directory, size, "/tmp/orion-lodge-%s-%ld", name, (long)getpid()); test_mkdir(directory, 0700);
  script_application_t *app = script_application_open("apps/lodge/share", directory, error, sizeof(error));
  if (!app) { fprintf(stderr, "%s\n", error); return NULL; }
  hotel = get_database_by_name("lodge");
  if (!hotel_script(app, "assert(require('hotel').configure_hotel('DE','EUR'))")) return NULL;
  return app;
}
static void close_sample(script_application_t *app, const char *directory) {
  script_application_close(app); hotel = NULL;
  const char *files[] = {"hotel.xml", "hotel.xml.bak", "hotel.xml.lock", "lodge-reservations.csv", "lodge-guests.csv", "lodge-payments.csv", "bill.html", "front-desk-list.html"};
  char path[256];
  for (int i = 0; i < (int)(sizeof(files) / sizeof(files[0])); i++) { snprintf(path, sizeof(path), "%s/%s", directory, files[i]); unlink(path); }
  rmdir(directory);
}
static void test_desk_tools(void) {
  TEST("sample hotel, walk-in, extend, no-show, search, calendar, CSV export and letterhead");
  char directory[128];
  script_application_t *app = open_sample("tools", directory, sizeof(directory)); ASSERT_NOT_NULL(app);
  ASSERT_TRUE(run_spec(app, "apps/lodge/tests/desk_spec.lua"));
  // Every page renders the new data, and the calendar's titles follow its dates.
  script_page_t *calendar = script_application_page(app, "availability"); ASSERT_NOT_NULL(calendar);
  script_page_refresh(calendar);
  ASSERT_STR_EQUAL(script_page_message(calendar), "");
  ASSERT_TRUE(script_page_perform(calendar, "next_week"));
  ASSERT_TRUE(script_page_perform(calendar, "go_today"));
  script_page_t *audit = script_application_page(app, "day_close"); ASSERT_NOT_NULL(audit);
  ASSERT_NOT_NULL(strstr(script_page_control(audit, "status")->title, "Resolve"));
  ASSERT_FALSE(script_page_can_perform(audit, "close_day"));
  ASSERT_TRUE(script_page_can_perform(audit, "export_overdue"));
  script_page_t *reports = script_application_page(app, "reports"); ASSERT_NOT_NULL(reports);
  ASSERT_TRUE(script_page_perform(reports, "export_csv"));
  ASSERT_TRUE(file_contains(directory, "lodge-guests.csv", "Nora Walker"));
  script_page_t *today = script_application_page(app, "today"); ASSERT_NOT_NULL(today);
  ASSERT_TRUE(script_page_can_perform(today, "extend"));
  ASSERT_FALSE(script_page_can_perform(today, "no_such_action"));
  // The walk-in window opens over the main one and checks a guest in.
  char error[1024];
  ASSERT_TRUE(script_runtime_action(script_application_runtime(app), "orion.application", "open", "walk_in", error, sizeof(error)));
  script_page_t *walk_in = script_application_page(app, "walk_in"); ASSERT_NOT_NULL(walk_in);
  send_message(script_page_control(walk_in, "name"), edSetText, 0, "Ola Desk");
  send_message(script_page_control(walk_in, "free"), RVM_SETSELECTION, 0, NULL);
  ASSERT_TRUE(script_page_perform(walk_in, "check_in"));
  ASSERT_NOT_NULL(strstr(script_page_control(walk_in, "notice")->title, "Ola Desk"));
  ASSERT_FALSE(script_page_perform(walk_in, "check_in"));
  // Opening the assistant twice brings the same window forward.
  ASSERT_TRUE(script_runtime_action(script_application_runtime(app), "orion.application", "open", "assistant", error, sizeof(error)));
  script_page_t *assistant = script_application_page(app, "assistant"); ASSERT_NOT_NULL(assistant);
  ASSERT_TRUE(script_runtime_action(script_application_runtime(app), "orion.application", "open", "assistant", error, sizeof(error)));
  ASSERT_TRUE(script_application_page(app, "assistant") == assistant);
  send_message(script_page_control(assistant, "ask"), edSetText, 0, "check out Taylor");
  ASSERT_TRUE(script_page_perform(assistant, "ask"));
  ASSERT_NOT_NULL(strstr(script_page_control(assistant, "reply")->title, "Enter again"));
  ASSERT_TRUE(script_page_perform(assistant, "ask"));
  ASSERT_NOT_NULL(strstr(script_page_control(assistant, "done")->title, "checked out"));
  ASSERT_TRUE(hotel_script(app, "assert(require('hotel').search('taylor')[1].status=='checked_out')"));
  close_sample(app, directory);
  PASS();
}
static void test_assistant(void) {
  TEST("assistant understands English, German and Russian requests and carries them out");
  char directory[128];
  script_application_t *app = open_sample("assistant", directory, sizeof(directory)); ASSERT_NOT_NULL(app);
  ASSERT_TRUE(hotel_script(app, "assert(require('samples').load())"));
  // LODGE_SAMPLE_OUT keeps a copy of the fresh sample hotel, for screenshots.
  const char *keep = getenv("LODGE_SAMPLE_OUT");
  if (keep) {
    ASSERT_TRUE(send_db_message(hotel, dbSave, 0, NULL));
    char from[256], to[256]; snprintf(from, sizeof(from), "%s/hotel.xml", directory); snprintf(to, sizeof(to), "%s/hotel.xml", keep);
    FILE *in = fopen(from, "rb"), *out = fopen(to, "wb"); ASSERT_NOT_NULL(in); ASSERT_NOT_NULL(out);
    int byte; while ((byte = fgetc(in)) != EOF) fputc(byte, out);
    fclose(in); fclose(out);
  }
  ASSERT_TRUE(run_spec(app, "apps/lodge/tests/assistant_spec.lua"));
  close_sample(app, directory);
  PASS();
}
// Each language's examples must be requests the assistant understands there.
static void test_assistant_languages(void) {
  TEST("assistant examples work in German and Russian");
  const char *languages[] = {"de", "ru"};
  for (int i = 0; i < 2; i++) {
    ui_set_language(languages[i]);
    char directory[128];
    script_application_t *app = open_sample(languages[i], directory, sizeof(directory)); ASSERT_NOT_NULL(app);
    ASSERT_TRUE(hotel_script(app, "assert(require('samples').load())"));
    ASSERT_TRUE(hotel_script(app,
      "local orion,E=require('orion'),require('assistant_engine'); local misunderstood=orion.tr('I did not understand that. Try a guest\\'s name, a room number, or one of these examples.'); "
      "assert(misunderstood~='I did not understand that. Try a guest\\'s name, a room number, or one of these examples.','catalog loaded'); "
      "for _,example in ipairs(E.examples()) do local a=E.answer(example.arg); "
      "assert(a.reply~=misunderstood and #a.rows>0 and a.rows[1].action~='ask', example.arg..' -> '..a.reply) end; "
      "local pay=E.answer(orion.tr('Taylor paid 50 cash')); assert(pay.rows[1].action=='payment' and pay.rows[1].arg:match(':5000:[1-9]%d*$'), pay.rows[1].arg)"));
    close_sample(app, directory);
  }
  ui_set_language("en");
  PASS();
}
// The ribbon in the running application: its commands follow the selected
// page, grey out where the page cannot run them, and run the page's actions.
static uint32_t ribbon_ident(const form_ribbon_t *ribbon, const char *action, const char *page) {
  for (int i = 0; i < ribbon->command_count; i++)
    if (!strcmp(ribbon->commands[i].action, action) && !ribbon->commands[i].target &&
        ((!page && !ribbon->commands[i].page) || (page && ribbon->commands[i].page && !strcmp(ribbon->commands[i].page, page))))
      return ribbon->first_ident + (uint32_t)i;
  return 0;
}
static void test_ribbon_host(void) {
  TEST("the ribbon runs page actions, follows the selected page and greys out what it cannot do");
  script_application_allow_ribbon(true);
  char directory[128];
  script_application_t *app = open_sample("ribbon", directory, sizeof(directory)); ASSERT_NOT_NULL(app);
  ASSERT_TRUE(hotel_script(app, "assert(require('samples').load())"));
  window_t *ribbon_win = script_application_menu_window(app); ASSERT_NOT_NULL(ribbon_win);
  ASSERT_NOT_NULL(script_application_accelerators(app));
  form_library_t *library = form_library_load("apps/lodge/share/application.orion"); ASSERT_NOT_NULL(library);
  const form_ribbon_t *ribbon = form_library_ribbon(library); ASSERT_NOT_NULL(ribbon);
  uint32_t check_in = ribbon_ident(ribbon, "check_in", NULL), confirmation = ribbon_ident(ribbon, "confirmation", NULL);
  ASSERT_TRUE(check_in && confirmation);
  // Today shows: Check in works there, Confirmation belongs to Reservations.
  script_page_t *today = script_application_page(app, "today");
  script_page_refresh(today);
  ASSERT_EQUAL((int)send_message(ribbon_win, rbGetState, check_in, NULL) & TBSTATE_DISABLED, 0);
  ASSERT_EQUAL((int)send_message(ribbon_win, rbGetState, confirmation, NULL) & TBSTATE_DISABLED, TBSTATE_DISABLED);
  // Choose Alex Morgan's arrival (room 201 is ready) and check in from the ribbon.
  window_t *arrivals = script_page_control(today, "arrivals");
  int count = (int)send_message(arrivals, RVM_GETITEMCOUNT, 0, NULL), row = -1;
  for (int i = 0; i < count; i++) {
    db_arrival_t *record = (void *)send_message(arrivals, tvGetRecord, (uint32_t)i, NULL);
    if (record && !strcmp(record->state, "ready")) row = i;
  }
  ASSERT_TRUE(row >= 0);
  send_message(arrivals, RVM_SETSELECTION, (uint32_t)row, NULL);
  send_message(ribbon_win, evCommand, MAKEDWORD(check_in, btnClicked), NULL);
  ASSERT_TRUE(hotel_script(app, "assert(require('hotel').search('morgan')[1].status=='checked_in')"));
  ASSERT_NOT_NULL(strstr(script_page_control(today, "notice")->title, "Alex Morgan"));
  // Choosing Reservations shows its tab's commands.
  char error[512]; ASSERT_TRUE(script_runtime_action(script_application_runtime(app), "orion.application", "open", "reception", error, sizeof(error)));
  ASSERT_EQUAL((int)send_message(ribbon_win, rbGetState, confirmation, NULL) & TBSTATE_DISABLED, 0);
  form_library_free(library);
  close_sample(app, directory);
  script_application_allow_ribbon(false);
  PASS();
}
static void test_ribbon(void) {
  TEST("the application ribbon: commands, shortcuts and page tabs");
  form_library_t *library = form_library_load("apps/lodge/share/application.orion"); ASSERT_NOT_NULL(library);
  const form_ribbon_t *ribbon = form_library_ribbon(library); ASSERT_NOT_NULL(ribbon);
  ASSERT_TRUE(ribbon->def.tab_count >= 8);
  ASSERT_STR_EQUAL(ribbon->def.tabs[0].label, "Home");
  // As in a compiled application: File is the application menu, <menus> the command registry.
  ASSERT_STR_EQUAL(ribbon->def.menu_label, "File");
  ASSERT_TRUE(ribbon->menu_count >= 8);
  uint32_t check_in_id = form_ribbon_command_id(ribbon, "desk.check_in"); ASSERT_TRUE(check_in_id != 0);
  ASSERT_STR_EQUAL(form_ribbon_command(ribbon, check_in_id)->action, "check_in");
  ASSERT_STR_EQUAL(ribbon->menus[1].items[0].label, "Check in\tCtrl+I");
  ASSERT_EQUAL(ribbon->def.tabs[0].groups[1].items[0].ident, check_in_id);
  ASSERT_STR_EQUAL(ribbon->def.tabs[0].groups[1].items[0].text, "Check in");
  ASSERT_STR_EQUAL(ribbon->def.tabs[0].groups[0].items[1].text, "Walk-in guest");   // the menu label less its ellipsis
  ASSERT_EQUAL(ribbon->def.tabs[0].context, 0);
  ASSERT_NOT_NULL(ribbon->def.quick_access);
  ASSERT_TRUE(form_ribbon_context(ribbon, "reception") != 0);
  ASSERT_EQUAL(form_ribbon_context(ribbon, "guests"), 0);
  bool check_in = false, new_booking = false;
  for (int i = 0; i < ribbon->accelerator_count; i++) {
    const form_command_t *command = form_ribbon_command(ribbon, ribbon->accelerators[i].cmd); ASSERT_NOT_NULL(command);
    if (!strcmp(command->action, "check_in") && ribbon->accelerators[i].key == AX_KEY_I && (ribbon->accelerators[i].fVirt & FCONTROL)) check_in = true;
    if (!strcmp(command->action, "new_booking") && command->page && !strcmp(command->page, "reception")) new_booking = true;
  }
  ASSERT_TRUE(check_in); ASSERT_TRUE(new_booking);
  // The Quick Access Toolbar's New reservation is the Home tab's: one command, one state.
  ASSERT_EQUAL(ribbon->def.quick_access->items[0].ident, ribbon->def.tabs[0].groups[0].items[0].ident);
  form_library_free(library);
  // Malformed ribbons are left out: an unknown shortcut, a button with an inline action, an undeclared command.
  const char *bad[] = {
    "<orion><menus><menu name=\"m\" label=\"M\"><item name=\"a\" label=\"A\" action=\"a\" shortcut=\"Ctrl+Nope\" /></menu></menus>"
    "<ribbon><Tab label=\"Home\"><Group label=\"G\"><Button command=\"m.a\" /></Group></Tab></ribbon></orion>",
    "<orion><menus><menu name=\"m\" label=\"M\"><item name=\"a\" label=\"A\" action=\"a\" /></menu></menus>"
    "<ribbon><Tab label=\"Home\"><Group label=\"G\"><Button action=\"a\" text=\"A\" /></Group></Tab></ribbon></orion>",
    "<orion><menus><menu name=\"m\" label=\"M\"><item name=\"a\" label=\"A\" action=\"a\" /></menu></menus>"
    "<ribbon><Tab label=\"Home\"><Group label=\"G\"><Button command=\"m.b\" /></Group></Tab></ribbon></orion>",
  };
  for (int i = 0; i < 3; i++) {
    library = form_library_parse(bad[i], strlen(bad[i]), "bad-ribbon"); ASSERT_NOT_NULL(library);
    ASSERT_NULL(form_library_ribbon(library));
    form_library_free(library);
  }
  accel_t a = {0};
  ASSERT_TRUE(accel_parse("Ctrl+Shift+K", &a)); ASSERT_EQUAL(a.key, AX_KEY_K); ASSERT_EQUAL(a.fVirt, FVIRTKEY | FCONTROL | FSHIFT);
  ASSERT_TRUE(accel_parse("F5", &a)); ASSERT_EQUAL(a.key, AX_KEY_F5);
  ASSERT_TRUE(accel_parse("alt+left", &a)); ASSERT_EQUAL(a.key, AX_KEY_LEFTARROW);
  ASSERT_FALSE(accel_parse("Ctrl+", &a)); ASSERT_FALSE(accel_parse("Ctrl+A+B", &a));
  PASS();
}
int main(void) {
  TEST_START("Orion Lodge");
  test_env_init(); DB_CLASS(db_schema_xml);
  snprintf(hotel_path, sizeof(hotel_path), "/tmp/orion-lodge-%ld.xml", (long)getpid());
  hotel = create_database_with_schema("lodge", "db_schema_xml", &lodge_lodge_schema, hotel_path);
  register_database("lodge", hotel);
  test_hotel(); test_persistence_and_page(); test_refused_migration(); test_payment_form(); test_nightly_prices(); test_service(); test_group_form("DE", "EUR"); test_group_form("RU", "RUB"); test_currency_setup(); test_front_desk(); test_demo(); test_ribbon(); test_desk_tools(); test_assistant(); test_assistant_languages(); test_ribbon_host();
  if (hotel) { unregister_database("lodge"); destroy_database(hotel); } unlink(hotel_path);
  test_env_shutdown(); TEST_END();
}
