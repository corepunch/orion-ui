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
  const form_def_t def = {.name = "Workspace", .width = 900, .height = 600,
    .flags = WINDOW_NOTITLE | WINDOW_AUTO_LAYOUT, .role = WINDOW_ROLE_PAGE};
  window_t *host = create_window_from_form(&def, 30, 40, NULL, pane_test_page, 0, NULL);
  create_window("", WINDOW_NOTITLE | WINDOW_FLEXSPACE, MAKERECT(0, 0, 900, 600), host, "TabView", 0, NULL);
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
  TEST("Page docking moves existing tab windows, remembers focus and collapses empty branches");
  test_env_init();
  window_t *host = pane_test_host(), *left = page_pane_at(host, 0);
  window_t *a = pane_test_tab(left, "A"), *b = pane_test_tab(left, "B");
  window_t *right = page_split_pane(host, left, PANE_RIGHT);
  ASSERT_NOT_NULL(right);
  window_t *c = pane_test_tab(right, "C");
  window_t *focus = create_window("saved", WINDOW_NOTITLE, MAKERECT(0, 30, 80, 24), b, "TextEdit", 0, NULL);
  set_focus(focus);
  ASSERT_TRUE(page_move_tab(host, b, right, PANE_CENTER, 0));
  ASSERT_EQUAL(b->parent, right); ASSERT_EQUAL(g_ui_runtime.focused, focus);
  ASSERT_EQUAL(send_message(left, tcGetPage, 0, NULL), (intptr_t)a);
  ASSERT_EQUAL(send_message(right, tcGetPage, 0, NULL), (intptr_t)b);
  ASSERT_TRUE(send_message(right, tcSetSelection, 1, NULL));
  ASSERT_TRUE(send_message(right, tcSetSelection, 0, NULL));
  ASSERT_EQUAL(g_ui_runtime.focused, focus);
  ASSERT_TRUE(page_move_tab(host, b, right, PANE_CENTER, -1));
  ASSERT_EQUAL(send_message(right, tcGetPage, 0, NULL), (intptr_t)c);
  ASSERT_EQUAL(send_message(right, tcGetPage, 1, NULL), (intptr_t)b);
  ASSERT_TRUE(page_move_tab(host, b, right, PANE_BOTTOM, -1));
  window_t *bottom = b->parent;
  ASSERT_EQUAL(page_pane_count(host), 3);
  ASSERT_EQUAL(splitview_get_right(right->parent), bottom);
  ASSERT_TRUE(bottom->frame.y > right->frame.y);
  ASSERT_TRUE(page_move_tab(host, a, bottom, PANE_CENTER, -1));
  ASSERT_EQUAL(page_pane_count(host), 2);
  ASSERT_EQUAL(page_pane_at(host, 0), right);
  ASSERT_EQUAL(page_pane_at(host, 1), bottom);
  ASSERT_TRUE(page_close_tab(host, c));
  ASSERT_EQUAL(page_pane_count(host), 1);
  ASSERT_EQUAL(bottom->parent, host);
  ASSERT_EQUAL(bottom->frame.w, 900); ASSERT_EQUAL(bottom->frame.h, 600);
  ASSERT_FALSE(window_reparent(bottom, b, NULL));
  ASSERT_TRUE(page_close_tab(host, a));
  ASSERT_EQUAL(g_ui_runtime.focused, focus);
  ASSERT_TRUE(page_move_tab(host, b, bottom, PANE_CENTER, -1));
  ASSERT_FALSE(page_move_tab(host, b, bottom, PANE_LEFT, -1));
  host->frame.w = 150; host->frame.h = 100;
  send_message(host, evResize, 0, NULL);
  ASSERT_NULL(page_split_pane(host, bottom, PANE_RIGHT));
  ASSERT_EQUAL(page_pane_count(host), 1);
  ASSERT_TRUE(page_close_tab(host, b));
  ASSERT_EQUAL(page_pane_count(host), 1);
  ASSERT_EQUAL(send_message(bottom, tcGetCount, 0, NULL), 0);
  destroy_window(host); test_env_shutdown(); PASS();
}

