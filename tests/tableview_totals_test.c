// tableview_totals_test.c — TableView presentation for accounting lists:
// status icons chosen by record state (<RowIcon>) and a totals row under the
// rows (total="sum") that leaves out rows repeating their parent (totals-skip).

#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/commctl/commctl.h>
#include <orion/commctl/columnview_internal.h>

enum { TABLE_ROWS = 1 };

typedef struct { int id; char name[32]; double amount; bool sub; bool posted; } row_t;

static const db_field_meta_t row_meta[] = {
  { "id", DB_TYPE_INT, offsetof(row_t, id), 0, 0 },          { "name", DB_TYPE_STRING, offsetof(row_t, name), 32, 0 },
  { "amount", DB_TYPE_DOUBLE, offsetof(row_t, amount), 0, 2 }, { "sub", DB_TYPE_BOOL, offsetof(row_t, sub), 0, 0 },
  { "posted", DB_TYPE_BOOL, offsetof(row_t, posted), 0, 0 },
};
static const db_field_schema_t row_fields[] = {
  { "id", DB_TYPE_INT, 0, true, NULL, NULL },        { "name", DB_TYPE_STRING, 32, false, NULL, NULL },
  { "amount", DB_TYPE_DOUBLE, 0, false, NULL, NULL }, { "sub", DB_TYPE_BOOL, 0, false, NULL, NULL },
  { "posted", DB_TYPE_BOOL, 0, false, NULL, NULL },
};
static const db_table_schema_t tables[] = {
  { TABLE_ROWS, "rows", NULL, row_fields, 5, NULL, 0, sizeof(row_t), row_meta, 5, false },
};
static const db_schema_def_t schema = { "totals", "db_schema_xml", NULL, tables, 1 };

static result_t parent_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)wparam; (void)lparam;
  return msg == evCreate || msg == evDestroy;
}

static void test_totals_and_row_icons(void) {
  TEST("TableView: totals skip subaccount rows; row icons follow record state");
  test_env_init();
  DB_CLASS(db_schema_xml);
  database_t *db = create_database_with_schema("totals", "db_schema_xml", &schema, "");  // in memory
  send_db_message(db, dbInsert, TABLE_ROWS, &(row_t){ .name = "50", .amount = 100, .posted = true });
  send_db_message(db, dbInsert, TABLE_ROWS, &(row_t){ .name = "50.01", .amount = 100, .sub = true });
  send_db_message(db, dbInsert, TABLE_ROWS, &(row_t){ .name = "51", .amount = 20.5 });

  window_t *root = test_env_create_window("Test", 0, 0, 300, 200, parent_proc, NULL);
  static const char *fields[] = { "name", "amount", NULL }, *titles[] = { "Account", "Amount", NULL };
  static const bool totals[] = { false, true };
  static const tableview_row_icon_t icons[] = { { "posted", NULL, "posted-icon", brTextSuccess }, { NULL, NULL, "draft-icon", -1 } };
  tableview_params_t params = { .db = db, .table_id = TABLE_ROWS, .field_names = fields, .column_titles = titles,
                                .row_icons = icons, .row_icon_count = 2, .column_totals = totals, .totals_skip = "sub" };
  form_ctrl_def_t def = { .class_name = "TableView", .lparam = &params };
  window_t *tv = create_window("", 0, MAKERECT(0, 0, 300, 200), root, win_tableview, 0, &def);
  ASSERT_NOT_NULL(tv);
  reportview_data_t *rv = (reportview_data_t *)tv->userdata2;
  ASSERT_EQUAL((int)rv->count, 3);

  char expected[64];
  ui_format_number(120.5, 2, expected, sizeof(expected));
  ASSERT_TRUE(rv->footer_visible);
  ASSERT_STR_EQUAL(rv->footer[0], ui_tr("Totals"));
  ASSERT_STR_EQUAL(rv->footer[1], expected);

  ASSERT_STR_EQUAL(rv->items[0].icon_name, "posted-icon");
  ASSERT_EQUAL(rv->items[0].icon_color, get_sys_color(brTextSuccess));
  ASSERT_STR_EQUAL(rv->items[1].icon_name, "draft-icon");
  ASSERT_EQUAL(rv->items[1].icon_color, 0u);

  // The totals row is pinned: the rows scroll above it.
  irect16_t client = get_client_rect(tv), viewport = window_scroll_viewport(tv);
  ASSERT_EQUAL(viewport.y + viewport.h, client.h - rv_report_footer_height(rv));
  ASSERT_EQUAL((int)send_message(tv, RVM_HITTEST, MAKEDWORD(10, client.h - 2), NULL), -1);

  send_message(tv, RVM_SETFOOTER, 0, NULL);
  ASSERT_FALSE(rv->footer_visible);
  ASSERT_EQUAL(rv_report_footer_height(rv), 0);

  destroy_window(root);
  destroy_database(db);
  test_env_shutdown();
  PASS();
}

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  TEST_START("tableview_totals");
  test_totals_and_row_icons();
  TEST_END();
}
