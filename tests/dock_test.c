#include "test_framework.h"
#include "test_env.h"
#include <orion/user/dock.h>
#include <orion/user/toolbar.h>

static int dock_clicks;
static ipoint16_t dock_point;
static result_t dock_test_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: case evDestroy: return true;
    case evPaint: return false;
    case evCommand: if (HIWORD(wparam) != btnClicked) return false; dock_clicks++; return true;
    case evLeftButtonDown:
      dock_point = (ipoint16_t){(int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)};
      return true;
    default: return false;
  }
}

static window_t *dock_test_host(void) {
  return create_window("Workspace", 0, MAKERECT(40, 50, 900, 700), NULL, dock_test_proc, 0, NULL);
}

static window_t *dock_test_pane(window_t *host, dock_side_t side, int extent) {
  window_t *pane = create_window("Library", WINDOW_TOOLBAR | WINDOW_TITLETOOLBAR | WINDOW_NORESIZE,
                                  MAKERECT(10, 10, 500, 240), host, dock_test_proc, 0, NULL);
  toolbar_item_t items[] = {
    {.type = TOOLBAR_ITEM_BUTTON, .ident = 41, .icon = "play"},
    {.type = TOOLBAR_ITEM_SPACER, .style = TOOLBAR_ITEM_FLAG_FLEXSPACE},
    {.type = TOOLBAR_ITEM_TEXTEDIT, .ident = 42, .w = 160},
  };
  send_message(pane, tbSetItems, ARRAY_LEN(items), items);
  dock_window(pane, side, DOCK_ALL_EDGES, DOCK_RESIZABLE, extent, 90);
  return pane;
}

// A point on the caption with no item under it: the middle of the flexible spacer.
static ipoint16_t dock_caption_point(window_t *pane) {
  irect16_t gap = toolbar_get_state(pane)->item_rects[1];
  return (ipoint16_t){ (int16_t)(window_screen_x(pane) + gap.x + gap.w / 2), (int16_t)(window_screen_y(pane) + gap.y + gap.h / 2) };
}

static void dock_mouse(int msg, int x, int y) {
  ui_event_t e = {.message = msg, .x = x * UI_WINDOW_SCALE, .y = y * UI_WINDOW_SCALE};
  dispatch_message(&e);
}

static void test_dock_ownership_and_layout(void) {
  TEST("dock edges reserve space, floating preserves identity, and hidden panes release space");
  test_env_init();
  window_t *host = dock_test_host();
  window_t *pane = dock_test_pane(host, DOCK_BOTTOM, 240);
  window_t *doc = create_window("Document", WINDOW_NOTITLE, MAKERECT(0, 0, 1, 1), host, dock_test_proc, 0, NULL);
  ASSERT_TRUE(dock_window(doc, DOCK_FILL, 0, DOCK_NOFLOAT, 0, 0));
  window_t *field = get_window_item(pane, 42);
  send_message(field, edSetText, 0, "retained query");
  irect16_t full = get_client_rect(host);
  ASSERT_EQUAL(doc->frame.h, full.h - 240 - DOCK_SPLITTER);
  for (int side = DOCK_TOP; side <= DOCK_RIGHT; side++) {
    ASSERT_TRUE(dock_set_side(pane, side));
    ASSERT_TRUE(pane->parent == host && get_window_item(pane, 42) == field);
    ASSERT_TRUE(doc->frame.w >= 0 && doc->frame.h >= 0);
  }
  ASSERT_TRUE(dock_float(pane, R(100, 100, 500, 240)));
  ASSERT_EQUAL(doc->frame.h, full.h);
  ASSERT_EQUAL(doc->frame.w, full.w);
  char text[32]; send_message(field, edGetText, sizeof(text), text);
  ASSERT_STR_EQUAL(text, "retained query");
  ASSERT_TRUE(dock_set_side(pane, DOCK_BOTTOM));
  show_window(pane, false);
  ASSERT_EQUAL(doc->frame.h, full.h);
  show_window(pane, true);
  ASSERT_EQUAL(doc->frame.h, full.h - 240 - DOCK_SPLITTER);
  destroy_window(pane);
  ASSERT_EQUAL(doc->frame.h, full.h);
  test_env_shutdown();
  PASS();
}

