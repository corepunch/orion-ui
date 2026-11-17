#include "test_framework.h"
#include "test_env.h"
#include "test_graphics.h"
#include <orion/commctl/commctl.h>
#include <orion/user/toolbar.h>

enum { ID_TARGET = 5, ID_LIST = 6, ID_REMOVE = 7 };

static result_t owner_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)wparam; (void)lparam;
  return msg == evCreate || msg == evPaint || msg == evDestroy;
}

// A list whose "item" part is its second child, like Jamsheet's selected block.
static result_t list_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  if (msg == evGetTutorialAnchor) {
    tutorial_anchor_t *anchor = lparam;
    if (!anchor || !anchor->part || strcmp(anchor->part, "item") || !win->children) return false;
    anchor->window = win->children->next;
    return anchor->window != NULL;
  }
  return owner_proc(win, msg, wparam, lparam);
}

static const tutorial_step_t kSteps[] = {
  { .target = ID_TARGET, .text = "This button starts it." },
  { .target = ID_REMOVE, .text = "Remove takes it away.", .until = ID_REMOVE },
  { .target = ID_LIST, .part = "item", .text = "The item is outlined." },
};
static const tutorial_def_t kTutorial = { .name = "orion_test.tutorial", .title = "Test", .steps = kSteps,
                                          .step_count = ARRAY_LEN(kSteps) };

// The ring is the smallest of the tutorial's popups; the scrim covers the owner.
static window_t *spot_window(void) {
  window_t *bubble = tutorial_bubble_window(), *spot = NULL;
  for (window_t *win = g_ui_runtime.windows; win; win = win->next)
    if (win != bubble && win->owner && (win->flags & WINDOW_ALWAYSONTOP) && window_has_state(win, WINDOW_STATE_VISIBLE) &&
        (!spot || win->frame.w * win->frame.h < spot->frame.w * spot->frame.h))
      spot = win;
  return spot;
}

static bool ring_surrounds(irect16_t target) {
  window_t *spot = spot_window();
  return spot && spot->frame.x <= target.x && spot->frame.y <= target.y &&
         spot->frame.x + spot->frame.w >= target.x + target.w && spot->frame.y + spot->frame.h >= target.y + target.h;
}

static irect16_t screen_frame(window_t *win) {
  return R(window_screen_x(win), window_screen_y(win), win->frame.w, win->frame.h);
}

static void test_steps(void) {
  TEST("tutorial: steps point at a control, a toolbar command and a part, then finish");
  test_env_init();
  window_t *owner = create_window("Owner", WINDOW_NOTITLE | WINDOW_TOOLBAR, MAKERECT(0, 0, 600, 420), NULL, owner_proc, 0, NULL);
  toolbar_item_t item = {.type = TOOLBAR_ITEM_BUTTON, .ident = ID_REMOVE, .tooltip = "Remove"};
  send_message(owner, tbSetItems, 1, &item);
  show_window(owner, true);
  window_t *button = create_window("Start", WINDOW_NOTITLE, MAKERECT(40, 60, 80, 30), owner, win_button, 0, NULL);
  button->id = ID_TARGET;
  window_t *list = create_window("List", WINDOW_NOTITLE, MAKERECT(200, 200, 300, 120), owner, list_proc, 0, NULL);
  list->id = ID_LIST;
  create_window("A", WINDOW_NOTITLE, MAKERECT(0, 0, 100, 30), list, owner_proc, 0, NULL);
  window_t *second = create_window("B", WINDOW_NOTITLE, MAKERECT(0, 40, 100, 30), list, owner_proc, 0, NULL);
  for (window_t *c = owner->children; c; c = c->next) show_window(c, true);
  for (window_t *c = list->children; c; c = c->next) show_window(c, true);

  ASSERT_TRUE(tutorial_start(owner, &kTutorial));
  window_t *bubble = tutorial_bubble_window();
  ASSERT_NOT_NULL(bubble);
  ASSERT_TRUE(window_has_state(bubble, WINDOW_STATE_VISIBLE));
  ASSERT_EQUAL(tutorial_current_step(), 0);
  irect16_t target = screen_frame(button);
  ASSERT_TRUE(ring_surrounds(target));
  ASSERT_TRUE(bubble->frame.y >= target.y + target.h);   // room below: the bubble hangs under it
  ASSERT_TRUE(bubble->frame.h > 0 && bubble->frame.w > 0);

  ASSERT_TRUE(tutorial_next());
  irect16_t tool = {0};
  ASSERT_TRUE(send_message(owner, tbGetItemRect, ID_REMOVE, &tool));
  tool = rect_offset(tool, window_screen_x(owner), window_screen_y(owner) + toolbar_content_offset(owner));
  ASSERT_TRUE(ring_surrounds(tool));
  tutorial_command(ID_TARGET);                // not the command this step waits for
  ASSERT_EQUAL(tutorial_current_step(), 1);
  tutorial_command(ID_REMOVE);
  ASSERT_EQUAL(tutorial_current_step(), 2);
  ASSERT_TRUE(ring_surrounds(screen_frame(second)));
  ASSERT_FALSE(ring_surrounds(tool));

  // The bubble lets the pointer through outside its body, and the ring always does.
  window_t *hit = owner;
  send_message(spot_window(), evHitTest, MAKEDWORD(2, 2), &hit);
  ASSERT_NULL(hit);

  ASSERT_FALSE(tutorial_next());               // past the last step it closes
  ASSERT_FALSE(tutorial_is_running(NULL));
  ASSERT_NULL(tutorial_bubble_window());
  ASSERT_TRUE(tutorial_seen("orion_test.tutorial"));
  ASSERT_FALSE(tutorial_start_once(owner, &kTutorial));
  ASSERT_TRUE(tutorial_set_seen("orion_test.tutorial", false));   // leave the shared settings as found
  ASSERT_FALSE(tutorial_seen("orion_test.tutorial"));
  test_env_shutdown();
  PASS();
}

static void test_owner_destroyed(void) {
  TEST("tutorial: destroying the owner ends the tutorial");
  test_env_init();
  window_t *owner = create_window("Owner", WINDOW_NOTITLE, MAKERECT(0, 0, 400, 300), NULL, owner_proc, 0, NULL);
  show_window(owner, true);
  ASSERT_TRUE(tutorial_start(owner, &kTutorial));
  destroy_window(owner);
  ASSERT_FALSE(tutorial_is_running(NULL));
  ASSERT_FALSE(tutorial_next());
  tutorial_stop();
  tutorial_set_seen("orion_test.tutorial", false);
  test_env_shutdown();
  PASS();
}

static void test_measured_text_fits_one_line(void) {
  TEST("text: a line wrapped at its own measured width stays one line");
  static const char *texts[] = { "New Song", "Open Song...", "Remove selected block (Delete)", "Step 1 of 3" };
  bool ok = true;
  for (size_t i = 0; i < ARRAY_LEN(texts); i++) {
    int w = text_strwidth("Tooltip", texts[i]);
    ok &= calc_text_height_font("Tooltip", texts[i], w) == text_line_height("Tooltip");
  }
  ASSERT_TRUE(ok);
  PASS();
}

int main(void) {
  TEST_START("Tutorials");
  if (!test_graphics_init(800, 600)) return 1;
  test_measured_text_fits_one_line();
  test_steps();
  test_owner_destroyed();
  test_graphics_shutdown();
  TEST_END();
}
