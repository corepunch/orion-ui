#include "test_framework.h"
#include "test_env.h"

static result_t pane_test_page(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)wparam; (void)lparam;
  return msg == evCreate || msg == evDestroy;
}

static window_t *pane_test_tab(window_t *pane, const char *title) {
  window_t *page = create_window(title, WINDOW_NOTITLE, MAKERECT(0, 0, 100, 100), pane, pane_test_page, 0, NULL);
  create_window("draft", WINDOW_NOTITLE, MAKERECT(0, 0, 80, 24), page, "TextEdit", 0, NULL);
  send_message(pane, tcSetSelection, send_message(pane, tcGetCount, 0, NULL) - 1, NULL);
  return page;
}

static window_t *pane_test_host(void) {
  window_t *host = create_window("", WINDOW_NOTITLE, MAKERECT(30, 40, 900, 600), NULL, "PaneHost", 0, NULL);
  create_window("", WINDOW_NOTITLE, MAKERECT(0, 0, 900, 600), host, "TabView", 0, NULL);
  send_message(host, evResize, 0, NULL);
  return host;
}

static void pane_mouse(window_t *win, int message, ipoint16_t pt) {
  pt = window_map_point(win, NULL, pt);
  ui_event_t event = {.message = message, .x = pt.x * UI_WINDOW_SCALE, .y = pt.y * UI_WINDOW_SCALE};
  dispatch_message(&event);
}

static void pane_drag(window_t *source, int index, window_t *target, ipoint16_t pt) {
  irect16_t tab; send_message(source, tcGetTabRect, index, &tab);
  pane_mouse(source, kEventLeftButtonDown, (ipoint16_t){tab.x + 10, 8});
  pane_mouse(target, kEventMouseMoved, pt);
}

static void test_pane_moves_and_collapses(void) {
  TEST("PaneHost moves existing tab windows, remembers focus and collapses empty branches");
  test_env_init();
  window_t *host = pane_test_host(), *left = pane_host_pane(host, 0);
  window_t *a = pane_test_tab(left, "A"), *b = pane_test_tab(left, "B");
  window_t *right = pane_host_split(host, left, PANE_RIGHT);
  ASSERT_NOT_NULL(right);
  window_t *c = pane_test_tab(right, "C");
  window_t *focus = create_window("saved", WINDOW_NOTITLE, MAKERECT(0, 30, 80, 24), b, "TextEdit", 0, NULL);
  set_focus(focus);
  ASSERT_TRUE(pane_host_move_tab(host, b, right, PANE_CENTER, 0));
  ASSERT_EQUAL(b->parent, right); ASSERT_EQUAL(g_ui_runtime.focused, focus);
  ASSERT_EQUAL(send_message(left, tcGetPage, 0, NULL), (intptr_t)a);
  ASSERT_EQUAL(send_message(right, tcGetPage, 0, NULL), (intptr_t)b);
  ASSERT_TRUE(send_message(right, tcSetSelection, 1, NULL));
  ASSERT_TRUE(send_message(right, tcSetSelection, 0, NULL));
  ASSERT_EQUAL(g_ui_runtime.focused, focus);
  ASSERT_TRUE(pane_host_move_tab(host, b, right, PANE_CENTER, -1));
  ASSERT_EQUAL(send_message(right, tcGetPage, 0, NULL), (intptr_t)c);
  ASSERT_EQUAL(send_message(right, tcGetPage, 1, NULL), (intptr_t)b);
  ASSERT_TRUE(pane_host_move_tab(host, b, right, PANE_BOTTOM, -1));
  window_t *bottom = b->parent;
  ASSERT_EQUAL(pane_host_count(host), 3);
  ASSERT_EQUAL(splitview_get_right(right->parent), bottom);
  ASSERT_TRUE(bottom->frame.y > right->frame.y);
  ASSERT_TRUE(pane_host_move_tab(host, a, bottom, PANE_CENTER, -1));
  ASSERT_EQUAL(pane_host_count(host), 2);
  ASSERT_EQUAL(pane_host_pane(host, 0), right);
  ASSERT_EQUAL(pane_host_pane(host, 1), bottom);
  ASSERT_TRUE(pane_host_close_tab(host, c));
  ASSERT_EQUAL(pane_host_count(host), 1);
  ASSERT_EQUAL(bottom->parent, host);
  ASSERT_EQUAL(bottom->frame.w, 900); ASSERT_EQUAL(bottom->frame.h, 600);
  ASSERT_FALSE(window_reparent(bottom, b, NULL));
  ASSERT_TRUE(pane_host_close_tab(host, a));
  ASSERT_EQUAL(g_ui_runtime.focused, focus);
  ASSERT_TRUE(pane_host_move_tab(host, b, bottom, PANE_CENTER, -1));
  ASSERT_FALSE(pane_host_move_tab(host, b, bottom, PANE_LEFT, -1));
  host->frame.w = 150; host->frame.h = 100;
  send_message(host, evResize, 0, NULL);
  ASSERT_NULL(pane_host_split(host, bottom, PANE_RIGHT));
  ASSERT_EQUAL(pane_host_count(host), 1);
  ASSERT_TRUE(pane_host_close_tab(host, b));
  ASSERT_EQUAL(pane_host_count(host), 1);
  ASSERT_EQUAL(send_message(bottom, tcGetCount, 0, NULL), 0);
  destroy_window(host); test_env_shutdown(); PASS();
}

