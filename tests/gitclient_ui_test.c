// tests/gitclient_ui_test.c — integration tests for the git client UI layer.
//
// Creates a real git repository in /tmp and drives the gitclient view layer
// (branches panel, log panel, files panel, diff buffer, evCommand navigation)
// headlessly — no display or OpenGL context required.
//
// Each test creates a minimal window hierarchy through the panel procs
// (gc_branches_proc, gc_log_proc, gc_files_proc, gc_diff_proc), calls the
// refresh functions, and queries the results via RVM_GETITEMCOUNT and the
// gc_state_t data arrays.
//
// Build: see Makefile (GITCLIENT_TEST_BINS target).
// Run:   build/bin/test_gitclient_ui_test

#include "test_framework.h"
#include "test_env.h"
#include "../examples/gitclient/gitclient.h"
#include "../commctl/columnview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// ── Application state ─────────────────────────────────────────────────────────
// Defined here so that all gitclient view_*.c translation units can resolve
// the extern gc_state_t *g_gc declaration from gitclient.h.
static gc_state_t g_test_state;
gc_state_t *g_gc = &g_test_state;

// ── Temporary test repository ─────────────────────────────────────────────────

static char s_repo[256] = {0};

static const char *detect_default_branch(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/.git/refs/heads/main", s_repo);
    struct stat st;
    return (stat(path, &st) == 0) ? "main" : "master";
}

static bool setup_repo(void) {
    strncpy(s_repo, "/tmp/orion_gcui_XXXXXX", sizeof(s_repo) - 1);
    if (!mkdtemp(s_repo)) {
        printf("[setup] mkdtemp failed\n");
        return false;
    }

    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
        "set -e;"
        "cd '%s';"
        "git init -b main 2>/dev/null || (git init && git checkout -b main 2>/dev/null || true);"
        "git config user.email 'ci@test';"
        "git config user.name 'CI';"
        "echo hello > file1.txt;"
        "git add file1.txt;"
        "git commit -m 'Initial commit';"
        "echo world > file2.txt;"
        "git add file2.txt;"
        "git commit -m 'Add file2';"
        "git checkout -b feature;"
        "echo feat > feature.txt;"
        "git add feature.txt;"
        "git commit -m 'Feature commit';"
        "git checkout main 2>/dev/null || git checkout master",
        s_repo);

    if (system(cmd) != 0) {
        printf("[setup] git repo creation failed — is git in PATH?\n");
        return false;
    }
    return true;
}

static void teardown_repo(void) {
    if (s_repo[0]) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf '%s'", s_repo);
        system(cmd);
        s_repo[0] = '\0';
    }
}

// ── Per-test setup / teardown ─────────────────────────────────────────────────
//
// Creates the four panel windows with their real window procs and opens the
// test repository.  test_env_shutdown() destroys all windows on teardown.

static void gc_test_setup(void) {
    test_env_init();
    memset(&g_test_state, 0, sizeof(gc_state_t));
    g_gc->selected_commit = -1;
    g_gc->selected_file   = -1;
    g_gc->right_w         = PANEL_RIGHT_W_DEFAULT;

    // Branches panel — single full-width column.
    rect_t fr = {0, 0, 200, 400};
    g_gc->branches_win = create_window("Branches",
        WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOTRAYBUTTON,
        &fr, NULL, gc_branches_proc, 0, NULL);

    // Log panel — Subject / Author / Date / Hash columns.
    fr = (rect_t){0, 0, 600, 200};
    g_gc->log_win = create_window("Log",
        WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOTRAYBUTTON,
        &fr, NULL, gc_log_proc, 0, NULL);

    // Files panel — Status / Path columns.
    fr = (rect_t){0, 0, 400, 200};
    g_gc->files_win = create_window("Files",
        WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOTRAYBUTTON,
        &fr, NULL, gc_files_proc, 0, NULL);

    // Diff panel — gc_diff_proc allocates diff_state_t in evCreate.
    fr = (rect_t){0, 0, 400, 400};
    g_gc->diff_win = create_window("Diff",
        WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOTRAYBUTTON,
        &fr, NULL, gc_diff_proc, 0, NULL);

    g_gc->repo = git_repo_open(s_repo);
}

