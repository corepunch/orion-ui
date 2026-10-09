#ifndef __GITCLIENT_H__
#define __GITCLIENT_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <orion/ui.h>
#include "components/diff_view.h"
#include <orion/commctl/columnview.h>
#include <orion/commctl/menubar.h>
#include <orion/commctl/appchrome.h>
#include <orion/user/accel.h>

#include "build/generated/apps/gitclient/gitclient.h"

#ifndef GITCLIENT_DEBUG
#define GITCLIENT_DEBUG 0
#endif

#if GITCLIENT_DEBUG
#define GC_LOG(...) do { fprintf(stderr, "[gitclient] " __VA_ARGS__); fputc('\n', stderr); axLog("[gitclient] " __VA_ARGS__); } while (0)
#else
#define GC_LOG(...) ((void)0)
#endif

static const char *gc_status_label(char c) {
  switch (c) {
    case 'M': return "Modified";
    case 'A': return "Added";
    case 'D': return "Deleted";
    case 'R': return "Renamed";
    case 'C': return "Copied";
    case 'U': return "Conflicted";
    case '?': return "Untracked";
    case 'T': return "Type changed";
    default:  return "";
  }
}

#define SCREEN_W 1024
#define SCREEN_H 768
#define evGitOpDone (evUser + 500)
#define evOpenRepo  (evUser + 501)

typedef struct { char hash[41]; char author[64]; char date[20]; char subject[256]; } git_commit_t;
typedef struct {
  char path[512]; char orig_path[512]; char status; char index_status; char worktree_status;
  bool staged; bool untracked; bool conflicted;
} git_file_status_t;
typedef struct {
  char head[256]; char upstream[256]; char remote[256];
  int ahead, behind; bool detached, initial, gone, dirty;
} git_sync_status_t;
typedef struct {
  char name[256]; char hash[41]; char activity[32]; char kind[16];
  bool is_current; bool is_remote; bool is_default; bool is_remote_only;
} git_branch_t;
typedef struct { char name[256]; char hash[41]; char date[20]; } git_tag_t;
typedef struct { char ref[64]; char message[512]; char branch[256]; } git_stash_t;
typedef struct git_repo_s git_repo_t;

#define GC_MAX_WORKTREES 16
#define GC_MAX_TILES 64
#define GC_WORKSPACE_MAGIC "gitclient-workspace 1"
typedef struct {
  char path[512], branch[96];
  bool linked, bare, prunable, detached;
} git_worktree_t;
typedef struct {
  char path[512], repo[96], dir[96], branch[96], upstream[96], subject[160], when[32];
  int staged, unstaged, untracked, conflicts, ahead, behind, stashes;
  int worktrees, dirty_worktrees, other_attention;
  bool linked, detached, no_upstream, gone, missing, initial, prunable;
} git_summary_t;
static inline bool gc_tile_needs_attention(const git_summary_t *t) {
  return t->missing || t->conflicts || t->staged || t->unstaged || t->untracked || t->ahead || t->behind
      || t->no_upstream || t->gone || t->dirty_worktrees || t->other_attention;
}

#define GC_MAX_BRANCHES 256
#define GC_MAX_COMMITS 1024
#define GC_MAX_FILES 4096
#define GC_MAX_DIFF_SIZE 262144
#define GC_MAX_TAGS 256
#define GC_MAX_RECENT_REPOS 24

#define ID_DB_BRANCHES TABLE_BRANCHES
#define ID_DB_COMMITS TABLE_COMMITS
#define ID_DB_FILES TABLE_FILES
#define ID_DB_DIFF TABLE_DIFF
#define ID_DB_TAGS TABLE_TAGS
#define ID_DB_STASH TABLE_STASH
#define ID_DB_REMOTES TABLE_REMOTES
#define ID_DB_WORKTREES TABLE_WORKTREES
#define ID_DB_BRANCHES_ID 0
#define ID_DB_BRANCHES_NAME 1
#define ID_DB_BRANCHES_HASH 2
#define ID_DB_BRANCHES_IS_CURRENT 3
#define ID_DB_BRANCHES_IS_REMOTE 4
#define ID_DB_BRANCHES_ACTIVITY 5
#define ID_DB_BRANCHES_KIND 6
#define ID_DB_COMMITS_ID 0
#define ID_DB_COMMITS_BRANCH_ID 1
#define ID_DB_COMMITS_HASH 2
#define ID_DB_COMMITS_AUTHOR 3
#define ID_DB_COMMITS_DATE 4
#define ID_DB_COMMITS_SUBJECT 5
#define ID_DB_FILES_ID 0
#define ID_DB_FILES_COMMIT_ID 1
#define ID_DB_FILES_PATH 2
#define ID_DB_FILES_STATUS 3
#define ID_DB_FILES_STAGED 4
#define ID_DB_DIFF_ID 0
#define ID_DB_DIFF_CONTENT 1
#define ID_DB_TAGS_ID 0
#define ID_DB_TAGS_NAME 1
#define ID_DB_TAGS_HASH 2
#define ID_DB_TAGS_DATE 3
#define ID_DB_STASH_ID 0
#define ID_DB_STASH_REF 1
#define ID_DB_STASH_MESSAGE 2
#define ID_DB_STASH_BRANCH 3
#define ID_DB_REMOTES_ID 0
#define ID_DB_REMOTES_NAME 1
#define ID_DB_REMOTES_URL 2
#define ID_DB_WORKTREES_ID 0
#define ID_DB_WORKTREES_NAME 1
#define ID_DB_WORKTREES_PATH 2
#define ID_DB_WORKTREES_BRANCH 3
#define ID_DB_WORKTREES_STATUS 4
#define ID_DB_WORKTREES_IS_CURRENT 5
#define ID_DB_WORKTREES_IS_LINKED 6
#define ID_DB_WORKTREES_IS_DETACHED 7
#define ID_DB_WORKTREES_IS_PRUNABLE 8

