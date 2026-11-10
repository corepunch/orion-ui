// Main window — thin router. Page sub-forms own their UI and event logic.

#include "gitclient.h"
#include "gc_actions.h"
#include "pages/changes/page_changes.h"
#include "pages/history/page_history.h"
#include "pages/github/page_github.h"
#include <orion/commctl/menubar.h>
#include <sys/stat.h>

// ============================================================
// Open / refresh
// ============================================================

static const uint16_t k_gc_view_commands[] = {ID_VIEW_OVERVIEW, ID_VIEW_CHANGES, ID_VIEW_HISTORY, ID_VIEW_GITHUB};

void gc_set_view_mode(int tab) {
  gc_state_t *gc = g_gc; if (!gc || !gc->main_win) return;
  if (tab < GC_TAB_OVERVIEW || tab > GC_TAB_GITHUB) {
    fprintf(stderr, "[gitclient] view rejected win=%u view=%d\n", gc->main_win->id, tab);
    fflush(stderr);
    return;
  }
  page_host_select(gc->main_win, k_gc_view_commands[tab]);
}

static void gc_view_activated(int tab) {
  gc_state_t *gc = g_gc;
  gc->tab = tab;
  gc_diff_invalidate();
  gc->history_mode = (tab == GC_TAB_HISTORY);

  switch (tab) {
    case GC_TAB_OVERVIEW:
      gc->files_win = NULL;
      gc->diff_win  = NULL;
      gc_overview_refresh();
      break;
    case GC_TAB_CHANGES:
      gc->files_win = gc->changes_files_win;
      gc->diff_win  = gc->changes_diff_win;
      gc->selected_commit = -1;
      gc->selected_file   = -1;
      if (gc->changes_files_win)
        send_message(gc->changes_files_win, tvSetFilter, ID_DB_FILES_COMMIT_ID, (void *)(intptr_t)0);
      break;
    case GC_TAB_HISTORY:
      gc->files_win = gc->history_files_win;
      gc->diff_win  = gc->history_diff_win;
      gc->selected_commit = gc->log_win
        ? (int)send_message(gc->log_win, RVM_GETSELECTION, 0, NULL) : -1;
      gc->selected_file = -1;
      break;
    case GC_TAB_GITHUB:
      gc->files_win = NULL;
      gc->diff_win  = NULL;
      page_github_refresh();
      break;
    default: break;
  }


  if (tab == GC_TAB_CHANGES || tab == GC_TAB_HISTORY) gc_diff_refresh();
  gc_update_status();
  invalidate_window(gc->main_win);
}

static const char *gc_repo_display_name(const git_repo_t *repo) {
  const char *path = git_repo_path((git_repo_t *)repo);
  const char *slash = path ? strrchr(path, '/') : NULL;
  const char *backslash = path ? strrchr(path, '\\') : NULL;
  const char *separator = slash > backslash ? slash : backslash;
  return separator && separator[1] ? separator + 1 : path;
}

static bool gc_path_is_dir(const char *path) {
  struct stat st;
  if (!path || stat(path, &st) != 0) return false;
#ifdef S_ISDIR
  return S_ISDIR(st.st_mode);
#else
  return (st.st_mode & _S_IFMT) == _S_IFDIR;
#endif
}

void gc_update_title(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->main_win) return;
  const char *name = "Git Client";
  char file_name[256];
  if (gc->workspace_file[0]) {
    const char *base = gc->workspace_file;
    for (const char *p = base; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    snprintf(file_name, sizeof(file_name), "%s", base);
    name = file_name;
  } else if (gc->workspace_count > 1) name = "Workspace";
  else if (gc->repo) name = gc_repo_display_name(gc->repo);
  char title[600];
  snprintf(title, sizeof(title), "Git Client - %s%s", name, gc_workspace_unsaved() ? " *" : "");
  strncpy(gc->main_win->title, title, sizeof(gc->main_win->title) - 1);
  gc->main_win->title[sizeof(gc->main_win->title) - 1] = '\0';
  invalidate_window(gc->main_win);
}

