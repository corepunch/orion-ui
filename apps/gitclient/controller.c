// Controller — business logic; database population is owned by datasource/gitclient_db.c.

#include "gitclient.h"

// ═══════════════════════════════════════════════════════════════════════════
// Stage / unstage files
// ═══════════════════════════════════════════════════════════════════════════

bool gc_stage_file(const char *path) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo || !path) return false;
  const char *args[] = { "git", "add", path, NULL };
  char buf[512] = {0};
  return git_run_sync(gc->repo, args, buf, sizeof(buf));
}

bool gc_unstage_file(const char *path) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo || !path) return false;
  char buf[512] = {0};
  const char *check[] = { "git", "rev-parse", "--verify", "HEAD", NULL };
  if (git_run_sync(gc->repo, check, buf, sizeof(buf))) {
    const char *args[] = { "git", "restore", "--staged", "--", path, NULL };
    return git_run_sync(gc->repo, args, buf, sizeof(buf));
  }
  const char *args[] = { "git", "rm", "--cached", "--", path, NULL };
  return git_run_sync(gc->repo, args, buf, sizeof(buf));
}

bool gc_stage_all(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo) return false;
  char buf[1024] = {0};
  const char *args[] = { "git", "add", "-A", NULL };
  return git_run_sync(gc->repo, args, buf, sizeof(buf));
}

bool gc_unstage_all(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo) return false;
  char buf[1024] = {0}, head[64] = {0};
  const char *check[] = { "git", "rev-parse", "--verify", "HEAD", NULL };
  if (git_run_sync(gc->repo, check, head, sizeof(head))) {
    const char *args[] = { "git", "reset", "HEAD", "--", ".", NULL };
    return git_run_sync(gc->repo, args, buf, sizeof(buf));
  }
  const char *args[] = { "git", "rm", "--cached", "-r", "--", ".", NULL };
  return git_run_sync(gc->repo, args, buf, sizeof(buf));
}

bool gc_sync(void) {
  gc_state_t *gc = g_gc; git_sync_status_t st;
  if (!gc || !gc->repo || !git_get_sync_status(gc->repo, &st) || st.detached || st.initial) return false;
  char remote_buf[8][256];
  if (!st.remote[0] && git_get_remotes(gc->repo, remote_buf, 8) > 0)
    strncpy(st.remote, remote_buf[0], sizeof(st.remote) - 1);
  if (!st.remote[0]) return false;
  char buf[4096] = {0};
  if (!st.upstream[0] || st.gone) {
    const char *args[] = { "git", "push", "-u", st.remote, st.head, NULL };
    return git_run_sync(gc->repo, args, buf, sizeof(buf));
  }
  if (st.behind > 0 && st.ahead > 0) return false;
  if (st.behind > 0) {
    const char *args[] = { "git", "pull", "--ff-only", NULL };
    if (!git_run_sync(gc->repo, args, buf, sizeof(buf))) return false;
  }
  if (st.ahead > 0) {
    const char *args[] = { "git", "push", NULL };
    if (!git_run_sync(gc->repo, args, buf, sizeof(buf))) return false;
  }
  return true;
}

bool gc_undo_commit(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo) return false;
  char buf[2048] = {0}; const char *args[] = { "git", "reset", "--soft", "HEAD~1", NULL };
  return git_run_sync(gc->repo, args, buf, sizeof(buf));
}

// ═══════════════════════════════════════════════════════════════════════════
// Commit
// ═══════════════════════════════════════════════════════════════════════════

bool gc_commit(const char *message, bool amend) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo || !message || !message[0]) return false;

  char buf[4096] = {0};
  bool ok;
  if (amend) {
    const char *args[] = { "git", "commit", "--amend", "-m", message, NULL };
    ok = git_run_sync(gc->repo, args, buf, sizeof(buf));
  } else {
    const char *args[] = { "git", "commit", "-m", message, NULL };
    ok = git_run_sync(gc->repo, args, buf, sizeof(buf));
  }
  if (!ok) {
    GC_LOG("gc_commit failed: %s", buf);
    return false;
  }
  return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Branch operations
// ═══════════════════════════════════════════════════════════════════════════

bool gc_create_branch(const char *name, const char *from, bool checkout) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo || !name || !name[0]) return false;

  char buf[1024] = {0};
  const char *args[] = { "git", "branch", name, from && from[0] ? from : NULL, NULL };
  if (!git_run_sync(gc->repo, args, buf, sizeof(buf))) {
    GC_LOG("gc_create_branch failed: %s", buf);
    return false;
  }

  if (checkout) {
    const char *args_co[] = { "git", "checkout", name, NULL };
    git_run_sync(gc->repo, args_co, buf, sizeof(buf));
  }
  return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Checkout / delete / merge / rebase / rename
