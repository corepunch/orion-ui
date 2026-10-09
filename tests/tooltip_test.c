#include "test_framework.h"
#include "test_env.h"
#include <orion/commctl/commctl.h>
#include <orion/user/toolbar.h>

static window_t *find_tooltip(const char *text) {
  for (window_t *win = g_ui_runtime.windows; win; win = win->next)
    if ((win->flags & WINDOW_NOACTIVATE) && !strcmp(win->title, text)) return win;
  return NULL;
}

static result_t owner_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)wparam; (void)lparam;
  return msg == evCreate || msg == evPaint || msg == evDestroy;
}

static void test_immediate_control_tooltip(void) {
  TEST("tooltip: control text is visible immediately, remains anchored, and cancels");
  test_env_init();
  window_t *button = create_window("Test", WINDOW_NOTITLE, MAKERECT(100, 100, 80, 30), NULL, win_button, 0, NULL);
  show_window(button, true);
  tooltip_update(button, "Control hint", 110, 110);
  window_t *tip = find_tooltip("Control hint");
  ASSERT_NOT_NULL(tip);
  ASSERT_TRUE(window_has_state(tip, WINDOW_STATE_VISIBLE));
  irect16_t frame = tip->frame;
  tooltip_update(button, "Control hint", 120, 115);
  ASSERT_TRUE(!memcmp(&frame, &tip->frame, sizeof(frame)));
  tooltip_update(button, "Updated hint", 120, 115);
  ASSERT_STR_EQUAL(tip->title, "Updated hint");
  ASSERT_TRUE(window_has_state(tip, WINDOW_STATE_VISIBLE));
  tooltip_cancel();
  ASSERT_FALSE(window_has_state(tip, WINDOW_STATE_VISIBLE));
  test_env_shutdown();
  PASS();
}

static void test_immediate_toolbar_tooltip(void) {
  TEST("tooltip: disabled toolbar item shows immediately through its hidden host");
  test_env_init();
  window_t *owner = create_window("Toolbar", WINDOW_NOTITLE | WINDOW_TOOLBAR, MAKERECT(100, 100, 200, 80), NULL, owner_proc, 0, NULL);
  toolbar_item_t item = {.type = TOOLBAR_ITEM_BUTTON, .ident = 1, .state = TBSTATE_DISABLED, .tooltip = "Disabled hint"};
  send_message(owner, tbSetItems, 1, &item);
  show_window(owner, true);
  irect16_t rect;
  ASSERT_TRUE(send_message(owner, tbGetItemRect, 1, &rect));
  tooltip_update(owner->toolbar, item.tooltip, window_screen_x(owner->toolbar) + rect.x + 1,
                 window_screen_y(owner->toolbar) + rect.y + 1);
  window_t *tip = find_tooltip(item.tooltip);
  ASSERT_NOT_NULL(tip);
  ASSERT_TRUE(window_has_state(tip, WINDOW_STATE_VISIBLE));
  tooltip_update(NULL, NULL, 0, 0);
  ASSERT_FALSE(window_has_state(tip, WINDOW_STATE_VISIBLE));
  show_window(owner, false);
  tooltip_update(owner->toolbar, item.tooltip, 110, 110);
  ASSERT_FALSE(window_has_state(tip, WINDOW_STATE_VISIBLE));
  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("Immediate tooltips");
  if (!ui_init_graphics(UI_INIT_HIDDEN, "tooltip-test", 640, 480)) {
    fprintf(stderr, "Offscreen graphics unavailable\n");
    return 1;
  }
  test_immediate_control_tooltip();
  test_immediate_toolbar_tooltip();
  ui_shutdown_graphics();
  TEST_END();
}