static void test_dock_pointer_and_caption(void) {
  TEST("merged title toolbar routes buttons and nested scrolled content without dragging");
  test_env_init(); dock_clicks = 0;
  window_t *host = dock_test_host(), *pane = dock_test_pane(host, DOCK_BOTTOM, 240);
  toolbar_state_t *tb = toolbar_get_state(pane);
  ASSERT_TRUE(toolbar_merged_title(pane));
  ASSERT_EQUAL(titlebar_height(pane), toolbar_effective_item_height(pane) + 2 * toolbar_effective_padding(pane));
  irect16_t button = tb->item_rects[0];
  int x = window_screen_x(pane) + button.x + button.w / 2;
  int y = window_screen_y(pane) + button.y + button.h / 2;
  dock_mouse(kEventMouseMoved, x, y);
  ASSERT_EQUAL(tb->hot_item, 0);
  dock_mouse(kEventLeftButtonDown, x, y);
  dock_mouse(kEventLeftButtonUp, x, y);
  ASSERT_EQUAL(dock_clicks, 1);
  ASSERT_EQUAL(pane->dock->side, DOCK_BOTTOM);
  dock_mouse(kEventLeftButtonDown, x, y);
  dock_mouse(kEventLeftButtonUp, 5, 5);
  ASSERT_EQUAL(dock_clicks, 1);
  ASSERT_EQUAL(tb->pressed_item, -1);
  ASSERT_NULL(g_ui_runtime.captured);
  dock_mouse(kEventLeftButtonDown, x, y);
  show_window(pane, false);
  ASSERT_NULL(g_ui_runtime.captured);
  ASSERT_EQUAL(tb->pressed_item, -1);
  show_window(pane, true);
  window_t *content = create_window("Content", WINDOW_NOTITLE | WINDOW_VSCROLL, MAKERECT(0, 0, 1, 1), pane, dock_test_proc, 0, NULL);
  dock_window(content, DOCK_FILL, 0, DOCK_NOFLOAT, 0, 0);
  content->vscroll.pos = 37;
  x = window_screen_x(content) + 20; y = window_screen_y(content) + 25;
  dock_mouse(kEventLeftButtonDown, x, y);
  ASSERT_EQUAL(dock_point.x, 20);
  ASSERT_EQUAL(dock_point.y, 62);
  test_env_shutdown();
  PASS();
}

static void test_dock_drag_and_resize(void) {
  TEST("caption drag floats and redocks; splitters resize and pointer cancellation restores extent");
  test_env_init();
  window_t *host = dock_test_host(), *pane = dock_test_pane(host, DOCK_BOTTOM, 240);
  ipoint16_t grab = dock_caption_point(pane);
  int x = grab.x, y = grab.y;
  dock_mouse(kEventLeftButtonDown, x, y);
  dock_mouse(kEventLeftButtonDragged, x + 150, y - 100);
  ASSERT_TRUE(window_is_lifted(pane));
  dock_mouse(kEventLeftButtonUp, x + 150, y - 100);
  ASSERT_TRUE(dock_is_floating(pane));
  ASSERT_FALSE(window_is_lifted(pane));
  grab = dock_caption_point(pane);
  x = grab.x; y = grab.y;
  dock_mouse(kEventLeftButtonDown, x, y);
  int top = window_screen_y(host) + titlebar_height(host) + 2;
  dock_mouse(kEventLeftButtonDragged, x, top);
  dock_mouse(kEventLeftButtonUp, x, top);
  ASSERT_EQUAL(pane->dock->side, DOCK_TOP);
  irect16_t split = pane->dock->splitter;
  x = window_screen_x(host) + split.x + 20;
  y = window_screen_y(host) + titlebar_height(host) + split.y + 2;
  dock_mouse(kEventLeftButtonDown, x, y);
  dock_mouse(kEventLeftButtonDragged, x, y + 50);
  ASSERT_EQUAL(pane->frame.h, 290);
  dock_mouse(kEventPointerCancel, x, y + 50);
  ASSERT_EQUAL(pane->frame.h, 240);
  dock_mouse(kEventLeftButtonDown, x, y);
  dock_mouse(kEventLeftButtonUp, x, y + 40);
  ASSERT_EQUAL(pane->frame.h, 280);
  test_env_shutdown();
  PASS();
}