// ═══════════════════════════════════════════════════════════════════════════

bool gc_checkout_branch(const char *name) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo || !name || !name[0]) return false;
  char buf[1024] = {0};
  const char *args[] = { "git", "checkout", name, NULL };
  if (!git_run_sync(gc->repo, args, buf, sizeof(buf))) {
    GC_LOG("gc_checkout_branch failed: %s", buf);
    return false;
  }
  return true;
}

bool gc_delete_branch(const char *name, bool remote) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo || !name || !name[0]) return false;
  char buf[1024] = {0};
  if (remote) {
    const char *args[] = { "git", "push", "origin", "--delete", name, NULL };
    if (!git_run_sync(gc->repo, args, buf, sizeof(buf))) {
      GC_LOG("gc_delete_branch remote failed: %s", buf);
      return false;
    }
  } else {
    const char *args[] = { "git", "branch", "-d", name, NULL };
    if (!git_run_sync(gc->repo, args, buf, sizeof(buf))) {
      // Try force delete if -d fails (unmerged)
      const char *args_f[] = { "git", "branch", "-D", name, NULL };
      if (!git_run_sync(gc->repo, args_f, buf, sizeof(buf))) {
        GC_LOG("gc_delete_branch failed: %s", buf);
        return false;
      }
    }
  }
  return true;
}

int gc_delete_merged_branches(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo) return -1;
  return git_delete_merged_branches(gc->repo);
}

bool gc_prune_remote(const char *remote) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo) return false;
  return git_prune_remote(gc->repo, remote);
}

bool gc_reload_history_log(const char *ref) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo || !gc->history_db) return false;
  send_db_message(gc->history_db, dbDelete, ID_DB_COMMITS, (void *)(intptr_t)0);
  git_commit_t raw[500];
  int count = git_get_log_ref(gc->repo, ref, raw, 500);
  for (int i = 0; i < count; i++) {
    db_commit_t rec = {0};
    strncpy(rec.hash, raw[i].hash, sizeof(rec.hash) - 1);
    strncpy(rec.author, raw[i].author, sizeof(rec.author) - 1);
    strncpy(rec.date, raw[i].date, sizeof(rec.date) - 1);
    strncpy(rec.subject, raw[i].subject, sizeof(rec.subject) - 1);
    send_db_message(gc->history_db, dbInsert, ID_DB_COMMITS, &rec);
  }
  if (gc->log_win)
    send_message(gc->log_win, tvRefresh, 0, NULL);
  gc->selected_commit = -1;
  gc->selected_file = -1;
  return true;
}

bool gc_merge_branch(const char *name) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo || !name || !name[0]) return false;
  char buf[4096] = {0};
  const char *args[] = { "git", "merge", name, NULL };
  if (!git_run_sync(gc->repo, args, buf, sizeof(buf))) {
    GC_LOG("gc_merge_branch failed: %s", buf);
    return false;
  }
  return true;
}

bool gc_rebase_onto(const char *branch) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo || !branch || !branch[0]) return false;
  char buf[4096] = {0};
  const char *args[] = { "git", "rebase", branch, NULL };
  if (!git_run_sync(gc->repo, args, buf, sizeof(buf))) {
    GC_LOG("gc_rebase_onto failed: %s", buf);
    return false;
  }
  return true;
}

bool gc_rename_branch(const char *old_name, const char *new_name) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo || !old_name || !old_name[0] || !new_name || !new_name[0])
    return false;
  char buf[1024] = {0};
  const char *args[] = { "git", "branch", "-m", old_name, new_name, NULL };
  if (!git_run_sync(gc->repo, args, buf, sizeof(buf))) {
    GC_LOG("gc_rename_branch failed: %s", buf);
    return false;
  }
  return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Discard changes
// ═══════════════════════════════════════════════════════════════════════════

bool gc_discard_file(const char *path) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo || !path) return false;
  char buf[1024] = {0};
  git_file_status_t files[256]; int n = git_get_status(gc->repo, files, 256);
  git_file_status_t *file = NULL;
  for (int i = 0; i < n; i++) if (!strcmp(files[i].path, path)) { file = &files[i]; break; }
  if (!file) return false;
  if (file->untracked) {
    const char *args[] = { "git", "clean", "-f", "--", path, NULL };
    return git_run_sync(gc->repo, args, buf, sizeof(buf));
  }
  if (file->index_status == 'A') {
    const char *args[] = { "git", "rm", "-f", "--", path, NULL };
    return git_run_sync(gc->repo, args, buf, sizeof(buf));
  }
  const char *args[] = { "git", "restore", "--source=HEAD", "--staged", "--worktree", "--", path, NULL };
  return git_run_sync(gc->repo, args, buf, sizeof(buf));
}

