#include "test_framework.h"
#include <orion/ui.h>
#include <orion/user/gl_compat.h>
#include <string.h>

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
  glBindFramebuffer(GL_FRAMEBUFFER, window_surface(win)->fbo);
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
    int scale = window_surface(status)->w / status->frame.w;
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

  int bar_scale = window_surface(bar)->h / bar->frame.h;
  int popup_scale = window_surface(popup)->h / popup->frame.h;
  uint8_t bar_px[4] = {0}, popup_center[4] = {0}, popup_bottom[4] = {0};
  read_window_pixel(bar, 100 * bar_scale, (window_surface(bar)->h / 2), bar_px);
  read_window_pixel(popup, 40 * popup_scale, window_surface(popup)->h / 2, popup_center);
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

static void test_focus_preserves_transparent_chrome(void) {
  TEST("Focus changes preserve toolbar contents on a transparent chrome surface");
  if (!ui_init_graphics(UI_INIT_HIDDEN, "chrome-focus-test", 256, 256)) {
    SKIP("Offscreen graphics unavailable");
  }
  window_t *document = create_window("document", WINDOW_NOTITLE,
    MAKERECT(0, 0, 256, 256), NULL, composite_test_proc, 0, NULL);
  window_t *chrome = create_window("chrome", WINDOW_NOTITLE | WINDOW_TRANSPARENT | WINDOW_NOFILL,
    MAKERECT(0, 0, 256, 256), NULL, status_host_proc, 0, NULL);
  ASSERT_NOT_NULL(document);
  ASSERT_NOT_NULL(chrome);
  const irect16_t frames[] = {R(0, 0, 256, 24), R(0, 24, 24, 232), R(128, 80, 24, 96)};
  const ipoint16_t points[] = {{8, 8}, {8, 64}, {136, 88}};
  window_t *bars[3];
  for (int i = 0; i < 3; i++) {
    bars[i] = create_window("bar", WINDOW_NOTITLE, &frames[i], chrome, color_paint_proc, 0, NULL);
    ASSERT_NOT_NULL(bars[i]);
    show_window(bars[i], true);
  }
  g_paint_color = 0xFF0000FF;
  show_window(document, true);
  show_window(chrome, true);
  set_focus(bars[1]);
  repost_messages();
  repost_messages();
  int scale = window_surface(chrome)->w / chrome->frame.w;
  uint8_t before[3][4], after[3][4], cleared[4];
  bool painted = true, preserved = true;
  for (int i = 0; i < 3; i++) {
    read_window_pixel(chrome, points[i].x * scale, window_surface(chrome)->h - points[i].y * scale - 1, before[i]);
    painted &= before[i][0] == 255 && before[i][3] == 255;
  }
  set_focus(document);
  repost_messages();
  for (int i = 0; i < 3; i++) {
    read_window_pixel(chrome, points[i].x * scale, window_surface(chrome)->h - points[i].y * scale - 1, after[i]);
    preserved &= memcmp(before[i], after[i], 4) == 0;
  }
  show_window(bars[2], false);
  invalidate_window(chrome);
  repost_messages();
  read_window_pixel(chrome, points[2].x * scale, window_surface(chrome)->h - points[2].y * scale - 1, cleared);
  ui_shutdown_graphics();
  ASSERT_TRUE(painted);
  ASSERT_TRUE(preserved);
  ASSERT_EQUAL(cleared[3], 0);
  PASS();
}