static void gc_test_teardown(void) {
    if (g_gc->repo) {
        git_repo_close(g_gc->repo);
        g_gc->repo = NULL;
    }
    test_env_shutdown();
}

// ── Thin wrapper proc for evOpenRepo / evCommand routing tests ────────────────
//
// Skips gc_main_proc's evCreate (which creates the full visual hierarchy and
// loads the VGA font) but wires win->userdata = g_gc so that gc_main_proc can
// find gc->log_win / gc->files_win etc. when handling evCommand or evOpenRepo.
static result_t test_main_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam) {
    if (msg == evCreate) {
        win->userdata = g_gc;
        return true;
    }
    if (msg == evDestroy)
        return false;
    return gc_main_proc(win, msg, wparam, lparam);
}

// ── Tests ─────────────────────────────────────────────────────────────────────

// gc_open_repo() is the primary API to load a repository.  It opens the git
// handle, populates gc->branch_count and gc->commit_count, and refreshes all
// panel windows.
void test_gc_open_repo_populates_state(void) {
    TEST("gc_open_repo: sets repo handle and populates branch + commit counts");

    gc_test_setup();

    // Close whatever gc_test_setup opened and re-exercise the full path.
    if (g_gc->repo) { git_repo_close(g_gc->repo); g_gc->repo = NULL; }
    gc_open_repo(s_repo);

    ASSERT_NOT_NULL(g_gc->repo);
    ASSERT_TRUE(git_repo_valid(g_gc->repo));
    ASSERT_TRUE(g_gc->branch_count >= 2);
    ASSERT_TRUE(g_gc->commit_count >= 2);

    gc_test_teardown();
    PASS();
}

// ── Branches panel ────────────────────────────────────────────────────────────

void test_gc_branches_panel_has_items(void) {
    TEST("gc_branches_refresh: branches panel receives items");

    gc_test_setup();
    gc_branches_refresh();

    int count = (int)send_message(g_gc->branches_win, RVM_GETITEMCOUNT, 0, NULL);
    // At minimum: LOCAL BRANCHES header + main + REMOTE BRANCHES header +
    //             TAGS header + STASHES header = 5, plus the feature branch.
    ASSERT_TRUE(count >= 4);

    gc_test_teardown();
    PASS();
}

void test_gc_branches_panel_includes_all_branches(void) {
    TEST("gc_branches_refresh: panel item count exceeds local branch count (includes section headers)");

    gc_test_setup();
    gc_branches_refresh();

    int panel_count = (int)send_message(g_gc->branches_win, RVM_GETITEMCOUNT, 0, NULL);
    // Panel has section headers in addition to branches so panel_count > branch_count.
    ASSERT_TRUE(panel_count > g_gc->branch_count);
    ASSERT_TRUE(g_gc->branch_count >= 2);

    gc_test_teardown();
    PASS();
}

void test_gc_branches_refresh_twice_idempotent(void) {
    TEST("gc_branches_refresh: calling twice gives the same count");

    gc_test_setup();
    gc_branches_refresh();
    int n1 = (int)send_message(g_gc->branches_win, RVM_GETITEMCOUNT, 0, NULL);
    gc_branches_refresh();
    int n2 = (int)send_message(g_gc->branches_win, RVM_GETITEMCOUNT, 0, NULL);

    ASSERT_EQUAL(n1, n2);
    ASSERT_TRUE(n1 >= 4);

    gc_test_teardown();
    PASS();
}

// ── Log panel ─────────────────────────────────────────────────────────────────

void test_gc_log_panel_has_commits(void) {
    TEST("gc_log_refresh: log panel receives commit rows (main has 2 commits)");

    gc_test_setup();
    gc_log_refresh();

    int count = (int)send_message(g_gc->log_win, RVM_GETITEMCOUNT, 0, NULL);
    ASSERT_EQUAL(count, 2);

    gc_test_teardown();
    PASS();
}

void test_gc_log_panel_count_matches_commit_count(void) {
    TEST("gc_log_refresh: panel row count equals gc->commit_count");

    gc_test_setup();
    gc_log_refresh();

    int panel = (int)send_message(g_gc->log_win, RVM_GETITEMCOUNT, 0, NULL);
    ASSERT_EQUAL(panel, g_gc->commit_count);
    ASSERT_TRUE(g_gc->commit_count >= 2);

    gc_test_teardown();
    PASS();
}

