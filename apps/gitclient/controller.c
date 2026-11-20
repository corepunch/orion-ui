// Controller — business logic; database population is owned by datasource/gitclient_db.c.

#include "gitclient.h"
#include "components/diff_view.h"

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
  git_commit_t raw[500];
  int count = git_get_log_ref(gc->repo, ref, raw, 500);
  if (count <= 0) return false;
  send_db_message(gc->history_db, dbDelete, ID_DB_COMMITS, (void *)(intptr_t)0);
  for (int i = 0; i < count; i++) {
    db_commit_t rec = {0};
    strncpy(rec.hash, raw[i].hash, sizeof(rec.hash) - 1);
    strncpy(rec.author, raw[i].author, sizeof(rec.author) - 1);
    strncpy(rec.date, raw[i].date, sizeof(rec.date) - 1);
    strncpy(rec.subject, raw[i].subject, sizeof(rec.subject) - 1);
    send_db_message(gc->history_db, dbInsert, ID_DB_COMMITS, &rec);
  }
  gc->selected_commit = -1;
  gc->selected_file = -1;
  gc_diff_invalidate();
  if (gc->log_win)
    send_message(gc->log_win, tvRefresh, 0, NULL);
  gc_diff_refresh();
  return true;
}

// Runs one step of a merge or rebase. On failure err holds git's own explanation (its last lines).
static bool gc_git_step(const char *args[], bool noedit, char *err, size_t err_n) {
  gc_state_t *gc = g_gc;
  if (err && err_n) err[0] = '\0';
  if (!gc || !gc->repo) return false;
  char buf[8192] = {0};
  bool ok = noedit ? git_run_sync_noedit(gc->repo, args, buf, sizeof(buf)) : git_run_sync(gc->repo, args, buf, sizeof(buf));
  if (!ok && err && err_n) {
    size_t len = strlen(buf);
    while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' ')) buf[--len] = '\0';
    const char *tail = buf + (len > 600 ? len - 600 : 0);
    snprintf(err, err_n, "%s", tail);
  }
  return ok;
}

bool gc_merge_branch(const char *name, char *err, size_t err_n) {
  if (!name || !name[0]) return false;
  const char *args[] = { "git", "merge", "--no-edit", name, NULL };
  return gc_git_step(args, true, err, err_n);
}

bool gc_rebase_onto(const char *branch, char *err, size_t err_n) {
  if (!branch || !branch[0]) return false;
  const char *args[] = { "git", "rebase", "--autostash", branch, NULL };
  return gc_git_step(args, true, err, err_n);
}

bool gc_rebase_onto_default(char *onto, size_t onto_n, char *err, size_t err_n) {
  gc_state_t *gc = g_gc;
  if (err && err_n) err[0] = '\0';
  if (!gc || !gc->repo || !onto || !onto_n) return false;
  char def[256] = {0}, cur[256] = {0};
  if (!git_default_branch(gc->repo, def, sizeof(def)) || !git_rebase_base(gc->repo, onto, (int)onto_n)) {
    snprintf(err, err_n, "%s", ui_tr("This repository has no default branch to rebase onto."));
    return false;
  }
  if (git_current_branch(gc->repo, cur, sizeof(cur)) && !strcmp(cur, def)) {
    snprintf(err, err_n, ui_tr("You are on %s. Switch to the branch you want to rebase onto it."), def);
    return false;
  }
  return gc_rebase_onto(onto, err, err_n);
}

// The rebase, merge or pick in progress, read now (the snapshot may be a step behind).
static git_op_kind_t gc_operation_kind(void) {
  gc_state_t *gc = g_gc;
  git_operation_t op;
  return gc && gc->repo && git_get_operation(gc->repo, &op) ? op.kind : GIT_OP_NONE;
}

bool gc_operation_continue(char *err, size_t err_n) {
  switch (gc_operation_kind()) {
    case GIT_OP_REBASE:      { const char *a[] = { "git", "rebase", "--continue", NULL };      return gc_git_step(a, true, err, err_n); }
    case GIT_OP_MERGE:       { const char *a[] = { "git", "commit", "--no-edit", NULL };       return gc_git_step(a, true, err, err_n); }
    case GIT_OP_CHERRY_PICK: { const char *a[] = { "git", "cherry-pick", "--continue", NULL }; return gc_git_step(a, true, err, err_n); }
    case GIT_OP_REVERT:      { const char *a[] = { "git", "revert", "--continue", NULL };      return gc_git_step(a, true, err, err_n); }
    case GIT_OP_NONE: break;
  }
  return false;
}

