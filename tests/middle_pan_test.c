#include "test_framework.h"
#include "test_env.h"
#include <orion/commctl/filelist.h>
#include <orion/commctl/columnview.h>
#include <orion/user/scrollbar.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int presses, notifications;
static result_t pan_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: case evPaint: case evDestroy: return true;
    case evLeftButtonDown: presses++; return true;
    case evCommand: notifications++; return true;
    default: return false;
  }
}

static void mouse_at(window_t *win, uint32_t msg, ipoint16_t point) {
  int x = (window_screen_x(win) + point.x) * UI_WINDOW_SCALE;
  int y = (window_screen_y(win) + titlebar_height(win) + point.y) * UI_WINDOW_SCALE;
  ui_event_t event = {.message = msg, .wParam = MAKEDWORD(x, y)};
  dispatch_message(&event);
}

static void test_nested_scroller(void) {
  TEST("middle drag pans the nearest scrollable ancestor in exact points and retains capture outside it");
  test_env_init();
  set_theme(THEME_CLASSIC);
  window_t *root = create_window("Root", WINDOW_NOTITLE, MAKERECT(20, 30, 600, 400), NULL, pan_proc, 0, NULL);
  window_t *list = create_window("List", WINDOW_NOTITLE | WINDOW_HSCROLL | WINDOW_VSCROLL,
                                MAKERECT(20, 30, 200, 180), root, pan_proc, 0, NULL);
  window_t *row = create_window("Row", WINDOW_NOTITLE, MAKERECT(10, 10, 100, 80), list, pan_proc, 0, NULL);
  window_t *other = create_window("Other", WINDOW_NOTITLE | WINDOW_VSCROLL,
                                 MAKERECT(300, 30, 200, 180), root, pan_proc, 0, NULL);
  show_window(root, true); show_window(list, true); show_window(row, true); show_window(other, true);
  set_scroll_content(list, 1000, 1000, 100, 100);
  set_scroll_content(other, 200, 1000, 0, 100);
  presses = 0;
  mouse_at(row, kEventOtherButtonDown, (ipoint16_t){20, 20});
  ASSERT_TRUE(g_ui_runtime.captured == list);
  mouse_at(row, kEventOtherButtonDragged, (ipoint16_t){7, 11});
  ASSERT_EQUAL(list->hscroll.pos, 113); ASSERT_EQUAL(list->vscroll.pos, 109);
  mouse_at(row, kEventLeftButtonDown, (ipoint16_t){7, 11});
  mouse_at(row, kEventLeftButtonUp, (ipoint16_t){7, 11});
  mouse_at(row, kEventRightButtonUp, (ipoint16_t){7, 11});
  ASSERT_EQUAL(presses, 0);
  ASSERT_TRUE(g_ui_runtime.captured == list);
  mouse_at(row, kEventOtherButtonDragged, (ipoint16_t){-300, -400});
  ASSERT_EQUAL(list->hscroll.pos, 420); ASSERT_EQUAL(list->vscroll.pos, 520);
  ASSERT_EQUAL(other->vscroll.pos, 100);
  mouse_at(row, kEventOtherButtonDragged, (ipoint16_t){-4000, -4000});
  ASSERT_EQUAL(list->hscroll.pos, list->hscroll.max_val - list->hscroll.page);
  ASSERT_EQUAL(list->vscroll.pos, list->vscroll.max_val - list->vscroll.page);
  mouse_at(row, kEventOtherButtonUp, (ipoint16_t){-4000, -4000});
  ASSERT_NULL(g_ui_runtime.captured);
  int pos = list->vscroll.pos;
  mouse_at(row, kEventOtherButtonDragged, (ipoint16_t){20, 20});
  ASSERT_EQUAL(list->vscroll.pos, pos);
  test_env_shutdown();
  PASS();
}