void test_gc_log_commit_data_populated(void) {
    TEST("gc_log_refresh: gc->commits[] has correct subject, author, hash");

    gc_test_setup();
    gc_log_refresh();

    ASSERT_TRUE(g_gc->commit_count >= 2);
    // Commits are newest-first.
    ASSERT_STR_EQUAL(g_gc->commits[0].subject, "Add file2");
    ASSERT_STR_EQUAL(g_gc->commits[0].author,  "CI");
    ASSERT_TRUE(g_gc->commits[0].hash[0] != '\0');
    ASSERT_STR_EQUAL(g_gc->commits[1].subject, "Initial commit");

    gc_test_teardown();
    PASS();
}

void test_gc_log_refresh_twice_idempotent(void) {
    TEST("gc_log_refresh: calling twice gives the same commit count");

    gc_test_setup();
    gc_log_refresh();
    int n1 = g_gc->commit_count;
    gc_log_refresh();
    int n2 = g_gc->commit_count;

    ASSERT_EQUAL(n1, n2);

    gc_test_teardown();
    PASS();
}

// ── Files panel ───────────────────────────────────────────────────────────────

void test_gc_files_panel_clean_working_tree(void) {
    TEST("gc_files_refresh: clean repo with no commit selected shows 0 files");

    gc_test_setup();
    g_gc->selected_commit = -1;  // working-tree status mode
    gc_files_refresh();

    int count = (int)send_message(g_gc->files_win, RVM_GETITEMCOUNT, 0, NULL);
    ASSERT_EQUAL(count, 0);

    gc_test_teardown();
    PASS();
}

void test_gc_files_panel_shows_modified_file(void) {
    TEST("gc_files_refresh: unstaged modified file appears in the panel");

    gc_test_setup();

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "echo extra >> '%s/file1.txt'", s_repo);
    system(cmd);

    g_gc->selected_commit = -1;
    gc_files_refresh();

    int count = (int)send_message(g_gc->files_win, RVM_GETITEMCOUNT, 0, NULL);
    ASSERT_TRUE(count >= 1);
    ASSERT_TRUE(g_gc->file_count >= 1);

    bool found = false;
    for (int i = 0; i < g_gc->file_count; i++) {
        if (strcmp(g_gc->files[i].path, "file1.txt") == 0) {
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);

    // Restore.
    snprintf(cmd, sizeof(cmd), "cd '%s' && git checkout -- file1.txt", s_repo);
    system(cmd);

    gc_test_teardown();
    PASS();
}

void test_gc_files_panel_commit_mode_shows_changed_files(void) {
    TEST("gc_files_refresh: selecting commit index 0 shows files changed in that commit");

    gc_test_setup();
    gc_log_refresh();
    ASSERT_TRUE(g_gc->commit_count >= 2);

    // Select the most recent commit ("Add file2" touches file2.txt).
    g_gc->selected_commit = 0;
    g_gc->selected_file   = -1;
    gc_files_refresh();

    int count = (int)send_message(g_gc->files_win, RVM_GETITEMCOUNT, 0, NULL);
    ASSERT_TRUE(count >= 1);

    bool found_file2 = false;
    for (int i = 0; i < g_gc->file_count; i++) {
        if (strcmp(g_gc->files[i].path, "file2.txt") == 0) {
            found_file2 = true;
            break;
        }
    }
    ASSERT_TRUE(found_file2);

    gc_test_teardown();
    PASS();
}

void test_gc_files_panel_initial_commit_shows_file1(void) {
    TEST("gc_files_refresh: 'Initial commit' shows file1.txt");

    gc_test_setup();
    gc_log_refresh();
    ASSERT_TRUE(g_gc->commit_count >= 2);

    // The oldest commit (index n-1) is "Initial commit" and adds file1.txt.
    g_gc->selected_commit = g_gc->commit_count - 1;
    g_gc->selected_file   = -1;
    gc_files_refresh();

    bool found_file1 = false;
    for (int i = 0; i < g_gc->file_count; i++) {
        if (strcmp(g_gc->files[i].path, "file1.txt") == 0) {
            found_file1 = true;
            break;
        }
    }
    ASSERT_TRUE(found_file1);

    gc_test_teardown();
    PASS();
}

