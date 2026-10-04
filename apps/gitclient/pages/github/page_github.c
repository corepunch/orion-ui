// GitHub page — hosts the issues and pull-request table views.
// The database adaptor (github_database_proc) lives in datasource/github_database_proc.c.

#include "page_github.h"

// ──────────────────────────────────────────────────────────────────────────────
// Page window proc — captures outlets on evCreate.
// ──────────────────────────────────────────────────────────────────────────────

result_t page_github_proc(window_t *win, uint32_t msg,
                           uint32_t wparam, void *lparam) {
  (void)wparam; (void)lparam;
  if (msg != evCreate) return false;

  gc_state_t *gc = g_gc;
  if (!gc) return false;

  gc->github_issues_win = get_window_item(win, ID_GITHUB_PAGE_ISSUES);
  gc->github_pulls_win  = get_window_item(win, ID_GITHUB_PAGE_PULLS);

  if (gc->github_issues_win && gc->github_db)
    send_message(gc->github_issues_win, evSetDatabase, 0, gc->github_db);
  if (gc->github_pulls_win && gc->github_db)
    send_message(gc->github_pulls_win, evSetDatabase, 0, gc->github_db);

  return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// Refresh — reload gh data and repopulate the table views.
// ──────────────────────────────────────────────────────────────────────────────

void page_github_refresh(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->github_db || !gc->repo) return;

  send_db_message(gc->github_db, dbLoad, 0, gc->repo);

  if (gc->github_issues_win) send_message(gc->github_issues_win, tvRefresh, 0, NULL);
  if (gc->github_pulls_win)  send_message(gc->github_pulls_win,  tvRefresh, 0, NULL);
}

// ──────────────────────────────────────────────────────────────────────────────
// Event delegation — no complex routing needed yet.
// ──────────────────────────────────────────────────────────────────────────────

bool page_github_handle(window_t *main_win, uint32_t msg,
                         uint32_t wparam, void *lparam) {
  (void)main_win; (void)msg; (void)wparam; (void)lparam;
  return false;
}
