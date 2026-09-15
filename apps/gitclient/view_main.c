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
  gc->history_mode = (tab == 1);
  if (gc->tabs_win) send_message(gc->tabs_win, tcSetSelection, (uint32_t)tab, NULL);

  window_t *page = tab == 0 ? gc->changes_page_win :
                   tab == 1 ? gc->history_page_win :
                   tab == 2 ? gc->github_page_win : NULL;
  if (page) set_host_page(gc->main_win, page);

  switch (tab) {
    case 0:
      gc->files_win = gc->changes_files_win;
      gc->diff_win  = gc->changes_diff_win;
      gc->selected_commit = -1;
      gc->selected_file   = -1;
      if (gc->changes_files_win)
        send_message(gc->changes_files_win, tvSetFilter, ID_DB_FILES_COMMIT_ID, (void *)(intptr_t)0);
      break;
    case 1:
      gc->files_win = gc->history_files_win;
      gc->diff_win  = gc->history_diff_win;
      gc->selected_commit = gc->log_win
        ? (int)send_message(gc->log_win, RVM_GETSELECTION, 0, NULL) : -1;
      gc->selected_file = -1;
      break;
    case 2:
      gc->files_win = NULL;
      gc->diff_win  = NULL;
      page_github_refresh();
      break;
    default: break;
  }

  GC_TRACE("set_view_mode tab=%d diff_win=%p files_win=%p",
           tab, (void *)gc->diff_win, (void *)gc->files_win);

  if (tab != 2) gc_diff_refresh();
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

  GC_TRACE("open_repo: %s", path);
  git_repo_t *next = git_repo_open(path);
  if (!next) {
    GC_TRACE("open_repo: invalid repo");
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
  if (!gc || !gc->repo) return;

  GC_TRACE("refresh_all begin");
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
    int active_tab = gc->tabs_win ? (int)send_message(gc->tabs_win, tcGetSelection, 0, NULL) : 0;
    if (active_tab == 0) gc->selected_commit = -1;
  }

  int active_tab = gc->tabs_win ? (int)send_message(gc->tabs_win, tcGetSelection, 0, NULL) : 0;
  if (active_tab == 2) page_github_refresh();

  gc_diff_refresh();
  gc_update_status();
}

void gc_update_status(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->main_win) return;

  git_sync_status_t st = {0};
  if (gc->repo) git_get_sync_status(gc->repo, &st);

  char status[384] = "No repository";
  if (gc->repo) {
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
    set_window_item_text(gc->main_win, ID_CHANGES_PAGE_COMMIT_HINT,
                         st.initial ? "Create the first commit" : "Commit staged changes");
    set_window_item_text(gc->main_win, ID_CHANGES_PAGE_COMMIT_NOW, "Commit");
  }
}

// ============================================================
// Main window procedure
// ============================================================

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

      // Instantiate each page sub-form inside its tab slot.
      // Page procs capture their own outlets in evCreate.
      window_t *changes_tab = get_window_item(win, ID_MAIN_WINDOW_CHANGES_TAB);
      window_t *history_tab = get_window_item(win, ID_MAIN_WINDOW_HISTORY_TAB);
      window_t *github_tab  = get_window_item(win, ID_MAIN_WINDOW_GITHUB_TAB);

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

      GC_LOG("page outlets after sub-form creation: "
             "changes_files=%p branches=%p github_issues=%p",
             (void *)gc->changes_files_win,
             (void *)gc->branches_win,
             (void *)gc->github_issues_win);

      send_message(win, evStatusBar, 0, "No repository");
      send_message(win, tbSetStyle, TOOLBAR_STYLE_SHOW_LABELS, NULL);
      gc_set_view_mode(0);

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
      GC_TRACE("toolbar id=%d", (int)id);
      (void)gc_execute_action(id);
      return true;
    }

    case evCommand: {
      uint16_t code = (uint16_t)HIWORD(wparam);

      if (code == tcnSelChange && (window_t *)lparam == gc->tabs_win) {
        int tab = (int)send_message(gc->tabs_win, tcGetSelection, 0, NULL);
        GC_TRACE("evCommand tcnSelChange -> tab %d", tab);
        gc_set_view_mode(tab);
        return true;
      }

      if (code == kMenuBarNotificationItemClick) {
        GC_TRACE("evCommand menu: id=%d", (int)LOWORD(wparam));
        (void)gc_execute_action(LOWORD(wparam));
        return true;
      }

      // Delegate to the active page handler.
      int active_tab = gc->tabs_win
        ? (int)send_message(gc->tabs_win, tcGetSelection, 0, NULL) : 0;
      if (active_tab == 0) return page_changes_handle(win, msg, wparam, lparam);
      if (active_tab == 1) return page_history_handle(win, msg, wparam, lparam);
      if (active_tab == 2) return page_github_handle(win, msg, wparam, lparam);
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
