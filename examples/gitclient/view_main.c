// Main window — two-panel splitter layout inside a WINDOW_SIDEBAR:
//
//   ┌─────────────║──────────────────────────────┬────────────┐
//   │             ║   Commit Log (win_reportview) │            │
//   │  Branches   ║──────────────────────────────┤  Diff      │
//   │  (sidebar)  ║   Changed Files (reportview)  │  (right)   │
//   └─────────────║──────────────────────────────┴────────────┘
//
// The branches panel is the WINDOW_SIDEBAR child (fixed width, no drag).
// The right (diff) and centre horizontal splitters remain user-draggable.
// Splitter dividers are 4-pixel-wide strips between panels.
//
// The main window carries:
//   • WINDOW_TOOLBAR   → fetch / pull / push / commit toolbar
//   • WINDOW_STATUSBAR → branch name + ahead/behind counts
//   • WINDOW_SIDEBAR   → branches/tags/stashes panel
//   • win_menubar      → created by view_menubar.c

#include "gitclient.h"

// ============================================================
// Toolbar definition
// ============================================================

static const toolbar_item_t kMainToolbar[] = {
  { TOOLBAR_ITEM_BUTTON, ID_REMOTE_FETCH,   sysicon_arrow_down,    0, 0, "Fetch"   },
  { TOOLBAR_ITEM_BUTTON, ID_REMOTE_PULL,    sysicon_arrow_down,    0, 0, "Pull"    },
  { TOOLBAR_ITEM_BUTTON, ID_REMOTE_PUSH,    sysicon_arrow_up,      0, 0, "Push"    },
  { TOOLBAR_ITEM_SPACER, 0, 0, 0, 0, NULL },
  { TOOLBAR_ITEM_BUTTON, ID_COMMIT_COMMIT,  sysicon_add,           0, 0, "Commit"  },
  { TOOLBAR_ITEM_BUTTON, ID_BRANCH_NEW,     sysicon_star,          0, 0, "Branch"  },
  { TOOLBAR_ITEM_SPACER, 0, 0, 0, 0, NULL },
  { TOOLBAR_ITEM_BUTTON, ID_REPO_REFRESH,   sysicon_arrow_refresh, 0, 0, "Refresh" },
  { TOOLBAR_ITEM_BUTTON, ID_FILE_OPEN_REPO, sysicon_folder,        0, 0, "Open"    },
};
static const int kMainToolbarCount =
    (int)(sizeof(kMainToolbar) / sizeof(kMainToolbar[0]));

// ============================================================
// Layout helpers
// ============================================================

// Compute child window frames from current splitter positions.
// cr is the main window's client rect.  The sidebar occupies the first
// PANEL_LEFT_W_DEFAULT pixels and is managed by the framework (WINDOW_SIDEBAR);
// this function only lays out the centre and right content panels.
static void compute_layout(gc_state_t *gc, rect_t *cr,
                            rect_t *r_log,
                            rect_t *r_files,
                            rect_t *r_diff) {
  int lw = PANEL_LEFT_W_DEFAULT + PANEL_SPLITTER;  // fixed sidebar + separator gap
  int rw = gc->right_w;
  int total_w = cr->w;
  int total_h = cr->h;

  // Clamp right splitter.
  rw = CLAMP(rw, 80, total_w - lw - 80);
  gc->right_w = rw;

  int center_x = cr->x + lw;
  int center_w = total_w - lw - rw - PANEL_SPLITTER;
  if (center_w < 20) center_w = 20;

  int vs = CLAMP(gc->vsplit_y, 40, total_h - 40);
  gc->vsplit_y = vs;

  *r_diff  = (rect_t){ cr->x + total_w - rw, cr->y, rw, total_h };
  *r_log   = (rect_t){ center_x, cr->y, center_w, vs };
  *r_files = (rect_t){ center_x, cr->y + vs + PANEL_SPLITTER,
                        center_w, total_h - vs - PANEL_SPLITTER };
}

void gc_layout_panels(window_t *win) {
  gc_state_t *gc = (gc_state_t *)win->userdata;
  if (!gc) return;

  rect_t cr = get_client_rect(win);

  rect_t rl, rf, rd;
  compute_layout(gc, &cr, &rl, &rf, &rd);

  // Resize sidebar child to fill the full client height.
  if (win->sidebar_child) {
    move_window(win->sidebar_child, 0, 0);
    resize_window(win->sidebar_child, win->sidebar_width, cr.h);
  }
  if (gc->log_win) {
    move_window(gc->log_win, rl.x, rl.y);
    resize_window(gc->log_win, rl.w, rl.h);
  }
  if (gc->files_win) {
    move_window(gc->files_win, rf.x, rf.y);
    resize_window(gc->files_win, rf.w, rf.h);
  }
  if (gc->diff_win) {
    move_window(gc->diff_win, rd.x, rd.y);
    resize_window(gc->diff_win, rd.w, rd.h);
  }
}

