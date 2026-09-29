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

void gc_show_worktrees_dialog(window_t *parent) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->repo) {
    message_box(parent, "Open a repository first.", "Worktrees", MB_OK);
    return;
  }
  git_worktree_t wts[GC_MAX_WORKTREES];
  int n = git_worktree_list(git_repo_path(gc->repo), wts, GC_MAX_WORKTREES);
  if (n <= 0) {
    message_box(parent, "No worktrees found.", "Worktrees", MB_OK);
    return;
  }
  char buf[2048];
  int off = snprintf(buf, sizeof(buf), "%d worktree%s (Overview tiles each one):\n\n",
                     n, n == 1 ? "" : "s");
  for (int i = 0; i < n && off < (int)sizeof(buf) - 80; i++) {
    off += snprintf(buf + off, sizeof(buf) - (size_t)off, "%s%s%s\n",
                    wts[i].path,
                    wts[i].linked ? "  (linked)" : "  (main)",
                    wts[i].prunable ? "  [prunable]" : "");
  }
  message_box(parent, buf, "Worktrees", MB_OK);
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
