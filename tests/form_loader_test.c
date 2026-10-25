// form_loader_test.c — runtime .orion forms: controls, bindings resolved from a
// live schema, record forms named before they are declared, the attributes a
// script reads, and the forms the loader must leave out with a diagnostic.

#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/commctl/commctl.h>
#include <orion/commctl/form_loader.h>

enum { TABLE_PEOPLE = 1, TABLE_PETS = 2 };

typedef struct { int id; char name[32]; int age; bool active; char born[11]; } person_t;
typedef struct { int id; int owner_id; char name[24]; } pet_t;

static const db_field_meta_t person_meta[] = {
  { "id", DB_TYPE_INT, offsetof(person_t, id), 0, 0 },         { "name", DB_TYPE_STRING, offsetof(person_t, name), 32, 0 },
  { "age", DB_TYPE_INT, offsetof(person_t, age), 0, 0 },       { "active", DB_TYPE_BOOL, offsetof(person_t, active), 0, 0 },
  { "born", DB_TYPE_DATE, offsetof(person_t, born), 11, 0 },
};
static const db_field_schema_t person_fields[] = {
  { "id", DB_TYPE_INT, 0, true, NULL, NULL },   { "name", DB_TYPE_STRING, 32, false, NULL, NULL },
  { "age", DB_TYPE_INT, 0, false, NULL, NULL }, { "active", DB_TYPE_BOOL, 0, false, NULL, NULL },
  { "born", DB_TYPE_DATE, 11, false, NULL, NULL },
};
static const db_field_meta_t pet_meta[] = {
  { "id", DB_TYPE_INT, offsetof(pet_t, id), 0, 0 }, { "owner_id", DB_TYPE_INT, offsetof(pet_t, owner_id), 0, 0 },
  { "name", DB_TYPE_STRING, offsetof(pet_t, name), 24, 0 },
};
static const db_field_schema_t pet_fields[] = {
  { "id", DB_TYPE_INT, 0, true, NULL, NULL }, { "owner_id", DB_TYPE_INT, 0, false, "people", "id" },
  { "name", DB_TYPE_STRING, 24, false, NULL, NULL },
};
static const db_table_schema_t tables[] = {
  { TABLE_PEOPLE, "people", NULL, person_fields, 5, NULL, 0, sizeof(person_t), person_meta, 5, false },
  { TABLE_PETS, "pets", NULL, pet_fields, 3, NULL, 0, sizeof(pet_t), pet_meta, 3, false },
};
static const db_schema_def_t schema = { "club", "db_schema_xml", NULL, tables, 2 };

static const char kDoc[] =
  "<orion>\n"
  "  <menus><menu name=\"file\" label=\"File\" /></menus>\n"
  "  <forms>\n"
  "    <form name=\"people\" title=\"People\" width=\"400\" height=\"300\">\n"
  "      <TableView name=\"list\" source=\"club.people\" form=\"person\" flags=\"vscroll,flexspace\">\n"
  "        <Column field=\"name\" title=\"Name\" width=\"0\" />\n"
  "        <Column field=\"age\" title=\"Age\" width=\"40\" total=\"sum\" />\n"
  "      </TableView>\n"
  "      <TableView name=\"pets\" source=\"club.pets\">\n"
  "        <Column field=\"name\" title=\"Pet\" />\n"
  "      </TableView>\n"
  "      <StackView orientation=\"horizontal\">\n"
  "        <Button name=\"add\" text=\"Add\" target=\"list\" action=\"insert\" />\n"
  "        <Button name=\"calculate\" text=\"Calculate\" action=\"calculate\" />\n"
  "      </StackView>\n"
  "    </form>\n"
  "    <form name=\"person\" title=\"Person\" width=\"300\">\n"
  "      <TextBox name=\"name\" field=\"club.people.name\" placeholder=\"Full name\" />\n"
  "      <TextBox name=\"age\" field=\"club.people.age\" />\n"
  "      <TextBox name=\"born\" field=\"club.people.born\" />\n"
  "      <CheckBox name=\"active\" text=\"Active\" field=\"club.people.active\" />\n"
  "      <Button name=\"ok\" text=\"OK\" action=\"club.people.update\" />\n"
  "      <Button name=\"cancel\" text=\"Cancel\" />\n"
  "    </form>\n"
  "    <form name=\"no_width\" title=\"Broken\"><Label text=\"x\" /></form>\n"
  "    <form name=\"bad_flag\" width=\"10\"><Label text=\"x\" flags=\"WINDOW_NONSENSE\" /></form>\n"
  "    <form name=\"with_toolbar\" width=\"10\"><Toolbar><Button command=\"file.open\" /></Toolbar></form>\n"
  "    <form name=\"bad_source\" width=\"10\"><TableView source=\"club.nothing\" /></form>\n"
  "    <form name=\"bad_record_form\" width=\"10\"><TableView source=\"club.people\" form=\"nowhere\" /></form>\n"
  "    <form name=\"two_tables\" width=\"10\">\n"
  "      <TextBox field=\"club.people.name\" /><TextBox field=\"club.pets.name\" />\n"
  "    </form>\n"
  "  </forms>\n"
  "</orion>\n";