enum {
  GC_COL_BRANCH_ID, GC_COL_BRANCH_NAME, GC_COL_BRANCH_HASH,
  GC_COL_BRANCH_IS_CURRENT, GC_COL_BRANCH_IS_REMOTE,
  GC_COL_COMMIT_ID, GC_COL_COMMIT_BRANCH_ID, GC_COL_COMMIT_HASH,
  GC_COL_COMMIT_AUTHOR, GC_COL_COMMIT_DATE, GC_COL_COMMIT_SUBJECT,
  GC_COL_FILE_ID, GC_COL_FILE_COMMIT_ID, GC_COL_FILE_PATH,
  GC_COL_FILE_STATUS, GC_COL_FILE_STAGED,
  GC_COL_DIFF_ID, GC_COL_DIFF_CONTENT,
  GC_COL_TAG_ID, GC_COL_TAG_NAME, GC_COL_TAG_HASH, GC_COL_TAG_DATE,
  GC_COL_STASH_ID, GC_COL_STASH_REF, GC_COL_STASH_MESSAGE, GC_COL_STASH_BRANCH,
  GC_COL_REMOTE_ID, GC_COL_REMOTE_NAME, GC_COL_REMOTE_URL,
  GC_COL_ISSUE_ID, GC_COL_ISSUE_NUMBER, GC_COL_ISSUE_TITLE,
  GC_COL_ISSUE_STATE, GC_COL_ISSUE_AUTHOR, GC_COL_ISSUE_CREATED_AT,
  GC_COL_PULL_ID, GC_COL_PULL_NUMBER, GC_COL_PULL_TITLE,
  GC_COL_PULL_STATE, GC_COL_PULL_AUTHOR, GC_COL_PULL_BASE,
  GC_COL_BRANCH_ACTIVITY, GC_COL_BRANCH_KIND,
  GC_COL_WORKTREE_ID, GC_COL_WORKTREE_NAME, GC_COL_WORKTREE_PATH, GC_COL_WORKTREE_BRANCH,
  GC_COL_WORKTREE_STATUS, GC_COL_WORKTREE_IS_CURRENT, GC_COL_WORKTREE_IS_LINKED,
  GC_COL_WORKTREE_IS_DETACHED, GC_COL_WORKTREE_IS_PRUNABLE,
};

#define gc_main_window_form gitclient_main_window_form
#define gc_overview_page_form gitclient_overview_page_form
#define gc_changes_page_form gitclient_changes_page_form
#define gc_history_page_form gitclient_history_page_form
#define gc_github_page_form gitclient_github_page_form
#define gc_commit_dialog_form gitclient_commit_dialog_form
#define gc_new_branch_dialog_form gitclient_new_branch_dialog_form
#define gc_clone_dialog_form gitclient_clone_dialog_form
#define gc_remote_dialog_form gitclient_remote_dialog_form
#define gc_conflict_dialog_form gitclient_conflict_dialog_form
#define gc_search_dialog_form gitclient_search_dialog_form
#define gc_create_tag_dialog_form gitclient_create_tag_dialog_form
#define gc_push_pull_dialog_form gitclient_push_pull_dialog_form
#define gc_branch_rename_dialog_form gitclient_branch_rename_dialog_form
#define gc_database_schema gitclient_database_schema
#define gc_database_api gitclient_database_api

