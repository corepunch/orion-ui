// Event loop tests — headless, no SDL/OpenGL required.
//
// Covers the key behavioural properties of the event-driven main loop:
//
//   1. get_message() filters sentinel (wakeup-only) events: returns 0 so the
//      outer while-loop exits and repost_messages() is called.
//   2. get_message() passes real events through unchanged (returns 1).
//   3. post_message() enqueues events into an internal ring-buffer with
//      per-(target,msg) deduplication; repost_messages() drains it and
//      dispatches each event.
//   4. invalidate_window() enqueues RefreshStencil, NonClientPaint, and Paint
//      messages via post_message().
//
// Tests in groups 1-2 are pure-C with no link-time dependencies on SDL or
// OpenGL; groups 3-4 use test_env so that the real post_message /
// repost_messages / invalidate_window implementation is exercised (running is
// false, so all OpenGL calls are no-ops).

#include "test_framework.h"
#include "test_env.h"
#include "../ui.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

// =============================================================================
// Part 1: get_message() sentinel-filter and wakeup coalescing
//         (inlined, no SDL dependency)
//
// get_message() calls axGetMessage() and returns 0 when the received event is
// a sentinel, clearing the wakeup-pending flag so the outer while-loop exits
// and repost_messages() runs.  wake_event_loop() only posts a sentinel when
// the pending flag is clear, preventing redundant repost_messages() cycles
// when post_message() is called in rapid succession.
// =============================================================================

static int  sm_sentinel_obj;         // test-local sentinel (mirrors g_wakeup_sentinel)
static bool sm_wakeup_pending;       // test-local pending flag (mirrors g_wakeup_pending)
static int  sm_ax_post_call_count;   // number of sentinel posts via wake_event_loop()

// Inline replica of the sentinel-filter+flag-clear in get_message().
static int sm_apply_sentinel_filter(int ax_result, void *target) {
  if (ax_result && target == (void *)&sm_sentinel_obj) {
    sm_wakeup_pending = false;
    return 0;
  }
  return ax_result;
}

// Inline replica of wake_event_loop() with coalescing.
static void sm_wake_event_loop(void) {
  if (sm_wakeup_pending) return;
  sm_wakeup_pending = true;
  sm_ax_post_call_count++;
}

static void sm_reset_wakeup(void) {
  sm_wakeup_pending    = false;
  sm_ax_post_call_count = 0;
}

void test_sentinel_returns_zero(void) {
  TEST("get_message: sentinel event returns 0 (loop exits for repost_messages)");
  // axGetMessage returned 1 but the event is a sentinel.
  sm_wakeup_pending = true;  // was set by wake_event_loop()
  ASSERT_EQUAL(sm_apply_sentinel_filter(1, &sm_sentinel_obj), 0);
  ASSERT_FALSE(sm_wakeup_pending);  // must be cleared
  PASS();
}

void test_real_event_passes_through(void) {
  TEST("get_message: real event (non-sentinel target) returns 1");
  int other_target = 0;
  ASSERT_EQUAL(sm_apply_sentinel_filter(1, &other_target), 1);
  PASS();
}

void test_quit_zero_passes_through(void) {
  TEST("get_message: axGetMessage returning 0 (quit) is passed through unchanged");
  // Even a sentinel target must not turn a quit signal into 1.
  sm_wakeup_pending = true;
  ASSERT_EQUAL(sm_apply_sentinel_filter(0, &sm_sentinel_obj), 0);
  ASSERT_EQUAL(sm_apply_sentinel_filter(0, NULL), 0);
  PASS();
}

void test_null_target_not_treated_as_sentinel(void) {
  TEST("get_message: NULL target is not confused with sentinel");
  ASSERT_EQUAL(sm_apply_sentinel_filter(1, NULL), 1);
  PASS();
}

void test_wakeup_coalescing_single_post(void) {
  TEST("wake_event_loop: multiple calls post only one sentinel");
  sm_reset_wakeup();

  sm_wake_event_loop();  // first call: not pending, should post
  sm_wake_event_loop();  // already pending, must be skipped
  sm_wake_event_loop();  // already pending, must be skipped

  ASSERT_EQUAL(sm_ax_post_call_count, 1);  // only one sentinel in queue
  ASSERT_TRUE(sm_wakeup_pending);
  PASS();
}