int gc_fill_worktree_combo(window_t *combo, const char *select_path, char (*paths)[512], int max) {
  gc_state_t *gc = g_gc;
  if (!combo || !gc || !paths || max <= 0) return 0;
  send_message(combo, cbClear, 0, NULL);
  git_worktree_t wts[GC_MAX_WORKTREES];
  int listed = gc->repo ? git_worktree_list(git_repo_path(gc->repo), wts, GC_MAX_WORKTREES) : 0;
  char want[512];
  bool have = select_path && git_path_absolute(select_path, want, sizeof(want));
  char labels[GC_MAX_WORKTREES][64];
  int shown = 0, sel = 0;
  for (int i = 0; i < listed && shown < max && shown < GC_MAX_WORKTREES; i++) {
    if (wts[i].bare) continue;
    git_worktree_label(&wts[i], labels[shown], sizeof(labels[shown]));
    for (int k = 2; k < 10; k++) {
      bool clash = false;
      for (int j = 0; j < shown; j++) if (!strcmp(labels[j], labels[shown])) clash = true;
      if (!clash) break;
      char next[64];
      snprintf(next, sizeof(next), "%.54s #%d", labels[shown], k);
      memcpy(labels[shown], next, sizeof(labels[shown]));
    }
    send_message(combo, cbAddString, 0, labels[shown]);
    if (!git_path_absolute(wts[i].path, paths[shown], 512)) {
      strncpy(paths[shown], wts[i].path, 511); paths[shown][511] = 0;
    }
    if (have && !strcmp(paths[shown], want)) sel = shown;
    shown++;
  }
  if (!shown) send_message(combo, cbAddString, 0, (void *)(gc->repo ? "No worktrees" : "No repository"));
  else send_message(combo, cbSetCurrentSelection, (uint32_t)sel, NULL);
  invalidate_window(combo);
  return shown;
}

// Makes `checkout` the open repository and returns at once: the databases are emptied and the
// page shows "Loading..." until the worker thread posts evRepoLoaded. `known` skips the workspace lookup.
void gc_open_checkout(const char *checkout, bool known) {
  gc_state_t *gc = g_gc;
  if (!gc || !checkout || !checkout[0]) return;
  if (gc->repo) {
    char cur[512];
    if (git_path_absolute(git_repo_path(gc->repo), cur, sizeof(cur)) && !strcmp(cur, checkout)) {
      gc_refresh_all();
      return;
    }
  }
  git_repo_t *next = git_repo_open(checkout);
  if (!next) {
    message_box(gc->main_win, "Not a valid git repository.", "Open Repository", MB_OK);
    return;
  }
  git_repo_close(gc->repo);
  gc->repo = next;
  gc->repo_generation++;
  gc->sync_valid = false;
  snprintf(gc->repo_path, sizeof(gc->repo_path), "%s", checkout);
  if (!known) gc_workspace_add(checkout);
  gc_update_title();
  git_repo_snapshot_t *empty = calloc(1, sizeof(*empty));
  if (empty) { empty->repo = next; empty->commit_branch = -1; gc_apply_snapshot(empty); free(empty); }
  gc_refresh_all();
}

void gc_open_repo(const char *path) {
  gc_state_t *gc = g_gc;
  if (!gc || !path || !path[0]) return;
  char checkout[512], main_root[512];
  if (!git_locate(path, checkout, sizeof(checkout), main_root, sizeof(main_root))) {
    message_box(gc->main_win, "Not a valid git repository.", "Open Repository", MB_OK);
    return;
  }
  gc_open_checkout(checkout, false);
}

void gc_add_repo(const char *path) {
  gc_state_t *gc = g_gc;
  if (!gc) return;
  int before = gc->workspace_count;
  gc_open_repo(path);
  if (gc->repo && gc->workspace_count > before && gc->workspace_count > 1)
    gc_set_view_mode(GC_TAB_OVERVIEW);
}

