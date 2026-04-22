// Menu bar definitions and command dispatch for the git client.

#include "gitclient.h"
#include "../../gem_magic.h"

// ============================================================
// Menu definitions
// ============================================================

static const menu_item_t kMenuFile[] = {
  { "Open Repository...", ID_FILE_OPEN_REPO },
  { "Clone...",           ID_FILE_CLONE },
  { NULL, 0 },   // separator
  { "Exit",              ID_FILE_QUIT },
};

static const menu_item_t kMenuRepo[] = {
  { "Refresh",           ID_REPO_REFRESH },
  { "Open Terminal Here",ID_REPO_TERMINAL },
};

static const menu_item_t kMenuBranch[] = {
  { "New Branch...",     ID_BRANCH_NEW },
  { "Checkout...",       ID_BRANCH_CHECKOUT },
  { NULL, 0 },
  { "Merge Into Current",ID_BRANCH_MERGE },
  { "Rebase Current",    ID_BRANCH_REBASE },
  { NULL, 0 },
  { "Delete...",         ID_BRANCH_DELETE },
};

static const menu_item_t kMenuCommit[] = {
  { "Commit...",         ID_COMMIT_COMMIT },
  { "Amend Last Commit", ID_COMMIT_AMEND },
  { NULL, 0 },
  { "Stash Changes",     ID_COMMIT_STASH },
  { "Pop Stash",         ID_COMMIT_STASH_POP },
};

static const menu_item_t kMenuRemote[] = {
  { "Fetch",             ID_REMOTE_FETCH },
  { "Pull...",           ID_REMOTE_PULL },
  { "Push...",           ID_REMOTE_PUSH },
  { NULL, 0 },
  { "Manage Remotes...", ID_REMOTE_MANAGE },
};

static const menu_item_t kMenuHelp[] = {
  { "About Git Client",  ID_HELP_ABOUT },
};

menu_def_t kGCMenus[] = {
  { "File",     kMenuFile,   4 },
  { "Repo",     kMenuRepo,   2 },
  { "Branch",   kMenuBranch, 7 },
  { "Commit",   kMenuCommit, 5 },
  { "Remote",   kMenuRemote, 5 },
  { "Help",     kMenuHelp,   1 },
};
const int kGCMenuCount = 6;

// ============================================================
// Accelerator table
// ============================================================

static const accel_t kAccelEntries[] = {
  { FVIRTKEY | FCONTROL, AX_KEY_K,  ID_COMMIT_COMMIT },
  { FVIRTKEY,            AX_KEY_F5, ID_REPO_REFRESH  },
  { FVIRTKEY | FCONTROL, AX_KEY_F,  ID_REMOTE_FETCH  },
};
static const int kAccelCount =
    (int)(sizeof(kAccelEntries) / sizeof(kAccelEntries[0]));

// ============================================================
// Menubar window procedure (wraps win_menubar)
// ============================================================

result_t gc_menubar_proc(window_t *win, uint32_t msg,
                         uint32_t wparam, void *lparam) {
  if (msg == evCommand &&
      HIWORD(wparam) == kMenuBarNotificationItemClick) {
    gc_handle_command(LOWORD(wparam));
    return true;
  }
  return win_menubar(win, msg, wparam, lparam);
}

// ============================================================
// Create menu bar + accelerators
// ============================================================

void gc_create_menubar(void) {
  gc_state_t *gc = g_gc;
  if (!gc) return;

  gc->menubar_win = set_app_menu(gc_menubar_proc,
                                  kGCMenus, kGCMenuCount,
                                  gc_handle_command,
                                  gc->hinstance);

  // Wire up accelerator hints on the menu bar (standalone only).
  if (gc->menubar_win) {
    gc->accel = load_accelerators(kAccelEntries, kAccelCount);
    if (gc->accel)
      send_message(gc->menubar_win, kMenuBarMessageSetAccelerators,
                   0, gc->accel);
  } else {
    // Running as a .gem — accel table is still needed for the event loop.
    gc->accel = load_accelerators(kAccelEntries, kAccelCount);
  }
}

// ============================================================
// Command handler (all commands from menu, toolbar, accelerators)
// ============================================================

void gc_handle_command(uint16_t id) {
  gc_state_t *gc = g_gc;
  if (!gc) return;

  GC_LOG("gc_handle_command: id=%d", (int)id);

  switch (id) {
    // ── File ──────────────────────────────────────────────
    case ID_FILE_OPEN_REPO: {
      char path[512] = {0};
      openfilename_t ofn = {0};
      ofn.lStructSize = sizeof(ofn);
      ofn.lpstrFile   = path;
      ofn.nMaxFile    = sizeof(path);
      ofn.Flags       = OFN_PATHMUSTEXIST;
      if (get_open_filename(&ofn))
        gc_open_repo(path);
      break;
    }
    case ID_FILE_CLONE:
      // TODO: show clone dialog
      break;
    case ID_FILE_QUIT:
      ui_request_quit();
      break;

    // ── Repository ────────────────────────────────────────
    case ID_REPO_REFRESH:
      gc_refresh_all();
      break;
    case ID_REPO_TERMINAL:
      // Open a terminal in the repo directory (shell-execute).
      if (gc->repo) {
        char cmd[600];
        snprintf(cmd, sizeof(cmd), "xterm -e 'cd \"%s\" && bash' &",
                 git_repo_path(gc->repo));
        (void)system(cmd);
      }
      break;

    // ── Branch ────────────────────────────────────────────
    case ID_BRANCH_NEW:
      if (gc->main_win)
        gc_show_new_branch_dialog(gc->main_win);
      break;
    case ID_BRANCH_CHECKOUT:
      // Checkout is initiated from the branch panel double-click.
      break;
    case ID_BRANCH_MERGE:
    case ID_BRANCH_REBASE:
    case ID_BRANCH_DELETE:
      // TODO
      break;

    // ── Commit ────────────────────────────────────────────
    case ID_COMMIT_COMMIT:
      if (gc->main_win)
        gc_show_commit_dialog(gc->main_win, false);
      break;
    case ID_COMMIT_AMEND:
      if (gc->main_win)
        gc_show_commit_dialog(gc->main_win, true);
      break;
    case ID_COMMIT_STASH:
      if (gc->repo) {
        const char *args[] = { "git", "stash", NULL };
        char buf[256] = {0};
        git_run_sync(gc->repo, args, buf, sizeof(buf));
        gc_refresh_all();
      }
      break;
    case ID_COMMIT_STASH_POP:
      if (gc->repo) {
        const char *args[] = { "git", "stash", "pop", NULL };
        char buf[256] = {0};
        git_run_sync(gc->repo, args, buf, sizeof(buf));
        gc_refresh_all();
      }
      break;

    // ── Remote ────────────────────────────────────────────
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
      // TODO: manage remotes dialog
      break;

    // ── Help ──────────────────────────────────────────────
    case ID_HELP_ABOUT:
      gc_show_about_dialog(gc->main_win);
      break;

    default:
      break;
  }
}
