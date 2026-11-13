#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/commctl/commctl.h>

static int g_check_count, g_check_row = -1;

static result_t parent_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)lparam;
  if (msg == evCreate || msg == evDestroy) return true;
  if (msg == evCommand && HIWORD(wparam) == RVN_ITEMCHECK) {
    g_check_count++;
    g_check_row = (int)(uint16_t)LOWORD(wparam);
    return true;
  }
  return false;
}

static window_t *make_view(window_t *parent) {
  irect16_t fr = {0, 0, 240, 120};
  window_t *view = create_window("", WINDOW_NOTITLE, &fr, parent,
                                 win_reportview, 0, NULL);
  reportview_column_t col = {.title = "Files", .width = 0};
  send_message(view, RVM_ADDCOLUMN, 0, &col);
  send_message(view, RVM_SETEXTENDEDSTYLE, RVS_EX_CHECKBOXES,
               (void *)(uintptr_t)RVS_EX_CHECKBOXES);
  reportview_item_t item = {.text = "one.c", .state = RV_INDEXTOSTATEIMAGEMASK(1)};
  send_message(view, RVM_ADDITEM, 0, &item);
  return view;
}

static void test_state_api(void) {
  TEST("reportview checkbox: WinAPI-style item state get/set");
  test_env_init();
  window_t *parent = test_env_create_window("P", 0, 0, 240, 120, parent_proc, NULL);
  window_t *view = make_view(parent);
  ASSERT_FALSE(ReportView_GetCheckState(view, 0));
  ReportView_SetCheckState(view, 0, true);
  ASSERT_TRUE(ReportView_GetCheckState(view, 0));
  ASSERT_EQUAL(g_check_count, 0);
  destroy_window(parent); test_env_shutdown(); PASS();
}

static void test_mouse_and_keyboard_notify(void) {
  TEST("reportview checkbox: mouse and Space toggle and notify");
  test_env_init(); g_check_count = 0; g_check_row = -1;
  window_t *parent = test_env_create_window("P", 0, 0, 240, 120, parent_proc, NULL);
  window_t *view = make_view(parent);
  send_message(view, evLeftButtonDown,
               MAKEDWORD(5, COLUMNVIEW_HEADER_HEIGHT + 2), NULL);
  ASSERT_TRUE(ReportView_GetCheckState(view, 0));
  ASSERT_EQUAL(g_check_count, 1); ASSERT_EQUAL(g_check_row, 0);
  send_message(view, evKeyDown, AX_KEY_SPACE, NULL);
  ASSERT_FALSE(ReportView_GetCheckState(view, 0));
  ASSERT_EQUAL(g_check_count, 2);
  destroy_window(parent); test_env_shutdown(); PASS();
}

int main(void) {
  TEST_START("reportview checkbox tests");
  test_state_api();
  test_mouse_and_keyboard_notify();
  TEST_END();
}
