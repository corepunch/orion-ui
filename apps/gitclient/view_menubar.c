// Menu bar and command dispatch — uses generated menus from gitclient.orion.

#include "gitclient.h"
#include "gc_actions.h"
#include <orion/gem.h>

// ============================================================
// Accelerator table — generated from menu-item hotkeys in gitclient.orion.
// ============================================================

// ============================================================
// Helper: get selected branch name from branches list
// ============================================================

static bool gc_get_selected_branch(char *buf, int buf_sz, bool *is_current) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->branches_win || !gc->history_db) return false;
  int sel = (int)send_message(gc->branches_win, RVM_GETSELECTION, 0, NULL);
  if (sel < 0) return false;
  result_node_t *rows = (result_node_t *)send_db_message(
    gc->history_db, dbFetch, MAKEDWORD(ID_DB_BRANCHES, 0), (void *)(intptr_t)0);
  int row = 0;
  bool found = false;
  for (result_node_t *n = rows; n; n = n->next, row++) {
    if (row == sel) {
      db_branche_t *b = *(db_branche_t **)n->data;
      strncpy(buf, b->name, (size_t)buf_sz - 1);
      if (is_current) *is_current = b->is_current;
      found = true;
      break;
    }
  }
  free_result_list(rows);
  return found;
}

// ============================================================
// Menubar window procedure
// ============================================================

result_t gc_menubar_proc(window_t *win, uint32_t msg,
                         uint32_t wparam, void *lparam) {
  if (msg == evCommand &&
      (HIWORD(wparam) == kMenuBarNotificationItemClick ||
       HIWORD(wparam) == kAcceleratorNotification)) {
    (void)gc_execute_action(LOWORD(wparam));
    return true;
  }
  return win_menubar(win, msg, wparam, lparam);
}

void gc_create_menubar(void) {
  gc_state_t *gc = g_gc;
  if (!gc) return;

  // kGCMenus and kGCMenuCount are generated from gitclient.orion.
  gc->menubar_win = set_app_menu(gc_menubar_proc,
                                 kGCMenus, kGCMenuCount,
                                 gc_handle_command,
                                 gc->hinstance);

  gc->accel = load_accelerators(gitclient_default_accels,
                                gitclient_default_accel_count);
  if (gc->menubar_win && gc->accel)
    send_message(gc->menubar_win, kMenuBarMessageSetAccelerators,
                 0, gc->accel);
}

// ============================================================
// Command handler
// ============================================================

