// tests/palette_list_test.c — the pieces of a command palette: a report list
// whose rows carry section captions (grouped lists), a list that measures to
// all its rows (RVS_EX_FITROWS), and a text field that drives a list
// (edSetList): arrows move the list's selection, Enter opens its row until
// something new is typed.

#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/commctl/columnview.h>
#include <orion/commctl/commctl.h>

static int g_opened = -1, g_submitted = 0;

static result_t root_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)lparam;
  if (msg == evCreate || msg == evDestroy) return 1;
  if (msg == evCommand && HIWORD(wparam) == RVN_DBLCLK) { g_opened = (int)(uint16_t)LOWORD(wparam); return 1; }
  if (msg == evCommand && HIWORD(wparam) == edUpdate) { g_submitted++; return 1; }
  return msg == evCommand;
}

static window_t *make_list(window_t *parent, uint32_t id) {
  irect16_t fr = {0, 40, 300, 160};
  window_t *list = create_window("list", WINDOW_NOTITLE | WINDOW_NOFILL, &fr, parent, win_reportview, 0, NULL);
  list->id = id;
  send_message(list, RVM_SETVIEWMODE, RVM_VIEW_REPORT, NULL);
  send_message(list, RVM_SETCOLUMNTITLESVISIBLE, false, NULL);
  const char *rows[][2] = {{"Commands", "New Document"}, {NULL, "New Payroll Document"}, {"Suggestions", "Documents not posted: 2"}};
  for (int i = 0; i < 3; i++) {
    reportview_item_t item = {.text = rows[i][1], .icon = -1, .userdata = (uint32_t)i, .section = rows[i][0]};
    send_message(list, RVM_ADDITEM, 0, &item);
  }
  return list;
}

static void test_sections_and_fit(void) {
  TEST("ReportView: section captions take a row above their group; fit-rows measures to every row");
  test_env_init();
  window_t *root = test_env_create_window("P", 0, 0, 320, 240, root_proc, NULL);
  window_t *list = make_list(root, 7);
  layout_measure_t m = {0};
  send_message(list, evMeasure, 0, &m);
  int one_row = m.desired_h;                 // a list measures to one row by default
  send_message(list, RVM_SETEXTENDEDSTYLE, RVS_EX_FITROWS, (void *)(uintptr_t)RVS_EX_FITROWS);
  m = (layout_measure_t){0};
  send_message(list, evMeasure, 0, &m);
  ASSERT_EQUAL(m.desired_h, one_row * 5);    // 3 rows and 2 captions
  // A click on the second caption hits no row; one below it hits the third row.
  irect16_t content = window_scroll_content_rect(list);
  int second_caption_y = content.y + one_row * 3 + one_row / 2;
  send_message(list, evLeftButtonDown, MAKEDWORD(20, second_caption_y), NULL);
  send_message(list, evLeftButtonUp, MAKEDWORD(20, second_caption_y), NULL);
  ASSERT_EQUAL((int)send_message(list, RVM_GETSELECTION, 0, NULL), -1);
  send_message(list, evLeftButtonDown, MAKEDWORD(20, second_caption_y + one_row), NULL);
  send_message(list, evLeftButtonUp, MAKEDWORD(20, second_caption_y + one_row), NULL);
  ASSERT_EQUAL((int)send_message(list, RVM_GETSELECTION, 0, NULL), 2);
  destroy_window(root);
  test_env_shutdown();
  PASS();
}

static void test_field_drives_list(void) {
  TEST("TextBox edSetList: arrows move the list, Enter opens its row until something new is typed");
  test_env_init();
  g_opened = -1; g_submitted = 0;
  window_t *root = test_env_create_window("P", 0, 0, 320, 240, root_proc, NULL);
  window_t *list = make_list(root, 7);
  irect16_t fr = {0, 0, 300, 24};
  window_t *field = create_window("", 0, &fr, root, win_textedit, 0, NULL);
  field->id = 8;
  ASSERT_TRUE(send_message(field, edSetList, list->id, NULL));
  ASSERT_EQUAL((uint32_t)send_message(field, edGetList, 0, NULL), list->id);
  set_focus(field);
  send_message(list, RVM_SETSELECTION, 0, NULL);
  send_message(field, evKeyDown, AX_KEY_DOWNARROW, NULL);
  ASSERT_EQUAL((int)send_message(list, RVM_GETSELECTION, 0, NULL), 1);
  ASSERT(g_ui_runtime.focused == field, "the field keeps the focus");
  send_message(field, evKeyDown, AX_KEY_ENTER, NULL);
  ASSERT_EQUAL(g_opened, 1);
  // Typing makes Enter submit the text instead, then the list is reachable again.
  g_opened = -1;
  send_message(field, evTextInput, 0, "x");
  send_message(field, evKeyDown, AX_KEY_ENTER, NULL);
  ASSERT_EQUAL(g_submitted, 1);
  ASSERT_EQUAL(g_opened, -1);
  send_message(field, evKeyDown, AX_KEY_ENTER, NULL);
  ASSERT_EQUAL(g_opened, 1);
  destroy_window(root);
  test_env_shutdown();
  PASS();
}

int main(int argc, char *argv[]) {
  (void)argc; (void)argv;
  TEST_START("palette list: sections, fit rows and a driving field");
  test_sections_and_fit();
  test_field_drives_list();
  TEST_END();
}
