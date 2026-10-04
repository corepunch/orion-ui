// Main window — thin router. Page sub-forms own their UI and event logic.

#include "gitclient.h"
#include "gc_actions.h"
#include "pages/changes/page_changes.h"
#include "pages/history/page_history.h"
#include "pages/github/page_github.h"
#include <orion/user/vga_font.h>
#include <orion/commctl/menubar.h>

// ============================================================
// Open / refresh
// ============================================================

void gc_set_view_mode(int tab) {
  gc_state_t *gc = g_gc; if (!gc || !gc->main_win) return;
  if (gc->tab != GC_TAB_OVERVIEW && tab == GC_TAB_OVERVIEW) gc->focus_tab = gc->tab ? gc->tab : GC_TAB_CHANGES;
  gc->tab = tab;
  gc_diff_invalidate();   // each page owns its own diff window
  gc->history_mode = (tab == GC_TAB_HISTORY);
  if (gc->tabs_win) send_message(gc->tabs_win, tcSetSelection, (uint32_t)tab, NULL);

  window_t *page = tab == GC_TAB_OVERVIEW ? gc->overview_page_win :
                   tab == GC_TAB_CHANGES  ? gc->changes_page_win :
                   tab == GC_TAB_HISTORY  ? gc->history_page_win :
                   tab == GC_TAB_GITHUB   ? gc->github_page_win : NULL;
  if (page) set_host_page(gc->main_win, page);

  switch (tab) {
    case GC_TAB_OVERVIEW:
      gc->files_win = NULL;
      gc->diff_win  = NULL;
      gc_overview_refresh();
      if (gc->board_win) set_focus(gc->board_win);
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

void gc_open_repo(const char *path) {
  gc_state_t *gc = g_gc;
  if (!gc) return;

  git_repo_t *next = git_repo_open(path);
  if (!next) {
    message_box(gc->main_win, "Not a valid git repository.", "Open Repository", MB_OK);
    return;
  }
  git_repo_close(gc->repo);
  gc->repo = next;

  strncpy(gc->repo_path, path, sizeof(gc->repo_path) - 1);
  gc_recent_add(git_repo_path(gc->repo));

  if (gc->main_win) {
    char title[600];
    snprintf(title, sizeof(title), "Git Client - %s", gc_repo_display_name(gc->repo));
    strncpy(gc->main_win->title, title, sizeof(gc->main_win->title) - 1);
    gc->main_win->title[sizeof(gc->main_win->title) - 1] = '\0';
    invalidate_window(gc->main_win);
  }

  gc_refresh_all();
}

void gc_refresh_all(void) {
  gc_state_t *gc = g_gc;
  if (!gc) return;
  if (!gc->repo) { if (gc->tab == GC_TAB_OVERVIEW) gc_overview_refresh(); return; }

  gc_diff_invalidate();
  gc->selected_commit = -1;
  gc->selected_file   = -1;

  send_db_message(gc->changes_db, dbLoad, 0, gc->repo);
  send_db_message(gc->history_db, dbLoad, 0, gc->repo);

  if (gc->branches_win)
    send_message(gc->branches_win, tvRefresh, 0, NULL);
  if (gc->tags_win)
    send_message(gc->tags_win, tvRefresh, 0, NULL);
  if (gc->stash_win)
    send_message(gc->stash_win, tvRefresh, 0, NULL);

  if (gc->branches_win) {
    result_node_t *rows = (result_node_t *)send_db_message(
      gc->history_db, dbFetch, MAKEDWORD(ID_DB_BRANCHES, 0), (void *)(intptr_t)0);
    int row = 0;
    for (result_node_t *n = rows; n; n = n->next, row++) {
      db_branche_t *branch = *(db_branche_t **)n->data;
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
  gc_update_status();
}

void gc_update_status(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->main_win) return;

  git_sync_status_t st = {0};
  if (gc->repo) git_get_sync_status(gc->repo, &st);

  char status[768] = "No repository";
  if (gc->tab == GC_TAB_OVERVIEW) gc_overview_status(status, sizeof(status));
  else if (gc->repo) {
    const char *kind = st.initial ? "first commit" : st.detached ? "detached" :
                       st.gone ? "upstream gone" : !st.upstream[0] ? "not published" : NULL;
    snprintf(status, sizeof(status), "Branch: %s%s  ^%d  v%d%s%s%s%s",
             st.head, st.dirty ? " *" : "", st.ahead, st.behind,
             st.upstream[0] ? "  " : "", st.upstream[0] ? st.upstream : "",
             kind ? "  (" : "", kind ? kind : "");
    if (kind) strncat(status, ")", sizeof(status) - strlen(status) - 1);
  }
  send_message(gc->main_win, evStatusBar, 0, (void *)status);
  if (gc->repo) {
    git_file_status_t *files = malloc(sizeof(*files) * GC_MAX_FILES);
    int staged = 0, modified = 0, fresh = 0, conflicts = 0, n = files ? git_get_status(gc->repo, files, GC_MAX_FILES) : 0;
    for (int i = 0; i < n; i++) {
      if (files[i].conflicted) conflicts++; else if (files[i].untracked) fresh++;
      else { if (files[i].staged) staged++; if (files[i].worktree_status != ' ') modified++; }
    }
    free(files);
    char summary[256]; int len = snprintf(summary, sizeof(summary), "%s", st.head);
    if (st.ahead)  len += snprintf(summary + len, sizeof(summary) - len, "  |  %d to push", st.ahead);
    if (st.behind) len += snprintf(summary + len, sizeof(summary) - len, "  |  %d to pull", st.behind);
    if (staged)    len += snprintf(summary + len, sizeof(summary) - len, "  |  %d staged", staged);
    if (modified)  len += snprintf(summary + len, sizeof(summary) - len, "  |  %d modified", modified);
    if (fresh)     len += snprintf(summary + len, sizeof(summary) - len, "  |  %d new", fresh);
    if (conflicts) len += snprintf(summary + len, sizeof(summary) - len, "  |  %d CONFLICTED", conflicts);
    if (!staged && !modified && !fresh && !conflicts) snprintf(summary + len, sizeof(summary) - len, "  |  working tree clean");
    set_window_item_text(gc->main_win, ID_CHANGES_PAGE_SUMMARY, "%s", summary);
    char hint[96];
    if (st.initial) snprintf(hint, sizeof(hint), "Create the first commit");
    else if (staged) snprintf(hint, sizeof(hint), "Commit %d staged file%s", staged, staged == 1 ? "" : "s");
    else snprintf(hint, sizeof(hint), "Nothing staged - check files above to include them");
    set_window_item_text(gc->main_win, ID_CHANGES_PAGE_COMMIT_HINT, "%s", hint);
    set_window_item_text(gc->main_win, ID_CHANGES_PAGE_COMMIT_NOW, "Commit");
  }
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

      gc->tabs_win = get_window_item(win, ID_MAIN_WINDOW_VIEWS);

      window_t *overview_tab = get_window_item(win, ID_MAIN_WINDOW_OVERVIEW_TAB);
      window_t *changes_tab = get_window_item(win, ID_MAIN_WINDOW_CHANGES_TAB);
      window_t *history_tab = get_window_item(win, ID_MAIN_WINDOW_HISTORY_TAB);
      window_t *github_tab  = get_window_item(win, ID_MAIN_WINDOW_GITHUB_TAB);

      if (overview_tab) {
        gc->overview_page_win = create_window_from_form(
          &gc_overview_page_form, 0, 0, overview_tab, gc_page_overview_proc, gc->hinstance, NULL);
      }
      if (changes_tab)
        gc->changes_page_win = create_window_from_form(
          &gc_changes_page_form, 0, 0, changes_tab, page_changes_proc,
          gc->hinstance, NULL);
      if (history_tab)
        gc->history_page_win = create_window_from_form(
          &gc_history_page_form, 0, 0, history_tab, page_history_proc,
          gc->hinstance, NULL);
      if (github_tab)
        gc->github_page_win = create_window_from_form(
          &gc_github_page_form, 0, 0, github_tab, page_github_proc,
          gc->hinstance, NULL);

      send_message(win, evStatusBar, 0, "No repository");
      gc_set_view_mode(GC_TAB_CHANGES);

      char font_path[600];
      snprintf(font_path, sizeof(font_path),
               "%s/../share/orion/fonts/monoid.ttf",
               ui_get_exe_dir());
      vga_font_init(font_path, 12.0f);

      return true;
    }

    case evDestroy:
      vga_font_shutdown();
      git_repo_close(gc->repo);
      gc->repo = NULL;
      return false;

    case evActivate:
      return false;

    case evPaint:
      return false;

    case tbButtonClick: {
      uint16_t id = (uint16_t)wparam;
      (void)gc_execute_action(id);
      return true;
    }

    case evCommand: {
      uint16_t code = (uint16_t)HIWORD(wparam);

      if (gc_overview_handle_command(wparam, lparam)) return true;

      if (code == tcnSelChange && (window_t *)lparam == gc->tabs_win) {
        int tab = (int)send_message(gc->tabs_win, tcGetSelection, 0, NULL);
        gc_set_view_mode(tab);
        return true;
      }

      if (code == kMenuBarNotificationItemClick) {
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
            gc_open_repo(gc_->clone_path);
          gc_->clone_path[0] = '\0';
        } else {
          if (gc->fetching_all) { gc->fetching_all = false; }
          gc_refresh_all();
        }
        git_async_result_free(res);
      }
      return true;
    }

    case evOpenRepo:
      if (lparam)
        gc_open_repo((const char *)lparam);
      return true;

    default:
      return false;
  }
}
