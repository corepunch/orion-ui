#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/commctl/commctl.h>

static result_t parent_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)wparam; (void)lparam;
  return msg == evCreate || msg == evDestroy;
}

static window_t *make_view(window_t *parent) {
  irect16_t fr = {0, 0, 240, 140};
  window_t *view = create_window("", WINDOW_NOTITLE, &fr, parent,
                                 win_reportview, 0, NULL);
  reportview_column_t title = {.title = "Title", .width = 0};
  reportview_column_t subtitle = {.title = "Subtitle", .width = 0};
  send_message(view, RVM_ADDCOLUMN, 0, &title);
  send_message(view, RVM_ADDCOLUMN, 0, &subtitle);
  return view;
}

static void test_default_columns_style(void) {
  TEST("reportview cell style: columns remain the default");
  test_env_init();
  window_t *parent = test_env_create_window("P", 0, 0, 240, 140, parent_proc, NULL);
  window_t *view = make_view(parent);
  ASSERT_EQUAL((int)send_message(view, RVM_GETCELLSTYLE, 0, NULL),
               REPORTVIEW_CELL_COLUMNS);
  ASSERT_TRUE(send_message(view, RVM_GETCOLUMNTITLESVISIBLE, 0, NULL));
  destroy_window(parent); test_env_shutdown(); PASS();
}

static void test_two_line_style(void) {
  TEST("reportview cell style: two-line title/subtitle rows");
  test_env_init();
  window_t *parent = test_env_create_window("P", 0, 0, 240, 140, parent_proc, NULL);
  window_t *view = make_view(parent);
  ASSERT_TRUE(send_message(view, RVM_SETCELLSTYLE, TABLEVIEW_CELL_TWO_LINE, NULL));
  ASSERT_EQUAL((int)send_message(view, RVM_GETCELLSTYLE, 0, NULL),
               REPORTVIEW_CELL_TWO_LINE);
  ASSERT_FALSE(send_message(view, RVM_GETCOLUMNTITLESVISIBLE, 0, NULL));
  reportview_item_t first = {.text = "Title one", .subitems = {"Subtitle one"},
                             .subitem_count = 1};
  reportview_item_t second = {.text = "Title two", .subitems = {"Subtitle two"},
                              .subitem_count = 1};
  send_message(view, RVM_ADDITEM, 0, &first);
  send_message(view, RVM_ADDITEM, 0, &second);
  ASSERT_EQUAL((int)send_message(view, RVM_HITTEST, MAKEDWORD(5, 1), NULL), 0);
  ASSERT_EQUAL((int)send_message(view, RVM_HITTEST,
                                 MAKEDWORD(5, REPORTVIEW_TWO_LINE_ENTRY_HEIGHT + 1), NULL), 1);
  destroy_window(parent); test_env_shutdown(); PASS();
}

int main(void) {
  TEST_START("reportview cell style tests");
  test_default_columns_style();
  test_two_line_style();
  TEST_END();
}
