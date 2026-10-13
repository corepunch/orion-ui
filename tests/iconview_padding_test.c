#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/commctl/columnview.h>

static result_t parent_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)wparam; (void)lparam;
  return msg == evCreate || msg == evPaint || msg == evDestroy;
}

static window_t *make_view(window_t *parent, int count, int height) {
  irect16_t frame = {0, 0, 240, height};
  window_t *view = create_window("", WINDOW_NOTITLE, &frame, parent, win_iconview, 0, NULL);
  for (int i = 0; view && i < count; i++) {
    reportview_item_t item = {.text = "File.ptf"};
    send_message(view, RVM_ADDITEM, 0, &item);
  }
  return view;
}

static void test_scroll_end_padding(void) {
  TEST("iconview: scrolling to the end reveals the whole last row with bottom padding");
  test_env_init();
  window_t *parent = test_env_create_window("P", 0, 0, 240, 200, parent_proc, NULL);
  window_t *view = make_view(parent, 20, 103);
  ASSERT_NOT_NULL(view);
  send_message(view, evVScroll, 10000, NULL);
  int last_bottom = COLUMNVIEW_WIN_PADDING + 20 * COLUMNVIEW_ENTRY_HEIGHT - view->vscroll.pos;
  ASSERT_EQUAL(get_client_rect(view).h - last_bottom, COLUMNVIEW_WIN_PADDING);
  destroy_window(parent); test_env_shutdown(); PASS();
}

static void test_selection_padding(void) {
  TEST("iconview: selection and arrow navigation preserve padding at both ends");
  test_env_init();
  window_t *parent = test_env_create_window("P", 0, 0, 240, 200, parent_proc, NULL);
  window_t *view = make_view(parent, 20, 103);
  ASSERT_NOT_NULL(view);
  send_message(view, RVM_SETSELECTION, 19, NULL);
  int bottom = COLUMNVIEW_WIN_PADDING + 20 * COLUMNVIEW_ENTRY_HEIGHT - view->vscroll.pos;
  ASSERT_EQUAL(get_client_rect(view).h - bottom, COLUMNVIEW_WIN_PADDING);
  send_message(view, RVM_SETSELECTION, 0, NULL);
  ASSERT_EQUAL(view->vscroll.pos, 0);
  for (int i = 0; i < 19; i++) send_message(view, evKeyDown, AX_KEY_DOWNARROW, NULL);
  bottom = COLUMNVIEW_WIN_PADDING + 20 * COLUMNVIEW_ENTRY_HEIGHT - view->vscroll.pos;
  ASSERT_EQUAL(get_client_rect(view).h - bottom, COLUMNVIEW_WIN_PADDING);
  for (int i = 0; i < 19; i++) send_message(view, evKeyDown, AX_KEY_UPARROW, NULL);
  ASSERT_EQUAL(view->vscroll.pos, 0);
  destroy_window(parent); test_env_shutdown(); PASS();
}

static void test_padding_overflow_and_resize(void) {
  TEST("iconview: padding participates in overflow and resize clamps the scroll position");
  test_env_init();
  window_t *parent = test_env_create_window("P", 0, 0, 240, 200, parent_proc, NULL);
  window_t *view = make_view(parent, 5, 5 * COLUMNVIEW_ENTRY_HEIGHT);
  ASSERT_NOT_NULL(view);
  ASSERT_TRUE(view->vscroll.visible);
  send_message(view, evVScroll, 10000, NULL);
  ASSERT_EQUAL(view->vscroll.pos, 2 * COLUMNVIEW_WIN_PADDING);
  view->frame.h += 2 * COLUMNVIEW_WIN_PADDING;
  send_message(view, evResize, 0, NULL);
  ASSERT_EQUAL(view->vscroll.pos, 0);
  ASSERT_FALSE(view->vscroll.visible);
  send_message(view, RVM_CLEAR, 0, NULL);
  ASSERT_EQUAL(view->vscroll.max_val, 0);
  destroy_window(parent); test_env_shutdown(); PASS();
}

