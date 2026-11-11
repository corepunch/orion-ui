// Terminal Application Integration Tests
// Tests terminal_run_lua_file() directly without a GL context.

#include "test_framework.h"
#include <orion/ui.h>
#include "vgat.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
static int pipe(int fds[2]) { return _pipe(fds, 65536, 0); }
static void portable_usleep(unsigned int us) { Sleep(us / 1000); }
#else
#include <unistd.h>
static void portable_usleep(unsigned int us) { usleep(us); }
#endif

// The emulator ships in the terminal_components plugin; unit tests compile it in.
#include "apps/terminal/components/view_main.c"
#include "apps/terminal/components/vgat_screen.c"
#include "apps/terminal/components/ansi_parser.c"
#include "apps/terminal/components/pty.c"

static const vgat_cmd_t *find_cmd(const char *name) {
  for (int i = 0; g_cmds[i].name; i++)
    if (strcmp(g_cmds[i].name, name) == 0) return &g_cmds[i];
  return NULL;
}

// Test: terminal_run_lua_file with a valid script
void test_run_lua_file_valid(void) {
  TEST("terminal_run_lua_file with valid script");
#if defined(HAVE_LUA)
  vgat_state_t *st = calloc(1, sizeof(vgat_state_t));
  ASSERT_NOT_NULL(st);
  vgat_screen_init(&st->screen, 24, 80);

  bool result = terminal_run_lua_file(st, "tests/test_simple.lua");
  ASSERT_TRUE(result);
  ASSERT_TRUE(st->screen.cursor_row > 0);

  vgat_screen_shutdown(&st->screen);
  if (st->L) lua_close(st->L);
  free(st);
#else
  SKIP("Lua not available");
#endif
  PASS();
}

// Test: terminal_run_lua_file with a missing script
void test_run_lua_file_missing(void) {
  TEST("terminal_run_lua_file with missing script");
#if defined(HAVE_LUA)
  vgat_state_t *st = calloc(1, sizeof(vgat_state_t));
  ASSERT_NOT_NULL(st);
  vgat_screen_init(&st->screen, 24, 80);

  bool result = terminal_run_lua_file(st, "tests/nonexistent.lua");
  ASSERT_FALSE(result);
  // Error was written to screen, cursor_row shows output
  ASSERT_TRUE(st->screen.cursor_row > 0);

  vgat_screen_shutdown(&st->screen);
  if (st->L) lua_close(st->L);
  free(st);
#else
  SKIP("Lua not available");
#endif
  PASS();
}

// Test: max_row tracks the highest row written via cursor_position
void test_max_row_tracking(void) {
  TEST("max_row tracking via cursor_position");
  vgat_screen screen;
  vgat_screen_init(&screen, 24, 80);

  // After init, max_row is 0
  ASSERT_EQUAL(screen.max_row, 0);

  // Simulate nano: clear screen, position to row 0, write text
  vgat_screen_clear(&screen);
  ASSERT_EQUAL(screen.max_row, 0);

  // Position to row 5 and write — max_row should become 6 (0-indexed row + 1)
  vgat_screen_cursor_position(&screen, 5, 0);
  vgat_screen_write_cell(&screen, 'x', 7, 0);
  ASSERT_EQUAL(screen.max_row, 6);

  // Position to row 0 — max_row should stay 6 (doesn't shrink)
  vgat_screen_cursor_position(&screen, 0, 0);
  ASSERT_EQUAL(screen.max_row, 6);

  // Position to row 23 — max_row should become 24
  vgat_screen_cursor_position(&screen, 23, 0);
  ASSERT_EQUAL(screen.max_row, 24);

  // Verify render: with written=24 and content_rows=24, first=0 (all rows visible)
  int written = screen.max_row;
  int content_rows = 24;
  int first = written - content_rows;
  if (first < 0) first = 0;
  ASSERT_EQUAL(first, 0);

  // Verify render: with written=24 and content_rows=20, first=4 (last 20 rows)
  written = screen.max_row;
  content_rows = 20;
  first = written - content_rows;
  if (first < 0) first = 0;
  ASSERT_EQUAL(first, 4);

  vgat_screen_shutdown(&screen);
  PASS();
}