static database_t *g_db;

static form_library_t *load(void) { return form_library_parse(kDoc, sizeof(kDoc) - 1, "club.orion"); }

static void test_rejects_non_orion(void) {
  TEST("form_library_parse: a document that is not <orion> yields no library");
  static const char other[] = "<html><body /></html>";
  ASSERT_NULL(form_library_parse(other, sizeof(other) - 1, "page.html"));
  ASSERT_NULL(form_library_parse("<orion>", 7, "broken.orion"));
  ASSERT_NULL(form_library_load("does/not/exist.orion"));
  PASS();
}

static void test_forms_and_rejections(void) {
  TEST("form_library: valid forms load; each broken one is left out alone");
  form_library_t *lib = load();
  ASSERT_NOT_NULL(lib);
  ASSERT_EQUAL(form_library_form_count(lib), 2);
  ASSERT_STR_EQUAL(form_library_form_at(lib, 0)->id, "people");
  ASSERT_STR_EQUAL(form_library_form_at(lib, 1)->id, "person");
  ASSERT_NULL(form_library_form_at(lib, 2));
  const char *left_out[] = { "no_width", "bad_flag", "with_toolbar", "bad_source", "bad_record_form", "two_tables" };
  for (size_t i = 0; i < ARRAY_LEN(left_out); i++) ASSERT_NULL(form_library_form(lib, left_out[i]));
  form_library_free(lib);
  PASS();
}

static void test_controls(void) {
  TEST("form_library: controls, ids, targets, master/detail and record forms");
  form_library_t *lib = load();
  const form_def_t *people = form_library_form(lib, "people"), *person = form_library_form(lib, "person");
  ASSERT_STR_EQUAL(people->name, "People");
  ASSERT_TRUE(people->flags & WINDOW_AUTO_LAYOUT);
  ASSERT_EQUAL(people->child_count, 5);

  uint32_t list = form_library_control_id(lib, people, "list"), pets = form_library_control_id(lib, people, "pets");
  uint32_t add = form_library_control_id(lib, people, "add"), calculate = form_library_control_id(lib, people, "calculate");
  ASSERT_TRUE(list >= FORM_LIBRARY_ID_BASE && pets && add && calculate);
  ASSERT_EQUAL(form_library_control_id(lib, people, "nothing"), 0);

  const form_ctrl_def_t *tv = form_library_control(lib, people, list);
  ASSERT_STR_EQUAL(tv->class_name, "TableView");
  ASSERT_TRUE(tv->flags & WINDOW_VSCROLL);
  const tableview_params_t *p = tv->lparam;
  ASSERT_EQUAL(p->table_id, TABLE_PEOPLE);
  ASSERT_STR_EQUAL(p->field_names[1], "age");
  ASSERT_NULL(p->field_names[2]);
  ASSERT_EQUAL(p->column_widths[1], 40);
  ASSERT_TRUE(p->column_totals && !p->column_totals[0] && p->column_totals[1]);
  ASSERT(p->record_form == person, "form= names a form declared later in the file");

  const tableview_params_t *detail = form_library_control(lib, people, pets)->lparam;
  ASSERT_EQUAL(detail->master_id, list);
  ASSERT_EQUAL(detail->master_filter_field, 1);
  ASSERT_STR_EQUAL(detail->master_key, "id");

  const form_ctrl_def_t *button = form_library_control(lib, people, add);
  ASSERT_EQUAL(button->target, list);
  ASSERT_EQUAL(button->target_message, tvInsertRecord);
  ASSERT_TRUE(form_library_control(lib, people, button->parent)->flags & WINDOW_STACK_HORIZONTAL);
  ASSERT_STR_EQUAL(form_library_control_attr(lib, people, calculate, "action"), "calculate");
  ASSERT_NULL(form_library_control_attr(lib, people, calculate, "target"));
  ASSERT_NULL(form_library_control_attr(lib, person, calculate, "action"));
  form_library_free(lib);
  PASS();
}

