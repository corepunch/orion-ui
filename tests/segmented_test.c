// SegmentedControl: radio-group selection as a child window and as a toolbar item.

#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/commctl/commctl.h>
#include <orion/user/toolbar.h>

#define ID_SEG 77

static int g_changes, g_last_selection;

static result_t host_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win;
  if (msg == evCreate || msg == evDestroy) return true;
  if (msg == evCommand && LOWORD(wparam) == ID_SEG && HIWORD(wparam) == sgnSelChange) {
    g_changes++;
    g_last_selection = (int)send_message(lparam, sgGetSelection, 0, NULL);
    return true;
  }
  return false;
}

static void click_at(int x, int y) {
  ui_event_t ev = {.message = kEventLeftButtonDown, .x = x * UI_WINDOW_SCALE, .y = y * UI_WINDOW_SCALE};
  dispatch_message(&ev);
  ev.message = kEventLeftButtonUp;
  dispatch_message(&ev);
}

static ipoint16_t segment_center(window_t *seg, int index) {
  irect16_t r = {0};
  send_message(seg, sgGetSegmentRect, index, &r);
  return (ipoint16_t){ (int16_t)(window_screen_x(seg) + r.x + r.w / 2), (int16_t)(window_screen_y(seg) + r.y + r.h / 2) };
}

static void test_selection(void) {
  TEST("segments come from the title; one is selected; set/get and bounds are checked");
  test_env_init();
  window_t *host = create_window("Host", 0, MAKERECT(20, 20, 400, 200), NULL, host_proc, 0, NULL);
  window_t *seg = create_window("All|Dance|Hip Hop", WINDOW_NOTITLE, MAKERECT(10, 10, 300, 24), host, win_segmented, 0, NULL);
  ASSERT_NOT_NULL(seg);
  seg->id = ID_SEG;
  show_window(host, true);
  ASSERT_EQUAL(send_message(seg, sgGetCount, 0, NULL), 3);
  ASSERT_EQUAL(send_message(seg, sgGetSelection, 0, NULL), 0);
  g_changes = 0;
  ASSERT_TRUE(send_message(seg, sgSetSelection, 2, NULL));
  ASSERT_EQUAL(send_message(seg, sgGetSelection, 0, NULL), 2);
  ASSERT(g_changes == 0, "programmatic selection does not notify");
  ASSERT_FALSE(send_message(seg, sgSetSelection, 3, NULL));
  ASSERT_EQUAL(send_message(seg, sgGetSelection, 0, NULL), 2);
  ASSERT_EQUAL(send_message(seg, sgAddSegment, 0, "Rave"), 3);
  ASSERT_EQUAL(send_message(seg, sgGetCount, 0, NULL), 4);
  irect16_t a, b, bad;
  ASSERT_TRUE(send_message(seg, sgGetSegmentRect, 0, &a) && send_message(seg, sgGetSegmentRect, 3, &b));
  ASSERT(a.x == SEGMENTED_INSET && b.x + b.w == 300 - SEGMENTED_INSET, "segments fill the track");
  ASSERT_FALSE(send_message(seg, sgGetSegmentRect, 4, &bad));
  send_message(seg, sgSetSegments, 0, "One|Two");
  ASSERT_EQUAL(send_message(seg, sgGetCount, 0, NULL), 2);
  ASSERT(send_message(seg, sgGetSelection, 0, NULL) == 1, "selection is clamped when segments are replaced");
  for (int i = 0; i < SEGMENTED_MAX_SEGMENTS + 2; i++) send_message(seg, sgAddSegment, 0, "More");
  ASSERT_EQUAL(send_message(seg, sgGetCount, 0, NULL), SEGMENTED_MAX_SEGMENTS);
  destroy_window(host);
  test_env_shutdown();
  PASS();
}