// Each terminal owns its cwd; cursor timers must never resync it from the process.
void test_terminal_cwds_are_independent(void) {
  TEST("terminal cwd remains independent across cursor timers");
  char process_cwd[1024];
  ASSERT_NOT_NULL(getcwd(process_cwd, sizeof(process_cwd)));

  vgat_state_t first = { .pty_fd = -1 }, second = { .pty_fd = -1 };
  snprintf(first.cwd, sizeof(first.cwd), "%s", process_cwd);
  snprintf(second.cwd, sizeof(second.cwd), "%s", process_cwd);

  const vgat_cmd_t *cd = find_cmd("cd");
  ASSERT_NOT_NULL(cd);
  char *argv[] = { "cd", "..", NULL };
  cd->func(&first, 2, argv);
  ASSERT_NOT_EQUAL(strcmp(first.cwd, second.cwd), 0);
  ASSERT_STR_EQUAL(second.cwd, process_cwd);
  char actual_process_cwd[1024];
  ASSERT_NOT_NULL(getcwd(actual_process_cwd, sizeof(actual_process_cwd)));
  ASSERT_STR_EQUAL(actual_process_cwd, process_cwd);

  window_t first_win = { .userdata = &first }, second_win = { .userdata = &second };
  first.cursor_timer_id = 41;
  second.cursor_timer_id = 42;
  terminal_proc(&first_win, evTimer, first.cursor_timer_id, NULL);
  terminal_proc(&second_win, evTimer, second.cursor_timer_id, NULL);

  ASSERT_NOT_EQUAL(strcmp(first.cwd, second.cwd), 0);
  ASSERT_STR_EQUAL(second.cwd, process_cwd);
  PASS();
}

void test_cursor_blink_routes_only_its_timer(void) {
  TEST("cursor blink routes only its own timer ID");
  vgat_state_t st = { .pty_fd = -1, .cursor_visible = true, .cursor_timer_id = 41 };
  window_t win = { .userdata = &st };

  ASSERT_FALSE(terminal_proc(&win, evTimer, 99, NULL));
  ASSERT_TRUE(st.cursor_visible);
  ASSERT_TRUE(terminal_proc(&win, evTimer, 41, NULL));
  ASSERT_FALSE(st.cursor_visible);
  PASS();
}

void test_pty_watch_wakes_on_readability(void) {
  TEST("PTY watcher posts readiness only after bytes arrive");
#ifdef _WIN32
  SKIP("PTY watcher not implemented on Windows");
#endif
  int fds[2] = {-1, -1};
  ASSERT_EQUAL(pipe(fds), 0);
  window_t target = {0};
  vgat_pty_watch_t *watch = vgat_pty_watch_start(
      fds[0], &target, evTerminalPtyReady, 77);
  ASSERT_NOT_NULL(watch);

  ASSERT_EQUAL(write(fds[1], "x", 1), 1);
  ui_event_t evt = {0};
  bool received = false;
  for (int i = 0; i < 200 && !received; i++) {
    if (axPeekMessage(&evt) && evt.target == &target &&
        evt.message == evTerminalPtyReady && evt.wParam == 77) received = true;
    if (!received) portable_usleep(1000);
  }

  char byte;
  (void)read(fds[0], &byte, 1);
  vgat_pty_watch_rearm(watch);
  vgat_pty_watch_stop(watch);
  close(fds[0]); close(fds[1]);
  ASSERT_TRUE(received);
  PASS();
}

int main(int argc, char *argv[]) {
  (void)argc; (void)argv;
  TEST_START("Terminal Application Integration");

  test_run_lua_file_valid();
  test_run_lua_file_missing();
  test_max_row_tracking();
  test_terminal_cwds_are_independent();
  test_cursor_blink_routes_only_its_timer();
  test_pty_watch_wakes_on_readability();

  TEST_END();
}
