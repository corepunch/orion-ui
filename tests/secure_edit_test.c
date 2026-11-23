#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/commctl/commctl.h>

static result_t parent_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)wparam; (void)lparam;
  return msg == evCreate || msg == evDestroy;
}

static void test_masked_edit(void) {
  TEST("password field: isolated input, bounded credential, wipe on read/clear");
  test_env_init();
  window_t *parent = test_env_create_window("P", 0, 0, 200, 100, parent_proc, NULL);
  ASSERT_NOT_NULL(parent);
  window_t *field = create_window("DO NOT KEEP", TEXTEDIT_PASSWORD,
                                  MAKERECT(5, 5, 180, 20), parent, win_textedit, 0, NULL);
  ASSERT_NOT_NULL(field);
  ASSERT_STR_EQUAL(field->title, "");
  set_focus(field);
  ASSERT_TRUE(send_message(field, evTextInput, 0, "A.b_9-/+=#^"));
  ASSERT_TRUE(send_message(field, evTextInput, 0, "rejected\nheader"));
  ASSERT_FALSE(send_message(field, edSetText, 0, "generic setter cannot carry credential"));
  ASSERT_EQUAL(send_message(field, edGetText, 0, NULL), 0);
  char out[TEXTEDIT_PASSWORD_CAPACITY] = "unchanged";
  ASSERT_EQUAL(send_message(field, edGetText, sizeof(out), out), 0);
  ASSERT_STR_EQUAL(out, "");
  ASSERT_STR_EQUAL(field->title, "");
  send_message(field, evKeyDown, AX_KEY_BACKSPACE, NULL);
  ASSERT_TRUE(textedit_take_password(field, out, sizeof(out)));
  ASSERT_STR_EQUAL(out, "A.b_9-/+=#");
  ASSERT_FALSE(textedit_take_password(field, out, sizeof(out)));
  ASSERT_STR_EQUAL(out, "");

  char oversized[TEXTEDIT_PASSWORD_CAPACITY + 1];
  memset(oversized, 's', sizeof(oversized)-1);
  oversized[sizeof(oversized)-1] = 0;
  send_message(field, evTextInput, 0, oversized);
  ASSERT_FALSE(textedit_take_password(field, out, sizeof(out)));

  oversized[TEXTEDIT_PASSWORD_CAPACITY-1] = 0;
  send_message(field, evTextInput, 0, oversized);
  ASSERT_TRUE(textedit_take_password(field, out, sizeof(out)));
  ASSERT_STR_EQUAL(out, oversized);
  send_message(field, evTextInput, 0, oversized);
  ASSERT_FALSE(textedit_take_password(field, out, sizeof(out)-1));
  ASSERT_FALSE(textedit_take_password(field, out, sizeof(out)));

  send_message(field, evTextInput, 0, "previousToken");
  ASSERT_TRUE(send_message(field, edSetText, 0, ""));
  send_message(field, evTextInput, 0, "replacementToken");
  ASSERT_TRUE(textedit_take_password(field, out, sizeof(out)));
  ASSERT_STR_EQUAL(out, "replacementToken");
  send_message(field, evTextInput, 0, "clearOnDestroy");
  destroy_window(parent);
  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("secure TextBox");
  test_masked_edit();
  TEST_END();
}