bool gc_discard_all(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo) return false;
  char buf[4096] = {0};
  const char *args_co[] = { "git", "checkout", "--", ".", NULL };
  git_run_sync(gc->repo, args_co, buf, sizeof(buf));
  const char *args_cl[] = { "git", "clean", "-fd", NULL };
  return git_run_sync(gc->repo, args_cl, buf, sizeof(buf));
}

// ═══════════════════════════════════════════════════════════════════════════
// Clone
// ═══════════════════════════════════════════════════════════════════════════

bool gc_clone_repo(const char *url, const char *path) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo || !url || !url[0] || !path || !path[0]) return false;
  const char *args[] = { "git", "clone", url, path, NULL };
  char buf[4096] = {0};
  if (!git_run_sync(gc->repo, args, buf, sizeof(buf))) {
    GC_LOG("gc_clone_repo failed: %s", buf);
    return false;
  }
  return true;
}

bool gc_init_repo(const char *path) {
  return git_repo_init_path(path);
}

// ═══════════════════════════════════════════════════════════════════════════
// Remote management
// ═══════════════════════════════════════════════════════════════════════════

bool gc_add_remote(const char *name, const char *url) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo || !name || !name[0] || !url || !url[0]) return false;
  char buf[1024] = {0};
  const char *args[] = { "git", "remote", "add", name, url, NULL };
  if (!git_run_sync(gc->repo, args, buf, sizeof(buf))) {
    GC_LOG("gc_add_remote failed: %s", buf);
    return false;
  }
  return true;
}

bool gc_remove_remote(const char *name) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo || !name || !name[0]) return false;
  char buf[1024] = {0};
  const char *args[] = { "git", "remote", "remove", name, NULL };
  if (!git_run_sync(gc->repo, args, buf, sizeof(buf))) {
    GC_LOG("gc_remove_remote failed: %s", buf);
    return false;
  }
  return true;
}

bool gc_set_remote_url(const char *name, const char *url) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo || !name || !name[0] || !url || !url[0]) return false;
  char buf[1024] = {0};
  const char *args[] = { "git", "remote", "set-url", name, url, NULL };
  if (!git_run_sync(gc->repo, args, buf, sizeof(buf))) {
    GC_LOG("gc_set_remote_url failed: %s", buf);
    return false;
  }
  return true;
}

bool gc_create_tag(const char *name, const char *ref) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo || !name || !name[0]) return false;
  char buf[1024] = {0};
  const char *args[] = { "git", "tag", name, ref && ref[0] ? ref : NULL, NULL };
  if (!git_run_sync(gc->repo, args, buf, sizeof(buf))) {
    GC_LOG("gc_create_tag failed: %s", buf);
    return false;
  }
  return true;
}
bool gc_delete_tag(const char *name) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo || !name || !name[0]) return false;
  char buf[1024] = {0};
  const char *args[] = { "git", "tag", "-d", name, NULL };
  if (!git_run_sync(gc->repo, args, buf, sizeof(buf))) {
    GC_LOG("gc_delete_tag failed: %s", buf);
    return false;
  }
  return true;
}

void gc_push_tags(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo) return;
  const char *args[] = { "git", "push", "--tags", NULL };
  git_run_async(gc->repo, GIT_OP_GENERIC, args, gc->main_win);
}

// ═══════════════════════════════════════════════════════════════════════════
// Conflict resolution
// ═══════════════════════════════════════════════════════════════════════════

int gc_get_conflicted_files(char (*out)[512], int max) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo || max <= 0) return 0;
  char buf[16 * 1024] = {0};
  const char *args[] = { "git", "diff", "--name-only", "--diff-filter=U", NULL };
  if (!git_run_sync(gc->repo, args, buf, sizeof(buf))) return 0;
  int count = 0;
  char *p = buf;
  while (*p && count < max) {
    char *nl = strchr(p, '\n');
    if (!nl) break;
    *nl = '\0';
    if (p[0]) { strncpy(out[count], p, 511); out[count][511] = '\0'; count++; }
    p = nl + 1;
  }
  return count;
}

