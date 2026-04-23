// Main window — two-panel splitter layout inside a WINDOW_SIDEBAR:
//
//   ┌─────────────║──────────────────────────────┬────────────┐
//   │             ║   Commit Log (win_reportview) │            │
//   │  Branches   ║──────────────────────────────┤  Diff      │
//   │  (sidebar)  ║   Changed Files (reportview)  │  (right)   │
//   └─────────────║──────────────────────────────┴────────────┘
//
// The branches panel is the WINDOW_SIDEBAR child (fixed width, no drag).
// The right (diff) and centre horizontal splitters are win_splitter children
// that notify gc_main_proc via spnDragStart when the user clicks them.
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

  // Keep the win_splitter bar windows in sync with the layout.
  // vsplitter_win is the right vertical bar; hsplitter_win is the centre
  // horizontal bar.  Their frame positions are the ground truth used by the
  // drag handler in evMouseMove.
  int lw = PANEL_LEFT_W_DEFAULT + PANEL_SPLITTER;
  int center_w = cr.w - lw - gc->right_w - PANEL_SPLITTER;
  if (center_w < 20) center_w = 20;

  if (gc->vsplitter_win) {
    int spl_x = cr.x + lw + center_w;
    move_window(gc->vsplitter_win, spl_x, cr.y);
    resize_window(gc->vsplitter_win, PANEL_SPLITTER, cr.h);
  }
  if (gc->hsplitter_win) {
    int spl_x = cr.x + lw;
    move_window(gc->hsplitter_win, spl_x, cr.y + gc->vsplit_y);
    resize_window(gc->hsplitter_win, center_w, PANEL_SPLITTER);
  }
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

      // Sidebar (branches / tags / stashes).
      send_message(win, sbSetContent,
                   (uint32_t)PANEL_LEFT_W_DEFAULT,
                   gc_branches_proc);
      gc->branches_win = win->sidebar_child;

      rect_t cr = get_client_rect(win);
      int lx = PANEL_LEFT_W_DEFAULT + PANEL_SPLITTER;

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

      // win_splitter children for the two divider bars.
      // They paint themselves and send spnDragStart to this proc on click.
      int center_w = cr.w - lx - gc->right_w - PANEL_SPLITTER;
      gc->vsplitter_win = create_window("",
          WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOTRAYBUTTON,
          MAKERECT(lx + center_w, cr.y, PANEL_SPLITTER, cr.h),
          win, win_splitter, gc->hinstance, (void *)SPLIT_VERT);

      gc->hsplitter_win = create_window("",
          WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOTRAYBUTTON,
          MAKERECT(lx, cr.y + gc->vsplit_y, center_w, PANEL_SPLITTER),
          win, win_splitter, gc->hinstance, (void *)SPLIT_HORZ);

      show_window(gc->log_win,        true);
      show_window(gc->files_win,      true);
      show_window(gc->diff_win,       true);
      show_window(gc->vsplitter_win,  true);
      show_window(gc->hsplitter_win,  true);

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

    // ── evPaint: nothing extra — splitter bars paint themselves ─
    case evPaint:
      return false;

    // ── Splitter drag — mouse move (after set_capture in spnDragStart) ──
    case evMouseMove: {
      if (!gc || !gc->dragging_splitter) return false;
      int mx = (int)LOWORD(wparam);
      int my = (int)HIWORD(wparam);
      int orient = win_splitter_orientation(gc->dragging_splitter);
      int delta  = (orient == SPLIT_HORZ)
                   ? my - gc->drag_start_mouse
                   : mx - gc->drag_start_mouse;

      if (orient == SPLIT_VERT) {
        gc->right_w = gc->drag_start_val - delta;
      } else {
        gc->vsplit_y = gc->drag_start_val + delta;
      }
      gc_layout_panels(win);
      invalidate_window(win);
      return true;
    }

    // ── Splitter drag — mouse up ──────────────────────────────
    case evLeftButtonUp:
      if (gc && gc->dragging_splitter) {
        gc->dragging_splitter = NULL;
        set_capture(NULL);
        return true;
      }
      return false;

    // ── Commands and control notifications ────────────────────
    case evCommand: {
      uint16_t code = (uint16_t)HIWORD(wparam);

      // ── Toolbar / menu / accelerator ────────────────────────
      if (code == btnClicked || code == 0) {
        gc_handle_command((uint16_t)LOWORD(wparam));
        return true;
      }

      // ── Splitter drag start ──────────────────────────────────
      // win_splitter sends spnDragStart with the hit point packed in lparam
      // (as MAKEDWORD(parent_local_x, parent_local_y)).  We capture the main
      // window so subsequent evMouseMove arrives here with stable parent coords.
      if (code == spnDragStart) {
        if (!gc) return false;
        // win_splitter packed the parent-local hit point into lparam via
        // (void*)(uintptr_t)MAKEDWORD(px,py).  The intermediate uintptr_t cast
        // is required on 64-bit platforms where sizeof(void*)>sizeof(uint32_t).
        uint32_t pos  = (uint32_t)(uintptr_t)lparam;
        int px = (int)(int16_t)LOWORD(pos);
        int py = (int)(int16_t)HIWORD(pos);
        // Find which splitter sent the notification by its id.
        uint16_t spl_id = (uint16_t)LOWORD(wparam);
        window_t *spl = NULL;
        if (gc->vsplitter_win && gc->vsplitter_win->id == spl_id)
          spl = gc->vsplitter_win;
        else if (gc->hsplitter_win && gc->hsplitter_win->id == spl_id)
          spl = gc->hsplitter_win;
        if (!spl) return false;

        gc->dragging_splitter = spl;
        int orient = win_splitter_orientation(spl);
        gc->drag_start_mouse = (orient == SPLIT_HORZ) ? py : px;
        gc->drag_start_val   = (orient == SPLIT_VERT) ? gc->right_w
                                                       : gc->vsplit_y;
        set_capture(win);
        return true;
      }

      // ── ReportView selection change ──────────────────────────
      // rv_notify sends lparam = source window (WinAPI WM_COMMAND convention),
      // so we can identify the control directly without probing all views.
      if (code == RVN_SELCHANGE) {
        if (!gc) return true;
        int sel   = (int)(int16_t)LOWORD(wparam);
        window_t *src = (window_t *)lparam;

        if (src == gc->log_win) {
          if (sel != gc->selected_commit) {
            gc->selected_commit = sel;
            gc->selected_file   = -1;
            gc_files_refresh();
            gc_diff_refresh();
          }
        } else if (src == gc->files_win) {
          if (sel != gc->selected_file) {
            gc->selected_file = sel;
            gc_diff_refresh();
          }
        } else if (src == gc->branches_win) {
          gc_log_refresh();
        }
        return true;
      }

      // ── ReportView double-click ──────────────────────────────
      if (code == RVN_DBLCLK) {
        if (!gc) return false;
        int idx       = (int)(int16_t)LOWORD(wparam);
        window_t *src = (window_t *)lparam;

        // Files double-click → stage / unstage.
        if (src == gc->files_win && gc->selected_commit < 0 &&
            gc->repo && idx >= 0 && idx < gc->file_count) {
          git_file_status_t *f = &gc->files[idx];
          char buf[512] = {0};
          if (f->staged) {
            const char *args[] = { "git", "restore", "--staged", f->path, NULL };
            git_run_sync(gc->repo, args, buf, sizeof(buf));
          } else {
            const char *args[] = { "git", "add", f->path, NULL };
            git_run_sync(gc->repo, args, buf, sizeof(buf));
          }
          gc_files_refresh();
          gc_diff_refresh();
          return true;
        }

        // Branches double-click → checkout.
        if (src == gc->branches_win && gc->repo && idx >= 0) {
          reportview_item_t item = {0};
          send_message(gc->branches_win, RVM_GETITEMDATA, (uint32_t)idx, &item);
          uint32_t ud = item.userdata;
          if (ud < 0xFF00u && (int)ud < gc->branch_count) {
            git_branch_t *b = &gc->branches[(int)ud];
            if (!b->is_remote && !b->is_current) {
              const char *args[] = { "git", "checkout", b->name, NULL };
              char buf[512] = {0};
              if (!git_run_sync(gc->repo, args, buf, sizeof(buf)))
                message_box(win, buf, "Checkout failed", MB_OK);
              else
                gc_refresh_all();
            }
          }
          return true;
        }
        return false;
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