// ============================================================
// Splitter hit-test — returns 0=none,1=left,2=right,3=centre
// ============================================================

static int splitter_at(gc_state_t *gc, rect_t *cr, int mx, int my) {
  (void)my;
  int lx = cr->x + PANEL_LEFT_W_DEFAULT + PANEL_SPLITTER;  // content starts here
  int rx = cr->x + cr->w - gc->right_w - PANEL_SPLITTER;

  // Right vertical splitter.
  if (mx >= rx && mx < rx + PANEL_SPLITTER) return 2;
  // Centre horizontal splitter (only in the centre column).
  int center_r = rx;
  if (mx >= lx && mx <= center_r) {
    int sy = cr->y + gc->vsplit_y;
    if (my >= sy && my < sy + PANEL_SPLITTER) return 3;
  }
  return 0;
}

// ============================================================
// Open / refresh
// ============================================================

void gc_open_repo(const char *path) {
  gc_state_t *gc = g_gc;
  if (!gc) return;

  git_repo_close(gc->repo);
  gc->repo = git_repo_open(path);
  if (!gc->repo) {
    message_box(gc->main_win, "Not a valid git repository.", "Open Repository",
                MB_OK);
    return;
  }

  strncpy(gc->repo_path, path, sizeof(gc->repo_path) - 1);

  // Update window title.
  char title[600];
  snprintf(title, sizeof(title), "Git Client — %s", path);
  strncpy(gc->main_win->title, title, sizeof(gc->main_win->title) - 1);
  gc->main_win->title[sizeof(gc->main_win->title) - 1] = '\0';
  invalidate_window(gc->main_win);

  gc_refresh_all();
}

void gc_refresh_all(void) {
  gc_branches_refresh();
  gc_log_refresh();
  gc_files_refresh();
  gc_diff_refresh();
  gc_update_status();
}

void gc_update_status(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->main_win) return;

  char status[256] = "No repository";
  if (gc->repo) {
    char branch[128] = "HEAD";
    git_current_branch(gc->repo, branch, sizeof(branch));

    // Ahead / behind relative to upstream (best-effort; may fail).
    char ahead[16] = "?", behind[16] = "?";
    {
      char buf[64] = {0};
      const char *aa[] = {
        "git", "rev-list", "--count", "@{u}..HEAD", NULL
      };
      if (git_run_sync(gc->repo, aa, buf, sizeof(buf))) {
        char *nl = strchr(buf, '\n');
        if (nl) *nl = '\0';
        strncpy(ahead, buf, sizeof(ahead) - 1);
      }
    }
    {
      char buf[64] = {0};
      const char *aa[] = {
        "git", "rev-list", "--count", "HEAD..@{u}", NULL
      };
      if (git_run_sync(gc->repo, aa, buf, sizeof(buf))) {
        char *nl = strchr(buf, '\n');
        if (nl) *nl = '\0';
        strncpy(behind, buf, sizeof(behind) - 1);
      }
    }
    snprintf(status, sizeof(status),
             "Branch: %s  ↑%s  ↓%s", branch, ahead, behind);
  }
  send_message(gc->main_win, evStatusBar, 0, (void *)status);
}

// ============================================================
// Main window procedure
// ============================================================