bool gc_handle_open_file(const char *path) {
  gc_state_t *gc = g_gc;
  if (!gc || !path || !path[0]) return false;
  if (gc_workspace_file_is(path)) return gc_workspace_open(path, true);
  if (!gc_path_is_dir(path)) {
    if (gc->main_win) message_box(gc->main_win, "Drop a git folder, or a gitclient workspace file.", "Git Client", MB_OK);
    return false;
  }
  char checkout[512], main_root[512];
  if (!git_locate(path, checkout, sizeof(checkout), main_root, sizeof(main_root))) {
    if (gc->main_win) message_box(gc->main_win, "That folder is not a git repository.", "Git Client", MB_OK);
    return false;
  }
  gc_add_repo(checkout);
  return gc->repo != NULL;
}

// Shows a finished (or empty) snapshot: both databases, the sidebars, the selection and the diff.
void gc_apply_snapshot(git_repo_snapshot_t *snap) {
  gc_state_t *gc = g_gc;
  if (!gc || !snap) return;
  snap->repo = gc->repo;
  gc_diff_invalidate();
  gc->selected_commit = -1;
  gc->selected_file   = -1;

  send_db_message(gc->changes_db, dbLoad, GC_DB_LOAD_SNAPSHOT, snap);
  send_db_message(gc->history_db, dbLoad, GC_DB_LOAD_SNAPSHOT, snap);
  if (snap->parts & GIT_SNAP_SYNC) { gc->sync = snap->sync; gc->sync_valid = true; }

  if (gc->branches_win)
    send_message(gc->branches_win, tvRefresh, 0, NULL);
  if (gc->tags_win)
    send_message(gc->tags_win, tvRefresh, 0, NULL);
  page_history_refresh_sidebar();

  if (gc->branches_win) {
    result_node_t *rows = (result_node_t *)send_db_message(
      gc->history_db, dbFetch, MAKEDWORD(ID_DB_BRANCHES, 0), (void *)(intptr_t)0);
    int row = 0;
    for (result_node_t *n = rows; n; n = n->next, row++) {
      db_branch_t *branch = *(db_branch_t **)n->data;
      if (branch && branch->is_current) {
        send_message(gc->branches_win, RVM_SETSELECTION, (uint32_t)row, NULL);
        break;
      }
    }
    free_result_list(rows);
    tableview_handle_master_selection(get_root_window(gc->branches_win),
                                      gc->branches_win);
  }
  if (gc->changes_files_win) {
    send_message(gc->changes_files_win, tvSetFilter, ID_DB_FILES_COMMIT_ID, (void *)(intptr_t)0);
    if (gc->tab == GC_TAB_CHANGES) gc->selected_commit = -1;
  }

  if (gc->tab == GC_TAB_GITHUB) page_github_refresh();
  if (gc->tab == GC_TAB_OVERVIEW) gc_overview_refresh();

  gc_diff_refresh();
  gc_render_status();
}

void gc_refresh_all_sync(void) {
  gc_state_t *gc = g_gc;
  if (!gc) return;
  if (!gc->repo) { if (gc->tab == GC_TAB_OVERVIEW) gc_overview_refresh(); return; }
  git_repo_snapshot_t *snap = git_repo_snapshot_read(git_repo_path(gc->repo), GIT_SNAP_ALL, gc->repo_generation);
  if (!snap) return;
  gc_apply_snapshot(snap);
  free(snap);
}

// One load runs at a time; a refresh asked for meanwhile runs once it has finished.
void gc_refresh_all(void) {
  gc_state_t *gc = g_gc;
  if (!gc) return;
  if (!gc->repo) { if (gc->tab == GC_TAB_OVERVIEW) gc_overview_refresh(); return; }
  if (gc->sync_loads) { gc_refresh_all_sync(); return; }
  if (gc->repo_loading) { gc->repo_stale = true; return; }
  if (git_repo_snapshot_async(git_repo_path(gc->repo), GIT_SNAP_ALL, gc->repo_generation, gc->main_win)) {
    gc->repo_loading = true;
    gc_render_status();
  } else {
    fprintf(stderr, "[gitclient] background load not started for %s; reading on the UI thread\n", git_repo_path(gc->repo));
    fflush(stderr);
    gc_refresh_all_sync();
  }
}