static void test_radio_input(void) {
  TEST("click and arrow keys select exactly one segment and notify the parent once per change");
  test_env_init();
  window_t *host = create_window("Host", 0, MAKERECT(20, 20, 400, 200), NULL, host_proc, 0, NULL);
  window_t *seg = create_window("All|Dance|Hip Hop|Rave", WINDOW_NOTITLE, MAKERECT(10, 10, 320, 24), host, win_segmented, 0, NULL);
  seg->id = ID_SEG;
  show_window(host, true);
  g_changes = 0; g_last_selection = -1;
  ipoint16_t p = segment_center(seg, 2);
  click_at(p.x, p.y);
  ASSERT(g_changes == 1 && g_last_selection == 2, "click selects and notifies");
  ASSERT_NULL(g_ui_runtime.captured);
  click_at(p.x, p.y);
  ASSERT(g_changes == 1, "clicking the selected segment keeps it selected without notifying");
  ui_event_t ev = {.message = kEventLeftButtonDown, .x = p.x * UI_WINDOW_SCALE, .y = p.y * UI_WINDOW_SCALE};
  ipoint16_t other = segment_center(seg, 0);
  ev.x = other.x * UI_WINDOW_SCALE;
  dispatch_message(&ev);
  ASSERT_TRUE(g_ui_runtime.captured == seg);
  ev.message = kEventLeftButtonUp;
  ev.x = p.x * UI_WINDOW_SCALE;
  dispatch_message(&ev);
  ASSERT(g_changes == 1 && send_message(seg, sgGetSelection, 0, NULL) == 2, "releasing on another segment cancels the press");
  ASSERT_NULL(g_ui_runtime.captured);
  set_focus(seg);
  send_message(seg, evKeyDown, AX_KEY_RIGHTARROW, NULL);
  ASSERT(g_changes == 2 && g_last_selection == 3, "right arrow moves the selection");
  send_message(seg, evKeyDown, AX_KEY_RIGHTARROW, NULL);
  ASSERT(g_changes == 2, "the selection stops at the last segment");
  send_message(seg, evKeyDown, AX_KEY_LEFTARROW, NULL);
  ASSERT(g_changes == 3 && g_last_selection == 2, "left arrow moves back");
  window_set_state(seg, WINDOW_STATE_DISABLED, true);
  send_message(seg, evLeftButtonDown, MAKEDWORD(other.x - window_screen_x(seg), 12), NULL);
  send_message(seg, evLeftButtonUp, MAKEDWORD(other.x - window_screen_x(seg), 12), NULL);
  ASSERT(g_changes == 3, "a disabled control ignores input");
  destroy_window(host);
  test_env_shutdown();
  PASS();
}

static void test_toolbar_item(void) {
  TEST("TOOLBAR_ITEM_SEGMENTED embeds the control, fits its labels and routes clicks to the toolbar owner");
  test_env_init();
  window_t *bar = create_window("", WINDOW_TOOLBAR | WINDOW_NOTITLE | WINDOW_NORESIZE,
                                MAKERECT(40, 50, 600, 60), NULL, host_proc, 0, NULL);
  toolbar_item_t items[] = {
    { TOOLBAR_ITEM_BUTTON,    1,      "missing", 0, 0, NULL, "Button" },
    { TOOLBAR_ITEM_SEGMENTED, ID_SEG, NULL,      0, 0, "All|Dance|Hip Hop|Rave|Techno", "Genre" },
    { TOOLBAR_ITEM_BUTTON,    2,      "missing", 0, 0, NULL, "Button" },
  };
  send_message(bar, tbSetItems, ARRAY_LEN(items), items);
  show_window(bar, true);
  window_t *seg = get_window_item(bar, ID_SEG);
  ASSERT_NOT_NULL(seg);
  ASSERT_TRUE(seg->parent == bar && seg->proc == win_segmented);
  toolbar_state_t *tb = toolbar_get_state(bar);
  layout_measure_t m = {0};
  send_message(seg, evMeasure, 0, &m);
  ASSERT(m.desired_w > 0 && seg->frame.w == m.desired_w, "automatic width is the control's own measure");
  ASSERT(tb->item_rects[2].x >= seg->frame.x + seg->frame.w, "following items sit after the fitted control");
  ASSERT_EQUAL(seg->frame.h, toolbar_effective_bsz(bar) - 4);
  g_changes = 0; g_last_selection = -1;
  ipoint16_t p = segment_center(seg, 3);
  click_at(p.x, p.y);
  ASSERT(g_changes == 1 && g_last_selection == 3, "routed toolbar click selects the segment");
  ASSERT_NULL(g_ui_runtime.captured);
  toolbar_item_t fixed = { TOOLBAR_ITEM_SEGMENTED, ID_SEG, NULL, 180, 0, "A|B", NULL };
  send_message(bar, tbSetItems, 1, &fixed);
  seg = get_window_item(bar, ID_SEG);
  ASSERT(seg && seg->frame.w == 180, "explicit width is kept");
  destroy_window(bar);
  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("SegmentedControl");
  test_selection();
  test_radio_input();
  test_toolbar_item();
  TEST_END();
}
