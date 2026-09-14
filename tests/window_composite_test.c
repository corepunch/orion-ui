#include "test_framework.h"
#include <orion/ui.h>
#include <orion/user/gl_compat.h>

#if defined(__APPLE__) && !TARGET_OS_IOS
static result_t composite_test_proc(window_t *win, uint32_t msg, uint32_t wp, void *lp) {
  if (msg == evPaint) fill_rect(0xFFFFFFFF, get_client_rect(win));
  return true;
}

static void test_platform_framebuffer(void) {
  TEST("Window compositing draws into the platform's nonzero framebuffer");
  if (!ui_init_graphics(UI_INIT_HIDDEN, "composite-test", 256, 256)) {
    SKIP("Offscreen graphics unavailable");
  }
  axBindFramebuffer();
  GLint screen_fbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &screen_fbo);
  window_t *win = create_window("white", WINDOW_NOTITLE,
    MAKERECT(0, 0, 256, 256), NULL, composite_test_proc, 0, NULL);
  ASSERT_NOT_NULL(win);
  show_window(win, true);
  repost_messages();

  GLint presented_fbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &presented_fbo);
  axBindFramebuffer();
  uint8_t pixel[4] = {0};
  glReadPixels(128, 128, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
  GLenum error = glGetError();
  fprintf(stderr, "[composite-test] screen=%d presented=%d pixel=%u,%u,%u,%u error=0x%x\n",
    screen_fbo, presented_fbo, pixel[0], pixel[1], pixel[2], pixel[3], error);
  ui_shutdown_graphics();
  ASSERT_NOT_EQUAL(screen_fbo, 0);
  ASSERT_EQUAL(presented_fbo, screen_fbo);
  ASSERT_EQUAL(error, GL_NO_ERROR);
  ASSERT_EQUAL(pixel[0], 255);
  ASSERT_EQUAL(pixel[1], 255);
  ASSERT_EQUAL(pixel[2], 255);
  PASS();
}
#else
static void test_platform_framebuffer(void) {
  TEST("Window compositing draws into the platform's nonzero framebuffer");
  SKIP("Requires the macOS offscreen platform framebuffer");
}
#endif

int main(void) {
  TEST_START("Window compositing");
  test_platform_framebuffer();
  TEST_END();
}