void gc_repo_loaded(git_repo_snapshot_t *snap) {
  gc_state_t *gc = g_gc;
  if (!gc) { free(snap); return; }
  gc->repo_loading = false;
  bool current = snap && gc->repo && snap->generation == gc->repo_generation &&
                 !strcmp(git_repo_path(gc->repo), snap->path);
  if (current) gc_apply_snapshot(snap);
  else if (snap && !current) { GC_LOG("repo load for %s discarded (generation %u, now %u)", snap->path, snap->generation, gc->repo_generation); }
  bool snap_seen = snap != NULL;
  free(snap);
  if (gc->repo_stale || (snap_seen && gc->repo && !current)) {
    gc->repo_stale = false;
    gc_refresh_all();
  } else gc_render_status();
}

void gc_sync_status_loaded(git_sync_result_t *res) {
  gc_state_t *gc = g_gc;
  if (!gc) { free(res); return; }
  gc->sync_loading = false;
  if (res && gc->repo && res->generation == gc->repo_generation &&
      !strcmp(git_repo_path(gc->repo), res->path)) {
    gc->sync = res->sync; gc->sync_valid = true;
    gc_render_status();
  }
  free(res);
  if (gc->sync_stale) { gc->sync_stale = false; gc_update_status(); }
}

// Draws the status bar and the commit button from what is known; never starts a git process.
void gc_render_status(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->main_win) return;
  const git_sync_status_t *st = gc->sync_valid ? &gc->sync : NULL;
  git_sync_status_t none = {0};
  if (!st) st = &none;

  char status[768];
  snprintf(status, sizeof(status), "%s", ui_tr("No repository"));
  if (gc->tab == GC_TAB_OVERVIEW) gc_overview_status(status, sizeof(status));
  else if (gc->repo && gc->repo_loading && !gc->sync_valid)
    snprintf(status, sizeof(status), ui_tr("Loading %s..."), gc_repo_display_name(gc->repo));
  else if (gc->repo && !gc->sync_valid)
    snprintf(status, sizeof(status), "%s", ui_tr("Loading..."));
  else if (gc->repo) {
    const char *kind = st->initial ? "first commit" : st->detached ? "detached" :
                       st->gone ? "upstream gone" : !st->upstream[0] ? "not published" : NULL;
    snprintf(status, sizeof(status), "Branch: %s%s  ^%d  v%d%s%s%s%s",
             st->head, st->dirty ? " *" : "", st->ahead, st->behind,
             st->upstream[0] ? "  " : "", st->upstream[0] ? st->upstream : "",
             kind ? "  (" : "", kind ? kind : "");
    if (kind) strncat(status, ")", sizeof(status) - strlen(status) - 1);
    if (gc->repo_loading) {
      size_t len = strlen(status);
      snprintf(status + len, sizeof(status) - len, "  |  %s", ui_tr("Refreshing..."));
    }
    if (gc->worktree_count > 1) {
      size_t len = strlen(status);
      if (len < sizeof(status))
        snprintf(status + len, sizeof(status) - len, "  |  %d worktrees", gc->worktree_count);
    }
  }
  send_message(gc->main_win, evStatusBar, 0, (void *)status);
  if (gc->repo && gc->sync_valid)
    set_window_item_text(gc->main_win, ID_CHANGES_PAGE_COMMIT_NOW, st->detached ? "Commit" : "Commit to %s", st->head);
  page_changes_update_commit();
}