static void test_pane_drag_edges(void) {
  TEST("Tab drag previews and creates all four split directions through the input router");
  for (pane_edge_t edge = PANE_LEFT; edge <= PANE_BOTTOM; edge++) {
    test_env_init();
    window_t *host = pane_test_host(), *source = page_pane_at(host, 0);
    pane_test_tab(source, "Keep"); window_t *page = pane_test_tab(source, "Move");
    window_t *target = page_split_pane(host, source, PANE_RIGHT);
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
    ASSERT_TRUE(page_get_drop_preview(host, &preview));
    ASSERT_EQUAL(preview.pane, target); ASSERT_EQUAL(preview.edge, edge);
    ASSERT_EQUAL(page_pane_count(host), 2);
    pane_mouse(target, kEventLeftButtonUp, point);
    ASSERT_NULL(g_ui_runtime.captured);
    ASSERT_FALSE(page_get_drop_preview(host, &preview));
    ASSERT_EQUAL(page_pane_count(host), 3);
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
  window_t *host = pane_test_host(), *source = page_pane_at(host, 0);
  window_t *a = pane_test_tab(source, "A"), *b = pane_test_tab(source, "B");
  window_t *target = page_split_pane(host, source, PANE_RIGHT);
  pane_test_tab(target, "Target");
  ipoint16_t center = {target->frame.w / 2, 200};
  pane_drag(source, 1, target, center);
  ui_event_t escape = {.message = kEventKeyDown, .keyCode = AX_KEY_ESCAPE};
  dispatch_message(&escape);
  pane_drop_t preview;
  ASSERT_NULL(g_ui_runtime.captured);
  ASSERT_FALSE(page_get_drop_preview(host, &preview));
  ASSERT_EQUAL(b->parent, source);
  pane_drag(source, 1, target, center);
  pane_mouse(host, kEventLeftButtonUp, (ipoint16_t){-10, -10});
  ASSERT_EQUAL(b->parent, source);
  pane_drag(source, 1, target, center);
  send_message(host, evResize, 0, NULL);
  ASSERT_NULL(g_ui_runtime.captured);
  ASSERT_FALSE(page_get_drop_preview(host, &preview));
  pane_drag(source, 1, target, center);
  send_message(host, evPointerCancel, 0, NULL);
  ASSERT_EQUAL(b->parent, source);
  pane_drag(source, 1, target, (ipoint16_t){4, 8});
  ASSERT_TRUE(page_get_drop_preview(host, &preview));
  ASSERT_EQUAL(preview.edge, PANE_CENTER); ASSERT_EQUAL(preview.index, 0);
  ASSERT_EQUAL(preview.rect.w, 3);
  pane_mouse(target, kEventLeftButtonUp, (ipoint16_t){4, 8});
  ASSERT_EQUAL(send_message(target, tcGetPage, 0, NULL), (intptr_t)b);
  pane_drag(source, 0, target, center);
  ASSERT_TRUE(send_message(source, tcDeletePage, 0, NULL));
  ASSERT_NULL(g_ui_runtime.captured);
  ASSERT_FALSE(page_get_drop_preview(host, &preview));
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
  TEST("Page docking ignores collapsed panes for active focus and snap targets");
  test_env_init();
  window_t *host = pane_test_host(), *hidden = page_pane_at(host, 0);
  window_t *hidden_page = pane_test_tab(hidden, "Collapsed");
  window_t *visible = page_split_pane(host, hidden, PANE_RIGHT);
  pane_test_tab(visible, "A"); pane_test_tab(visible, "B");
  set_focus(hidden_page->children);
  window_set_state(hidden, WINDOW_STATE_VISIBLE, false);
  send_message(host, evResize, 0, NULL);
  ASSERT_EQUAL(page_active_pane(host), visible);
  pane_drag(visible, 1, host, (ipoint16_t){200, 200});
  pane_drop_t preview;
  ASSERT_TRUE(page_get_drop_preview(host, &preview));
  ASSERT_EQUAL(preview.pane, visible);
  send_message(host, evPointerCancel, 0, NULL);
  destroy_window(host); test_env_shutdown(); PASS();
}

static int docking_page_destroys;
typedef struct { int initial_panes, commands; } docking_app_state_t;

static result_t docking_app_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  docking_app_state_t *st = win->userdata;
  switch (msg) {
    case evCreate:
      st = allocate_window_data(win, sizeof(*st));
      if (st) st->initial_panes = page_pane_count(win);
      return st != NULL;
    case evCommand: st->commands++; return true;
    case evResize: return true;
    case evDestroy: docking_page_destroys++; free(st); win->userdata = NULL; return true;
    case evPaint: return false;
    default: return false;
  }
}