// ── Diff buffer ───────────────────────────────────────────────────────────────

void test_gc_diff_buf_populated_for_commit(void) {
    TEST("gc_diff_refresh: diff_buf is non-empty when a commit is selected");

    gc_test_setup();
    gc_log_refresh();
    ASSERT_TRUE(g_gc->commit_count >= 1);

    g_gc->selected_commit = 0;  // "Add file2"
    g_gc->selected_file   = -1;
    gc_files_refresh();
    gc_diff_refresh();

    ASSERT_TRUE(g_gc->diff_buf[0] != '\0');
    // The diff for "Add file2" must mention file2.
    ASSERT_TRUE(strstr(g_gc->diff_buf, "file2") != NULL);

    gc_test_teardown();
    PASS();
}

void test_gc_diff_buf_populated_for_file_in_commit(void) {
    TEST("gc_diff_refresh: diff_buf is non-empty when a commit + file are selected");

    gc_test_setup();
    gc_log_refresh();
    ASSERT_TRUE(g_gc->commit_count >= 1);

    g_gc->selected_commit = 0;
    g_gc->selected_file   = -1;
    gc_files_refresh();
    ASSERT_TRUE(g_gc->file_count >= 1);

    g_gc->selected_file = 0;
    gc_diff_refresh();

    ASSERT_TRUE(g_gc->diff_buf[0] != '\0');

    gc_test_teardown();
    PASS();
}

void test_gc_diff_buf_empty_for_clean_working_tree(void) {
    TEST("gc_diff_refresh: diff_buf is empty for clean working tree (no commit selected)");

    gc_test_setup();
    g_gc->selected_commit = -1;
    g_gc->selected_file   = -1;
    gc_diff_refresh();

    // git diff HEAD on a clean repo returns nothing.
    ASSERT_TRUE(g_gc->diff_buf[0] == '\0');

    gc_test_teardown();
    PASS();
}

// ── Navigation flow ───────────────────────────────────────────────────────────

// Simulates the flow that gc_main_proc produces when a log row is selected:
//   1. selected_commit changes
//   2. gc_files_refresh() is called → files panel updates
//   3. gc_diff_refresh() is called → diff_buf updates
void test_gc_log_selection_triggers_file_update(void) {
    TEST("Navigation: selecting log row 0 shows files of that commit");

    gc_test_setup();
    gc_log_refresh();
    ASSERT_TRUE(g_gc->commit_count >= 2);

    // Start with no selection.
    ASSERT_EQUAL(g_gc->selected_commit, -1);

    // Simulate RVN_SELCHANGE on log_win for row 0.
    int sel = 0;
    g_gc->selected_commit = sel;
    g_gc->selected_file   = -1;
    gc_files_refresh();
    gc_diff_refresh();

    // Files panel should now show the files for commit 0.
    int file_count = (int)send_message(g_gc->files_win, RVM_GETITEMCOUNT, 0, NULL);
    ASSERT_TRUE(file_count >= 1);
    ASSERT_TRUE(g_gc->diff_buf[0] != '\0');

    gc_test_teardown();
    PASS();
}

void test_gc_file_selection_triggers_diff_update(void) {
    TEST("Navigation: selecting a file within a commit updates diff_buf");

    gc_test_setup();
    gc_log_refresh();

    g_gc->selected_commit = 0;
    g_gc->selected_file   = -1;
    gc_files_refresh();
    ASSERT_TRUE(g_gc->file_count >= 1);

    // Select first file.
    int file_sel = 0;
    if (file_sel != g_gc->selected_file) {
        g_gc->selected_file = file_sel;
        gc_diff_refresh();
    }

    ASSERT_TRUE(g_gc->diff_buf[0] != '\0');

    gc_test_teardown();
    PASS();
}

void test_gc_branch_selection_refreshes_log(void) {
    TEST("Navigation: 'branch selected' refresh re-reads the current log");

    gc_test_setup();
    gc_log_refresh();
    int initial_count = g_gc->commit_count;
    ASSERT_EQUAL(initial_count, 2);

    // Simulate what gc_main_proc does for RVN_SELCHANGE from branches_win:
    // it just calls gc_log_refresh().
    gc_log_refresh();

    int count = (int)send_message(g_gc->log_win, RVM_GETITEMCOUNT, 0, NULL);
    ASSERT_EQUAL(count, initial_count);

    gc_test_teardown();
    PASS();
}