enum { GC_TAB_OVERVIEW, GC_TAB_CHANGES, GC_TAB_HISTORY, GC_TAB_GITHUB };
typedef enum { GIT_OP_FETCH, GIT_OP_PULL, GIT_OP_PUSH, GIT_OP_CLONE, GIT_OP_GENERIC } git_op_t;
typedef struct { git_op_t op; bool success; char output[4096]; } git_async_result_t;

typedef struct {
  git_repo_t *repo;
  char repo_path[512];
  database_t *changes_db;
  database_t *history_db;
  database_t *github_db;
  int selected_commit;
  int selected_file;
  bool unified_diff;
  char clone_path[512];
  char recent_repos[GC_MAX_RECENT_REPOS][512];
  int recent_repo_count;
  window_t *main_win;
  window_t *menubar_win;
  window_t *chrome_win;
  window_t *toolbar_win;
  window_t *activity_win;
  window_t *overview_page_win;
  window_t *summary_win;
  window_t *filter_btn;
  window_t *board_win;
  bool attention_only;
  int visible_tiles[GC_MAX_TILES];
  window_t *changes_page_win;
  window_t *history_page_win;
  window_t *github_page_win;
  window_t *files_win;
  window_t *diff_win;
  window_t *changes_files_win;
  window_t *changes_diff_win;
  window_t *branches_win;
  window_t *tags_win;
  window_t *stash_win;
  window_t *log_win;
  window_t *history_files_win;
  window_t *history_diff_win;
  window_t *github_issues_win;
  window_t *github_pulls_win;
  accel_table_t *accel;
  hinstance_t hinstance;
  bool history_mode;
  int tab;
  int focus_tab;
  git_summary_t tiles[GC_MAX_TILES];
  int tile_count;
  bool fetching_all;
  bool ephemeral;
  char workspace[GC_MAX_RECENT_REPOS][512];
  int workspace_count;
  char workspace_file[512];
  bool workspace_dirty;
  window_t *worktrees_win;
  int worktree_count;
  bool worktree_syncing;
  bool diff_cache_valid;
  int last_diff_commit;
  int last_diff_file;
  char last_diff_path[512];
  bool last_diff_staged;
  bool last_diff_untracked;
  bool last_diff_unified;
} gc_state_t;

extern gc_state_t *g_gc;

lresult_t gitclient_db(database_t *db, uint32_t msg, uint32_t wparam, void *lparam);
lresult_t changes_database_proc(database_t *db, uint32_t msg, uint32_t wparam, void *lparam);
lresult_t github_database_proc(database_t *db, uint32_t msg, uint32_t wparam, void *lparam);

bool gc_stage_file(const char *path);
bool gc_unstage_file(const char *path);
bool gc_stage_all(void);
bool gc_unstage_all(void);
bool gc_sync(void);
bool gc_undo_commit(void);
bool gc_commit(const char *message, bool amend);
bool gc_create_branch(const char *name, const char *from, bool checkout);
bool gc_checkout_branch(const char *name);
bool gc_delete_branch(const char *name, bool remote);
int  gc_delete_merged_branches(void);
bool gc_prune_remote(const char *remote);
bool gc_reload_history_log(const char *ref);
bool gc_merge_branch(const char *name);
bool gc_rebase_onto(const char *branch);
bool gc_rename_branch(const char *old_name, const char *new_name);
bool gc_discard_file(const char *path);
bool gc_discard_all(void);
bool gc_stage_hunk(const char *path, int hunk_idx);
int  gc_get_conflicted_files(char (*out)[512], int max);
bool gc_conflict_resolve(const char *path, const char *strategy);
void gc_abort_merge(void);
bool gc_create_tag(const char *name, const char *ref);
bool gc_delete_tag(const char *name);
void gc_push_tags(void);
bool gc_stash_drop(const char *ref);
bool gc_stash_branch(const char *name, const char *ref);
bool gc_clone_repo(const char *url, const char *path);
bool gc_init_repo(const char *path);
bool gc_add_remote(const char *name, const char *url);
bool gc_remove_remote(const char *name);
bool gc_set_remote_url(const char *name, const char *url);
void gc_stash(void);
void gc_stash_pop(void);

