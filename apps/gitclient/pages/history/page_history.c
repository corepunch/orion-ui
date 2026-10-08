// History page — branches / tags / worktrees / stash sidebar, commit log, files, diff.

#include "page_history.h"

void page_history_refresh_sidebar(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->history_db) return;
  if (gc->stash_win) {
    send_message(gc->stash_win, tvRefresh, 0, NULL);
    bool visible = send_message(gc->stash_win, tvGetRecord, 0, NULL) != 0;
    if (visible != window_has_state(gc->stash_win, WINDOW_STATE_VISIBLE)) {
      show_window(gc->stash_win, visible);
      send_message(gc->stash_win->parent, evResize, 0, NULL);
      invalidate_window(gc->stash_win->parent);
    }
  }
  gc->worktree_syncing = true;
  if (gc->worktrees_win) send_message(gc->worktrees_win, tvRefresh, 0, NULL);
  result_node_t *rows = (result_node_t *)send_db_message(gc->history_db, dbFetch, MAKEDWORD(ID_DB_WORKTREES, 0), NULL);
  gc->worktree_count = 0;
  for (result_node_t *n = rows; n; n = n->next) {
    const db_worktree_t *rec = *(db_worktree_t **)n->data;
    if (rec->is_current && gc->worktrees_win)
      send_message(gc->worktrees_win, RVM_SETSELECTION, gc->worktree_count, NULL);
    gc->worktree_count++;
  }
  free_result_list(rows);
  gc->worktree_syncing = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Window proc — captures outlets on evCreate.
// ─────────────────────────────────────────────────────────────────────────────

result_t page_history_proc(window_t *win, uint32_t msg,
                            uint32_t wparam, void *lparam) {
  (void)wparam; (void)lparam;
  if (msg != evCreate) return false;

  gc_state_t *gc = g_gc;
  if (!gc) return false;

  gc->branches_win      = get_window_item(win, ID_HISTORY_PAGE_BRANCHES);
  gc->tags_win          = get_window_item(win, ID_HISTORY_PAGE_TAGS);
  gc->stash_win         = get_window_item(win, ID_HISTORY_PAGE_STASH_LIST);
  gc->worktrees_win     = get_window_item(win, ID_HISTORY_PAGE_WORKTREES);
  gc->log_win           = get_window_item(win, ID_HISTORY_PAGE_LOG);
  gc->history_files_win = get_window_item(win, ID_HISTORY_PAGE_HISTORY_FILES);
  gc->history_diff_win  = get_window_item(win, ID_HISTORY_PAGE_HISTORY_DIFF);
  page_history_refresh_sidebar();

  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Event delegation — called from gc_main_proc when tab 1 is active.
// ─────────────────────────────────────────────────────────────────────────────

bool page_history_handle(window_t *main_win, uint32_t msg,
                          uint32_t wparam, void *lparam) {
  (void)main_win;
  if (msg != evCommand) return false;
  gc_state_t *gc = g_gc;
  if (!gc) return false;

  uint16_t code = (uint16_t)HIWORD(wparam);
  window_t *src = (window_t *)lparam;

  if (code == RVN_SELCHANGE) {
    int sel = (int)(int16_t)LOWORD(wparam);

    if (src == gc->worktrees_win) {
      if (gc->worktree_syncing) return true;
      const db_worktree_t *rec = (const db_worktree_t *)send_message(src, tvGetRecord, (uint32_t)sel, NULL);
      if (!rec || rec->is_current) return true;
      if (rec->is_prunable) {
        fprintf(stderr, "[gitclient] worktree unavailable win=%u path=%s prunable=1\n", (unsigned)src->id, rec->path);
        fflush(stderr);
        page_history_refresh_sidebar();
        return true;
      }
      char path[512];
      snprintf(path, sizeof(path), "%s", rec->path);
      gc_open_repo(path);
      page_history_refresh_sidebar();
      return true;
    }

    if (src == gc->branches_win) {
      gc->selected_commit = -1;
      gc->selected_file   = -1;
      result_node_t *rows = (result_node_t *)send_db_message(
        gc->history_db, dbFetch, MAKEDWORD(ID_DB_BRANCHES, 0), (void *)(intptr_t)0);
      int row = 0;
      for (result_node_t *n = rows; n; n = n->next, row++) {
        if (row == sel) {
          db_branche_t *b = *(db_branche_t **)n->data;
          if (b && b->name[0])
            gc_reload_history_log(b->name);
          break;
        }
      }
      free_result_list(rows);
      return true;
    }

    if (src == gc->log_win) {
      if (sel != gc->selected_commit) {
        gc->selected_commit = sel;
        gc->selected_file   = -1;
        gc->files_win = gc->history_files_win;
        gc->diff_win  = gc->history_diff_win;
        gc_diff_refresh();
      }
      return true;
    }

    if (src == gc->history_files_win) {
      if (sel != gc->selected_file) {
        gc->files_win = gc->history_files_win;
        gc->diff_win  = gc->history_diff_win;
        gc->selected_file = sel;
        gc_diff_refresh();
      }
      return true;
    }

    return false;
  }

  if (code == RVN_DBLCLK && src == gc->stash_win) {
    gc_stash_pop();
    gc_refresh_all();
    return true;
  }

  if (code == GC_DIFF_TOGGLE_UNIFIED) {
    if (gc->diff_win) {
      gc_diff_state_t *st = (gc_diff_state_t *)gc->diff_win->userdata;
      if (st) { gc->unified_diff = st->unified_mode; gc_diff_refresh(); }
    }
    return true;
  }

  if (code == GC_DIFF_STAGE_HUNK) {
    int hunk_idx = (int)(int16_t)LOWORD(wparam);
    if (gc->diff_win) {
      gc_diff_state_t *st = (gc_diff_state_t *)gc->diff_win->userdata;
      if (st && st->hunk_path[0]) {
        gc_stage_hunk(st->hunk_path, hunk_idx);
        gc_refresh_all();
      }
    }
    return true;
  }

  return false;
}
