// Event loop tests — headless, no SDL/OpenGL required.
//
// Covers the key behavioural changes introduced by the event-driven main loop:
//
//   1. dispatch_message() ignores the wakeup event type.
//   2. get_message() enters wait-mode on the first call per cycle, then
//      switches to poll-mode to drain remaining events, and resets to
//      wait-mode when the queue is empty.
//   3. post_message() deduplication still works after the wakeup push was
//      added (no internal messages are lost or duplicated).
//   4. invalidate_window() still enqueues both NonClientPaint and Paint
//      messages via post_message().
//
// Tests in groups 1-2 are pure-C with no link-time dependencies on SDL or
// OpenGL; groups 3-4 use test_env so that the real post_message /
// repost_messages / invalidate_window implementation is exercised (running is
// false, so all OpenGL calls are no-ops and SDL push is skipped because
// g_ui_repaint_event remains at its sentinel value).

#include "test_framework.h"
#include "test_env.h"
#include "../ui.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// =============================================================================
// Part 1: dispatch_message wakeup-guard logic (inlined, no SDL dependency)
// =============================================================================

// Sentinel value used in the real code to indicate "event type not yet
// registered".  Must match the definition of g_ui_repaint_event in
// kernel/event.c.  If that sentinel ever changes, update this too.
#define UI_REPAINT_SENTINEL ((uint32_t)-1)

// Inline replica of the guard at the top of dispatch_message().
// Returns true when the event should be silently ignored (it is a wakeup).
static bool should_ignore_event(uint32_t repaint_event, uint32_t evt_type) {
  return repaint_event != UI_REPAINT_SENTINEL && evt_type == repaint_event;
}

void test_sentinel_does_not_ignore_any_event(void) {
  TEST("dispatch guard: sentinel value never ignores any event type");
  // When g_ui_repaint_event == sentinel, the guard must be inactive.
  // No event type — not even one whose numeric value equals the sentinel —
  // should be silently dropped.
  ASSERT_FALSE(should_ignore_event(UI_REPAINT_SENTINEL, 0));
  ASSERT_FALSE(should_ignore_event(UI_REPAINT_SENTINEL, 1));
  ASSERT_FALSE(should_ignore_event(UI_REPAINT_SENTINEL, UI_REPAINT_SENTINEL));
  PASS();
}

void test_registered_event_is_ignored(void) {
  TEST("dispatch guard: registered wakeup event type is ignored");
  uint32_t repaint_event = 0x8000u; // arbitrary registered value
  ASSERT_TRUE(should_ignore_event(repaint_event, repaint_event));
  PASS();
}

void test_other_events_are_not_ignored(void) {
  TEST("dispatch guard: non-wakeup events are not ignored");
  uint32_t repaint_event = 0x8000u;
  ASSERT_FALSE(should_ignore_event(repaint_event, 0u));
  ASSERT_FALSE(should_ignore_event(repaint_event, 1u));
  ASSERT_FALSE(should_ignore_event(repaint_event, repaint_event - 1));
  ASSERT_FALSE(should_ignore_event(repaint_event, repaint_event + 1));
  PASS();
}

// =============================================================================
// Part 2: get_message() wait→poll state machine (inlined, no SDL dependency)
// =============================================================================

// Inline replica of the get_message() state machine from kernel/event.c.
// Instead of calling SDL, it invokes user-supplied fake functions so we can
// control return values and observe which path was taken.

static int sm_wait_return = 0;
static int sm_poll_return = 0;
static int sm_wait_call_count = 0;
static int sm_poll_call_count = 0;
static bool sm_draining = false;  // mirrors s_draining_queue in get_message()

static void sm_reset(void) {
  sm_wait_return    = 0;
  sm_poll_return    = 0;
  sm_wait_call_count = 0;
  sm_poll_call_count = 0;
  sm_draining        = false;
}

static int sm_fake_wait(void) { sm_wait_call_count++; return sm_wait_return; }
static int sm_fake_poll(void) { sm_poll_call_count++; return sm_poll_return; }

// Mirrors the get_message() body but uses the fake functions above.
static int sm_get_message(void) {
  if (sm_draining) {
    int r = sm_fake_poll();
    if (!r) sm_draining = false;
    return r;
  }
  sm_draining = true;
  return sm_fake_wait();
}

void test_first_call_uses_wait(void) {
  TEST("get_message: first call per cycle uses wait (not poll)");
  sm_reset();
  sm_wait_return = 1;  // simulated: an event arrived

  int r = sm_get_message();

  ASSERT_EQUAL(r, 1);
  ASSERT_EQUAL(sm_wait_call_count, 1);
  ASSERT_EQUAL(sm_poll_call_count, 0);
  ASSERT_TRUE(sm_draining);  // switched to drain mode
  PASS();
}