git_repo_t *git_repo_open(const char *path);
void git_repo_close(git_repo_t *repo);
bool git_repo_valid(git_repo_t *repo);
const char *git_repo_path(git_repo_t *repo);
bool git_repo_init_path(const char *path);
int  git_get_log(git_repo_t *repo, git_commit_t *out, int max);
int  git_get_log_ref(git_repo_t *repo, const char *ref, git_commit_t *out, int max);
int  git_get_status(git_repo_t *repo, git_file_status_t *out, int max);
bool git_get_sync_status(git_repo_t *repo, git_sync_status_t *out);
bool git_get_identity(git_repo_t *repo, char *name, int name_sz, char *email, int email_sz);
bool git_set_identity(git_repo_t *repo, const char *name, const char *email, bool global);
bool git_get_diff(git_repo_t *repo, const char *path, bool staged, char *buf, int buf_sz);
int  git_get_branches(git_repo_t *repo, git_branch_t *out, int max);
bool git_current_branch(git_repo_t *repo, char *buf, int buf_sz);
bool git_default_branch(git_repo_t *repo, char *buf, int buf_sz);
bool git_prune_remote(git_repo_t *repo, const char *remote);
int  git_delete_merged_branches(git_repo_t *repo);
int  git_get_remotes(git_repo_t *repo, char (*out)[256], int max);
bool git_get_remote_url(git_repo_t *repo, const char *name, char *buf, int buf_sz);
int  git_get_tags(git_repo_t *repo, git_tag_t *out, int max);
int  git_get_stash(git_repo_t *repo, git_stash_t *out, int max);
int  git_worktree_list(const char *path, git_worktree_t *out, int max);
void git_worktree_label(const git_worktree_t *wt, char *out, size_t n);
bool git_path_absolute(const char *in, char *out, size_t n);
bool git_locate(const char *path, char *checkout, size_t checkout_n, char *main_root, size_t main_n);
bool git_get_summary(const char *path, const char *repo_name, bool linked, git_summary_t *out);
int  git_workspace_scan(char (*roots)[512], int root_count, git_summary_t *out, int max);
bool git_fetch_all_async(char (*roots)[512], int count, window_t *notify_win);
bool git_run_async(git_repo_t *repo, git_op_t op, const char *args[], window_t *notify_win);
bool git_run_sync(git_repo_t *repo, const char *args[], char *buf, int buf_sz);
void git_async_result_free(git_async_result_t *r);

void gc_create_menubar(void);
void gc_handle_command(uint16_t id);
void gc_handle_command_impl(uint16_t id);
result_t gc_menubar_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t gc_toolbar_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

result_t gc_main_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
void gc_open_repo(const char *path);
void gc_add_repo(const char *path);
void gc_update_title(void);
bool gc_handle_open_file(const char *path);
int  gc_fill_worktree_combo(window_t *combo, const char *select_path, char (*paths)[512], int max);
void gc_refresh_all(void);
void gc_show_search_dialog(window_t *parent);
void gc_show_worktrees_dialog(window_t *parent);
void gc_show_reflog_dialog(window_t *parent);
void gc_update_status(void);
void gc_set_view_mode(int tab);
void gc_overview_refresh(void);
void gc_overview_open(int index);
void gc_overview_fetch_all(void);
void gc_overview_status(char *out, size_t n);
bool gc_overview_handle_command(uint32_t wparam, void *lparam);
result_t gc_page_overview_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
void gc_recent_load(void);
void gc_recent_save(void);
void gc_recent_add(const char *path);
bool gc_workspace_file_is(const char *path);
int  gc_workspace_read(const char *file, char (*out)[512], int max);
bool gc_workspace_write(const char *file, char (*paths)[512], int count);
bool gc_workspace_add(const char *path);
bool gc_workspace_remove(const char *path);
bool gc_workspace_open(const char *file, bool confirm);
bool gc_workspace_save(const char *file);
bool gc_workspace_unsaved(void);
void gc_show_repositories_dialog(window_t *parent);
void gc_show_create_repo_dialog(window_t *parent);
void gc_show_identity_dialog(window_t *parent);
void gc_diff_refresh(void);
void gc_diff_invalidate(void);
result_t page_changes_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
bool page_changes_handle(window_t *main_win, uint32_t msg, uint32_t wparam, void *lparam);
result_t page_history_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
bool page_history_handle(window_t *main_win, uint32_t msg, uint32_t wparam, void *lparam);
result_t page_github_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
bool page_github_handle(window_t *main_win, uint32_t msg, uint32_t wparam, void *lparam);
void page_github_refresh(void);
bool gc_show_commit_dialog(window_t *parent, bool amend);
bool gc_show_new_branch_dialog(window_t *parent);
bool gc_show_switch_branch_dialog(window_t *parent);
bool gc_show_rename_branch_dialog(window_t *parent, const char *cur_name);
void gc_show_conflict_dialog(window_t *parent);
void gc_show_create_tag_dialog(window_t *parent);
void gc_show_push_pull_dialog(window_t *parent, git_op_t op);
void gc_show_clone_dialog(window_t *parent);
void gc_show_remote_dialog(window_t *parent);
void gc_show_about_dialog(window_t *parent);

#endif