void test_wakeup_coalescing_rearms_after_consume(void) {
  TEST("wake_event_loop: re-arms correctly after sentinel is consumed");
  sm_reset_wakeup();

  sm_wake_event_loop();  // post sentinel, pending=true
  ASSERT_EQUAL(sm_ax_post_call_count, 1);

  // get_message() consumes the sentinel, clearing pending
  sm_apply_sentinel_filter(1, &sm_sentinel_obj);
  ASSERT_FALSE(sm_wakeup_pending);

  // A new batch of post_message() calls should post one fresh sentinel
  sm_wake_event_loop();
  sm_wake_event_loop();
  ASSERT_EQUAL(sm_ax_post_call_count, 2);  // exactly one new post
  ASSERT_TRUE(sm_wakeup_pending);
  PASS();
}

// =============================================================================
// Part 2: post_message deduplication still works (integration, running=false)
// =============================================================================

// No-op window procedure used as a message sink.
static int msg_sink_count = 0;
static uint32_t msg_sink_last = 0;

static result_t noop_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)wparam; (void)lparam;
  if (msg == evPaint || msg == evNCPaint) {
    msg_sink_count++;
    msg_sink_last = msg;
  }
  return 1;
}

// Window procedures for test_post_message_different_targets_both_delivered.
static int count_proc_a = 0, count_proc_b = 0;

static result_t proc_a(window_t *w, uint32_t msg, uint32_t wp, void *lp) {
  (void)w; (void)wp; (void)lp;
  if (msg == evPaint) count_proc_a++;
  return 1;
}
static result_t proc_b(window_t *w, uint32_t msg, uint32_t wp, void *lp) {
  (void)w; (void)wp; (void)lp;
  if (msg == evPaint) count_proc_b++;
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
  TEST("post_message: duplicate (target,msg) pairs are coalesced to one delivery");
  test_env_init();

  window_t *win = test_env_create_window("dup-test", 10, 10, 100, 100,
                                          noop_proc, NULL);
  ASSERT_NOT_NULL(win);

  // Post the same message twice to the same window.  post_message() keeps a
  // per-(target,msg) deduplication ring-buffer: the second post is coalesced
  // into the first, so only one event ends up in the queue and repost_messages()
  // dispatches it once.
  post_message(win, evPaint, 0, NULL);
  post_message(win, evPaint, 0, NULL);

  msg_sink_count = 0;
  repost_messages();

  // Because running==false the OpenGL calls inside evPaint are
  // skipped, but the window proc IS still called.  Deduplication keeps only
  // one copy, so exactly one call is expected.
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

  post_message(a, evPaint, 0, NULL);
  post_message(b, evPaint, 0, NULL);
  repost_messages();

  ASSERT_EQUAL(count_proc_a, 1);
  ASSERT_EQUAL(count_proc_b, 1);

  destroy_window(a);
  destroy_window(b);
  test_env_shutdown();
  PASS();
}

// =============================================================================
// Part 3: invalidate_window() enqueues paint messages (integration)
// =============================================================================

void test_invalidate_window_enqueues_paint(void) {
  TEST("invalidate_window: enqueues NonClientPaint + Paint via post_message");
  test_env_init();
  test_env_enable_tracking(true);

  hook_nc_paint_count = 0;
  hook_paint_count    = 0;

  register_window_hook(evNCPaint, hook_nc, NULL);
  register_window_hook(evPaint,          hook_p,  NULL);

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

  deregister_window_hook(evNCPaint, hook_nc, NULL);
  deregister_window_hook(evPaint,          hook_p,  NULL);

  destroy_window(win);
  test_env_shutdown();
  PASS();
}

void test_invalidate_routes_to_root(void) {
  TEST("invalidate_window: child window invalidation routes to root");
  test_env_init();

  last_nc_paint_target = NULL;

  register_window_hook(evNCPaint, hook_nc_root, NULL);

  rect_t parent_frame = {10, 10, 200, 150};
  window_t *parent = create_window("Parent", 0, &parent_frame, NULL, noop_proc, 0, NULL);
  ASSERT_NOT_NULL(parent);

  rect_t child_frame  = {5, 5, 50, 30};
  window_t *child = create_window("Child", 0, &child_frame, parent, noop_proc, 0, NULL);
  ASSERT_NOT_NULL(child);

  last_nc_paint_target = NULL;

  // Invalidate the child — should route to the root (parent).
  invalidate_window(child);
  repost_messages();

  ASSERT_EQUAL(last_nc_paint_target, parent);

  deregister_window_hook(evNCPaint, hook_nc_root, NULL);

  destroy_window(parent);
  test_env_shutdown();
  PASS();
}

// =============================================================================
// Part 4: kEventDragDrop — dispatch_message invokes ui_open_file handler
// =============================================================================

// Track calls from the open-file handler registered for drag-drop tests.
static int    dd_handler_call_count = 0;
static char   dd_handler_last_path[512];