void gc_handle_command_impl(uint16_t id) {
  gc_state_t *gc = g_gc;
  if (!gc) return;

  GC_LOG("gc_handle_command: id=%d", (int)id);

  switch (id) {
    case ID_VIEW_WINDOW_MODE:
      if (gc->main_win->maximized) restore_window(gc->main_win);
      else maximize_window(gc->main_win);
      break;
    case ID_VIEW_CHANGES: gc_set_view_mode(0); break;
    case ID_VIEW_HISTORY: gc_set_view_mode(1); break;
    case ID_VIEW_GITHUB:  gc_set_view_mode(2); break;
    case ID_FILE_OPEN_REPO: {
      char path[512] = {0};
      openfilename_t ofn = {0};
      ofn.lStructSize = sizeof(ofn);
      ofn.lpstrFile   = path;
      ofn.nMaxFile    = sizeof(path);
      ofn.Flags       = OFN_PICKFOLDER;
      if (get_folder_name(&ofn))
        gc_open_repo(path);
      break;
    }
    case ID_FILE_CLONE:
      if (gc->main_win)
        gc_show_clone_dialog(gc->main_win);
      break;
    case ID_FILE_REPOSITORIES:
      gc_show_repositories_dialog(gc->main_win);
      break;
    case ID_FILE_NEW_REPO:
      gc_show_create_repo_dialog(gc->main_win);
      break;
    case ID_FILE_QUIT:
      ui_request_quit();
      break;

    case ID_REPO_REFRESH:
      gc_refresh_all();
      break;
    case ID_REPO_SEARCH:
      if (gc->main_win)
        gc_show_search_dialog(gc->main_win);
      break;
    case ID_REPO_IDENTITY:
      gc_show_identity_dialog(gc->main_win);
      break;
    case ID_REPO_TERMINAL:
      if (gc->repo) {
        char cmd[600];
#ifdef __APPLE__
        snprintf(cmd, sizeof(cmd),
                 "open -a Terminal \"%s\"", git_repo_path(gc->repo));
#else
        snprintf(cmd, sizeof(cmd), "xterm -e 'cd \"%s\" && bash' &",
                 git_repo_path(gc->repo));
#endif
        (void)system(cmd);
      }
      break;

    case ID_BRANCH_NEW:
      if (gc->main_win)
        gc_show_new_branch_dialog(gc->main_win);
      break;
    case ID_BRANCH_CHECKOUT: {
      gc_show_switch_branch_dialog(gc->main_win);
      break;
    }
    case ID_BRANCH_MERGE: {
      char name[256] = {0};
      if (gc_get_selected_branch(name, sizeof(name), NULL)) {
        if (!gc_merge_branch(name)) {
          char files[64][512];
          int n = gc_get_conflicted_files(files, 64);
          if (n > 0)
            gc_show_conflict_dialog(gc->main_win);
          else
            message_box(gc->main_win, "Merge failed.", "Merge", MB_OK);
        } else {
          gc_refresh_all();
        }
      } else {
        message_box(gc->main_win, "No branch selected.", "Merge", MB_OK);
      }
      break;
    }
    case ID_BRANCH_REBASE: {
      char name[256] = {0};
      if (gc_get_selected_branch(name, sizeof(name), NULL)) {
        if (!gc_rebase_onto(name)) {
          char files[64][512];
          int n = gc_get_conflicted_files(files, 64);
          if (n > 0)
            gc_show_conflict_dialog(gc->main_win);
          else
            message_box(gc->main_win, "Rebase failed.", "Rebase", MB_OK);
        } else {
          gc_refresh_all();
        }
      } else {
        message_box(gc->main_win, "No branch selected.", "Rebase", MB_OK);
      }
      break;
    }
    case ID_BRANCH_DELETE: {
      char name[256] = {0};
      bool is_cur = false;
      if (gc_get_selected_branch(name, sizeof(name), &is_cur)) {
        if (is_cur && !strncmp(name, "remotes/", 8)) {
          message_box(gc->main_win, "Cannot delete remote-tracking branch.", "Delete", MB_OK);
          break;
        }
        if (is_cur) {
          message_box(gc->main_win, "Cannot delete the current branch.", "Delete", MB_OK);
          break;
        }
        bool remote = !strncmp(name, "remotes/", 8);
        if (!gc_delete_branch(name, remote))
          message_box(gc->main_win, "Delete failed.", "Delete", MB_OK);
        else
          gc_refresh_all();
      } else {
        message_box(gc->main_win, "No branch selected.", "Delete", MB_OK);
      }
      break;
    }
    case ID_BRANCH_RENAME: {
      char cur[256] = {0};
      git_current_branch(gc->repo, cur, sizeof(cur));
      if (cur[0] && gc->main_win)
        gc_show_rename_branch_dialog(gc->main_win, cur);
      break;
    }

    case ID_TAG_CREATE:
      if (gc->main_win)
        gc_show_create_tag_dialog(gc->main_win);
      break;
    case ID_TAG_DELETE: {
      if (!gc->repo || !gc->tags_win) break;
      int sel = (int)send_message(gc->tags_win, RVM_GETSELECTION, 0, NULL);
      if (sel < 0) {
        message_box(gc->main_win, "No tag selected.", "Delete Tag", MB_OK);
        break;
      }
      result_node_t *rows = (result_node_t *)send_db_message(
        gc->history_db, dbFetch, MAKEDWORD(ID_DB_TAGS, 0), (void *)(intptr_t)0);
      int row = 0;
      const char *tag_name = NULL;
      for (result_node_t *n = rows; n; n = n->next, row++) {
        if (row == sel) {
          db_tag_t *tag = *(db_tag_t **)n->data;
          tag_name = tag ? tag->name : NULL;
          break;
        }
      }
      if (!tag_name) { free_result_list(rows); break; }
      char msg[320];
      snprintf(msg, sizeof(msg), "Delete tag \"%s\"?", tag_name);
      if (message_box(gc->main_win, msg, "Delete Tag", MB_YESNO) == IDYES) {
        if (!gc_delete_tag(tag_name))
          message_box(gc->main_win, "Failed to delete tag.", "Delete Tag", MB_OK);
        else
          gc_refresh_all();
      }
      free_result_list(rows);
      break;
    }
    case ID_TAG_PUSH:
      gc_push_tags();
      break;

    case ID_COMMIT_COMMIT:
      if (gc->main_win)
        gc_show_commit_dialog(gc->main_win, false);
      break;
    case ID_COMMIT_AMEND:
      if (gc->main_win)
        gc_show_commit_dialog(gc->main_win, true);
      break;
    case ID_COMMIT_UNDO: {
      git_sync_status_t st = {0};
      git_get_sync_status(gc->repo, &st);
      char prompt[256];
      snprintf(prompt, sizeof(prompt),
               "Undo the last commit and keep its changes staged?%s",
               st.ahead == 0 && st.upstream[0] ? "\nThe commit may already be published." : "");
      if (message_box(gc->main_win, prompt, "Undo Commit", MB_YESNO) == IDYES) {
        if (gc_undo_commit()) gc_refresh_all();
        else message_box(gc->main_win, "Undo failed.", "Undo Commit", MB_OK);
      }
      break;
    }
    case ID_COMMIT_STASH:
      gc_stash();
      gc_refresh_all();
      break;
    case ID_COMMIT_STASH_POP:
      gc_stash_pop();
      gc_refresh_all();
      break;
    case ID_COMMIT_DISCARD: {
      if (message_box(gc->main_win,
                       "Discard ALL uncommitted changes?\n"
                       "This cannot be undone.",
                       "Discard Changes", MB_YESNO) == IDYES) {
        gc_discard_all();
        gc_refresh_all();
      }
      break;
    }

    case ID_FILES_STAGE:
    case ID_FILES_UNSTAGE:
    case ID_FILES_DISCARD: {
      db_file_t *file = gc->files_win ? (db_file_t *)(intptr_t)send_message(
        gc->files_win, tvGetSelectedRecord, 0, NULL) : NULL;
      if (!file) { message_box(gc->main_win, "No file selected.", "File", MB_OK); break; }
      if (id == ID_FILES_DISCARD) {
        char prompt[640]; snprintf(prompt, sizeof(prompt),
          "Permanently discard all uncommitted changes to \"%s\"?\nThis cannot be undone.", file->path);
        if (message_box(gc->main_win, prompt, "Discard File", MB_YESNO) != IDYES) break;
      }
      bool ok = id == ID_FILES_STAGE   ? gc_stage_file(file->path) :
                id == ID_FILES_UNSTAGE ? gc_unstage_file(file->path) :
                                         gc_discard_file(file->path);
      if (ok) gc_refresh_all();
      else message_box(gc->main_win, "Operation failed.", "File", MB_OK);
      break;
    }
    case ID_FILES_STAGE_ALL:
    case ID_FILES_UNSTAGE_ALL: {
      bool ok = id == ID_FILES_STAGE_ALL ? gc_stage_all() : gc_unstage_all();
      if (ok) gc_refresh_all();
      else message_box(gc->main_win, "Operation failed.", "Files", MB_OK);
      break;
    }
    case ID_FILES_REVEAL: {
      db_file_t *file = gc->files_win ? (db_file_t *)(intptr_t)send_message(
        gc->files_win, tvGetSelectedRecord, 0, NULL) : NULL;
      if (!file || strchr(gc->repo_path, '"') || strchr(file->path, '"')) break;
      char full[1100], cmd[1300]; snprintf(full, sizeof(full), "%s/%s", gc->repo_path, file->path);
#ifdef __APPLE__
      snprintf(cmd, sizeof(cmd), "open -R \"%s\"", full);
#elif defined(_WIN32)
      snprintf(cmd, sizeof(cmd), "explorer /select,\"%s\"", full);
#else
      char *slash = strrchr(full, '/'); if (slash) *slash = 0;
      snprintf(cmd, sizeof(cmd), "xdg-open \"%s\"", full);
#endif
      (void)system(cmd); break;
    }

    case ID_COMMIT_STASH_DROP: {
      db_stash_t *stash = gc->stash_win ? (db_stash_t *)(intptr_t)send_message(
        gc->stash_win, tvGetSelectedRecord, 0, NULL) : NULL;
      if (!stash) { message_box(gc->main_win, "No stash selected.", "Drop Stash", MB_OK); break; }
      char prompt[160]; snprintf(prompt, sizeof(prompt), "Drop %s?", stash->ref);
      if (message_box(gc->main_win, prompt, "Drop Stash", MB_YESNO) == IDYES) {
        if (gc_stash_drop(stash->ref)) gc_refresh_all();
        else message_box(gc->main_win, "Drop failed.", "Drop Stash", MB_OK);
      }
      break;
    }

    case ID_REMOTE_SYNC:
      if (gc_sync()) gc_refresh_all();
      else message_box(gc->main_win,
        "Sync failed. Check the remote, upstream, and working tree.", "Sync", MB_OK);
      break;
    case ID_REMOTE_FETCH:
      if (gc->main_win)
        gc_show_push_pull_dialog(gc->main_win, GIT_OP_FETCH);
      break;
    case ID_REMOTE_PULL:
      if (gc->main_win)
        gc_show_push_pull_dialog(gc->main_win, GIT_OP_PULL);
      break;
    case ID_REMOTE_PUSH:
      if (gc->main_win)
        gc_show_push_pull_dialog(gc->main_win, GIT_OP_PUSH);
      break;
    case ID_REMOTE_MANAGE:
      if (gc->main_win)
        gc_show_remote_dialog(gc->main_win);
      break;

    case ID_HELP_ABOUT:
      gc_show_about_dialog(gc->main_win);
      break;

    default:
      break;
  }
}