// Shows the last known status at once and re-reads it on a worker thread (several git processes).
void gc_update_status(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->main_win) return;
  if (gc->repo && gc->tab != GC_TAB_OVERVIEW && !gc->repo_loading) {   // a repository load brings its own status
    if (gc->sync_loads) { git_sync_status_t st = {0}; git_get_sync_status(gc->repo, &st); gc->sync = st; gc->sync_valid = true; }
    else if (gc->sync_loading) gc->sync_stale = true;
    else if (git_sync_status_async(git_repo_path(gc->repo), gc->repo_generation, gc->main_win)) gc->sync_loading = true;
    else {
      fprintf(stderr, "[gitclient] status load not started for %s; reading on the UI thread\n", git_repo_path(gc->repo));
      fflush(stderr);
      git_sync_status_t st = {0}; git_get_sync_status(gc->repo, &st); gc->sync = st; gc->sync_valid = true;
    }
  }
  gc_render_status();
}

result_t gc_main_proc(window_t *win, uint32_t msg,
                      uint32_t wparam, void *lparam) {
  gc_state_t *gc = (gc_state_t *)win->userdata;

  switch (msg) {
    case evGetWorkspaceRect:
      *(irect16_t *)lparam = rect_trim_top(*(irect16_t *)lparam, MENUBAR_HEIGHT);
      return true;
    case evCreate: {
      gc = g_gc;
      win->userdata = gc;
      gc->main_win = win;

      gc->activity_win = get_window_item(win, ID_MAIN_WINDOW_ACTIVITY);
      static const page_handler_t pages[] = {
        {"overview_page", gc_page_overview_proc},
        {"changes_page",  page_changes_proc},
        {"history_page",  page_history_proc},
        {"github_page",   page_github_proc},
      };
      send_message(win, evStatusBar, 0, "No repository");
      if (!page_host_init(win, &gc_main_window_form, pages, ARRAY_LEN(pages))) return false;

      return true;
    }

    case evDestroy:
      git_repo_close(gc->repo);
      gc->repo = NULL;
      return false;

    case evPageChanged:
      for (int i = 0; i < ARRAY_LEN(k_gc_view_commands); i++)
        if (k_gc_view_commands[i] == wparam) { gc_view_activated(i); return true; }
      return false;

    case evActivate:
      return false;

    case evPaint:
      return false;

    case evCommand: {
      uint16_t code = (uint16_t)HIWORD(wparam);
      if (code == btnClicked && lparam && ((window_t *)lparam)->flags & WINDOW_TOOLBAR) {
        (void)gc_execute_action(LOWORD(wparam));
        return true;
      }

      if (gc_overview_handle_command(wparam, lparam)) return true;

      if (code == kMenuBarNotificationItemClick || code == kAcceleratorNotification) {
        (void)gc_execute_action(LOWORD(wparam));
        return true;
      }

      if (gc->tab == GC_TAB_CHANGES) return page_changes_handle(win, msg, wparam, lparam);
      if (gc->tab == GC_TAB_HISTORY) return page_history_handle(win, msg, wparam, lparam);
      if (gc->tab == GC_TAB_GITHUB)  return page_github_handle(win, msg, wparam, lparam);
      return false;
    }

    case evGitOpDone: {
      git_async_result_t *res = (git_async_result_t *)lparam;
      if (res) {
        if (!res->success) {
          message_box(win, res->output, "Operation failed", MB_OK);
        } else if (res->op == GIT_OP_CLONE) {
          gc_state_t *gc_ = g_gc;
          if (gc_ && gc_->clone_path[0])
            gc_add_repo(gc_->clone_path);
          gc_->clone_path[0] = '\0';
        } else {
          if (gc->fetching_all) { gc->fetching_all = false; }
          gc_refresh_all();
        }
        git_async_result_free(res);
      }
      return true;
    }

    case evRepoLoaded:
      gc_repo_loaded((git_repo_snapshot_t *)lparam);
      return true;

    case evSyncStatusLoaded:
      gc_sync_status_loaded((git_sync_result_t *)lparam);
      return true;

    case evWorkspaceLoaded:
      gc_overview_loaded((git_workspace_result_t *)lparam);
      return true;

    case evOpenRepo:
      if (lparam)
        gc_add_repo((const char *)lparam);
      return true;

    default:
      return false;
  }
}