static void test_pane_drag_edges(void) {
  TEST("Tab drag previews and creates all four split directions through the input router");
  for (pane_edge_t edge = PANE_LEFT; edge <= PANE_BOTTOM; edge++) {
    test_env_init();
    window_t *host = pane_test_host(), *source = pane_host_pane(host, 0);
    pane_test_tab(source, "Keep"); window_t *page = pane_test_tab(source, "Move");
    window_t *target = pane_host_split(host, source, PANE_RIGHT);
    pane_test_tab(target, "Target");
    irect16_t r = get_client_rect(target);
    ipoint16_t point = {r.w / 2, r.h / 2};
    if (edge == PANE_LEFT) point.x = 4;
    if (edge == PANE_RIGHT) point.x = r.w - 4;
    if (edge == PANE_TOP) point.y = TAB_CONTROL_HEIGHT + 4;
    if (edge == PANE_BOTTOM) point.y = r.h - 4;
    pane_drag(source, 1, target, point);
    ASSERT_EQUAL(g_ui_runtime.captured, host);
    pane_drop_t preview;
    ASSERT_TRUE(pane_host_get_preview(host, &preview));
    ASSERT_EQUAL(preview.pane, target); ASSERT_EQUAL(preview.edge, edge);
    ASSERT_EQUAL(pane_host_count(host), 2);
    pane_mouse(target, kEventLeftButtonUp, point);
    ASSERT_NULL(g_ui_runtime.captured);
    ASSERT_FALSE(pane_host_get_preview(host, &preview));
    ASSERT_EQUAL(pane_host_count(host), 3);
    ASSERT_NOT_EQUAL(page->parent, source); ASSERT_NOT_EQUAL(page->parent, target);
    ASSERT_EQUAL(g_ui_runtime.focused, page->children);
    ipoint16_t origin = window_map_point(page->parent, host, (ipoint16_t){0, 0});
    ASSERT_TRUE(abs(origin.x - preview.rect.x) <= 1 && abs(origin.y - preview.rect.y) <= 1);
    ASSERT_TRUE(abs(page->parent->frame.w - preview.rect.w) <= 1 && abs(page->parent->frame.h - preview.rect.h) <= 1);
    destroy_window(host); test_env_shutdown();
  }
  PASS();
}

