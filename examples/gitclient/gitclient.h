#ifndef __GITCLIENT_H__
#define __GITCLIENT_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../../ui.h"
#include "../../commctl/columnview.h"
#include "../../commctl/menubar.h"
#include "../../user/accel.h"
#include "../../user/icons.h"

// ============================================================
// Debug logging
// ============================================================

#ifndef GITCLIENT_DEBUG
#define GITCLIENT_DEBUG 1
#endif

#if GITCLIENT_DEBUG
#define GC_LOG(...) do { axLog("[gitclient] " __VA_ARGS__); } while (0)
#else
#define GC_LOG(...) ((void)0)
#endif

// ============================================================
// Layout constants
// ============================================================

#define SCREEN_W         920
#define SCREEN_H         600

#define PANEL_SPLITTER   4      // px width/height of a splitter divider
#define PANEL_LEFT_W_DEFAULT   180
#define PANEL_RIGHT_W_DEFAULT  260
#define PANEL_VSPLIT_FRAC      60  // % of centre height given to log

// ============================================================
// Command IDs (menu / toolbar / accelerator)
// ============================================================

// File
#define ID_FILE_OPEN_REPO   1
#define ID_FILE_CLONE       2
#define ID_FILE_QUIT        3

// Repository
#define ID_REPO_REFRESH     10
#define ID_REPO_TERMINAL    11

// Branch
#define ID_BRANCH_NEW       20
#define ID_BRANCH_CHECKOUT  21
#define ID_BRANCH_MERGE     22
#define ID_BRANCH_REBASE    23
#define ID_BRANCH_DELETE    24

// Commit
#define ID_COMMIT_COMMIT    30
#define ID_COMMIT_AMEND     31
#define ID_COMMIT_STASH     32
#define ID_COMMIT_STASH_POP 33

// Remote
#define ID_REMOTE_FETCH     40
#define ID_REMOTE_PULL      41
#define ID_REMOTE_PUSH      42
#define ID_REMOTE_MANAGE    43

// View
#define ID_VIEW_LOG         50
#define ID_VIEW_FILES       51
#define ID_VIEW_DIFF        52

// Help
#define ID_HELP_ABOUT       100

// ============================================================
// Custom event messages (above evUser range)
// ============================================================

// Posted by the async git worker thread to the main window when a
// background operation completes.
//   wparam = git_op_t  (operation type)
//   lparam = git_async_result_t *  (heap-allocated; handler must free with
//            git_async_result_free())
#define evGitOpDone     (evUser + 500)

// ============================================================
// Git data types
// ============================================================

#define GC_MAX_COMMITS    500
#define GC_MAX_FILES      256
#define GC_MAX_BRANCHES   128
#define GC_MAX_DIFF_LINES 4096
#define GC_MAX_DIFF_SIZE  (256 * 1024)

typedef struct {
  char hash[41];
  char author[64];
  char date[20];      // ISO-8601 short "YYYY-MM-DD HH:MM"
  char subject[256];
} git_commit_t;

typedef struct {
  char path[512];
  char status;        // M, A, D, ?, R, C …
  bool staged;
} git_file_status_t;

typedef struct {
  char name[256];
  bool is_current;
  bool is_remote;
} git_branch_t;

// Opaque repository handle
typedef struct git_repo_s git_repo_t;

// Result returned (via evGitOpDone) from a background git operation.
typedef enum {
  GIT_OP_FETCH,
  GIT_OP_PULL,
  GIT_OP_PUSH,
  GIT_OP_CLONE,
  GIT_OP_GENERIC,
} git_op_t;

typedef struct {
  git_op_t  op;
  bool      success;
  char      output[4096];  // captured stdout/stderr
} git_async_result_t;

// ============================================================
// Application state (main app controller)
// ============================================================