static void test_dock_collapse_and_limits(void) {
  TEST("collapse keeps caption, restores size, and rejects disallowed transitions without mutation");
  test_env_init();
  window_t *host = dock_test_host(), *pane = dock_test_pane(host, DOCK_BOTTOM, 240);
  ASSERT_TRUE(dock_collapse(pane, true));
  ASSERT_EQUAL(pane->frame.h, titlebar_height(pane));
  ASSERT_TRUE(dock_collapse(pane, false));
  ASSERT_EQUAL(pane->frame.h, 240);
  pane->dock->allowed = DOCK_EDGE(DOCK_BOTTOM);
  ASSERT_FALSE(dock_set_side(pane, DOCK_LEFT));
  ASSERT_EQUAL(pane->dock->side, DOCK_BOTTOM);
  ASSERT_FALSE(dock_window(pane, DOCK_BOTTOM, DOCK_ALL_EDGES, 0, 100, 0));
  resize_window(host, 30, 50);
  ASSERT_TRUE(pane->frame.w >= 0 && pane->frame.h >= 0);
  ASSERT_TRUE(host->dock_host->content.w >= 0 && host->dock_host->content.h >= 0);
  test_env_shutdown();
  PASS();
}

static void test_dock_menu(void) {
  TEST("floating menus stack vertically and open their popup beside the selected row");
  test_env_init();
  window_t *host = dock_test_host();
  menu_item_t item = {.label = "Open", .id = 44};
  menu_def_t menus[] = {{"File", &item, 1}, {"Edit", &item, 1}, {"Help", &item, 1}};
  window_t *menu = create_window("Menu", WINDOW_NOTITLE | WINDOW_NORESIZE, MAKERECT(0, 0, 200, 30), host, win_menubar, 0, NULL);
  send_message(menu, kMenuBarMessageSetMenus, ARRAY_LEN(menus), menus);
  ASSERT_TRUE(dock_window(menu, DOCK_TOP, DOCK_ALL_EDGES, DOCK_MENU, 0, 0));
  ASSERT_TRUE(dock_float(menu, R(100, 100, 120, 100)));
  ASSERT_EQUAL(menu->frame.h, TOOLBAR_GRIP_HEIGHT + 3 * get_theme()->menubar_height);
  int x = window_screen_x(menu) + menu->frame.w / 2;
  int y = window_screen_y(menu) + TOOLBAR_GRIP_HEIGHT + get_theme()->menubar_height + 4;
  dock_mouse(kEventLeftButtonDown, x, y);
  window_t *popup = g_ui_runtime.captured;
  ASSERT_NOT_NULL(popup);
  ASSERT_EQUAL(popup->frame.x, window_screen_x(menu) + menu->frame.w);
  ASSERT_EQUAL(popup->frame.y, window_screen_y(menu) + TOOLBAR_GRIP_HEIGHT + get_theme()->menubar_height);
  ASSERT_TRUE(dock_set_side(menu, DOCK_TOP));
  ASSERT_FALSE(is_window(popup));
  ASSERT_NULL(g_ui_runtime.captured);
  ASSERT_EQUAL(menu->frame.h, get_theme()->menubar_height);
  test_env_shutdown();
  PASS();
}