bool gc_operation_skip(char *err, size_t err_n) {
  switch (gc_operation_kind()) {
    case GIT_OP_REBASE:      { const char *a[] = { "git", "rebase", "--skip", NULL };      return gc_git_step(a, true, err, err_n); }
    case GIT_OP_CHERRY_PICK: { const char *a[] = { "git", "cherry-pick", "--skip", NULL }; return gc_git_step(a, true, err, err_n); }
    case GIT_OP_REVERT:      { const char *a[] = { "git", "revert", "--skip", NULL };      return gc_git_step(a, true, err, err_n); }
    default: break;
  }
  return false;
}

bool gc_operation_abort(char *err, size_t err_n) {
  switch (gc_operation_kind()) {
    case GIT_OP_REBASE:      { const char *a[] = { "git", "rebase", "--abort", NULL };      return gc_git_step(a, false, err, err_n); }
    case GIT_OP_MERGE:       { const char *a[] = { "git", "merge", "--abort", NULL };       return gc_git_step(a, false, err, err_n); }
    case GIT_OP_CHERRY_PICK: { const char *a[] = { "git", "cherry-pick", "--abort", NULL }; return gc_git_step(a, false, err, err_n); }
    case GIT_OP_REVERT:      { const char *a[] = { "git", "revert", "--abort", NULL };      return gc_git_step(a, false, err, err_n); }
    case GIT_OP_NONE: break;
  }
  return false;
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

// Discarded work goes to a stash ("Discarded changes to <path>"), not into the void: the working tree ends
// up exactly as checkout + clean would leave it, and Pop Stash brings the changes back.
static bool gc_discard_to_stash(const char *label, const char *path) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo) return false;
  char buf[4096] = {0}, message[700];
  snprintf(message, sizeof(message), "%s", label);
  const char *args[] = { "git", "stash", "push", "--include-untracked", "--message", message, path ? "--" : NULL, path, NULL };
  if (git_run_sync(gc->repo, args, buf, sizeof(buf))) return true;
  fprintf(stderr, "[gitclient] discard to stash failed path=%s: %s\n", path ? path : "(all)", buf);
  fflush(stderr);
  return false;
}

bool gc_discard_file(const char *path) {
  if (!path || !path[0]) return false;
  char label[640];
  snprintf(label, sizeof(label), "Discarded changes to %s", path);
  return gc_discard_to_stash(label, path);
}

bool gc_discard_all(void) {
  return gc_discard_to_stash("Discarded changes", NULL);
}

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

// Resolves a conflicted file with one side. "Mine" is the branch being worked on: in a merge or pick
// that is git's --ours, but a rebase replays my commits onto the other branch, so there it is --theirs.
bool gc_conflict_use_version(const char *path, bool mine, char *err, size_t err_n) {
  if (!path || !path[0]) return false;
  bool ours = gc_operation_kind() == GIT_OP_REBASE ? !mine : mine;
  const char *pick[] = { "git", "checkout", ours ? "--ours" : "--theirs", "--", path, NULL };
  if (!gc_git_step(pick, false, err, err_n)) return false;
  const char *add[] = { "git", "add", "--", path, NULL };
  return gc_git_step(add, false, err, err_n);
}

// Stages a file the user resolved by hand, refusing while git's conflict markers are still in it.
bool gc_conflict_mark_resolved(const char *path, char *err, size_t err_n) {
  gc_state_t *gc = g_gc;
  if (err && err_n) err[0] = '\0';
  if (!gc || !gc->repo || !path || !path[0]) return false;
  char full[1100];
  snprintf(full, sizeof(full), "%s/%s", git_repo_path(gc->repo), path);
  FILE *f = fopen(full, "r");
  if (f) {
    char line[4096];
    bool marked = false;
    while (!marked && fgets(line, sizeof(line), f))
      marked = !strncmp(line, "<<<<<<< ", 8) || !strncmp(line, ">>>>>>> ", 8);
    fclose(f);
    if (marked) {
      snprintf(err, err_n, ui_tr("%s still has conflict markers. Edit them out, or use one version."), path);
      return false;
    }
  }
  const char *add[] = { "git", "add", "--", path, NULL };
  return gc_git_step(add, false, err, err_n);
}

// ═══════════════════════════════════════════════════════════════════════════
// Per-line staging
// ═══════════════════════════════════════════════════════════════════════════

static bool hunk_header(const char *s, int *os, int *oc, int *ns, int *nc) {
  *oc = *nc = 1;
  if (sscanf(s, "@@ -%d,%d +", os, oc) != 2 && sscanf(s, "@@ -%d +", os) != 1) return false;
  if (!(s = strstr(s, " +"))) return false;
  return sscanf(s, " +%d,%d", ns, nc) == 2 || sscanf(s, " +%d", ns) == 1;
}