bool gc_conflict_resolve(const char *path, const char *strategy) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo || !path || !strategy) return false;
  char buf[1024] = {0};

  if (strcmp(strategy, "ours") == 0) {
    const char *args[] = { "git", "checkout", "--ours", path, NULL };
    if (!git_run_sync(gc->repo, args, buf, sizeof(buf))) return false;
    const char *add_args[] = { "git", "add", path, NULL };
    return git_run_sync(gc->repo, add_args, buf, sizeof(buf));
  }
  if (strcmp(strategy, "theirs") == 0) {
    const char *args[] = { "git", "checkout", "--theirs", path, NULL };
    if (!git_run_sync(gc->repo, args, buf, sizeof(buf))) return false;
    const char *add_args[] = { "git", "add", path, NULL };
    return git_run_sync(gc->repo, add_args, buf, sizeof(buf));
  }
  if (strcmp(strategy, "both") == 0) {
    const char *add_args[] = { "git", "add", path, NULL };
    return git_run_sync(gc->repo, add_args, buf, sizeof(buf));
  }
  return false;
}

void gc_abort_merge(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo) return;
  char buf[1024] = {0};
  const char *args[] = { "git", "merge", "--abort", NULL };
  git_run_sync(gc->repo, args, buf, sizeof(buf));
}

// ═══════════════════════════════════════════════════════════════════════════
// Per-hunk staging
// ═══════════════════════════════════════════════════════════════════════════

bool gc_stage_hunk(const char *path, int hunk_idx) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo || !path || !path[0]) return false;

  char full[GC_DIFF_BUF_SIZE] = {0};
  git_get_diff(gc->repo, path, false, full, sizeof(full));

  char patch[GC_DIFF_BUF_SIZE] = {0};
  int pi = 0;

  pi += snprintf(patch + pi, sizeof(patch) - (size_t)pi,
                 "--- a/%s\n+++ b/%s\n", path, path);

  int hunk_found = -1;
  char *p = full;
  while (*p && hunk_found < hunk_idx) {
    if (p[0] == '@' && strncmp(p, "@@ -", 4) == 0)
      hunk_found++;
    if (hunk_found < hunk_idx) {
      char *nl = strchr(p, '\n');
      if (!nl) break;
      p = nl + 1;
    }
  }
  if (hunk_found != hunk_idx) return false;

  while (*p && pi < (int)sizeof(patch) - 2) {
    patch[pi++] = *p;
    if (*p == '\n') { p++; break; }
    p++;
  }

  while (*p && pi < (int)sizeof(patch) - 2) {
    if (p[0] == '@' && strncmp(p, "@@ -", 4) == 0) break;
    patch[pi++] = *p;
    if (*p == '\n') p++;
    else p++;
  }
  patch[pi] = '\0';

  // Write patch to temp file and apply via git apply --cached
  char tmp_path[512];
  snprintf(tmp_path, sizeof(tmp_path), "%s/.git/gc_hunk_patch", gc->repo_path);

  FILE *f = fopen(tmp_path, "w");
  if (!f) return false;
  fwrite(patch, 1, (size_t)pi, f);
  fclose(f);

  char buf[4096] = {0};
  const char *args[] = { "git", "apply", "--cached", tmp_path, NULL };
  bool ok = git_run_sync(gc->repo, args, buf, sizeof(buf));
  remove(tmp_path);
  if (!ok) GC_LOG("gc_stage_hunk failed: %s", buf);
  return ok;
}

bool gc_stash_drop(const char *ref) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo || !ref) return false;
  char buf[512] = {0};
  const char *args[] = { "git", "stash", "drop", ref, NULL };
  return git_run_sync(gc->repo, args, buf, sizeof(buf));
}

bool gc_stash_branch(const char *name, const char *ref) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo || !name) return false;
  char buf[1024] = {0};
  const char *args[] = { "git", "stash", "branch", name,
                          ref && ref[0] ? ref : NULL, NULL };
  return git_run_sync(gc->repo, args, buf, sizeof(buf));
}

// ═══════════════════════════════════════════════════════════════════════════
// Stash
// ═══════════════════════════════════════════════════════════════════════════

void gc_stash(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo) return;
  const char *args[] = { "git", "stash", NULL };
  char buf[256] = {0};
  git_run_sync(gc->repo, args, buf, sizeof(buf));
}

void gc_stash_pop(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo) return;
  const char *args[] = { "git", "stash", "pop", NULL };
  char buf[256] = {0};
  git_run_sync(gc->repo, args, buf, sizeof(buf));
}
