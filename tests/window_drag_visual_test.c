// A visual drag paints the real child under the cursor and leaves its frame put.
#include "test_framework.h"
#include <orion/ui.h>
#include <orion/user/gl_compat.h>

#if defined(__APPLE__) && !TARGET_OS_IOS
static result_t paint_color(window_t *win, uint32_t msg, uint32_t wp, void *lp) {
  (void)wp; (void)lp;
  if (msg == evPaint) {
    fill_rect((uint32_t)(uintptr_t)win->userdata, get_client_rect(win));
    // The card swallows paint. The root and the panel let their children paint.
    return win->parent && win->parent->parent;
  }
  return msg == evCreate || msg == evDestroy;
}

static void read_logical(window_t *root, int x, int y, uint8_t px[4]) {
  int scale = root->surface_w / root->frame.w;
  if (scale < 1) scale = 1;
  glBindFramebuffer(GL_FRAMEBUFFER, root->surface_fbo);
  glReadPixels(x * scale + scale / 2, root->surface_h - (y * scale + scale / 2),
               1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
}

static bool is_red(const uint8_t px[4])   { return px[0] == 255 && px[1] == 0 && px[2] == 0; }
static bool is_green(const uint8_t px[4]) { return px[0] == 0 && px[1] == 255 && px[2] == 0; }
static bool is_blue(const uint8_t px[4])  { return px[0] == 0 && px[1] == 0 && px[2] == 255; }

static void test_drag_visual_moves_paint_only(void) {
  TEST("Drag visual hides the child in place and paints it past its parent");
  if (!ui_init_graphics(UI_INIT_HIDDEN, "drag-visual", 220, 160)) {
    SKIP("Offscreen graphics unavailable");
  }
  window_t *root = create_window("root", WINDOW_NOTITLE | WINDOW_NOFILL,
    MAKERECT(0, 0, 220, 160), NULL, paint_color, 0, NULL);
  window_t *panel = create_window("panel", WINDOW_NOTITLE | WINDOW_NOFILL,
    MAKERECT(0, 0, 100, 160), root, paint_color, 0, NULL);
  window_t *card = create_window("card", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_TRANSPARENT,
    MAKERECT(20, 50, 40, 40), panel, paint_color, 0, NULL);
  if (!root || !panel || !card) {
    ui_shutdown_graphics();
    ASSERT_NOT_NULL(root);
    ASSERT_NOT_NULL(panel);
    ASSERT_NOT_NULL(card);
  }
  root->userdata = (void *)(uintptr_t)0xFF0000FF;
  panel->userdata = (void *)(uintptr_t)0xFFFF0000;
  card->userdata = (void *)(uintptr_t)0xFF00FF00;
  show_window(root, true);

  send_message(root, evNCPaint, 0, NULL);
  send_message(root, evPaint, 0, NULL);
  uint8_t home[4] = {0}, away[4] = {0};
  read_logical(root, 40, 70, home);
  read_logical(root, 140, 70, away);
  fprintf(stderr, "[drag-visual] in place home=%u,%u,%u away=%u,%u,%u\n",
          home[0], home[1], home[2], away[0], away[1], away[2]);

  int frame_x = card->frame.x, frame_y = card->frame.y;
  window_set_drag_visual(card, 100, 0);
  bool frame_held = card->frame.x == frame_x && card->frame.y == frame_y;
  send_message(root, evPaint, 0, NULL);
  uint8_t hole[4] = {0}, lifted[4] = {0}, under[4] = {0}, side[4] = {0}, far_px[4] = {0};
  read_logical(root, 40, 70, hole);
  read_logical(root, 140, 70, lifted);
  // Card paint is (120,50,40,40). Four pixels below is the drop shadow;
  // four pixels to the side is only its fringe.
  read_logical(root, 140, 94, under);
  read_logical(root, 164, 70, side);
  read_logical(root, 200, 140, far_px);
  fprintf(stderr, "[drag-visual] lifted hole=%u,%u,%u card=%u,%u,%u under=%u,%u,%u side=%u,%u,%u far=%u,%u,%u\n",
          hole[0], hole[1], hole[2], lifted[0], lifted[1], lifted[2],
          under[0], under[1], under[2], side[0], side[1], side[2], far_px[0], far_px[1], far_px[2]);

  window_set_drag_copy(card, 100, 0);
  send_message(root, evPaint, 0, NULL);
  uint8_t copy_home[4] = {0}, copy_away[4] = {0};
  read_logical(root, 40, 70, copy_home);
  read_logical(root, 140, 70, copy_away);

  window_clear_drag_visual(card);
  frame_held = frame_held && card->frame.x == frame_x && card->frame.y == frame_y;
  send_message(root, evPaint, 0, NULL);
  uint8_t back[4] = {0}, cleared[4] = {0}, under_gone[4] = {0};
  read_logical(root, 40, 70, back);
  read_logical(root, 140, 70, cleared);
  read_logical(root, 140, 94, under_gone);
  fprintf(stderr, "[drag-visual] cleared home=%u,%u,%u away=%u,%u,%u under=%u,%u,%u\n",
          back[0], back[1], back[2], cleared[0], cleared[1], cleared[2],
          under_gone[0], under_gone[1], under_gone[2]);
  fflush(stderr);

  ui_shutdown_graphics();
  ASSERT_TRUE(frame_held);
  ASSERT_TRUE(is_green(home));
  ASSERT_TRUE(is_red(away));
  ASSERT_TRUE(is_blue(hole));
  ASSERT_TRUE(is_green(lifted));
  ASSERT_TRUE(under[0] < 180 && under[1] == 0 && under[2] == 0);
  ASSERT_TRUE(under[0] < side[0]);
  ASSERT_TRUE(is_red(far_px));
  ASSERT_TRUE(is_green(copy_home));
  ASSERT_TRUE(is_green(copy_away));
  ASSERT_TRUE(is_green(back));
  ASSERT_TRUE(is_red(cleared));
  ASSERT_TRUE(is_red(under_gone));
  PASS();
}
#else
static void test_drag_visual_moves_paint_only(void) {
  TEST("Drag visual hides the child in place and paints it past its parent");
  SKIP("Requires the macOS offscreen platform framebuffer");
}
#endif

int main(void) {
  TEST_START("Window drag visual");
  test_drag_visual_moves_paint_only();
  TEST_END();
}