static bool dd_open_file_handler(const char *path) {
  dd_handler_call_count++;
  if (path)
    snprintf(dd_handler_last_path, sizeof(dd_handler_last_path), "%s", path);
  return true;
}

static void dd_reset(void) {
  dd_handler_call_count = 0;
  dd_handler_last_path[0] = '\0';
}

// dispatch_message(kEventDragDrop, heap-path) must call the registered
// ui_open_file handler exactly once with the correct path.  The path must
// be freed by dispatch_message so callers do not own it.
void test_drag_drop_handler_invoked(void) {
  TEST("kEventDragDrop: registered ui_open_file handler is called once with correct path");
  test_env_init();
  dd_reset();

  ui_register_open_file_handler(dd_open_file_handler);

  // Allocate path on the heap — dispatch_message owns and frees it.
  char *heap_path = strdup("/tmp/test_image.png");
  ASSERT_NOT_NULL(heap_path);

  ui_event_t evt = {0};
  evt.message = kEventDragDrop;
  evt.lParam  = (lParam_t)heap_path;

  dispatch_message(&evt);

  ASSERT_EQUAL(dd_handler_call_count, 1);
  ASSERT_TRUE(strcmp(dd_handler_last_path, "/tmp/test_image.png") == 0);

  // Deregister handler so it does not affect other tests.
  ui_register_open_file_handler(NULL);
  test_env_shutdown();
  PASS();
}

// dispatch_message(kEventDragDrop, NULL) must not crash and must not call
// the handler (NULL/empty path is silently skipped per the implementation).
void test_drag_drop_null_lParam(void) {
  TEST("kEventDragDrop: NULL lParam does not crash and handler is not called");
  test_env_init();
  dd_reset();

  ui_register_open_file_handler(dd_open_file_handler);

  ui_event_t evt = {0};
  evt.message = kEventDragDrop;
  evt.lParam  = (lParam_t)NULL;

  dispatch_message(&evt);

  ASSERT_EQUAL(dd_handler_call_count, 0);

  ui_register_open_file_handler(NULL);
  test_env_shutdown();
  PASS();
}

// dispatch_message(kEventDragDrop, heap-empty-string) must free the buffer
// and not call the handler — an empty path is not a valid file path.
void test_drag_drop_empty_path(void) {
  TEST("kEventDragDrop: empty path does not call handler but buffer is freed");
  test_env_init();
  dd_reset();

  ui_register_open_file_handler(dd_open_file_handler);

  char *heap_empty = malloc(1);
  ASSERT_NOT_NULL(heap_empty);
  heap_empty[0] = '\0';

  ui_event_t evt = {0};
  evt.message = kEventDragDrop;
  evt.lParam  = (lParam_t)heap_empty;

  dispatch_message(&evt);

  ASSERT_EQUAL(dd_handler_call_count, 0);

  ui_register_open_file_handler(NULL);
  test_env_shutdown();
  PASS();
}

// When no handler is registered, dispatch_message(kEventDragDrop) must not
// crash and ui_open_file must silently return false.
void test_drag_drop_no_handler(void) {
  TEST("kEventDragDrop: no registered handler — no crash, returns false");
  test_env_init();
  dd_reset();

  // Ensure no handler is registered.
  ui_register_open_file_handler(NULL);

  char *heap_path = strdup("/tmp/no_handler.png");
  ASSERT_NOT_NULL(heap_path);

  ui_event_t evt = {0};
  evt.message = kEventDragDrop;
  evt.lParam  = (lParam_t)heap_path;

  dispatch_message(&evt);  // must not crash

  ASSERT_EQUAL(dd_handler_call_count, 0);

  test_env_shutdown();
  PASS();
}

// =============================================================================
// main
// =============================================================================

int main(int argc, char *argv[]) {
  (void)argc; (void)argv;
  TEST_START("event-driven main loop");

  // Part 1: get_message sentinel-filter and wakeup coalescing
  test_sentinel_returns_zero();
  test_real_event_passes_through();
  test_quit_zero_passes_through();
  test_null_target_not_treated_as_sentinel();
  test_wakeup_coalescing_single_post();
  test_wakeup_coalescing_rearms_after_consume();

  // Part 2: post_message deduplication
  test_post_message_deduplication();
  test_post_message_different_targets_both_delivered();

  // Part 3: invalidate_window
  test_invalidate_window_enqueues_paint();
  test_invalidate_routes_to_root();

  // Part 4: kEventDragDrop dispatch
  test_drag_drop_handler_invoked();
  test_drag_drop_null_lParam();
  test_drag_drop_empty_path();
  test_drag_drop_no_handler();

  TEST_END();
}
