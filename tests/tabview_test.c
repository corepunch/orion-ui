#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/commctl/commctl.h>

static int g_tab_notifications, g_changes_clicks, g_history_clicks;
static window_t *g_changes_page, *g_history_page;
static result_t host_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)lparam;
  if (msg == evCreate || msg == evDestroy) return true;
  if (msg == evCommand && HIWORD(wparam) == tcnSelChange) { g_tab_notifications++; return true; }
  return false;
}

static result_t page_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)wparam; (void)lparam;
  if (msg == evLeftButtonDown) {
    if (win == g_changes_page) g_changes_clicks++;
    if (win == g_history_page) g_history_clicks++;
    return true;
  }
  return msg == evCreate || msg == evDestroy || msg == evArrange || msg == evPaint;
}

static void dispatch_mouse(int x, int y) {
  ui_event_t event = {.message = kEventLeftButtonDown,
                      .x = (uint16_t)(x * UI_WINDOW_SCALE),
                      .y = (uint16_t)(y * UI_WINDOW_SCALE)};
  dispatch_message(&event);
}

void test_tabview_selects_and_arranges_pages(void) {
  TEST("TabView selects one direct child page");
  test_env_init(); register_commctl_classes();
  window_t *host = create_window("Host", WINDOW_NOTITLE, MAKERECT(0, 0, 320, 200),
                                 NULL, host_proc, 0, NULL);
  window_t *tabs = create_window("", WINDOW_NOTITLE, MAKERECT(0, 0, 320, 200),
                                 host, "TabView", 0, NULL);
  tabs->id = 77;
  window_t *changes = create_window("Changes", WINDOW_NOTITLE, MAKERECT(0, 0, 1, 1),
                                    tabs, page_proc, 0, NULL);
  window_t *history = create_window("History", WINDOW_NOTITLE, MAKERECT(0, 0, 1, 1),
                                    tabs, page_proc, 0, NULL);
  g_changes_page = changes; g_history_page = history;
  layout_arrange_t a = {R(0, 0, 320, 200)}; send_message(tabs, evArrange, 0, &a);
  ASSERT_EQUAL(send_message(tabs, tcGetSelection, 0, NULL), 0);
  ASSERT_TRUE(window_has_state(changes, WINDOW_STATE_VISIBLE));
  ASSERT_FALSE(window_has_state(history, WINDOW_STATE_VISIBLE));
  ASSERT_EQUAL(changes->frame.y, TAB_CONTROL_HEIGHT);
  g_changes_clicks = g_history_clicks = 0;
  dispatch_mouse(20, TAB_CONTROL_HEIGHT + 20);
  ASSERT(g_changes_clicks == 1, "visible Changes page did not receive click");
  ASSERT(g_history_clicks == 0, "hidden History page received click");

  g_tab_notifications = 0;
  send_message(tabs, evLeftButtonDown,
               MAKEDWORD((uint16_t)(2 + 48 + 1 + 4), (uint16_t)4), NULL);
  ASSERT_EQUAL(send_message(tabs, tcGetSelection, 0, NULL), 1);
  ASSERT_FALSE(window_has_state(changes, WINDOW_STATE_VISIBLE));
  ASSERT_TRUE(window_has_state(history, WINDOW_STATE_VISIBLE));
  ASSERT_EQUAL(g_tab_notifications, 1);
  dispatch_mouse(20, TAB_CONTROL_HEIGHT + 20);
  ASSERT(g_changes_clicks == 1, "hidden Changes page received click");
  ASSERT(g_history_clicks == 1, "visible History page did not receive click");
  destroy_window(host); test_env_shutdown(); PASS();
}

void test_tabview_sidebar_lists_tabs_vertically(void) {
  TEST("TabView sidebar style lists tabs down the left edge");
  test_env_init(); register_commctl_classes();
  window_t *host = create_window("Host", WINDOW_NOTITLE, MAKERECT(0, 0, 320, 200),
                                 NULL, host_proc, 0, NULL);
  window_t *tabs = create_window("", WINDOW_NOTITLE, MAKERECT(0, 0, 320, 200),
                                 host, "TabView", 0, NULL);
  window_t *a = create_window("Changes", WINDOW_NOTITLE, MAKERECT(0, 0, 1, 1), tabs, page_proc, 0, NULL);
  window_t *b = create_window("History", WINDOW_NOTITLE, MAKERECT(0, 0, 1, 1), tabs, page_proc, 0, NULL);
  ASSERT_TRUE(send_message(tabs, tcSetStyle, TAB_STYLE_SIDEBAR, NULL));
  irect16_t page = R(0, 0, 320, 200);
  ASSERT_TRUE(send_message(tabs, tcAdjustRect, 0, &page));
  ASSERT_TRUE(page.x > 0 && page.y == 0 && page.x + page.w == 320 && page.h == 200);
  ASSERT_EQUAL(a->frame.x, page.x);
  ASSERT_EQUAL(a->frame.y, 0);

  g_tab_notifications = 0;
  send_message(tabs, evLeftButtonDown, MAKEDWORD(10, 6 + TAB_SIDEBAR_ROW_HEIGHT + 4), NULL);
  ASSERT_EQUAL(send_message(tabs, tcGetSelection, 0, NULL), 1);
  ASSERT_TRUE(window_has_state(b, WINDOW_STATE_VISIBLE));
  ASSERT_FALSE(window_has_state(a, WINDOW_STATE_VISIBLE));
  ASSERT_EQUAL(g_tab_notifications, 1);
  ASSERT_TRUE(send_message(tabs, evKeyDown, AX_KEY_UPARROW, NULL));
  ASSERT_EQUAL(send_message(tabs, tcGetSelection, 0, NULL), 0);
  destroy_window(host); test_env_shutdown(); PASS();
}

int main(void) {
  TEST_START("TabView tests");
  test_tabview_selects_and_arranges_pages();
  test_tabview_sidebar_lists_tabs_vertically();
  TEST_END();
}
