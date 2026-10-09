#include "test_framework.h"
#include "test_env.h"
#include <orion/commctl/commctl.h>
#include <orion/user/toolbar.h>
#include <orion/user/dock.h>
#include <orion/user/gl_compat.h>

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
  window_t *owner = create_window("Owner", WINDOW_NOTITLE | WINDOW_TOOLBAR, MAKERECT(0, 0, 400, 300), NULL, owner_proc, 0, NULL);
  show_window(owner, true);
  window_t *button = create_window("Test", WINDOW_NOTITLE, MAKERECT(100, 100, 80, 30), owner, win_button, 0, NULL);
  show_window(button, true);
  tooltip_update(button, "Control hint", 110, 110);
  window_t *tip = find_tooltip("Control hint");
  ASSERT_NOT_NULL(tip);
  ASSERT_TRUE(window_has_state(tip, WINDOW_STATE_VISIBLE));
  ASSERT_TRUE(tip->frame.y + tip->frame.h - get_theme()->tooltip_shadow_size <= window_screen_y(button));
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

static void test_toolbar_tooltip_sides(void) {
  TEST("tooltip: all four toolbar edges point inward, update after docking, and flip at screen boundaries");
  test_env_init();
  window_t *owner = create_window("Owner", WINDOW_NOTITLE, MAKERECT(150, 100, 300, 250), NULL, owner_proc, 0, NULL);
  show_window(owner, true);
  window_t *bar = create_docked_toolbar(owner, TOOLBAR_DOCK_LEFT, owner_proc);
  toolbar_item_t item = {.type = TOOLBAR_ITEM_BUTTON, .ident = 1, .tooltip = "Edge hint"};
  send_message(bar, tbSetItems, 1, &item);
  show_window(bar, true);
  const dock_side_t sides[] = {DOCK_LEFT, DOCK_RIGHT, DOCK_TOP, DOCK_BOTTOM};
  const theme_t *theme = get_theme();
  int pad = theme->tooltip_shadow_size, gap = theme->tooltip_gap;
  for (int i = 0; i < ARRAY_LEN(sides); i++) {
    ASSERT_TRUE(dock_set_side(bar, sides[i]));
    irect16_t item_rect;
    ASSERT_TRUE(send_message(bar, tbGetItemRect, 1, &item_rect));
    window_t *host = bar->toolbar;
    int x = window_screen_x(host), y = window_screen_y(host);
    int band_h = toolbar_effective_item_height(bar) + 2 * toolbar_effective_padding(bar);
    tooltip_update(host, item.tooltip, x + item_rect.x + 1, y + item_rect.y + 1);
    window_t *tip = find_tooltip(item.tooltip);
    ASSERT_NOT_NULL(tip);
    ASSERT_TRUE(window_has_state(tip, WINDOW_STATE_VISIBLE));
    switch (sides[i]) {
      case DOCK_LEFT:   ASSERT_TRUE(tip->frame.x + pad >= x + bar->frame.w + gap); break;
      case DOCK_RIGHT:  ASSERT_TRUE(tip->frame.x + tip->frame.w - pad <= x - gap); break;
      case DOCK_TOP:    ASSERT_TRUE(tip->frame.y + pad >= y + band_h + gap); break;
      case DOCK_BOTTOM: ASSERT_TRUE(tip->frame.y + tip->frame.h - pad <= y - gap); break;
      default: break;
    }
  }
  ASSERT_TRUE(dock_set_side(bar, DOCK_LEFT));
  owner->frame.x = ui_get_system_metrics(kSystemMetricScreenWidth) - bar->frame.w;
  irect16_t item_rect;
  ASSERT_TRUE(send_message(bar, tbGetItemRect, 1, &item_rect));
  int x = window_screen_x(bar->toolbar), y = window_screen_y(bar->toolbar);
  tooltip_update(bar->toolbar, item.tooltip, x + item_rect.x + 1, y + item_rect.y + 1);
  window_t *tip = find_tooltip(item.tooltip);
  ASSERT_NOT_NULL(tip);
  ASSERT_TRUE(tip->frame.x + tip->frame.w - pad <= x - gap);
  test_env_shutdown();
  PASS();
}

static void test_tooltip_bubble_tails(void) {
  TEST("tooltip: all four arrows share a filled silhouette with transparent space beside the tail");
  uint32_t fbo = 0, texture = 0;
  int w = 0, h = 0;
  ASSERT_TRUE(R_EnsureWindowTarget(&fbo, &texture, &w, &h, 64, 48));
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glViewport(0, 0, 64, 48);
  set_projection(0, 0, 64, 48);
  glDisable(GL_SCISSOR_TEST);
  glClearColor(0, 0, 0, 0);
  const tooltip_tail_side_t sides[] = {TOOLTIP_TAIL_TOP, TOOLTIP_TAIL_BOTTOM, TOOLTIP_TAIL_LEFT, TOOLTIP_TAIL_RIGHT};
  const ipoint16_t filled[] = {{32, 10}, {32, 38}, {10, 24}, {54, 24}};
  const ipoint16_t empty[] = {{16, 10}, {16, 38}, {10, 12}, {54, 12}};
  bool ok = true;
  for (int i = 0; i < ARRAY_LEN(sides); i++) {
    glClear(GL_COLOR_BUFFER_BIT);
    render_tooltip_bubble(R(8, 8, 48, 32), (isize16_t){48, 32}, 4, 8, i < 2 ? 24 : 16, 0, sides[i], 0xffffffff);
    uint8_t tail[4], outside[4], body[4];
    glReadPixels(filled[i].x, 47 - filled[i].y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, tail);
    glReadPixels(empty[i].x, 47 - empty[i].y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, outside);
    glReadPixels(32, 23, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, body);
    if (tail[3] <= 200 || outside[3] != 0 || body[3] != 255)
      fprintf(stderr, "[tooltip-test] side=%d tail=%u outside=%u body=%u\n", sides[i], tail[3], outside[3], body[3]);
    ok &= tail[3] > 200 && outside[3] == 0 && body[3] == 255;
  }
  ok &= glGetError() == GL_NO_ERROR;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, 640, 480);
  R_DestroyWindowTarget(&fbo, &texture, &w, &h);
  ASSERT_TRUE(ok);
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
  test_toolbar_tooltip_sides();
  test_tooltip_bubble_tails();
  ui_shutdown_graphics();
  TEST_END();
}
