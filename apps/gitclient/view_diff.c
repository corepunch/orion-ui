// Unified diff viewer — fill content into gc_diff_state_t
//
// Reads selected file/commit from DB to determine what diff to show.
// The window proc lives in the gitclient_components plugin (diff_view.c).

#include "gitclient.h"
#include "components/diff_view.h"
#include <orion/user/vga_font.h>

static void parse_hunks(gc_diff_state_t *st) {
  st->hunk_count = 0;
  for (int i = 0; i < st->line_count && st->hunk_count < GC_DIFF_MAX_HUNKS; i++) {
    if (st->lines[i][0] == '@' && strncmp(st->lines[i], "@@ -", 4) == 0)
      st->hunk_offsets[st->hunk_count++] = i;
  }
  st->current_hunk = st->hunk_count > 0 ? 0 : -1;
}

// Forget which diff the viewer shows: the next gc_diff_refresh() re-reads it from git.
void gc_diff_invalidate(void) {
  if (g_gc) g_gc->diff_cache_valid = false;
}

void gc_diff_refresh(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->diff_win) { return; }

  window_t *win = gc->diff_win;
  gc_diff_state_t *st = (gc_diff_state_t *)win->userdata;
  if (!st) { return; }

  st->unified_mode = gc->unified_diff;

  if (!gc->repo || !gc->history_db) {
    free(st->lines); st->lines = NULL; st->line_count = 0; st->hunk_count = 0;
    invalidate_window(win);
    return;
  }

  const char *path = NULL;
  bool staged = false;
  bool untracked = false;

  if (gc->selected_file >= 0) {
    db_file_t *f = (db_file_t *)(intptr_t)send_message(
      gc->files_win, tvGetSelectedRecord, 0, NULL);
    if (f) {
      path = f->path;
      staged = f->staged;
      if (!staged && f->status[0] == '?' && gc->selected_commit < 0)
        untracked = true;
    }
  }

  if (gc->diff_cache_valid                           &&
      gc->last_diff_commit    == gc->selected_commit &&
      gc->last_diff_file      == gc->selected_file    &&
      gc->last_diff_staged    == staged               &&
      gc->last_diff_untracked == untracked            &&
      gc->last_diff_unified   == st->unified_mode     &&
      strcmp(gc->last_diff_path, path ? path : "") == 0)
    return;

  free(st->lines);
  st->lines      = NULL;
  st->line_count = 0;
  st->scroll_y   = 0;
  st->hunk_path[0] = '\0';

  gc->diff_cache_valid    = true;
  gc->last_diff_commit    = gc->selected_commit;
  gc->last_diff_file      = gc->selected_file;
  gc->last_diff_staged    = staged;
  gc->last_diff_untracked = untracked;
  gc->last_diff_unified   = st->unified_mode;
  if (path) strncpy(gc->last_diff_path, path, sizeof(gc->last_diff_path) - 1);
  else gc->last_diff_path[0] = '\0';


  if (gc->selected_commit >= 0) {
    db_commit_t *c = (db_commit_t *)(intptr_t)send_message(
      gc->log_win, tvGetSelectedRecord, 0, NULL);
    if (c) {
      if (path && path[0]) {
        strncpy(st->hunk_path, path, sizeof(st->hunk_path) - 1);
        const char *args[] = {
          "git", "show", "--color=always", "--pretty=format:", c->hash,
          "--", path, NULL
        };
        git_run_sync(gc->repo, args, st->diff_buf, sizeof(st->diff_buf));
      } else {
        const char *args[] = {
          "git", "show", "--color=always", "--pretty=format:", c->hash, NULL
        };
        git_run_sync(gc->repo, args, st->diff_buf, sizeof(st->diff_buf));
      }
    }
  } else {
    if (path && path[0])
      strncpy(st->hunk_path, path, sizeof(st->hunk_path) - 1);
    if (untracked) {
      const char *args[] = { "git", "diff", "--no-index", "--color=always",
                             "/dev/null", path, NULL };
      git_run_sync(gc->repo, args, st->diff_buf, sizeof(st->diff_buf));
    } else if (st->unified_mode) {
      git_get_diff(gc->repo, path, staged, st->diff_buf, sizeof(st->diff_buf));
    } else {
      // Word-level diff for split/enhanced mode
      const char *args[] = { "git", "diff", "--word-diff=color", "--color=always",
                             staged ? "--cached" : "HEAD",
                             path && path[0] ? "--" : NULL,
                             path && path[0] ? path : NULL, NULL };
      git_run_sync(gc->repo, args, st->diff_buf, sizeof(st->diff_buf));
    }
  }

  if (!st->diff_buf[0]) {

    invalidate_window(win);
    return;
  }

  int count = 0;
  for (char *p = st->diff_buf; *p; p++)
    if (*p == '\n') count++;
  if (st->diff_buf[0]) count++;

  st->lines = (char **)malloc((size_t)count * sizeof(char *));
  if (!st->lines) { invalidate_window(win); return; }

  char *p = st->diff_buf;
  while (*p) {
    st->lines[st->line_count++] = p;
    char *nl = strchr(p, '\n');
    if (!nl) break;
    *nl = '\0';
    p = nl + 1;
  }

  parse_hunks(st);

  scroll_info_t si = {
    .fMask = SIF_RANGE | SIF_PAGE | SIF_POS,
    .nMin  = 0,
    .nMax  = st->line_count,
    .nPage = (uint32_t)MAX(1, win->frame.h / vga_char_height()),
    .nPos  = 0,
  };
  set_scroll_info(win, SB_VERT, &si, false);
  invalidate_window(win);
}