static void test_axes_and_lifetime(void) {
  TEST("middle pan respects disabled axes, exact-fit views, non-client areas, capture and target lifetime");
  test_env_init();
  set_theme(THEME_CLASSIC);
  window_t *win = create_window("Scroller", WINDOW_NOTITLE | WINDOW_HSCROLL | WINDOW_VSCROLL,
                               MAKERECT(20, 30, 200, 180), NULL, pan_proc, 0, NULL);
  show_window(win, true);
  set_scroll_content(win, 1000, 1000, 100, 100);
  enable_scroll_bar(win, SB_VERT, false);
  mouse_at(win, kEventOtherButtonDown, (ipoint16_t){30, 30});
  mouse_at(win, kEventOtherButtonDragged, (ipoint16_t){29, 29});
  ASSERT_EQUAL(win->hscroll.pos, 101); ASSERT_EQUAL(win->vscroll.pos, 100);
  ui_event_t escape = {.message = kEventKeyDown, .wParam = AX_KEY_ESCAPE};
  dispatch_message(&escape);
  ASSERT_NULL(g_ui_runtime.captured);
  mouse_at(win, kEventOtherButtonDragged, (ipoint16_t){20, 20});
  ASSERT_EQUAL(win->hscroll.pos, 101);
  mouse_at(win, kEventOtherButtonDown, (ipoint16_t){30, 30});
  ASSERT_TRUE(g_ui_runtime.captured == win);
  ui_event_t cancel = {.message = kEventPointerCancel};
  dispatch_message(&cancel);
  ASSERT_NULL(g_ui_runtime.captured);
  mouse_at(win, kEventOtherButtonDown, (ipoint16_t){30, 30});
  ui_event_t focus_lost = {.message = kEventKillFocus};
  dispatch_message(&focus_lost);
  ASSERT_NULL(g_ui_runtime.captured);
  window_set_state(win, WINDOW_STATE_DISABLED, true);
  mouse_at(win, kEventOtherButtonDown, (ipoint16_t){30, 30});
  ASSERT_NULL(g_ui_runtime.captured);
  window_set_state(win, WINDOW_STATE_DISABLED, false);
  set_capture(win);
  mouse_at(win, kEventOtherButtonDown, (ipoint16_t){30, 30});
  mouse_at(win, kEventOtherButtonDragged, (ipoint16_t){20, 20});
  ASSERT_EQUAL(win->hscroll.pos, 101);
  ASSERT_TRUE(g_ui_runtime.captured == win);
  set_capture(NULL);
  set_scroll_content(win, 50, 50, 0, 0);
  mouse_at(win, kEventOtherButtonDown, (ipoint16_t){30, 30});
  ASSERT_NULL(g_ui_runtime.captured);
  set_scroll_content(win, 1000, 1000, 100, 100);
  irect16_t client = get_client_rect(win);
  mouse_at(win, kEventOtherButtonDown, (ipoint16_t){client.w + 1, 30});
  ASSERT_NULL(g_ui_runtime.captured);
  ASSERT_NULL(g_ui_runtime.dragging); ASSERT_NULL(g_ui_runtime.resizing);
  mouse_at(win, kEventOtherButtonDown, (ipoint16_t){30, 30});
  ASSERT_TRUE(g_ui_runtime.captured == win);
  destroy_window(win);
  win = create_window("Replacement", WINDOW_NOTITLE | WINDOW_VSCROLL,
                      MAKERECT(20, 30, 200, 180), NULL, pan_proc, 0, NULL);
  show_window(win, true);
  set_scroll_content(win, 200, 1000, 0, 100);
  mouse_at(win, kEventOtherButtonDragged, (ipoint16_t){20, 20});
  mouse_at(win, kEventOtherButtonUp, (ipoint16_t){20, 20});
  ASSERT_EQUAL(win->vscroll.pos, 100);
  ASSERT_NULL(g_ui_runtime.captured);
  test_env_shutdown();
  PASS();
}

