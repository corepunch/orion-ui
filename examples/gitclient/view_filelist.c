// File status panel — shows staged/unstaged changed files using
// win_reportview in details mode.

#include "gitclient.h"

// Column indices
#define COL_STATUS   0
#define COL_PATH     1

static void files_setup_columns(window_t *win) {
  send_message(win, RVM_CLEARCOLUMNS, 0, NULL);
  reportview_column_t c0 = { "St", 20 };
  reportview_column_t c1 = { "File", (uint32_t)(win->frame.w - 20) };
  send_message(win, RVM_ADDCOLUMN, 0, &c0);
  send_message(win, RVM_ADDCOLUMN, 0, &c1);
}

// ============================================================
// Populate
// ============================================================

void gc_files_refresh(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->files_win) return;

  window_t *win = gc->files_win;
  send_message(win, RVM_SETREDRAW, 0, NULL);
  send_message(win, RVM_CLEAR, 0, NULL);

  if (!gc->repo) {
    send_message(win, RVM_SETREDRAW, 1, NULL);
    return;
  }

  gc->file_count = git_get_status(gc->repo, gc->files, GC_MAX_FILES);

  for (int i = 0; i < gc->file_count; i++) {
    git_file_status_t *f = &gc->files[i];

        char st[2] = { f->status, '\0' };

    // Colour based on status.
    uint32_t color;
    switch (f->status) {
      case 'A': color = 0xFF00BB00; break;  // added   → green
      case 'D': color = 0xFFBB0000; break;  // deleted → red
      case 'M': color = 0xFF0088CC; break;  // modified → blue
      default:  color = get_sys_color(brTextDisabled); break;
    }

    reportview_item_t item = {0};
    item.text          = f->path;
    item.subitems[0]   = st;
    item.subitems[1]   = f->path;
    item.subitem_count = 2;
    item.color         = color;
    item.userdata      = (uint32_t)i;
    send_message(win, RVM_ADDITEM, 0, &item);
  }

  send_message(win, RVM_SETREDRAW, 1, NULL);
}

// ============================================================
// Window procedure
// ============================================================

result_t gc_files_proc(window_t *win, uint32_t msg,
                       uint32_t wparam, void *lparam) {
  result_t r = win_reportview(win, msg, wparam, lparam);

  if (msg == evCreate) {
    send_message(win, RVM_SETVIEWMODE, RVM_VIEW_REPORT, NULL);
    files_setup_columns(win);
    return r;
  }

  if (msg == evResize) {
    files_setup_columns(win);
    return r;
  }

  // Row selection → update diff.
  if (msg == evCommand && HIWORD(wparam) == RVN_SELCHANGE) {
    gc_state_t *gc = g_gc;
    if (!gc) return r;
    gc->selected_file = (int)send_message(win, RVM_GETSELECTION, 0, NULL);
    gc_diff_refresh();
    return r;
  }

  // Space / double-click → stage or unstage the file.
  if (msg == evCommand && HIWORD(wparam) == RVN_DBLCLK) {
    gc_state_t *gc = g_gc;
    if (!gc || !gc->repo) return r;
    int sel = gc->selected_file;
    if (sel < 0 || sel >= gc->file_count) return r;
    git_file_status_t *f = &gc->files[sel];
    char buf[512] = {0};
    if (f->staged) {
      const char *args[] = { "git", "restore", "--staged", f->path, NULL };
      git_run_sync(gc->repo, args, buf, sizeof(buf));
    } else {
      const char *args[] = { "git", "add", f->path, NULL };
      git_run_sync(gc->repo, args, buf, sizeof(buf));
    }
    gc_files_refresh();
    gc_diff_refresh();
    return true;
  }

  return r;
}
