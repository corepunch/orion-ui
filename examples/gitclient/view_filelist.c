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

static int files_get_commit_changed(gc_state_t *gc, const char *hash,
                                    git_file_status_t *out, int max) {
  if (!gc || !gc->repo || !hash || !hash[0] || !out || max <= 0)
    return 0;

  char buf[64 * 1024] = {0};
  const char *args[] = {
    "git", "show", "--name-status", "--pretty=format:",
    "--no-renames", hash, NULL
  };
  if (!git_run_sync(gc->repo, args, buf, sizeof(buf)))
    return 0;

  int count = 0;
  char *line = buf;
  while (*line && count < max) {
    char *nl = strchr(line, '\n');
    if (nl) *nl = '\0';

    // Expected format: "M\tpath" / "A\tpath" / "D\tpath".
    if (line[0] && line[1] == '\t' && line[2]) {
      git_file_status_t *f = &out[count];
      f->status = line[0];
      f->staged = false;
      strncpy(f->path, line + 2, sizeof(f->path) - 1);
      f->path[sizeof(f->path) - 1] = '\0';
      count++;
    }

    if (!nl) break;
    line = nl + 1;
  }

  return count;
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

  if (gc->selected_commit >= 0 && gc->selected_commit < gc->commit_count) {
    const char *hash = gc->commits[gc->selected_commit].hash;
    gc->file_count = files_get_commit_changed(gc, hash, gc->files, GC_MAX_FILES);
  } else {
    gc->file_count = git_get_status(gc->repo, gc->files, GC_MAX_FILES);
  }

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
    item.text          = st;       // col 0 ("St"): status character
    item.subitems[0]   = f->path;  // col 1 ("File"): file path
    item.subitem_count = 1;
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

  return r;
}