// A patch against HEAD that holds only the included lines, the way GitHub
// Desktop formats one: an excluded removal stays as context, an excluded
// addition is left out, and each hunk header is recounted.
static int format_line_patch(const char *path, const gc_diff_state_t *st, char *out, size_t cap) {
  size_t n = (size_t)snprintf(out, cap, "--- a/%s\n+++ b/%s\n", path, path);
  int orig_delta = 0, delta = 0;
  char *body = malloc(cap);
  if (!body) return -1;
  for (int h = 0; h < st->hunk_count && n < cap; h++) {
    int os, oc, ns, nc;
    if (!hunk_header(st->lines[st->hunk_offsets[h]].text, &os, &oc, &ns, &nc)) continue;
    size_t b = 0;
    int old_n = 0, new_n = 0, changes = 0;
    bool kept = false;   // whether the previous line made it into the patch, for "\ No newline"
    for (int i = st->hunk_offsets[h] + 1; i < st->line_count && b < cap; i++) {
      const gc_diff_line_t *l = &st->lines[i];
      if (l->kind == GC_DIFF_HUNK || l->kind == GC_DIFF_META) break;
      char sign = 0;
      switch (l->kind) {
        case GC_DIFF_CONTEXT: sign = ' '; old_n++; new_n++; break;
        case GC_DIFF_DEL:     sign = l->included ? '-' : ' '; old_n++; new_n += !l->included; changes += l->included; break;
        case GC_DIFF_ADD:     if (l->included) { sign = '+'; new_n++; changes++; } break;
        case GC_DIFF_NOTE:    if (kept) b += (size_t)snprintf(body + b, cap - b, "\\ %s\n", l->text); continue;
      }
      kept = sign != 0;
      if (sign) b += (size_t)snprintf(body + b, cap - b, "%c%s\n", sign, l->text);
    }
    int start = ns + delta - orig_delta + (nc == 0 && new_n > 0) - (nc > 0 && new_n == 0);
    orig_delta += nc - oc;
    if (!changes) continue;
    delta += new_n - old_n;
    n += (size_t)snprintf(out + n, cap - n, "@@ -%d,%d +%d,%d @@\n%.*s", os, old_n, start, new_n, (int)MIN(b, cap), body);
  }
  free(body);
  return n < cap ? (int)n : -1;
}

bool gc_stage_lines(const char *path, const gc_diff_state_t *st) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo || !path || !path[0] || !st) return false;
  int selectable = 0, included = 0;
  for (int i = 0; i < st->line_count; i++)
    if (st->lines[i].selectable) { selectable++; included += st->lines[i].included; }
  if (!selectable) return false;
  if (included == selectable) return gc_stage_file(path);
  char out[1024] = {0};

  // Start the file over from HEAD, remembering the entry to put back if the patch fails.
  char entry[1024] = {0}, mode[16] = {0}, sha[65] = {0};
  const char *ls_args[]    = { "git", "ls-files", "-s", "--", path, NULL };
  const char *reset_args[] = { "git", "reset", "-q", "--", path, NULL };
  if (git_run_sync(gc->repo, ls_args, entry, sizeof(entry))) sscanf(entry, "%15s %64s", mode, sha);
  if (!git_run_sync(gc->repo, reset_args, out, sizeof(out))) { GC_LOG("unstage %s failed: %s", path, out); return false; }
  if (!included) return true;

  char git_dir[1024] = {0}, patch_path[1100];
  const char *dir_args[] = { "git", "rev-parse", "--absolute-git-dir", NULL };
  if (!git_run_sync(gc->repo, dir_args, git_dir, sizeof(git_dir))) return false;
  git_dir[strcspn(git_dir, "\r\n")] = '\0';
  snprintf(patch_path, sizeof(patch_path), "%s/gc_line_patch", git_dir);

  char *patch = malloc(GC_DIFF_BUF_SIZE);
  int len = patch ? format_line_patch(path, st, patch, GC_DIFF_BUF_SIZE) : -1;
  FILE *f = len > 0 ? fopen(patch_path, "wb") : NULL;
  bool ok = f && fwrite(patch, 1, (size_t)len, f) == (size_t)len;
  if (f) fclose(f);
  free(patch);
  const char *apply_args[] = { "git", "apply", "--cached", "--whitespace=nowarn", patch_path, NULL };
  ok = ok && git_run_sync(gc->repo, apply_args, out, sizeof(out));
  remove(patch_path);
  if (!ok) {
    fprintf(stderr, "[gitclient] staging lines of %s failed: %s\n", path, out);
    fflush(stderr);
    char cacheinfo[700];
    snprintf(cacheinfo, sizeof(cacheinfo), "%s,%s,%s", mode, sha, path);
    const char *restore_args[] = { "git", "update-index", "--add", "--cacheinfo", cacheinfo, NULL };
    if (mode[0] && sha[0]) git_run_sync(gc->repo, restore_args, out, sizeof(out));
  }
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