void test_subsequent_calls_use_poll(void) {
  TEST("get_message: subsequent calls in drain mode use poll");
  sm_reset();
  sm_wait_return = 1;
  sm_poll_return = 1;

  sm_get_message();  // first call → wait, enters drain mode
  sm_wait_call_count = 0;  // reset counters to check the second call only

  int r = sm_get_message();

  ASSERT_EQUAL(r, 1);
  ASSERT_EQUAL(sm_wait_call_count, 0);
  ASSERT_EQUAL(sm_poll_call_count, 1);
  ASSERT_TRUE(sm_draining);  // still draining
  PASS();
}

void test_empty_poll_resets_to_wait_mode(void) {
  TEST("get_message: poll returning 0 resets to wait mode");
  sm_reset();
  sm_wait_return = 1;
  sm_poll_return = 0;  // queue empty

  sm_get_message();  // first call → wait, enters drain mode

  int r = sm_get_message();  // second call → poll returns 0

  ASSERT_EQUAL(r, 0);
  ASSERT_FALSE(sm_draining);  // reset back to wait mode

  // Third call must use wait again (not poll).
  sm_wait_call_count = 0;
  sm_poll_call_count = 0;
  sm_wait_return = 1;
  sm_get_message();
  ASSERT_EQUAL(sm_wait_call_count, 1);
  ASSERT_EQUAL(sm_poll_call_count, 0);
  PASS();
}

void test_full_cycle_wait_then_drain_then_reset(void) {
  TEST("get_message: full cycle — wait, drain 2 events, empty, reset to wait");
  sm_reset();
  sm_wait_return = 1;
  sm_poll_return = 1;

  // Simulate: queue has 3 events — wait returns the first, poll returns 2 more,
  // then poll returns 0 (empty).
  int call = 0;
  int results[5] = {0};

  // call 0: wait → 1
  sm_wait_return = 1; sm_poll_return = 1;
  results[call++] = sm_get_message();  // wait, draining=true

  // call 1: poll → 1
  results[call++] = sm_get_message();  // poll

  // call 2: poll → 0 (empty), draining resets to false
  sm_poll_return = 0;
  results[call++] = sm_get_message();  // poll returns 0

  // call 3: wait again (new cycle starts)
  sm_wait_return = 1; sm_poll_return = 0;
  sm_wait_call_count = 0; sm_poll_call_count = 0;
  results[call++] = sm_get_message();  // must use wait

  ASSERT_EQUAL(results[0], 1);
  ASSERT_EQUAL(results[1], 1);
  ASSERT_EQUAL(results[2], 0);
  ASSERT_EQUAL(results[3], 1);
  ASSERT_EQUAL(sm_wait_call_count, 1);  // call 3 used wait
  ASSERT_EQUAL(sm_poll_call_count, 0);
  PASS();
}

// =============================================================================
// Part 3: post_message deduplication still works (integration, running=false)
// =============================================================================

// No-op window procedure used as a message sink.
static int msg_sink_count = 0;
static uint32_t msg_sink_last = 0;

static result_t noop_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)wparam; (void)lparam;
  if (msg == kWindowMessagePaint || msg == kWindowMessageNonClientPaint) {
    msg_sink_count++;
    msg_sink_last = msg;
  }
  return 1;
}

// Window procedures for test_post_message_different_targets_both_delivered.
static int count_proc_a = 0, count_proc_b = 0;

static result_t proc_a(window_t *w, uint32_t msg, uint32_t wp, void *lp) {
  (void)w; (void)wp; (void)lp;
  if (msg == kWindowMessagePaint) count_proc_a++;
  return 1;
}
static result_t proc_b(window_t *w, uint32_t msg, uint32_t wp, void *lp) {
  (void)w; (void)wp; (void)lp;
  if (msg == kWindowMessagePaint) count_proc_b++;
  return 1;
}

// Hooks for test_invalidate_window_enqueues_paint.
static int hook_nc_paint_count = 0;
static int hook_paint_count    = 0;
static void hook_nc(window_t *w, uint32_t msg, uint32_t wp, void *lp, void *ud) {
  (void)w; (void)msg; (void)wp; (void)lp; (void)ud;
  hook_nc_paint_count++;
}
static void hook_p(window_t *w, uint32_t msg, uint32_t wp, void *lp, void *ud) {
  (void)w; (void)msg; (void)wp; (void)lp; (void)ud;
  hook_paint_count++;
}

// Hook for test_invalidate_routes_to_root.
static window_t *last_nc_paint_target = NULL;
static void hook_nc_root(window_t *w, uint32_t msg, uint32_t wp, void *lp, void *ud) {
  (void)msg; (void)wp; (void)lp; (void)ud;
  last_nc_paint_target = w;
}

