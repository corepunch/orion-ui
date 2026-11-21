#include "test_framework.h"
#include "test_env.h"
#include <orion/user/i18n.h>
#include <string.h>

static result_t help_test_owner(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  return msg == evCreate || msg == evPaint || msg == evDestroy;
}

static window_t *help_test_control(window_t *win, const char *name) {
  if (strcmp(win->style_id, name) == 0) return win;
  for (window_t *child = win->children; child; child = child->next) {
    window_t *found = help_test_control(child, name);
    if (found) return found;
  }
  return NULL;
}

static void test_offline_help(void) {
  TEST("help books open illustrated topics, search body text and keep navigation history");
  test_env_init();
  ui_set_language("en");

  window_t *owner = create_window("Owner", 0, MAKERECT(0, 0, 960, 720), NULL, help_test_owner, 0, NULL);
  const char *path = "build/share/gitclient/gitclient.ohlp";
  ASSERT_TRUE(help_open(owner, path, NULL));
  window_t *viewer = NULL;
  for (window_t *win = g_ui_runtime.windows; win; win = win->next)
    if (win->owner == owner) viewer = win;
  ASSERT_NOT_NULL(viewer);
  window_t *article = help_test_control(viewer, "article");
  window_t *topics = help_test_control(viewer, "topics");
  window_t *search = help_test_control(viewer, "search");
  window_t *back = help_test_control(viewer, "back");
  window_t *forward = help_test_control(viewer, "forward");
  ASSERT(article && topics && search && back && forward, "Viewer controls should exist");
  ASSERT_EQUAL(send_message(topics, RVM_GETCOLUMNCOUNT, 0, NULL), 1);
  ASSERT(article->frame.w > topics->frame.w, "The article should have room to read");
  ASSERT_STR_EQUAL(article->children->title, "Get started with Git Client");
  bool illustrated = false;
  for (window_t *child = article->children; child; child = child->next)
    if (window_is_class(child, "ImageView") && child->frame.h > 1) illustrated = true;
  ASSERT_TRUE(illustrated);
  ASSERT_TRUE(window_is_disabled(back));

  send_message(search, evTextInput, 0, "RECONCILE");
  ASSERT_EQUAL(send_message(topics, RVM_GETITEMCOUNT, 0, NULL), 1);
  send_message(topics, RVM_SETSELECTION, 0, NULL);
  send_message(viewer, evCommand, MAKEDWORD(topics->id, RVN_SELCHANGE), topics);
  ASSERT_STR_EQUAL(article->children->title, "Fetch, pull and push");
  ASSERT_TRUE(window_is_enabled(back));
  send_message(viewer, evCommand, MAKEDWORD(back->id, btnClicked), back);
  ASSERT_STR_EQUAL(article->children->title, "Get started with Git Client");
  ASSERT_STR_EQUAL(search->title, "");
  send_message(viewer, evCommand, MAKEDWORD(forward->id, btnClicked), forward);
  ASSERT_STR_EQUAL(article->children->title, "Fetch, pull and push");

  ASSERT_TRUE(help_open(owner, path, "commit"));
  ASSERT_STR_EQUAL(article->children->title, "Review, stage and commit");
  resize_window(viewer, 700, 260);
  ASSERT_TRUE(article->vscroll.visible);
  send_message(article, evVScroll, article->vscroll.max_val, NULL);
  ASSERT(get_scroll_pos(article, SB_VERT) > 0, "Illustrated topic should scroll");
  ASSERT_TRUE(help_open(owner, path, "branches"));
  ASSERT_EQUAL(get_scroll_pos(article, SB_VERT), 0);
  destroy_window(owner);
  ASSERT_FALSE(is_window(viewer));

  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("Orion Help");
  test_offline_help();
  TEST_END();
}
