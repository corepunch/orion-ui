#include "test_framework.h"
#include "test_env.h"

static result_t label_layout_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)wparam; (void)lparam;
  return msg == evCreate || msg == evPaint || msg == evDestroy;
}

static void test_optional_labels(void) {
  TEST("optional labels release their frame and gaps, then reflow when text appears");
  test_env_init();
  for (int fallback = 0; fallback < 2; fallback++) {
    for (int horizontal = 0; horizontal < 2; horizontal++) {
      window_t *root = create_window("", WINDOW_NOTITLE | WINDOW_AUTO_LAYOUT,
        MAKERECT(0, 0, 320, 200), NULL, fallback ? label_layout_proc : win_stack, 0, NULL);
      window_set_layout(root, horizontal ? WINDOW_STACK_HORIZONTAL : WINDOW_STACK_VERTICAL, 8, R(0, 0, 0, 0));
      window_t *first = create_window("Summary", WINDOW_NOTITLE, MAKERECT(0, 0, 0, 0), root, win_label, 0, NULL);
      window_t *optional = create_window("", WINDOW_NOTITLE | LABEL_COLLAPSE_EMPTY,
        MAKERECT(0, 0, 0, 0), root, win_label, 0, NULL);
      window_t *last = create_window("Input", WINDOW_NOTITLE, MAKERECT(0, 0, 80, 25), root, win_textedit, 0, NULL);
      window_layout_sync(root);
      int compact = horizontal ? last->frame.x : last->frame.y;
      ASSERT_EQUAL(compact, (horizontal ? first->frame.w : first->frame.h) + 8);
      ASSERT_EQUAL(optional->frame.w, 0);
      ASSERT_EQUAL(optional->frame.h, 0);
      set_window_title(optional, "Previous request");
      ASSERT((horizontal ? last->frame.x : last->frame.y) > compact, "new text receives its own row or column");
      ASSERT_TRUE(optional->frame.w > 0 && optional->frame.h > 0);
      set_window_title(optional, "");
      ASSERT_EQUAL(horizontal ? last->frame.x : last->frame.y, compact);
      ASSERT_EQUAL(optional->frame.w, 0);
      ASSERT_EQUAL(optional->frame.h, 0);
      destroy_window(root);
    }
  }
  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("Optional label layout");
  test_optional_labels();
  TEST_END();
}
