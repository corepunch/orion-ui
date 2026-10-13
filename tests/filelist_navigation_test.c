#include "test_framework.h"
#include "test_env.h"
#include <orion/commctl/filelist.h>
#include <orion/commctl/columnview.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int navigations, activations;
static result_t navigation_host(window_t *win, uint32_t msg, uint32_t wp, void *lp) {
  if (msg == evCommand) {
    if (HIWORD(wp) == FLN_NAVDIR) navigations++;
    if (HIWORD(wp) == FLN_FILEOPEN) activations++;
    return true;
  }
  return msg == evCreate || msg == evPaint || msg == evDestroy;
}

static void pointer_at(window_t *win, uint32_t message, ipoint16_t point) {
  int x = (window_screen_x(win) + point.x) * UI_WINDOW_SCALE;
  int y = (window_screen_y(win) + titlebar_height(win) + point.y) * UI_WINDOW_SCALE;
  ui_event_t event = {.message = message, .wParam = MAKEDWORD(x, y)};
  dispatch_message(&event);
}

static void test_scrolled_navigation(void) {
  TEST("FileList: a repeated double-click cannot activate the replacement directory listing");
  test_env_init();
  set_theme(THEME_CLASSIC);
  char path[] = "/tmp/orion-filelist-nav-XXXXXX";
  ASSERT_NOT_NULL(mkdtemp(path));
  char build[512], generated[512], image[512];
  snprintf(build, sizeof(build), "%s/build", path);
  snprintf(generated, sizeof(generated), "%s/build/generated", path);
  ASSERT_EQUAL(mkdir(build, 0700), 0);
  ASSERT_EQUAL(mkdir(generated, 0700), 0);
  snprintf(image, sizeof(image), "%s/build/generated/image.png", path);
  FILE *file = fopen(image, "w");
  ASSERT_NOT_NULL(file);
  fclose(file);
  for (int i = 0; i < 20; i++) {
    char entry[512];
    snprintf(entry, sizeof(entry), "%s/a%02d", path, i);
    ASSERT_EQUAL(mkdir(entry, 0700), 0);
    snprintf(entry, sizeof(entry), "%s/z%02d", path, i);
    ASSERT_EQUAL(mkdir(entry, 0700), 0);
  }
  window_t *host = create_window("Open", WINDOW_DIALOG, MAKERECT(20, 30, 340, 260), NULL, navigation_host, 0, NULL);
  form_ctrl_def_t initial = {.text = path};
  window_t *list = create_window("", WINDOW_NOTITLE | WINDOW_VSCROLL, MAKERECT(15, 15, 300, 180), host, win_filelist, 0, &initial);
  show_window(host, true); show_window(list, true);
  send_message(list, evVScroll, 20 * COLUMNVIEW_ENTRY_HEIGHT, NULL);
  ASSERT_TRUE(list->vscroll.pos > 0);
  irect16_t row = window_scroll_content_rect(list);
  row = rect_offset(row, 0, 21 * COLUMNVIEW_ENTRY_HEIGHT);
  row.h = COLUMNVIEW_ENTRY_HEIGHT;
  row = rect_center(window_scroll_rect_to_client(list, row), 1, 1);
  ipoint16_t point = {row.x, row.y};
  ASSERT_EQUAL(send_message(list, RVM_HITTEST,
                           MAKEDWORD(point.x, point.y + list->vscroll.pos), NULL), 21);
  navigations = activations = 0;
  pointer_at(list, kEventLeftButtonDown, point);
  pointer_at(list, kEventLeftButtonUp, point);
  pointer_at(list, kEventLeftButtonDown, point);
  pointer_at(list, kEventLeftDoubleClick, point);
  char current[512];
  send_message(list, FLM_GETPATH, sizeof(current), current);
  ASSERT_STR_EQUAL(current, build);
  ASSERT_EQUAL(navigations, 1);
  ASSERT_EQUAL(list->vscroll.pos, 0);
  // The replacement list is hit in its new content space, after scroll reset.
  pointer_at(list, kEventLeftDoubleClick, point);
  send_message(list, FLM_GETPATH, sizeof(current), current);
  ASSERT_STR_EQUAL(current, build);
  ASSERT_EQUAL(navigations, 1);
  ASSERT_EQUAL(activations, 0);
  // A fresh double-click at the same point must work immediately.
  pointer_at(list, kEventLeftButtonUp, point);
  pointer_at(list, kEventLeftButtonDown, point);
  pointer_at(list, kEventLeftButtonUp, point);
  pointer_at(list, kEventLeftButtonDown, point);
  pointer_at(list, kEventLeftDoubleClick, point);
  send_message(list, FLM_GETPATH, sizeof(current), current);
  ASSERT_STR_EQUAL(current, generated);
  ASSERT_EQUAL(navigations, 2);
  // File activation is also consumed once, while its selection remains visible.
  pointer_at(list, kEventLeftButtonUp, point);
  pointer_at(list, kEventLeftButtonDown, point);
  pointer_at(list, kEventLeftButtonUp, point);
  pointer_at(list, kEventLeftButtonDown, point);
  ASSERT_EQUAL(activations, 0);
  pointer_at(list, kEventLeftDoubleClick, point);
  pointer_at(list, kEventLeftDoubleClick, point);
  ASSERT_EQUAL(activations, 1);
  char selected[512];
  send_message(list, FLM_GETSELECTEDPATH, sizeof(selected), selected);
  ASSERT_STR_EQUAL(selected, image);
  // Reloading or cancelling between press and activation invalidates the press.
  pointer_at(list, kEventLeftButtonDown, point);
  send_message(list, FLM_REFRESH, 0, NULL);
  pointer_at(list, kEventLeftDoubleClick, point);
  ASSERT_EQUAL(activations, 1);
  pointer_at(list, kEventLeftButtonDown, point);
  send_message(list, evPointerCancel, 0, NULL);
  pointer_at(list, kEventLeftDoubleClick, point);
  ASSERT_EQUAL(activations, 1);
  // A repeated activation of ".." must not go up twice either.
  point.y -= COLUMNVIEW_ENTRY_HEIGHT;
  pointer_at(list, kEventLeftButtonDown, point);
  pointer_at(list, kEventLeftDoubleClick, point);
  pointer_at(list, kEventLeftDoubleClick, point);
  send_message(list, FLM_GETPATH, sizeof(current), current);
  ASSERT_STR_EQUAL(current, build);
  ASSERT_EQUAL(navigations, 3);
  test_env_shutdown();
  unlink(image);
  rmdir(generated); rmdir(build);
  for (int i = 0; i < 20; i++) {
    char entry[512];
    snprintf(entry, sizeof(entry), "%s/a%02d", path, i);
    rmdir(entry);
    snprintf(entry, sizeof(entry), "%s/z%02d", path, i);
    rmdir(entry);
  }
  rmdir(path);
  PASS();
}

int main(void) {
  TEST_START("FileList navigation");
  test_scrolled_navigation();
  TEST_END();
}