result_t gc_main_proc(window_t *win, uint32_t msg,
                      uint32_t wparam, void *lparam) {
  gc_state_t *gc = (gc_state_t *)win->userdata;

  switch (msg) {
    // ── Window created ────────────────────────────────────────
    case evCreate: {
      gc = g_gc;
      win->userdata = gc;

      // Toolbar.
      send_message(win, tbSetItems,
                   (uint32_t)kMainToolbarCount,
                   (void *)kMainToolbar);

      // Status bar.
      send_message(win, evStatusBar, 0, "No repository");

      // Sidebar (branches / tags / stashes) — replaces the old manual branches_win.
      send_message(win, sbSetContent,
                   (uint32_t)PANEL_LEFT_W_DEFAULT,
                   gc_branches_proc);
      gc->branches_win = win->sidebar_child;

      rect_t cr = get_client_rect(win);
      int lx = PANEL_LEFT_W_DEFAULT + PANEL_SPLITTER;  // content starts after sidebar

      gc->log_win = create_window("Log",
          WINDOW_NOTITLE | WINDOW_NORESIZE | WINDOW_VSCROLL | WINDOW_NOTRAYBUTTON,
          MAKERECT(lx, cr.y,
                   cr.w - lx - gc->right_w - PANEL_SPLITTER,
                   gc->vsplit_y),
          win, gc_log_proc, gc->hinstance, NULL);

      gc->files_win = create_window("Files",
          WINDOW_NOTITLE | WINDOW_NORESIZE | WINDOW_VSCROLL | WINDOW_NOTRAYBUTTON,
          MAKERECT(lx,
                   cr.y + gc->vsplit_y + PANEL_SPLITTER,
                   cr.w - lx - gc->right_w - PANEL_SPLITTER,
                   cr.h - gc->vsplit_y - PANEL_SPLITTER),
          win, gc_files_proc, gc->hinstance, NULL);

      gc->diff_win = create_window("Diff",
          WINDOW_NOTITLE | WINDOW_NORESIZE | WINDOW_VSCROLL | WINDOW_NOTRAYBUTTON,
          MAKERECT(cr.x + cr.w - gc->right_w, cr.y,
                   gc->right_w, cr.h),
          win, gc_diff_proc, gc->hinstance, NULL);

      show_window(gc->log_win,      true);
      show_window(gc->files_win,    true);
      show_window(gc->diff_win,     true);

      // Load VGA font for the diff viewer.
      char font_path[600];
      snprintf(font_path, sizeof(font_path),
               "%s/../share/orion/vga-rom-font-8x16.png",
               ui_get_exe_dir());
      vga_font_init(font_path);

      return true;
    }

    // ── Window destroyed ──────────────────────────────────────
    case evDestroy:
      vga_font_shutdown();
      git_repo_close(gc->repo);
      gc->repo = NULL;
      return false;

    // ── Resize: re-layout panels ──────────────────────────────
    case evResize:
      if (gc) gc_layout_panels(win);
      return false;

    // ── Draw splitter dividers ────────────────────────────────
    case evPaint: {
      if (!gc) return false;
      rect_t cr = get_client_rect(win);
      uint32_t split_col = get_sys_color(brBorderFocus);

      // Right vertical splitter.
      fill_rect(split_col,
                R(cr.x + cr.w - gc->right_w - PANEL_SPLITTER, cr.y,
                  PANEL_SPLITTER, cr.h));

      // Centre horizontal splitter.
      int cx = cr.x + PANEL_LEFT_W_DEFAULT + PANEL_SPLITTER;
      int cw = cr.w - PANEL_LEFT_W_DEFAULT - gc->right_w - 2 * PANEL_SPLITTER;
      fill_rect(split_col,
                R(cx, cr.y + gc->vsplit_y, cw, PANEL_SPLITTER));
      return false;
    }

    // ── Splitter drag — mouse down ────────────────────────────
    case evLeftButtonDown: {
      if (!gc) return false;
      rect_t cr = get_client_rect(win);
      int mx = (int)LOWORD(wparam);
      int my = (int)HIWORD(wparam);
      int spl = splitter_at(gc, &cr, mx, my);
      if (spl) {
        gc->drag_splitter   = spl;
        gc->drag_start_mouse = (spl == 3) ? my : mx;
        gc->drag_start_val   = (spl == 2) ? gc->right_w
                             :               gc->vsplit_y;
        set_capture(win);
        return true;
      }
      return false;
    }

    // ── Splitter drag — mouse move ────────────────────────────
    case evMouseMove: {
      if (!gc || !gc->drag_splitter) return false;
      rect_t cr = get_client_rect(win);
      int mx = (int)LOWORD(wparam);
      int my = (int)HIWORD(wparam);
      int delta = (gc->drag_splitter == 3)
                  ? my - gc->drag_start_mouse
                  : mx - gc->drag_start_mouse;

      if (gc->drag_splitter == 2) {
        gc->right_w = gc->drag_start_val - delta;
      } else {
        gc->vsplit_y = gc->drag_start_val + delta;
      }
      (void)cr;
      gc_layout_panels(win);
      invalidate_window(win);
      return true;
    }

    // ── Splitter drag — mouse up ──────────────────────────────
    case evLeftButtonUp:
      if (gc && gc->drag_splitter) {
        gc->drag_splitter = 0;
        set_capture(NULL);
        return true;
      }
      return false;

    // ── Menu / toolbar / accelerator commands ─────────────────
    case evCommand: {
      if (HIWORD(wparam) == btnClicked || HIWORD(wparam) == 0) {
        gc_handle_command((uint16_t)LOWORD(wparam));
        return true;
      }
      return false;
    }

    // ── Background git operation completed ────────────────────
    case evGitOpDone: {
      git_async_result_t *res = (git_async_result_t *)lparam;
      if (res) {
        if (!res->success) {
          message_box(win, res->output, "Operation failed", MB_OK);
        } else {
          gc_refresh_all();
        }
        git_async_result_free(res);
      }
      return true;
    }

    default:
      return false;
  }
}
