#include "test_framework.h"
#include <orion/ui.h>
#include <orion/user/gl_compat.h>

#if defined(__APPLE__) && !TARGET_OS_IOS
static result_t composite_test_proc(window_t *win, uint32_t msg, uint32_t wp, void *lp) {
  if (msg == evPaint) fill_rect(0xFFFFFFFF, get_client_rect(win));
  return true;
}

static result_t status_host_proc(window_t *win, uint32_t msg, uint32_t wp, void *lp) {
  return false;
}

static uint32_t g_paint_color;

static result_t color_paint_proc(window_t *win, uint32_t msg, uint32_t wp, void *lp) {
  (void)wp; (void)lp;
  if (msg == evPaint) fill_rect(g_paint_color, get_client_rect(win));
  return true;
}

static void read_window_pixel(window_t *win, int x, int y, uint8_t pixel[4]) {
  glBindFramebuffer(GL_FRAMEBUFFER, win->surface_fbo);
  glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
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
  // App chrome spans the workspace but leaves its center transparent.
  window_t *chrome = create_app_chrome("chrome", NULL, NULL, 0, NULL, 0);
  ASSERT_NOT_NULL(chrome);
  repost_messages();

  GLint presented_fbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &presented_fbo);
  axBindFramebuffer();
  uint8_t pixel[4] = {0};
  glReadPixels(128, 128, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
  GLenum error = glGetError();
  fprintf(stderr, "[composite-test] screen=%d presented=%d pixel=%u,%u,%u,%u error=0x%x\n",
    screen_fbo, presented_fbo, pixel[0], pixel[1], pixel[2], pixel[3], error);
  glClearColor(1, 1, 1, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  draw_rect_shadow(R(80, 80, 96, 96), 8, 8, (ipoint16_t){0, 0}, 0x80000000);
  GLint viewport[4];
  glGetIntegerv(GL_VIEWPORT, viewport);
  uint8_t inside[4], near_edge[4], far_edge[4];
  glReadPixels(viewport[2] / 2, viewport[3] / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, inside);
  glReadPixels(178 * viewport[2] / 256, viewport[3] / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, near_edge);
  glReadPixels(220 * viewport[2] / 256, viewport[3] / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, far_edge);
  GLenum shadow_error = glGetError();
  fprintf(stderr, "[composite-test] shadow viewport=%d,%d inside=%u near=%u far=%u error=0x%x\n",
    viewport[2], viewport[3], inside[0], near_edge[0], far_edge[0], shadow_error);
  // An oversized child must not overwrite the status row, with or without
  // the horizontal bar. Use a distinctive background and inspect the FBO.
  window_t *status = create_window("", WINDOW_STATUSBAR | WINDOW_HSCROLL,
    MAKERECT(0, 0, 200, 200), NULL, status_host_proc, 0, NULL);
  window_t *child = create_window("", WINDOW_NOTITLE,
    MAKERECT(0, 0, 300, 300), status, composite_test_proc, 0, NULL);
  show_window(child, true);
  int color_id = brStatusbarBg;
  uint32_t red = 0xff0000ff;
  set_sys_colors(1, &color_id, &red);
  bool status_ok = true;
  for (int visible = 0; visible < 2; visible++) {
    scroll_info_t info = {.fMask = SIF_ALL, .nMax = visible ? 400 : 100, .nPage = 200};
    set_scroll_info(status, SB_HORZ, &info, false);
    send_message(status, evNCPaint, 0, NULL);
    send_message(status, evPaint, 0, NULL);
    int scale = status->surface_w / status->frame.w;
    uint8_t row_pixel[4];
    glReadPixels(190 * scale, 5 * scale, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, row_pixel);
    fprintf(stderr, "[composite-test] status hscroll=%d pixel=%u,%u,%u\n",
            visible, row_pixel[0], row_pixel[1], row_pixel[2]);
    status_ok &= row_pixel[0] == 255 && row_pixel[1] == 0 && row_pixel[2] == 0;
  }
  ui_shutdown_graphics();
  ASSERT_TRUE(status_ok);
  ASSERT_EQUAL(shadow_error, GL_NO_ERROR);
  ASSERT_TRUE(inside[0] < near_edge[0]);
  ASSERT_TRUE(near_edge[0] < far_edge[0]);
  ASSERT_EQUAL(far_edge[0], 255);
  ASSERT_NOT_EQUAL(screen_fbo, 0);
  ASSERT_EQUAL(presented_fbo, screen_fbo);
  ASSERT_EQUAL(error, GL_NO_ERROR);
  ASSERT_EQUAL(pixel[0], 255);
  ASSERT_EQUAL(pixel[1], 255);
  ASSERT_EQUAL(pixel[2], 255);
  PASS();
}

static void test_paint_binds_own_surface(void) {
  TEST("Interleaved root paints stay in each window's own surface");
  if (!ui_init_graphics(UI_INIT_HIDDEN, "paint-target-test", 256, 256)) {
    SKIP("Offscreen graphics unavailable");
  }
  window_t *bar = create_window("bar", WINDOW_NOTITLE,
    MAKERECT(0, 0, 200, 24), NULL, color_paint_proc, 0, NULL);
  window_t *popup = create_window("popup", WINDOW_NOTITLE,
    MAKERECT(8, 24, 80, 96), NULL, color_paint_proc, 0, NULL);
  ASSERT_NOT_NULL(bar);
  ASSERT_NOT_NULL(popup);
  show_window(bar, true);
  show_window(popup, true);
  send_message(bar, evNCPaint, 0, NULL);
  send_message(popup, evNCPaint, 0, NULL);
  g_paint_color = 0xFF00FF00;
  send_message(popup, evPaint, 0, NULL);
  g_paint_color = 0xFF0000FF;
  send_message(bar, evPaint, 0, NULL);

  int bar_scale = bar->surface_h / bar->frame.h;
  int popup_scale = popup->surface_h / popup->frame.h;
  uint8_t bar_px[4] = {0}, popup_center[4] = {0}, popup_bottom[4] = {0};
  read_window_pixel(bar, 100 * bar_scale, (bar->surface_h / 2), bar_px);
  read_window_pixel(popup, 40 * popup_scale, popup->surface_h / 2, popup_center);
  read_window_pixel(popup, 40 * popup_scale, 2 * popup_scale, popup_bottom);
  fprintf(stderr, "[paint-target] bar=%u,%u,%u popup_c=%u,%u,%u popup_b=%u,%u,%u\n",
          bar_px[0], bar_px[1], bar_px[2],
          popup_center[0], popup_center[1], popup_center[2],
          popup_bottom[0], popup_bottom[1], popup_bottom[2]);
  fflush(stderr);
  ui_shutdown_graphics();
  ASSERT_EQUAL(bar_px[0], 255);
  ASSERT_EQUAL(bar_px[1], 0);
  ASSERT_EQUAL(bar_px[2], 0);
  ASSERT_EQUAL(popup_center[0], 0);
  ASSERT_EQUAL(popup_center[1], 255);
  ASSERT_EQUAL(popup_center[2], 0);
  ASSERT_EQUAL(popup_bottom[0], 0);
  ASSERT_EQUAL(popup_bottom[1], 255);
  ASSERT_EQUAL(popup_bottom[2], 0);
  PASS();
}

#else
static void test_platform_framebuffer(void) {
  TEST("Window compositing draws into the platform's nonzero framebuffer");
  SKIP("Requires the macOS offscreen platform framebuffer");
}

static void test_paint_binds_own_surface(void) {
  TEST("Interleaved root paints stay in each window's own surface");
  SKIP("Requires the macOS offscreen platform framebuffer");
}
#endif

int main(void) {
  TEST_START("Window compositing");
  test_platform_framebuffer();
  test_paint_binds_own_surface();
  TEST_END();
}
