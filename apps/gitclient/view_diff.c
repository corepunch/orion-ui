// Diff viewer content: which diff the DiffView shows, and which of its lines the index holds.
//
// The window proc and the parser live in the gitclient_components plugin (diff_view.c).
// As in GitHub Desktop, a modified file shows every change since HEAD, and each
// changed line carries a check when the index already includes it.

#include "gitclient.h"
#include "components/diff_view.h"

// Forget which diff the viewer shows: the next gc_diff_refresh() re-reads it from git.
void gc_diff_invalidate(void) {
  if (g_gc) g_gc->diff_cache_valid = false;
}

static bool diff_hunk_start(const char *s, int *old_no, int *new_no) {
  return sscanf(s, "@@ -%d", old_no) == 1 && (s = strstr(s, " +")) && sscanf(s, " +%d", new_no) == 1;
}

// The line numbers a -U0 diff removes (sign '-', old side) or adds (sign '+', new side).
static bool *diff_line_set(const char *diff, char sign, int *count) {
  bool *set = NULL;
  int cap = 0, old_no = 0, new_no = 0;
  bool in_hunk = false;
  *count = 0;
  for (const char *p = diff; *p; ) {
    const char *nl = strchr(p, '\n');
    if (!strncmp(p, "diff ", 5)) in_hunk = false;
    else if (p[0] == '@') in_hunk = diff_hunk_start(p, &old_no, &new_no);
    else if (in_hunk && p[0] == sign) {
      int n = sign == '-' ? old_no++ : new_no++;
      if (n >= cap) {
        int grown_cap = MAX(n + 1, cap * 2 + 64);
        bool *grown = realloc(set, (size_t)grown_cap);
        if (!grown) break;
        memset(grown + cap, 0, (size_t)(grown_cap - cap));
        set = grown; cap = grown_cap;
      }
      set[n] = true;
      *count = MAX(*count, n + 1);
    } else if (in_hunk && p[0] == '-') old_no++;
    else if (in_hunk && p[0] == '+') new_no++;
    if (!nl) break;
    p = nl + 1;
  }
  return set;
}

static bool in_set(const bool *set, int count, int n) { return set && n >= 0 && n < count && set[n]; }

// Mark each changed line of a HEAD-to-worktree diff with whether the index has it:
// a removed line is in when the index removes it too, an added line when the
// worktree adds nothing there over the index.
static void diff_mark_included(gc_state_t *gc, gc_diff_state_t *st, const char *path) {
  char *cached = malloc(GC_DIFF_BUF_SIZE), *work = malloc(GC_DIFF_BUF_SIZE);
  if (!cached || !work) { free(cached); free(work); return; }
  const char *cached_args[] = { "git", "diff", "--no-color", "--no-ext-diff", "-U0", "--cached", "--", path, NULL };
  const char *work_args[]   = { "git", "diff", "--no-color", "--no-ext-diff", "-U0", "--", path, NULL };
  cached[0] = work[0] = '\0';
  if (git_run_sync(gc->repo, cached_args, cached, GC_DIFF_BUF_SIZE) && git_run_sync(gc->repo, work_args, work, GC_DIFF_BUF_SIZE)) {
    int removed_n, added_n;
    bool *removed = diff_line_set(cached, '-', &removed_n), *added = diff_line_set(work, '+', &added_n);
    for (int i = 0; i < st->line_count; i++) {
      gc_diff_line_t *l = &st->lines[i];
      if (l->kind == GC_DIFF_DEL) { l->selectable = true; l->included = in_set(removed, removed_n, l->old_no); }
      if (l->kind == GC_DIFF_ADD) { l->selectable = true; l->included = !in_set(added, added_n, l->new_no); }
    }
    free(removed); free(added);
  }
  free(cached); free(work);
}

void gc_diff_refresh(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->diff_win) { return; }

  window_t *win = gc->diff_win;
  gc_diff_state_t *st = (gc_diff_state_t *)win->userdata;
  if (!st) { return; }

  if (!gc->repo || !gc->history_db) {
    gc_diff_set_text(win, "");
    return;
  }

  const char *path = NULL;
  char status = 0;
  bool staged = false, untracked = false;

  if (gc->selected_file >= 0) {
    db_file_t *f = (db_file_t *)(intptr_t)send_message(gc->files_win, tvGetSelectedRecord, 0, NULL);
    if (f) {
      path = f->path;
      staged = f->staged;
      status = f->status[0];
      if (!staged && status == '?' && gc->selected_commit < 0)
        untracked = true;
    }
  }

  bool same_file = gc->last_diff_commit == gc->selected_commit && strcmp(gc->last_diff_path, path ? path : "") == 0;
  if (gc->diff_cache_valid && same_file && gc->last_diff_file == gc->selected_file &&
      gc->last_diff_staged == staged && gc->last_diff_untracked == untracked)
    return;

  st->hunk_path[0] = '\0';
  gc->diff_cache_valid    = true;
  gc->last_diff_commit    = gc->selected_commit;
  gc->last_diff_file      = gc->selected_file;
  gc->last_diff_staged    = staged;
  gc->last_diff_untracked = untracked;
  snprintf(gc->last_diff_path, sizeof(gc->last_diff_path), "%s", path ? path : "");
  if (path && path[0]) snprintf(st->hunk_path, sizeof(st->hunk_path), "%s", path);

  bool ok = true, selectable = false;
  if (gc->selected_commit >= 0) {
    db_commit_t *c = (db_commit_t *)(intptr_t)send_message(gc->log_win, tvGetSelectedRecord, 0, NULL);
    if (c) {
      const char *args[] = { "git", "show", "--no-color", "--no-ext-diff", "--pretty=format:", c->hash,
                             path && path[0] ? "--" : NULL, path, NULL };
      ok = git_run_sync(gc->repo, args, st->diff_buf, sizeof(st->diff_buf));
    } else {
      st->diff_buf[0] = '\0';
    }
  } else if (untracked) {
    const char *args[] = { "git", "diff", "--no-index", "--no-color", "--no-ext-diff", "/dev/null", path, NULL };
    git_run_sync(gc->repo, args, st->diff_buf, sizeof(st->diff_buf));   // exits 1 when the files differ
  } else if (path && path[0]) {
    const char *args[] = { "git", "diff", "--no-color", "--no-ext-diff", "HEAD", "--", path, NULL };
    ok = git_run_sync(gc->repo, args, st->diff_buf, sizeof(st->diff_buf));
    selectable = ok && status == 'M';
    if (!ok) ok = git_get_diff(gc->repo, path, staged, st->diff_buf, sizeof(st->diff_buf));   // no HEAD yet
  } else {
    ok = git_get_diff(gc->repo, NULL, staged, st->diff_buf, sizeof(st->diff_buf));
  }
  if (!ok) st->diff_buf[0] = '\0';   // the buffer holds git's error, not a diff

  int scroll = same_file ? (int)win->vscroll.pos : 0;   // a reload after staging keeps the place
  gc_diff_set_text(win, st->diff_buf);
  if (selectable) diff_mark_included(gc, st, path);
  if (scroll) {
    scroll_info_t si = { .fMask = SIF_POS, .nPos = scroll };
    set_scroll_info(win, SB_VERT, &si, true);
  }
  invalidate_window(win);
}

void gc_diff_set_text(window_t *win, const char *text) {
  if (win) send_message(win, edSetText, 0, (void *)text);
}
