// Terminal scrolling test
#include "test_framework.h"
#include "test_env.h"
#include "../ui.h"
#include <string.h>

void test_terminal_has_vscroll_flag(void) {
  TEST("Terminal has VSCROLL flag");
  
  test_env_init();
  
  // Create terminal
  rect_t frame = {10, 10, 300, 150};
  window_t *terminal = create_window("Terminal Scroll Test", 0, &frame, NULL, win_terminal, 0, NULL);
  ASSERT_NOT_NULL(terminal);
  
  // Verify VSCROLL flag is set
  ASSERT_TRUE(terminal->flags & WINDOW_VSCROLL);
  
  // Initial scroll position should be 0
  ASSERT_EQUAL(terminal->scroll[1], 0);
  
  destroy_window(terminal);
  test_env_shutdown();
  PASS();
}

void test_text_wrapping_calculation(void) {
  TEST("Text height calculation with wrapping");
  
  test_env_init();
  
  // Test with NULL/empty text
  ASSERT_EQUAL(calc_text_height(NULL, 200), 0);
  ASSERT_EQUAL(calc_text_height("", 200), 0);
  ASSERT_EQUAL(calc_text_height("test", 0), 0);
  
  test_env_shutdown();
  PASS();
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  TEST_START("Terminal Scrolling Tests");
  
  test_terminal_has_vscroll_flag();
  test_text_wrapping_calculation();
  
  TEST_END();
}