// ── evCommand / evOpenRepo message routing ────────────────────────────────────

// Send evCommand(RVN_SELCHANGE) to the main window and verify that gc_main_proc
// triggers the appropriate refresh exactly as it would in a live session.
void test_gc_evcommand_selchange_log_triggers_files_refresh(void) {
    TEST("evCommand/RVN_SELCHANGE on log_win: gc_main_proc refreshes files panel");

    gc_test_setup();
    gc_log_refresh();
    ASSERT_TRUE(g_gc->commit_count >= 2);

    // Create a thin main window that wires win->userdata = g_gc and forwards
    // all messages (except evCreate) to gc_main_proc.
    rect_t fr = {0, 0, 10, 10};
    window_t *main_win = create_window("TestMain",
        WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOTRAYBUTTON,
        &fr, NULL, test_main_proc, 0, NULL);
    ASSERT_NOT_NULL(main_win);

    // Simulate selecting row 0 on the log panel.
    g_gc->selected_commit = -1;
    send_message(main_win, evCommand,
                 MAKEDWORD(0, RVN_SELCHANGE), (void *)g_gc->log_win);

    // gc_main_proc should have called gc_files_refresh() + gc_diff_refresh().
    ASSERT_EQUAL(g_gc->selected_commit, 0);
    int fcount = (int)send_message(g_gc->files_win, RVM_GETITEMCOUNT, 0, NULL);
    ASSERT_TRUE(fcount >= 1);

    gc_test_teardown();
    PASS();
}

void test_gc_evcommand_selchange_branches_triggers_log_refresh(void) {
    TEST("evCommand/RVN_SELCHANGE on branches_win: gc_main_proc refreshes log panel");

    gc_test_setup();

    // Clear the log panel first.
    send_message(g_gc->log_win, RVM_CLEAR, 0, NULL);
    ASSERT_EQUAL((int)send_message(g_gc->log_win, RVM_GETITEMCOUNT, 0, NULL), 0);

    rect_t fr = {0, 0, 10, 10};
    window_t *main_win = create_window("TestMain",
        WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOTRAYBUTTON,
        &fr, NULL, test_main_proc, 0, NULL);
    ASSERT_NOT_NULL(main_win);

    // Simulate branch row 1 being selected.
    send_message(main_win, evCommand,
                 MAKEDWORD(1, RVN_SELCHANGE), (void *)g_gc->branches_win);

    // gc_main_proc should have called gc_log_refresh().
    int lcount = (int)send_message(g_gc->log_win, RVM_GETITEMCOUNT, 0, NULL);
    ASSERT_TRUE(lcount >= 2);

    gc_test_teardown();
    PASS();
}

void test_gc_evopenmsg_opens_repository(void) {
    TEST("evOpenRepo message: main window opens the repository on request");

    gc_test_setup();

    // Close the repo so we can verify it is (re)opened by the message.
    if (g_gc->repo) { git_repo_close(g_gc->repo); g_gc->repo = NULL; }
    g_gc->commit_count = 0;
    g_gc->branch_count = 0;

    rect_t fr = {0, 0, 10, 10};
    window_t *main_win = create_window("TestMain",
        WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOTRAYBUTTON,
        &fr, NULL, test_main_proc, 0, NULL);
    ASSERT_NOT_NULL(main_win);

    send_message(main_win, evOpenRepo, 0, (void *)s_repo);

    ASSERT_NOT_NULL(g_gc->repo);
    ASSERT_TRUE(git_repo_valid(g_gc->repo));
    ASSERT_TRUE(g_gc->branch_count >= 2);
    ASSERT_TRUE(g_gc->commit_count >= 2);

    gc_test_teardown();
    PASS();
}