static void test_page_owns_layout_and_lifecycle(void) {
  TEST("Page owns docking alongside app state and command rows; activity switching cancels drags");
  test_env_init(); docking_page_destroys = 0;
  const form_ctrl_def_t controls[] = {
    {.class_name = "Label", .id = 500, .text = "Commands"},
    {.class_name = "TabView", .id = 501, .flags = WINDOW_FLEXSPACE},
  };
  const form_def_t page_form = {.name = "Workspace", .width = 800, .height = 500,
    .flags = WINDOW_NOTITLE | WINDOW_AUTO_LAYOUT, .role = WINDOW_ROLE_PAGE,
    .layout_spacing = 4, .padding = {8, 8, 8, 8}, .children = controls, .child_count = 2};
  const form_ctrl_def_t rail = {.class_name = "ActivityBar", .id = 600};
  const page_binding_t bindings[] = {{2100, 600, "first", &page_form, 0}, {2101, 600, "second", &page_form, 0}};
  const form_def_t host_form = {.name = "Host", .width = 900, .height = 600,
    .flags = WINDOW_NOTITLE | WINDOW_AUTO_LAYOUT, .role = WINDOW_ROLE_HOST,
    .children = &rail, .child_count = 1, .page_bindings = bindings, .page_binding_count = 2};
  const page_handler_t handlers[] = {{"first", docking_app_proc}, {"second", docking_app_proc}};
  window_t *host = create_window_from_form(&host_form, 0, 0, NULL, pane_test_page, 0, NULL);
  ASSERT_TRUE(page_host_init(host, &host_form, handlers, 2));
  window_t *first = page_host_page(host, 2100), *second = page_host_page(host, 2101);
  docking_app_state_t *app = first->userdata;
  ASSERT_NOT_NULL(app); ASSERT_EQUAL(app->initial_panes, 1);
  ASSERT_NOT_EQUAL((void *)first->pages->docking, (void *)app);
  window_t *source = page_pane_at(first, 0), *other = page_pane_at(second, 0);
  ASSERT_EQUAL(source->parent, first);
  pane_test_tab(source, "A"); window_t *b = pane_test_tab(source, "B");
  window_t *local = create_window("", WINDOW_NOTITLE, MAKERECT(0, 0, 200, 150), b, "TabView", 0, NULL);
  window_t *local_view = pane_test_tab(local, "Local");
  window_t *target = page_split_pane(first, source, PANE_RIGHT);
  pane_test_tab(target, "Target");
  window_t *label = get_window_item(first, 500), *tree = source->parent;
  ASSERT_TRUE(tree->frame.y >= label->frame.y + label->frame.h + 4);
  irect16_t before = tree->frame;
  ASSERT_FALSE(page_move_tab(first, b, other, PANE_CENTER, -1));
  ASSERT_FALSE(page_move_tab(first, local_view, target, PANE_CENTER, -1));
  ASSERT_EQUAL(page_pane_count(first), 2);
  ASSERT_EQUAL(b->parent, source);
  ipoint16_t center = {target->frame.w / 2, target->frame.h / 2};
  int commands = app->commands;
  pane_drag(source, 1, target, center);
  pane_drop_t preview;
  ASSERT_TRUE(page_get_drop_preview(first, &preview));
  ASSERT_EQUAL(g_ui_runtime.captured, first);
  ASSERT_EQUAL(app->commands, commands); // Page handles docking even if the app consumes commands.
  ASSERT_TRUE(page_host_select(host, 2101));
  ASSERT_NULL(g_ui_runtime.captured);
  ASSERT_FALSE(page_get_drop_preview(first, &preview));
  ASSERT_EQUAL(b->parent, source);
  ASSERT_EQUAL(page_pane_count(first), 2); ASSERT_EQUAL(page_pane_count(second), 1);
  ASSERT_TRUE(page_host_select(host, 2100));
  ASSERT_EQUAL(first->userdata, app);
  ASSERT_EQUAL(tree->frame.y, before.y); ASSERT_EQUAL(tree->frame.h, before.h);
  pane_drag(source, 1, target, center);
  ASSERT_TRUE(page_get_drop_preview(first, &preview));
  destroy_window(host);
  ASSERT_NULL(g_ui_runtime.captured); ASSERT_EQUAL(docking_page_destroys, 2);
  test_env_shutdown(); PASS();
}

int main(void) {
  TEST_START("Page docking");
  test_pane_moves_and_collapses();
  test_pane_drag_edges();
  test_pane_drag_cancel_and_insert();
  test_pane_coordinate_mapping();
  test_hidden_panes_are_not_drop_targets();
  test_page_owns_layout_and_lifecycle();
  TEST_END();
}