static void test_pane_drag_cancel_and_insert(void) {
  TEST("Tab dragging cancels safely and inserts at TabBar slots without recreating pages");
  test_env_init();
  window_t *host = pane_test_host(), *source = pane_host_pane(host, 0);
  window_t *a = pane_test_tab(source, "A"), *b = pane_test_tab(source, "B");
  window_t *target = pane_host_split(host, source, PANE_RIGHT);
  pane_test_tab(target, "Target");
  ipoint16_t center = {target->frame.w / 2, 200};
  pane_drag(source, 1, target, center);
  ui_event_t escape = {.message = kEventKeyDown, .keyCode = AX_KEY_ESCAPE};
  dispatch_message(&escape);
  pane_drop_t preview;
  ASSERT_NULL(g_ui_runtime.captured);
  ASSERT_FALSE(pane_host_get_preview(host, &preview));
  ASSERT_EQUAL(b->parent, source);
  pane_drag(source, 1, target, center);
  pane_mouse(host, kEventLeftButtonUp, (ipoint16_t){-10, -10});
  ASSERT_EQUAL(b->parent, source);
  pane_drag(source, 1, target, center);
  send_message(host, evResize, 0, NULL);
  ASSERT_NULL(g_ui_runtime.captured);
  ASSERT_FALSE(pane_host_get_preview(host, &preview));
  pane_drag(source, 1, target, center);
  send_message(host, evPointerCancel, 0, NULL);
  ASSERT_EQUAL(b->parent, source);
  pane_drag(source, 1, target, (ipoint16_t){4, 8});
  ASSERT_TRUE(pane_host_get_preview(host, &preview));
  ASSERT_EQUAL(preview.edge, PANE_CENTER); ASSERT_EQUAL(preview.index, 0);
  ASSERT_EQUAL(preview.rect.w, 3);
  pane_mouse(target, kEventLeftButtonUp, (ipoint16_t){4, 8});
  ASSERT_EQUAL(send_message(target, tcGetPage, 0, NULL), (intptr_t)b);
  pane_drag(source, 0, target, center);
  ASSERT_TRUE(send_message(source, tcDeletePage, 0, NULL));
  ASSERT_NULL(g_ui_runtime.captured);
  ASSERT_FALSE(pane_host_get_preview(host, &preview));
  (void)a;
  destroy_window(host); test_env_shutdown(); PASS();
}

static void test_pane_coordinate_mapping(void) {
  TEST("Window point mapping includes nested frames and scroll offsets");
  test_env_init();
  window_t *root = create_window("", WINDOW_NOTITLE, MAKERECT(20, 30, 900, 600), NULL, pane_test_page, 0, NULL);
  window_t *parent = create_window("", WINDOW_NOTITLE, MAKERECT(10, 40, 400, 300), root, pane_test_page, 0, NULL);
  window_t *child = create_window("", WINDOW_NOTITLE, MAKERECT(50, 60, 200, 100), parent, pane_test_page, 0, NULL);
  root->hscroll.pos = 2; root->vscroll.pos = 3;
  parent->hscroll.pos = 4; parent->vscroll.pos = 5;
  child->hscroll.pos = 6; child->vscroll.pos = 7;
  ipoint16_t p = window_map_point(child, root, (ipoint16_t){10, 20});
  ASSERT_EQUAL(p.x, 60); ASSERT_EQUAL(p.y, 108);
  p = window_map_point(root, child, p);
  ASSERT_EQUAL(p.x, 10); ASSERT_EQUAL(p.y, 20);
  p = window_map_point(child, NULL, p);
  ASSERT_EQUAL(p.x, 78); ASSERT_EQUAL(p.y, 135);
  destroy_window(root); test_env_shutdown(); PASS();
}

static void test_hidden_panes_are_not_drop_targets(void) {
  TEST("PaneHost ignores collapsed panes for active focus and snap targets");
  test_env_init();
  window_t *host = pane_test_host(), *hidden = pane_host_pane(host, 0);
  window_t *hidden_page = pane_test_tab(hidden, "Collapsed");
  window_t *visible = pane_host_split(host, hidden, PANE_RIGHT);
  pane_test_tab(visible, "A"); pane_test_tab(visible, "B");
  set_focus(hidden_page->children);
  window_set_state(hidden, WINDOW_STATE_VISIBLE, false);
  send_message(host, evResize, 0, NULL);
  ASSERT_EQUAL(pane_host_active(host), visible);
  pane_drag(visible, 1, host, (ipoint16_t){200, 200});
  pane_drop_t preview;
  ASSERT_TRUE(pane_host_get_preview(host, &preview));
  ASSERT_EQUAL(preview.pane, visible);
  send_message(host, evPointerCancel, 0, NULL);
  destroy_window(host); test_env_shutdown(); PASS();
}

int main(void) {
  TEST_START("PaneHost");
  test_pane_moves_and_collapses();
  test_pane_drag_edges();
  test_pane_drag_cancel_and_insert();
  test_pane_coordinate_mapping();
  test_hidden_panes_are_not_drop_targets();
  TEST_END();
}