void test_post_message_deduplication(void) {
  TEST("post_message: duplicate msg+target is dropped from internal queue");
  test_env_init();

  window_t *win = test_env_create_window("dup-test", 10, 10, 100, 100,
                                          noop_proc, NULL);
  ASSERT_NOT_NULL(win);

  // Post the same message twice.  The second post should nullify the first
  // entry in the queue so that only one delivery happens.
  post_message(win, kWindowMessagePaint, 0, NULL);
  post_message(win, kWindowMessagePaint, 0, NULL);

  msg_sink_count = 0;
  repost_messages();

  // Because running==false the OpenGL calls inside kWindowMessagePaint are
  // skipped, but the window proc IS still called.  Only one call should occur
  // because duplicates are deduplicated by post_message().
  ASSERT_EQUAL(msg_sink_count, 1);

  destroy_window(win);
  test_env_shutdown();
  PASS();
}

void test_post_message_different_targets_both_delivered(void) {
  TEST("post_message: same message to different windows are both delivered");
  test_env_init();

  count_proc_a = 0; count_proc_b = 0;

  window_t *a = test_env_create_window("A", 10, 10, 100, 100, proc_a, NULL);
  window_t *b = test_env_create_window("B", 10, 10, 100, 100, proc_b, NULL);
  ASSERT_NOT_NULL(a);
  ASSERT_NOT_NULL(b);

  post_message(a, kWindowMessagePaint, 0, NULL);
  post_message(b, kWindowMessagePaint, 0, NULL);
  repost_messages();

  ASSERT_EQUAL(count_proc_a, 1);
  ASSERT_EQUAL(count_proc_b, 1);

  destroy_window(a);
  destroy_window(b);
  test_env_shutdown();
  PASS();
}

// =============================================================================
// Part 4: invalidate_window() enqueues paint messages (integration)
// =============================================================================

void test_invalidate_window_enqueues_paint(void) {
  TEST("invalidate_window: enqueues NonClientPaint + Paint via post_message");
  test_env_init();
  test_env_enable_tracking(true);

  hook_nc_paint_count = 0;
  hook_paint_count    = 0;

  register_window_hook(kWindowMessageNonClientPaint, hook_nc, NULL);
  register_window_hook(kWindowMessagePaint,          hook_p,  NULL);

  window_t *win = test_env_create_window("inv-test", 5, 5, 80, 60, noop_proc, NULL);
  ASSERT_NOT_NULL(win);

  // Reset counts — create_window itself may trigger some messages.
  hook_nc_paint_count = 0;
  hook_paint_count    = 0;

  invalidate_window(win);
  repost_messages();

  // invalidate_window() posts both NonClientPaint and Paint; repost_messages()
  // dispatches them (OpenGL calls are no-ops because running==false).
  ASSERT_TRUE(hook_nc_paint_count >= 1);
  ASSERT_TRUE(hook_paint_count    >= 1);

  deregister_window_hook(kWindowMessageNonClientPaint, hook_nc, NULL);
  deregister_window_hook(kWindowMessagePaint,          hook_p,  NULL);

  destroy_window(win);
  test_env_shutdown();
  PASS();
}

void test_invalidate_routes_to_root(void) {
  TEST("invalidate_window: child window invalidation routes to root");
  test_env_init();

  last_nc_paint_target = NULL;

  register_window_hook(kWindowMessageNonClientPaint, hook_nc_root, NULL);

  rect_t parent_frame = {10, 10, 200, 150};
  window_t *parent = create_window("Parent", 0, &parent_frame, NULL, noop_proc, NULL);
  ASSERT_NOT_NULL(parent);

  rect_t child_frame  = {5, 5, 50, 30};
  window_t *child = create_window("Child", 0, &child_frame, parent, noop_proc, NULL);
  ASSERT_NOT_NULL(child);

  last_nc_paint_target = NULL;

  // Invalidate the child — should route to the root (parent).
  invalidate_window(child);
  repost_messages();

  ASSERT_EQUAL(last_nc_paint_target, parent);

  deregister_window_hook(kWindowMessageNonClientPaint, hook_nc_root, NULL);

  destroy_window(parent);
  test_env_shutdown();
  PASS();
}

// =============================================================================
// main
// =============================================================================

int main(int argc, char *argv[]) {
  (void)argc; (void)argv;
  TEST_START("event-driven main loop");

  // Part 1: dispatch_message guard
  test_sentinel_does_not_ignore_any_event();
  test_registered_event_is_ignored();
  test_other_events_are_not_ignored();

  // Part 2: get_message state machine
  test_first_call_uses_wait();
  test_subsequent_calls_use_poll();
  test_empty_poll_resets_to_wait_mode();
  test_full_cycle_wait_then_drain_then_reset();

  // Part 3: post_message deduplication
  test_post_message_deduplication();
  test_post_message_different_targets_both_delivered();

  // Part 4: invalidate_window
  test_invalidate_window_enqueues_paint();
  test_invalidate_routes_to_root();

  TEST_END();
}