void test_gc_evopenmsg_invalid_path_leaves_repo_null(void) {
    TEST("evOpenRepo message: invalid path leaves g_gc->repo NULL");

    gc_test_setup();
    if (g_gc->repo) { git_repo_close(g_gc->repo); g_gc->repo = NULL; }

    rect_t fr = {0, 0, 10, 10};
    window_t *main_win = create_window("TestMain",
        WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOTRAYBUTTON,
        &fr, NULL, test_main_proc, 0, NULL);
    ASSERT_NOT_NULL(main_win);

    send_message(main_win, evOpenRepo, 0, (void *)"/tmp/this_repo_does_not_exist_12345");

    ASSERT_NULL(g_gc->repo);

    gc_test_teardown();
    PASS();
}

// ── Feature branch navigation ─────────────────────────────────────────────────

void test_gc_feature_branch_has_three_commits(void) {
    TEST("feature branch: log has 3 commits (Feature + Add file2 + Initial)");

    gc_test_setup();

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "cd '%s' && git checkout feature", s_repo);
    system(cmd);

    git_repo_close(g_gc->repo);
    g_gc->repo = git_repo_open(s_repo);

    gc_log_refresh();

    ASSERT_EQUAL(g_gc->commit_count, 3);
    ASSERT_STR_EQUAL(g_gc->commits[0].subject, "Feature commit");
    ASSERT_STR_EQUAL(g_gc->commits[1].subject, "Add file2");
    ASSERT_STR_EQUAL(g_gc->commits[2].subject, "Initial commit");

    // Restore.
    const char *def = detect_default_branch();
    snprintf(cmd, sizeof(cmd), "cd '%s' && git checkout %s", s_repo, def);
    system(cmd);

    gc_test_teardown();
    PASS();
}

void test_gc_feature_branch_commit_shows_feature_file(void) {
    TEST("feature branch: selecting the feature commit shows feature.txt");

    gc_test_setup();

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "cd '%s' && git checkout feature", s_repo);
    system(cmd);

    git_repo_close(g_gc->repo);
    g_gc->repo = git_repo_open(s_repo);

    gc_log_refresh();
    ASSERT_EQUAL(g_gc->commit_count, 3);

    // Commit 0 is "Feature commit" which adds feature.txt.
    g_gc->selected_commit = 0;
    g_gc->selected_file   = -1;
    gc_files_refresh();

    bool found = false;
    for (int i = 0; i < g_gc->file_count; i++) {
        if (strcmp(g_gc->files[i].path, "feature.txt") == 0) {
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);

    // Restore.
    const char *def = detect_default_branch();
    snprintf(cmd, sizeof(cmd), "cd '%s' && git checkout %s", s_repo, def);
    system(cmd);

    gc_test_teardown();
    PASS();
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    if (!setup_repo()) {
        printf("ERROR: could not create test repo (is git in PATH?)\n");
        return 1;
    }

    TEST_START("Git Client UI Integration");

    // Repository opening
    test_gc_open_repo_populates_state();

    // Branches panel
    test_gc_branches_panel_has_items();
    test_gc_branches_panel_includes_all_branches();
    test_gc_branches_refresh_twice_idempotent();

    // Log panel
    test_gc_log_panel_has_commits();
    test_gc_log_panel_count_matches_commit_count();
    test_gc_log_commit_data_populated();
    test_gc_log_refresh_twice_idempotent();

    // Files panel
    test_gc_files_panel_clean_working_tree();
    test_gc_files_panel_shows_modified_file();
    test_gc_files_panel_commit_mode_shows_changed_files();
    test_gc_files_panel_initial_commit_shows_file1();

    // Diff buffer
    test_gc_diff_buf_populated_for_commit();
    test_gc_diff_buf_populated_for_file_in_commit();
    test_gc_diff_buf_empty_for_clean_working_tree();

    // Navigation flow (direct function calls)
    test_gc_log_selection_triggers_file_update();
    test_gc_file_selection_triggers_diff_update();
    test_gc_branch_selection_refreshes_log();

    // Message-based routing (evCommand / evOpenRepo via gc_main_proc)
    test_gc_evcommand_selchange_log_triggers_files_refresh();
    test_gc_evcommand_selchange_branches_triggers_log_refresh();
    test_gc_evopenmsg_opens_repository();
    test_gc_evopenmsg_invalid_path_leaves_repo_null();

    // Feature branch navigation
    test_gc_feature_branch_has_three_commits();
    test_gc_feature_branch_commit_shows_feature_file();

    teardown_repo();

    TEST_END();
}