static void test_painted_row_hit_geometry(void) {
  TEST("small and large icon lists: root and nested pointer routing agrees with padded visible rows");
  for (int grid = 0; grid < 2; grid++) for (int nested = 0; nested < 2; nested++) {
    test_env_init(); set_theme(THEME_MODERN);
    window_t *root = create_window("", WINDOW_NOTITLE, MAKERECT(30, 40, 320, 200), NULL,
                                   nested ? parent_proc : (grid ? win_icongrid : win_iconview), 0, NULL);
    window_t *view = nested ? create_window("", WINDOW_NOTITLE, MAKERECT(17, 23, 240, 130), root,
                                           grid ? win_icongrid : win_iconview, 0, NULL) : root;
    if (!nested) view->frame = R(30, 40, 240, 130);
    if (grid) {
      send_message(view, RVM_SETCOLUMNWIDTH, 80, NULL);
      send_message(view, RVM_SETLARGEICONCOLS, 2, NULL);
    }
    for (int i = 0; i < 20; i++) {
      reportview_item_t item = {.text = "File.ptf"};
      send_message(view, RVM_ADDITEM, 0, &item);
    }
    irect16_t content = window_scroll_content_rect(view);
    ASSERT_EQUAL(send_message(view, RVM_HITTEST, MAKEDWORD(content.x, content.y - 1), NULL), -1);
    send_message(view, RVM_SETSELECTION, 19, NULL);
    int row_h = view->scroll_content.height / (grid ? 10 : 20);
    irect16_t last = R(grid ? content.x + content.w - 80 : content.x,
                      content.y + content.h - row_h, grid ? 80 : content.w, row_h);
    irect16_t painted = window_scroll_rect_to_client(view, last);
    ASSERT_EQUAL(get_client_rect(view).h - painted.y - painted.h, view->scroll_content.padding.h);
    show_window(root, true);
    send_message(view, RVM_SETSELECTION, 18, NULL);
    ui_event_t event = {.message = kEventLeftButtonDown,
      .x = (window_screen_x(view) + painted.x + 10) * UI_WINDOW_SCALE,
      .y = (window_screen_y(view) + painted.y + 2) * UI_WINDOW_SCALE};
    dispatch_message(&event);
    ASSERT_EQUAL(send_message(view, RVM_GETSELECTION, 0, NULL), 19);
    test_env_shutdown();
  }
  PASS();
}

static void test_report_header_geometry(void) {
  TEST("reportview: fixed header stays outside row hit-testing after scrolling");
  test_env_init();
  window_t *view = create_window("", WINDOW_NOTITLE, MAKERECT(0, 0, 240, 130), NULL, win_reportview, 0, NULL);
  reportview_column_t column = {.title = "File", .width = 500};
  send_message(view, RVM_ADDCOLUMN, 0, &column);
  for (int i = 0; i < 20; i++) {
    reportview_item_t item = {.text = "File.ptf"};
    send_message(view, RVM_ADDITEM, 0, &item);
  }
  send_message(view, evHScroll, 40, NULL);
  send_message(view, RVM_SETSELECTION, 19, NULL);
  ASSERT_EQUAL(view->hscroll.pos, 40);
  irect16_t header_point = window_scroll_rect_from_client(view, R(10, 2, 0, 0));
  ASSERT_EQUAL(send_message(view, RVM_HITTEST, MAKEDWORD(header_point.x, header_point.y), NULL), -1);
  irect16_t body = window_scroll_viewport(view);
  ASSERT_EQUAL(view->vscroll.page, body.h);
  irect16_t last = window_scroll_rect_from_client(view, R(10, body.y + body.h - 2, 0, 0));
  ASSERT_EQUAL(send_message(view, RVM_HITTEST, MAKEDWORD(last.x, last.y), NULL), 19);
  test_env_shutdown(); PASS();
}

int main(void) {
  TEST_START("iconview padding");
  test_scroll_end_padding();
  test_selection_padding();
  test_padding_overflow_and_resize();
  test_painted_row_hit_geometry();
  test_report_header_geometry();
  TEST_END();
}