static void test_dock_floating_resize_and_close(void) {
  TEST("floating resize cancels, double-click redocks, and caption controls collapse and hide");
  test_env_init();
  window_t *host = dock_test_host(), *pane = dock_test_pane(host, DOCK_BOTTOM, 240);
  ASSERT_NULL(find_default_button(host));
  ASSERT_TRUE(dock_float(pane, R(100, 100, 500, 240)));
  int x = window_screen_x(pane) + pane->frame.w - 3;
  int y = window_screen_y(pane) + pane->frame.h - 3;
  ASSERT_EQUAL(dock_cursor(pane, (ipoint16_t){x, y}), curResizeNWSE);
  dock_mouse(kEventLeftButtonDown, x, y);
  dock_mouse(kEventLeftButtonDragged, x - 100, y + 50);
  ASSERT_EQUAL(pane->frame.w, 400);
  ASSERT_EQUAL(pane->frame.h, 290);
  dock_mouse(kEventPointerCancel, x, y);
  ASSERT_EQUAL(pane->frame.w, 500);
  ASSERT_EQUAL(pane->frame.h, 240);
  ipoint16_t grab = dock_caption_point(pane);
  dock_mouse(kEventLeftDoubleClick, grab.x, grab.y);
  ASSERT_EQUAL(pane->dock->side, DOCK_BOTTOM);
  toolbar_state_t *tb = toolbar_get_state(pane);
  irect16_t button = tb->item_rects[tb->item_count - 2];
  x = window_screen_x(pane) + button.x + button.w / 2;
  y = window_screen_y(pane) + button.y + button.h / 2;
  dock_mouse(kEventLeftButtonDown, x, y);
  dock_mouse(kEventLeftButtonUp, x, y);
  ASSERT_TRUE(pane->dock->collapsed);
  button = tb->item_rects[tb->item_count - 1];
  x = window_screen_x(pane) + button.x + button.w / 2;
  y = window_screen_y(pane) + button.y + button.h / 2;
  dock_mouse(kEventLeftButtonDown, x, y);
  dock_mouse(kEventLeftButtonUp, x, y);
  ASSERT_TRUE(is_window(pane));
  ASSERT_FALSE(window_has_state(pane, WINDOW_STATE_VISIBLE));
  test_env_shutdown();
  PASS();
}

static void test_dock_fixed_pane(void) {
  TEST("WINDOW_NOCLOSE | WINDOW_NOCOLLAPSE with DOCK_NOFLOAT: no caption buttons, no collapse, no floating");
  test_env_init();
  window_t *host = dock_test_host();
  window_t *pane = create_window("Library", WINDOW_TOOLBAR | WINDOW_TITLETOOLBAR | WINDOW_NORESIZE | WINDOW_NOCLOSE | WINDOW_NOCOLLAPSE,
                                 MAKERECT(10, 10, 500, 240), host, dock_test_proc, 0, NULL);
  toolbar_item_t items[] = {{.type = TOOLBAR_ITEM_BUTTON, .ident = 41, .icon = "play"}};
  send_message(pane, tbSetItems, ARRAY_LEN(items), items);
  ASSERT_TRUE(dock_window(pane, DOCK_BOTTOM, DOCK_EDGE(DOCK_BOTTOM), DOCK_RESIZABLE | DOCK_NOFLOAT, 240, 90));
  toolbar_state_t *tb = toolbar_get_state(pane);
  ASSERT(tb->item_count == 2 && tb->items[0].ident == 41 && tb->items[1].type == TOOLBAR_ITEM_SPACER, "only the app items and a flexible spacer");
  ASSERT_FALSE(dock_collapse(pane, true));
  ASSERT_FALSE(pane->dock->collapsed);
  ASSERT_TRUE(dock_collapse(pane, false));
  ASSERT_FALSE(dock_float(pane, R(20, 20, 300, 200)));
  ASSERT_FALSE(dock_set_side(pane, DOCK_TOP));
  irect16_t gap = tb->item_rects[1];
  int x = window_screen_x(pane) + gap.x + gap.w / 2, y = window_screen_y(pane) + gap.y + gap.h / 2;
  dock_mouse(kEventLeftDoubleClick, x, y);
  dock_mouse(kEventLeftButtonDown, x, y);
  dock_mouse(kEventLeftButtonDragged, x, y - 200);
  dock_mouse(kEventLeftButtonUp, x, y - 200);
  ASSERT(pane->dock->side == DOCK_BOTTOM, "the caption neither floats nor moves the pane");
  destroy_window(host);
  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("Workspace docking");
  test_dock_ownership_and_layout();
  test_dock_pointer_and_caption();
  test_dock_drag_and_resize();
  test_dock_collapse_and_limits();
  test_dock_menu();
  test_dock_floating_resize_and_close();
  test_dock_fixed_pane();
  TEST_END();
}