static void test_content_view(void) {
  TEST("content views pan smoothly at high zoom and rotation without an application gesture handler");
  test_env_init();
  window_t *win = create_window("View", WINDOW_NOTITLE, MAKERECT(20, 30, 200, 180), NULL, pan_proc, 0, NULL);
  show_window(win, true);
  window_view_init(win, 1000, 1000, 2, true);
  ax_gesture_t rotation = {AX_GESTURE_UPDATE, 30, 30, 30, 30, 8, 0.8f};
  window_view_apply_gesture(win, &rotation);
  view_matrix_t before = win->view->matrix;
  mouse_at(win, kEventOtherButtonDown, (ipoint16_t){30, 30});
  ASSERT_TRUE(g_ui_runtime.captured == win);
  mouse_at(win, kEventOtherButtonDragged, (ipoint16_t){31, 29});
  ASSERT_TRUE(fabsf(win->view->matrix.tx - before.tx - 1) < 0.001f);
  ASSERT_TRUE(fabsf(win->view->matrix.ty - before.ty + 1) < 0.001f);
  ASSERT_TRUE(fabsf(win->view->matrix.a - before.a) < 0.001f);
  ASSERT_TRUE(fabsf(win->view->matrix.b - before.b) < 0.001f);
  mouse_at(win, kEventOtherButtonUp, (ipoint16_t){31, 29});
  ASSERT_NULL(g_ui_runtime.captured);
  ax_gesture_t invalid = {.phase = AX_GESTURE_BEGIN, .x = NAN, .scale = 1};
  ASSERT_FALSE(send_message(win, evGesture, 0, &invalid));
  test_env_shutdown();
  PASS();
}

static void test_file_dialog_list(void) {
  TEST("open-file dialog's FileList pans without selecting or opening files");
  test_env_init();
  char path[] = "/tmp/orion-middle-pan-XXXXXX";
  ASSERT_NOT_NULL(mkdtemp(path));
  for (int i = 0; i < 80; i++) {
    char filename[512];
    snprintf(filename, sizeof(filename), "%s/file_%03d.txt", path, i);
    FILE *file = fopen(filename, "w");
    ASSERT_NOT_NULL(file);
    fclose(file);
  }
  window_t *dialog = create_window("Open", WINDOW_DIALOG, MAKERECT(20, 30, 340, 260), NULL, pan_proc, 0, NULL);
  form_ctrl_def_t initial = {.text = path};
  window_t *list = create_window("", WINDOW_NOTITLE | WINDOW_VSCROLL, MAKERECT(15, 15, 300, 180),
                                dialog, win_filelist, 0, &initial);
  show_window(dialog, true); show_window(list, true);
  ASSERT_TRUE(send_message(list, RVM_GETITEMCOUNT, 0, NULL) >= 80);
  ASSERT_TRUE(scrollbar_can_scroll(list));
  send_pointer_message(list, evLeftButtonDown, MAKEDWORD(20, 45), NULL);
  char selected[512], after[512], directory[512];
  send_message(list, FLM_GETSELECTEDPATH, sizeof(selected), selected);
  ASSERT_TRUE(selected[0] != '\0');
  int selection = send_message(list, RVM_GETSELECTION, 0, NULL);
  notifications = 0;
  mouse_at(list, kEventOtherButtonDown, (ipoint16_t){80, 100});
  ASSERT_TRUE(g_ui_runtime.captured == list);
  mouse_at(list, kEventOtherButtonDragged, (ipoint16_t){80, 60});
  ASSERT_EQUAL(list->vscroll.pos, 40);
  mouse_at(list, kEventOtherButtonUp, (ipoint16_t){80, 60});
  ASSERT_NULL(g_ui_runtime.captured);
  ASSERT_EQUAL(send_message(list, RVM_GETSELECTION, 0, NULL), selection);
  send_message(list, FLM_GETSELECTEDPATH, sizeof(after), after);
  send_message(list, FLM_GETPATH, sizeof(directory), directory);
  ASSERT_STR_EQUAL(selected, after);
  ASSERT_STR_EQUAL(path, directory);
  ASSERT_EQUAL(notifications, 0);
  test_env_shutdown();
  for (int i = 0; i < 80; i++) {
    char filename[512];
    snprintf(filename, sizeof(filename), "%s/file_%03d.txt", path, i);
    unlink(filename);
  }
  rmdir(path);
  PASS();
}

int main(void) {
  TEST_START("Framework middle-button panning");
  test_nested_scroller();
  test_axes_and_lifetime();
  test_content_view();
  test_file_dialog_list();
  TEST_END();
}