typedef struct {
  // Repository
  git_repo_t  *repo;
  char         repo_path[512];

  // Current selection
  int          selected_commit;   // index in commits[]
  int          selected_file;     // index in files[]

  // Cached data
  git_commit_t  commits[GC_MAX_COMMITS];
  int           commit_count;

  git_file_status_t files[GC_MAX_FILES];
  int               file_count;

  git_branch_t  branches[GC_MAX_BRANCHES];
  int           branch_count;

  char diff_buf[GC_MAX_DIFF_SIZE];

  // UI windows
  window_t      *main_win;
  window_t      *menubar_win;
  window_t      *branches_win;
  window_t      *log_win;
  window_t      *files_win;
  window_t      *diff_win;

  // Splitter positions (in main client-area pixels)
  int           left_w;         // width of left (branches) panel
  int           right_w;        // width of right (diff) panel
  int           vsplit_y;       // y that separates log (top) from files (bottom)

  // Splitter drag state
  int           drag_splitter;  // 0=none, 1=left vert, 2=right vert, 3=centre horiz
  int           drag_start_mouse;
  int           drag_start_val;

  accel_table_t *accel;
  hinstance_t    hinstance;
} gc_state_t;

extern gc_state_t *g_gc;

// ============================================================
// Git backend (git_backend.c)
// ============================================================

git_repo_t *git_repo_open(const char *path);
void        git_repo_close(git_repo_t *repo);
bool        git_repo_valid(git_repo_t *repo);
const char *git_repo_path(git_repo_t *repo);

int  git_get_log(git_repo_t *repo, git_commit_t *out, int max);
int  git_get_status(git_repo_t *repo, git_file_status_t *out, int max);
bool git_get_diff(git_repo_t *repo, const char *path,
                  bool staged, char *buf, int buf_sz);
int  git_get_branches(git_repo_t *repo, git_branch_t *out, int max);
bool git_current_branch(git_repo_t *repo, char *buf, int buf_sz);
int  git_get_remotes(git_repo_t *repo, char (*out)[256], int max);

// Run a git command asynchronously.  args[] must be NULL-terminated and
// include "git" as the first element (e.g. {"git","fetch","origin",NULL}).
// On completion, posts evGitOpDone(wparam=op, lparam=git_async_result_t*)
// to notify_win.  The caller is responsible for freeing the result.
bool git_run_async(git_repo_t *repo, git_op_t op,
                   const char *args[],
                   window_t *notify_win);

// Run a git command synchronously and capture stdout into buf.
// Returns true if the command exited with code 0.
bool git_run_sync(git_repo_t *repo, const char *args[],
                  char *buf, int buf_sz);

void git_async_result_free(git_async_result_t *r);

// ============================================================
// View — menu bar (view_menubar.c)
// ============================================================

extern menu_def_t  kGCMenus[];
extern const int   kGCMenuCount;

void gc_create_menubar(void);
void gc_handle_command(uint16_t id);
result_t gc_menubar_proc(window_t *win, uint32_t msg,
                         uint32_t wparam, void *lparam);

// ============================================================
// View — main window (view_main.c)
// ============================================================

result_t gc_main_proc(window_t *win, uint32_t msg,
                      uint32_t wparam, void *lparam);
void gc_open_repo(const char *path);
void gc_refresh_all(void);
void gc_update_status(void);
void gc_layout_panels(window_t *win);

// ============================================================
// View — branches panel (view_branches.c)
// ============================================================

result_t gc_branches_proc(window_t *win, uint32_t msg,
                           uint32_t wparam, void *lparam);
void gc_branches_refresh(void);

// ============================================================
// View — commit log panel (view_log.c)
// ============================================================

result_t gc_log_proc(window_t *win, uint32_t msg,
                     uint32_t wparam, void *lparam);
void gc_log_refresh(void);

// ============================================================
// View — file status panel (view_filelist.c)
// ============================================================

result_t gc_files_proc(window_t *win, uint32_t msg,
                       uint32_t wparam, void *lparam);
void gc_files_refresh(void);

// ============================================================
// View — diff viewer (view_diff.c)
// ============================================================

result_t gc_diff_proc(window_t *win, uint32_t msg,
                      uint32_t wparam, void *lparam);
void gc_diff_refresh(void);

// ============================================================
// View — commit dialog (view_commit_dlg.c)
// ============================================================

bool gc_show_commit_dialog(window_t *parent, bool amend);

// ============================================================
// View — branch dialog (view_branch_dlg.c)
// ============================================================

bool gc_show_new_branch_dialog(window_t *parent);

// ============================================================
// View — push/pull/fetch dialog (view_push_pull_dlg.c)
// ============================================================

void gc_show_push_pull_dialog(window_t *parent, git_op_t op);

// ============================================================
// View — about dialog (shown inline)
// ============================================================

void gc_show_about_dialog(window_t *parent);

#endif // __GITCLIENT_H__