static void read_screen_pixel(int x, int y, uint8_t pixel[4]) {
  axBindFramebuffer();
  struct AXsize size;
  axGetSize(&size);
  float scale = axGetScaling();
  int width = (int)(size.width * scale + 0.5f), height = (int)(size.height * scale + 0.5f);
  int logical_w = ui_get_system_metrics(kSystemMetricScreenWidth);
  int logical_h = ui_get_system_metrics(kSystemMetricScreenHeight);
  glReadPixels(x * width / logical_w, height - y * height / logical_h - 1,
               1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
}

static void test_modal_composition(void) {
  TEST("Modal tint: immediate, stable across repaints, nested, and absent from cached surfaces");
  if (!ui_init_graphics(UI_INIT_HIDDEN, "modal-composite-test", 512, 512)) {
    SKIP("Offscreen graphics unavailable");
  }
  g_paint_color = 0xFF0000FF;
  int color_id = brModalOverlay;
  uint32_t overlay = 0x80FFFFFF;
  set_sys_colors(1, &color_id, &overlay);
  window_t *frame = create_window("frame", WINDOW_NOTITLE, MAKERECT(0, 0, 256, 256), NULL, color_paint_proc, 0, NULL);
  window_t *doc = create_window("document", WINDOW_NOTITLE, MAKERECT(40, 50, 80, 90), NULL, color_paint_proc, 0, NULL);
  window_t *palette = create_window("palette", WINDOW_NOTITLE, MAKERECT(180, 20, 30, 30), NULL, color_paint_proc, 0, NULL);
  window_t *dialog = create_window("dialog", WINDOW_NOTITLE | WINDOW_DIALOG, MAKERECT(100, 100, 50, 40), NULL, color_paint_proc, 0, NULL);
  window_t *other = create_window("other app", WINDOW_NOTITLE, MAKERECT(180, 180, 50, 50), NULL, color_paint_proc, 0, NULL);
  window_t *chrome = create_window("chrome", WINDOW_NOTITLE | WINDOW_TRANSPARENT | WINDOW_NOFILL,
                                 MAKERECT(0, 0, 256, 256), NULL, status_host_proc, 0, NULL);
  window_t *bar = create_window("bar", WINDOW_NOTITLE, MAKERECT(10, 180, 30, 30), chrome, color_paint_proc, 0, NULL);
  window_set_owner(doc, frame); window_set_owner(palette, frame);
  window_set_owner(dialog, doc); window_set_owner(chrome, frame);
  window_t *roots[] = {frame, doc, palette, dialog, other, chrome};
  for (int i = 0; i < ARRAY_LEN(roots); i++) show_window(roots[i], true);
  show_window(bar, true);
  move_to_top(other);
  repost_messages(); repost_messages();
  const ipoint16_t points[] = {{20,70}, {60,70}, {195,35}, {25,195}, {150,200}, {120,120}, {200,200}};
  uint8_t before[7][4], modal[7][4], repainted[7][4], restored[7][4];
  for (int i = 0; i < ARRAY_LEN(points); i++) read_screen_pixel(points[i].x, points[i].y, before[i]);
  g_ui_runtime.modal_window = dialog;
  request_composite(); repost_messages();
  for (int i = 0; i < ARRAY_LEN(points); i++) read_screen_pixel(points[i].x, points[i].y, modal[i]);
  for (int i = 0; i < ARRAY_LEN(roots); i++) invalidate_window(roots[i]);
  repost_messages(); repost_messages();
  for (int i = 0; i < ARRAY_LEN(points); i++) read_screen_pixel(points[i].x, points[i].y, repainted[i]);
  // The editor's surface must retain its original red content beneath the tint.
  uint8_t surface[4];
  read_window_pixel(doc, window_surface(doc)->w / 2, window_surface(doc)->h / 2, surface);
  window_t *nested = create_window("nested", WINDOW_NOTITLE | WINDOW_DIALOG,
                                  MAKERECT(10, 220, 40, 30), NULL, color_paint_proc, 0, NULL);
  window_set_owner(nested, dialog); show_window(nested, true);
  g_ui_runtime.modal_window = nested;
  request_composite(); repost_messages(); repost_messages();
  uint8_t nested_parent[4]; read_screen_pixel(120, 120, nested_parent);
  destroy_window(nested);
  g_ui_runtime.modal_window = dialog;
  request_composite(); repost_messages();
  uint8_t outer[4]; read_screen_pixel(120, 120, outer);
  g_ui_runtime.modal_window = NULL;
  request_composite(); repost_messages();
  for (int i = 0; i < ARRAY_LEN(points); i++) read_screen_pixel(points[i].x, points[i].y, restored[i]);
  GLenum error = glGetError();
  ui_shutdown_graphics();
  for (int i = 0; i < ARRAY_LEN(points); i++) {
    ASSERT_EQUAL(before[i][0], 255); ASSERT_EQUAL(before[i][1], 0);
    ASSERT(memcmp(modal[i], repainted[i], 4) == 0, "repaint must preserve modal appearance");
    ASSERT(memcmp(before[i], restored[i], 4) == 0, "closing restores cached content");
    if (i < 5) {
      ASSERT(modal[i][1] > 170 && modal[i][1] < 200, "one linear-light overlay");
    } else ASSERT(memcmp(before[i], modal[i], 4) == 0, "dialog and other app remain untinted");
  }
  ASSERT_EQUAL(surface[0], 255); ASSERT_EQUAL(surface[1], 0);
  ASSERT(memcmp(nested_parent, modal[0], 4) == 0, "nested modal blocks its parent exactly once");
  ASSERT(memcmp(outer, before[5], 4) == 0, "closing nested modal restores outer dialog");
  ASSERT_EQUAL(error, GL_NO_ERROR);
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
static void test_focus_preserves_transparent_chrome(void) {
  TEST("Focus changes preserve toolbar contents on a transparent chrome surface");
  SKIP("Requires the macOS offscreen platform framebuffer");
}
static void test_modal_composition(void) {
  TEST("Modal tint stays independent of cached window painting");
  SKIP("Requires the macOS offscreen platform framebuffer");
}

#endif

int main(void) {
  TEST_START("Window compositing");
  test_platform_framebuffer();
  test_paint_binds_own_surface();
  test_focus_preserves_transparent_chrome();
  test_modal_composition();
  TEST_END();
}
