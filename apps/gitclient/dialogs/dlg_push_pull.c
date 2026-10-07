// Push/Pull/Fetch dialog — uses generated form from gitclient.orion.

#include "gitclient.h"

typedef struct {
  git_op_t op;
  char     remote[256];
  char     branch[256];
  bool     prune;
  bool     force;
} ppf_state_t;

static const ctrl_binding_t ppf_bindings[] = {
  DDX_TEXT (ID_PUSH_PULL_DIALOG_REMOTE, ppf_state_t, remote),
  DDX_TEXT (ID_PUSH_PULL_DIALOG_BRANCH, ppf_state_t, branch),
  DDX_CHECK(ID_PUSH_PULL_DIALOG_PRUNE,  ppf_state_t, prune),
  DDX_CHECK(ID_PUSH_PULL_DIALOG_FORCE,  ppf_state_t, force),
};

static result_t ppf_dlg_proc(window_t *win, uint32_t msg,
                               uint32_t wparam, void *lparam) {
  ppf_state_t *st = (ppf_state_t *)win->userdata;

  switch (msg) {
    case evCreate:
      win->userdata = lparam; st = (ppf_state_t *)lparam;
      {
        gc_state_t *gc = g_gc;
        if (gc && gc->repo) {
          git_current_branch(gc->repo, st->branch, sizeof(st->branch));
          if (!st->remote[0] && gc->history_db) {
            result_node_t *remotes = (result_node_t *)send_db_message(
              gc->history_db, dbFetch, MAKEDWORD(ID_DB_REMOTES, 0), (void *)0);
            if (remotes) {
              db_remote_t *r = *(db_remote_t **)remotes->data;
              if (r) strncpy(st->remote, r->name, sizeof(st->remote) - 1);
              free_result_list(remotes);
            }
          }
        }
      }
      dialog_push(win, st, ppf_bindings, ARRAY_LEN(ppf_bindings));
      return true;

    case evCommand:
      if (HIWORD(wparam) == btnClicked) {
        window_t *src = (window_t *)lparam;
        if (!src) return false;
        if (src->id == ID_PUSH_PULL_DIALOG_CANCEL) { end_dialog(win, 0); return true; }
        if (src->id == ID_PUSH_PULL_DIALOG_OK) {
          gc_state_t *gc = g_gc;
          if (!gc || !gc->repo) { end_dialog(win, 0); return true; }
          dialog_pull(win, st, ppf_bindings, ARRAY_LEN(ppf_bindings));

          const char *args[8]; int ai = 0;
          args[ai++] = "git";
          switch (st->op) {
            case GIT_OP_FETCH:
              args[ai++] = "fetch";
              if (st->prune) args[ai++] = "--prune";
              if (st->remote[0]) args[ai++] = st->remote;
              break;
            case GIT_OP_PULL:
              args[ai++] = "pull";
              if (st->force) args[ai++] = "--force";
              if (st->remote[0]) args[ai++] = st->remote;
              if (st->branch[0]) args[ai++] = st->branch;
              break;
            case GIT_OP_PUSH:
              args[ai++] = "push";
              if (st->force) args[ai++] = "--force";
              if (st->remote[0]) args[ai++] = st->remote;
              if (st->branch[0]) args[ai++] = st->branch;
              break;
            default: break;
          }
          args[ai] = NULL;

          git_run_async(gc->repo, st->op, args, gc->main_win);
          end_dialog(win, 1);
          return true;
        }
      }
      return false;

    default:
      return false;
  }
}

void gc_show_push_pull_dialog(window_t *parent, git_op_t op) {
  ppf_state_t st = { .op = op };
  static const char *titles[] = {
    [GIT_OP_FETCH]   = "Fetch",
    [GIT_OP_PULL]    = "Pull",
    [GIT_OP_PUSH]    = "Push",
    [GIT_OP_CLONE]   = "Clone",
    [GIT_OP_GENERIC] = "Remote Operation",
  };
  const char *title = (op < 5) ? titles[op] : "Remote Operation";
  show_dialog_from_form(&gc_push_pull_dialog_form, title, parent, ppf_dlg_proc, &st);
}

void gc_show_about_dialog(window_t *parent) {
  message_box(parent,
    "Git Client\n"
    "A SmartGit-style repository viewer\n"
    "built with the Orion UI framework.",
    "About", MB_OK);
}

typedef struct {
  char paths[GC_MAX_WORKTREES][512];
  int  count;
  char chosen[512];
} wt_dlg_state_t;

static result_t wt_dlg_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  wt_dlg_state_t *st = (wt_dlg_state_t *)win->userdata;
  if (msg == evCreate) {
    win->userdata = lparam; st = (wt_dlg_state_t *)lparam;
    window_t *list = get_window_item(win, ID_WORKTREE_DIALOG_LIST);
    const char *cur = (g_gc && g_gc->repo) ? git_repo_path(g_gc->repo) : NULL;
    if (st && list) st->count = gc_fill_worktree_combo(list, cur, st->paths, GC_MAX_WORKTREES);
    return true;
  }
  if (msg != evCommand || HIWORD(wparam) != btnClicked || !st) return false;
  uint16_t id = LOWORD(wparam);
  if (id == ID_WORKTREE_DIALOG_CANCEL) { end_dialog(win, 0); return true; }
  if (id == ID_WORKTREE_DIALOG_OK) {
    window_t *list = get_window_item(win, ID_WORKTREE_DIALOG_LIST);
    int sel = list ? (int)send_message(list, cbGetCurrentSelection, 0, NULL) : -1;
    if (sel >= 0 && sel < st->count && st->paths[sel][0]) {
      strncpy(st->chosen, st->paths[sel], sizeof(st->chosen) - 1);
      st->chosen[sizeof(st->chosen) - 1] = 0;
      end_dialog(win, 1);
    }
    return true;
  }
  return false;
}

void gc_show_worktrees_dialog(window_t *parent) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo) {
    message_box(parent, "Open a repository first.", "Worktrees", MB_OK);
    return;
  }
  wt_dlg_state_t st = {0};
  show_dialog_from_form(&gitclient_worktree_dialog_form, "Worktrees", parent, wt_dlg_proc, &st);
  if (!st.chosen[0]) return;
  char cur[512] = {0};
  if (gc->repo && git_path_absolute(git_repo_path(gc->repo), cur, sizeof(cur)) && !strcmp(cur, st.chosen))
    return;
  gc_open_repo(st.chosen);
}

void gc_show_reflog_dialog(window_t *parent) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo) {
    message_box(parent, "Open a repository first.", "Reflog", MB_OK);
    return;
  }
  gc_set_view_mode(GC_TAB_HISTORY);
  if (!gc_reload_history_log("-g"))
    message_box(parent, "Could not read the reflog.", "Reflog", MB_OK);
}