static void test_bindings(void) {
  TEST("form_library: field= binds through the live schema with typed DDX");
  form_library_t *lib = load();
  const form_def_t *f = form_library_form(lib, "person");
  ASSERT_STR_EQUAL(f->db_name, "club");
  ASSERT_STR_EQUAL(f->db_table, "people");
  ASSERT_EQUAL(f->db_table_id, TABLE_PEOPLE);
  ASSERT_EQUAL((int)f->db_record_size, (int)sizeof(person_t));
  ASSERT_EQUAL(f->binding_count, 4);
  ASSERT_EQUAL(f->bindings[0].getter, edGetText);
  ASSERT_EQUAL((int)f->bindings[0].wparam, 32);
  ASSERT_EQUAL((int)f->bindings[1].offset, (int)offsetof(person_t, age));
  ASSERT_TRUE(f->bindings[1].push == ddx_push_int && f->bindings[2].push == ddx_push_date);
  ASSERT_TRUE(f->bindings[3].push == ddx_push_check);
  ASSERT_EQUAL(f->ok_id, form_library_control_id(lib, f, "ok"));
  ASSERT_EQUAL(f->cancel_id, form_library_control_id(lib, f, "cancel"));
  ASSERT_STR_EQUAL(form_library_control(lib, f, form_library_control_id(lib, f, "name"))->placeholder, "Full name");
  form_library_free(lib);
  PASS();
}

static result_t dialog_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)wparam; (void)lparam;
  return msg == evCreate || msg == evDestroy;
}

static void test_window_from_runtime_form(void) {
  TEST("form_library: a runtime form builds a window and exchanges a record");
  form_library_t *lib = load();
  const form_def_t *f = form_library_form(lib, "person");
  window_t *win = create_window_from_form(f, 0, 0, NULL, dialog_proc, 0, NULL);
  ASSERT_NOT_NULL(win);
  person_t in = { .name = "Ada", .age = 36, .active = true, .born = "1815-12-10" }, out = {0};
  dialog_push(win, &in, f->bindings, f->binding_count);
  window_t *name = get_window_item(win, form_library_control_id(lib, f, "name"));
  ASSERT_NOT_NULL(name);
  ASSERT_STR_EQUAL(name->title, "Ada");
  dialog_pull(win, &out, f->bindings, f->binding_count);
  ASSERT_STR_EQUAL(out.name, "Ada");
  ASSERT_EQUAL(out.age, 36);
  ASSERT_TRUE(out.active);
  destroy_window(win);
  form_library_free(lib);
  PASS();
}

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  TEST_START("Runtime form loader");
  test_env_init();
  DB_CLASS(db_schema_xml);
  g_db = create_database_with_schema("club", "db_schema_xml", &schema, "");
  register_database("club", g_db);
  test_rejects_non_orion();
  test_forms_and_rejections();
  test_controls();
  test_bindings();
  test_window_from_runtime_form();
  destroy_database(g_db);
  test_env_shutdown();
  TEST_END();
}
