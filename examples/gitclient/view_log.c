// Commit log view — win_reportview showing graph/subject/author/date/hash.

#include "gitclient.h"

// Column indices
#define COL_SUBJECT  0
#define COL_AUTHOR   1
#define COL_DATE     2
#define COL_HASH     3

static const int kColWidths[] = { 0, 110, 90, 50 };
/* COL_SUBJECT width is computed as remaining space */

// ============================================================
// Set up columns (called once on create)
// ============================================================

static void log_setup_columns(window_t *win) {
  int w = win->frame.w;
  int fixed = kColWidths[COL_AUTHOR] + kColWidths[COL_DATE] + kColWidths[COL_HASH];
  int subject_w = MAX(w - fixed, 80);

  send_message(win, RVM_CLEARCOLUMNS, 0, NULL);

  reportview_column_t cols[] = {
    { "Subject", (uint32_t)subject_w               },
    { "Author",  (uint32_t)kColWidths[COL_AUTHOR]  },
    { "Date",    (uint32_t)kColWidths[COL_DATE]     },
    { "Hash",    (uint32_t)kColWidths[COL_HASH]     },
  };
  for (int i = 0; i < 4; i++)
    send_message(win, RVM_ADDCOLUMN, 0, &cols[i]);
}

// ============================================================
// Populate rows
// ============================================================

void gc_log_refresh(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->log_win) return;

  window_t *win = gc->log_win;
  send_message(win, RVM_SETREDRAW, 0, NULL);
  send_message(win, RVM_CLEAR, 0, NULL);

  if (!gc->repo) {
    send_message(win, RVM_SETREDRAW, 1, NULL);
    return;
  }

  gc->commit_count = git_get_log(gc->repo, gc->commits, GC_MAX_COMMITS);

  for (int i = 0; i < gc->commit_count; i++) {
    git_commit_t *c = &gc->commits[i];
    // Abbreviate hash to 7 chars.
    char short_hash[8];
    strncpy(short_hash, c->hash, 7);
    short_hash[7] = '\0';

    reportview_item_t item = {0};
    item.text          = c->subject;
    item.subitems[0]   = c->author;
    item.subitems[1]   = c->date;
    item.subitems[2]   = short_hash;
    item.subitem_count = 3;
    item.color         = get_sys_color(brTextNormal);
    item.userdata      = (uint32_t)i;
    send_message(win, RVM_ADDITEM, 0, &item);
  }

  send_message(win, RVM_SETREDRAW, 1, NULL);
}

// ============================================================
// Window procedure
// ============================================================

result_t gc_log_proc(window_t *win, uint32_t msg,
                     uint32_t wparam, void *lparam) {
  result_t r = win_reportview(win, msg, wparam, lparam);

  if (msg == evCreate) {
    send_message(win, RVM_SETVIEWMODE, RVM_VIEW_REPORT, NULL);
    log_setup_columns(win);
    return r;
  }

  if (msg == evResize) {
    log_setup_columns(win);
    return r;
  }

  // Selection changed → refresh file list and diff for the chosen commit.
  if (msg == evCommand && HIWORD(wparam) == RVN_SELCHANGE) {
    gc_state_t *gc = g_gc;
    if (!gc) return r;
    gc->selected_commit = (int)send_message(win, RVM_GETSELECTION, 0, NULL);
    gc->selected_file   = -1;
    gc_files_refresh();
    gc_diff_refresh();
    return r;
  }

  return r;
}
