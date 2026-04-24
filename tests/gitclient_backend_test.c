// tests/gitclient_backend_test.c — headless unit tests for the git client
// backend (examples/gitclient/git_backend.c).
//
// Creates a real git repository in a temporary directory, makes commits and
// branches, then exercises every public git_* API without touching any UI code.
//
// Build: see Makefile (GITCLIENT_TEST_BINS target).
// Run:   build/bin/test_gitclient_backend_test

#include "test_framework.h"
#include "gitclient_test_helpers.h"
#include "../examples/gitclient/gitclient.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ── Application state stub ────────────────────────────────────────────────────
// gitclient.h declares g_gc as extern.  The async worker calls post_message
// which references g_gc, but these synchronous tests never exercise async
// operations so the stub is never dereferenced through that path.
static gc_state_t g_stub_state;
gc_state_t *g_gc = &g_stub_state;

// ── Temporary test repository ─────────────────────────────────────────────────

static char s_repo[256] = {0};

// Returns the default initial branch name (main or master) set by git init.
static const char *detect_default_branch(void) {
    // After setup we always checkout an explicit "main" or "master"; just check
    // which one exists in the test repo.
    char path[512];
    snprintf(path, sizeof(path), "%s/.git/refs/heads/main", s_repo);
    struct stat st;
    return (stat(path, &st) == 0) ? "main" : "master";
}

static bool setup_test_repo(void) {
    if (!gct_make_temp_dir(s_repo, sizeof(s_repo), "orion_gcbe")) {
        printf("[setup] failed to create temp dir\n");
        return false;
    }

    // git init — prefer -b main (git >= 2.28); fall back to plain init.
    if (!gct_git(s_repo, "init -b main")) {
        if (!gct_git(s_repo, "init")) {
            printf("[setup] git init failed — is git in PATH?\n");
            return false;
        }
        // Create 'main' on older git; ignore failure (may already be named main).
        gct_git(s_repo, "checkout -b main");
    }

    if (!gct_git(s_repo, "config user.email ci@test") ||
        !gct_git(s_repo, "config user.name CI")) {
        printf("[setup] git config failed\n");
        return false;
    }

    // First commit: file1.txt
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/file1.txt", s_repo);
    if (!gct_write_file(file_path, "hello\n") ||
        !gct_git(s_repo, "add file1.txt") ||
        !gct_git(s_repo, "commit -m \"Initial commit\"")) {
        printf("[setup] first commit failed\n");
        return false;
    }

    // Second commit: file2.txt
    snprintf(file_path, sizeof(file_path), "%s/file2.txt", s_repo);
    if (!gct_write_file(file_path, "world\n") ||
        !gct_git(s_repo, "add file2.txt") ||
        !gct_git(s_repo, "commit -m \"Add file2\"")) {
        printf("[setup] second commit failed\n");
        return false;
    }

    // Feature branch: feature.txt
    snprintf(file_path, sizeof(file_path), "%s/feature.txt", s_repo);
    if (!gct_git(s_repo, "checkout -b feature") ||
        !gct_write_file(file_path, "feat\n") ||
        !gct_git(s_repo, "add feature.txt") ||
        !gct_git(s_repo, "commit -m \"Feature commit\"")) {
        printf("[setup] feature branch failed\n");
        return false;
    }

    // Return to the default branch (main or master).
    if (!gct_git(s_repo, "checkout main") &&
        !gct_git(s_repo, "checkout master")) {
        printf("[setup] failed to checkout default branch\n");
        return false;
    }
    return true;
}

static void teardown_test_repo(void) {
    if (s_repo[0]) {
        gct_remove_dir(s_repo);
        s_repo[0] = '\0';
    }
}

// ── Tests ─────────────────────────────────────────────────────────────────────

void test_gc_open_invalid_path(void) {
    TEST("git_repo_open: non-git directory returns NULL");
    git_repo_t *r = git_repo_open("/tmp");
    ASSERT_NULL(r);
    PASS();
}

void test_gc_open_nonexistent_path(void) {
    TEST("git_repo_open: non-existent path returns NULL");
    git_repo_t *r = git_repo_open("/tmp/orion_no_such_dir_12345");
    ASSERT_NULL(r);
    PASS();
}

void test_gc_open_valid_repo(void) {
    TEST("git_repo_open: valid repo opens successfully");
    git_repo_t *r = git_repo_open(s_repo);
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(git_repo_valid(r));
    ASSERT_STR_EQUAL(git_repo_path(r), s_repo);
    git_repo_close(r);
    PASS();
}

void test_gc_current_branch_main(void) {
    TEST("git_current_branch: returns the expected branch name");
    git_repo_t *r = git_repo_open(s_repo);
    ASSERT_NOT_NULL(r);

    char branch[128] = {0};
    bool ok = git_current_branch(r, branch, sizeof(branch));
    ASSERT_TRUE(ok);
    ASSERT_TRUE(branch[0] != '\0');

    const char *expected = detect_default_branch();
    ASSERT_STR_EQUAL(branch, expected);

    git_repo_close(r);
    PASS();
}

void test_gc_branches_count(void) {
    TEST("git_get_branches: at least 2 local branches (main + feature)");
    git_repo_t *r = git_repo_open(s_repo);
    ASSERT_NOT_NULL(r);

    git_branch_t branches[GC_MAX_BRANCHES];
    int n = git_get_branches(r, branches, GC_MAX_BRANCHES);
    ASSERT_TRUE(n >= 2);

    git_repo_close(r);
    PASS();
}

void test_gc_branches_names(void) {
    TEST("git_get_branches: list contains the default branch and 'feature'");
    git_repo_t *r = git_repo_open(s_repo);
    ASSERT_NOT_NULL(r);

    git_branch_t branches[GC_MAX_BRANCHES];
    int n = git_get_branches(r, branches, GC_MAX_BRANCHES);
    ASSERT_TRUE(n >= 2);

    const char *def = detect_default_branch();
    bool found_main    = false;
    bool found_feature = false;

    for (int i = 0; i < n; i++) {
        if (strcmp(branches[i].name, def) == 0)
            found_main = true;
        if (strcmp(branches[i].name, "feature") == 0)
            found_feature = true;
    }

    ASSERT_TRUE(found_main);
    ASSERT_TRUE(found_feature);

    git_repo_close(r);
    PASS();
}

void test_gc_branches_current_flag(void) {
    TEST("git_get_branches: exactly one branch has is_current == true");
    git_repo_t *r = git_repo_open(s_repo);
    ASSERT_NOT_NULL(r);

    git_branch_t branches[GC_MAX_BRANCHES];
    int n = git_get_branches(r, branches, GC_MAX_BRANCHES);
    ASSERT_TRUE(n >= 1);

    int current_count = 0;
    for (int i = 0; i < n; i++) {
        if (branches[i].is_current)
            current_count++;
    }
    ASSERT_EQUAL(current_count, 1);

    // The current branch must be the default one (we checked it out in setup).
    const char *def = detect_default_branch();
    for (int i = 0; i < n; i++) {
        if (branches[i].is_current) {
            ASSERT_STR_EQUAL(branches[i].name, def);
            break;
        }
    }

    git_repo_close(r);
    PASS();
}

void test_gc_log_count(void) {
    TEST("git_get_log: main branch has exactly 2 commits");
    git_repo_t *r = git_repo_open(s_repo);
    ASSERT_NOT_NULL(r);

    git_commit_t commits[GC_MAX_COMMITS];
    int n = git_get_log(r, commits, GC_MAX_COMMITS);
    ASSERT_EQUAL(n, 2);

    git_repo_close(r);
    PASS();
}

void test_gc_log_subjects(void) {
    TEST("git_get_log: commits are ordered newest-first with correct subjects");
    git_repo_t *r = git_repo_open(s_repo);
    ASSERT_NOT_NULL(r);

    git_commit_t commits[GC_MAX_COMMITS];
    int n = git_get_log(r, commits, GC_MAX_COMMITS);
    ASSERT_EQUAL(n, 2);

    ASSERT_STR_EQUAL(commits[0].subject, "Add file2");
    ASSERT_STR_EQUAL(commits[1].subject, "Initial commit");

    git_repo_close(r);
    PASS();
}

void test_gc_log_fields_populated(void) {
    TEST("git_get_log: commits have non-empty hash, author, date, subject");
    git_repo_t *r = git_repo_open(s_repo);
    ASSERT_NOT_NULL(r);

    git_commit_t commits[GC_MAX_COMMITS];
    int n = git_get_log(r, commits, GC_MAX_COMMITS);
    ASSERT_TRUE(n >= 1);

    ASSERT_TRUE(commits[0].hash[0]    != '\0');
    ASSERT_TRUE(commits[0].author[0]  != '\0');
    ASSERT_TRUE(commits[0].date[0]    != '\0');
    ASSERT_TRUE(commits[0].subject[0] != '\0');

    ASSERT_STR_EQUAL(commits[0].author, "CI");

    git_repo_close(r);
    PASS();
}

void test_gc_log_hash_format(void) {
    TEST("git_get_log: commit hash is 40 lowercase hex characters");
    git_repo_t *r = git_repo_open(s_repo);
    ASSERT_NOT_NULL(r);

    git_commit_t commits[GC_MAX_COMMITS];
    int n = git_get_log(r, commits, GC_MAX_COMMITS);
    ASSERT_TRUE(n >= 1);

    ASSERT_EQUAL((int)strlen(commits[0].hash), 40);
    for (int i = 0; i < 40; i++) {
        char c = commits[0].hash[i];
        ASSERT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }

    git_repo_close(r);
    PASS();
}

void test_gc_log_feature_branch(void) {
    TEST("git_get_log: feature branch has 3 commits (inherits from main)");
    ASSERT_TRUE(gct_git(s_repo, "checkout feature"));

    git_repo_t *r = git_repo_open(s_repo);
    ASSERT_NOT_NULL(r);

    git_commit_t commits[GC_MAX_COMMITS];
    int n = git_get_log(r, commits, GC_MAX_COMMITS);
    ASSERT_EQUAL(n, 3);
    ASSERT_STR_EQUAL(commits[0].subject, "Feature commit");

    git_repo_close(r);

    // Restore default branch.
    const char *def = detect_default_branch();
    char restore_cmd[64];
    snprintf(restore_cmd, sizeof(restore_cmd), "checkout %s", def);
    ASSERT_TRUE(gct_git(s_repo, restore_cmd));

    PASS();
}

void test_gc_status_clean(void) {
    TEST("git_get_status: clean repo returns 0 files");
    git_repo_t *r = git_repo_open(s_repo);
    ASSERT_NOT_NULL(r);

    git_file_status_t files[GC_MAX_FILES];
    int n = git_get_status(r, files, GC_MAX_FILES);
    ASSERT_EQUAL(n, 0);

    git_repo_close(r);
    PASS();
}

void test_gc_status_modified_file(void) {
    TEST("git_get_status: modified unstaged file appears with status 'M'");

    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/file1.txt", s_repo);
    ASSERT_TRUE(gct_append_file(file_path, "modified\n"));

    git_repo_t *r = git_repo_open(s_repo);
    ASSERT_NOT_NULL(r);

    git_file_status_t files[GC_MAX_FILES];
    int n = git_get_status(r, files, GC_MAX_FILES);
    ASSERT_TRUE(n >= 1);

    bool found = false;
    for (int i = 0; i < n; i++) {
        if (strcmp(files[i].path, "file1.txt") == 0) {
            ASSERT_EQUAL(files[i].status, 'M');
            ASSERT_FALSE(files[i].staged);
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);

    git_repo_close(r);

    // Restore.
    ASSERT_TRUE(gct_git(s_repo, "checkout -- file1.txt"));

    PASS();
}

void test_gc_status_staged_file(void) {
    TEST("git_get_status: staged new file has staged == true");

    char staged_path[512];
    snprintf(staged_path, sizeof(staged_path), "%s/staged_file.txt", s_repo);
    ASSERT_TRUE(gct_write_file(staged_path, "staged\n"));
    ASSERT_TRUE(gct_git(s_repo, "add staged_file.txt"));

    git_repo_t *r = git_repo_open(s_repo);
    ASSERT_NOT_NULL(r);

    git_file_status_t files[GC_MAX_FILES];
    int n = git_get_status(r, files, GC_MAX_FILES);
    ASSERT_TRUE(n >= 1);

    bool found = false;
    for (int i = 0; i < n; i++) {
        if (strcmp(files[i].path, "staged_file.txt") == 0) {
            ASSERT_TRUE(files[i].staged);
            found = true;
            break;
        }
    }
    ASSERT_TRUE(found);

    git_repo_close(r);

    // Restore: unstage and delete the temporary file.
    ASSERT_TRUE(gct_git(s_repo, "restore --staged staged_file.txt"));
    (void)remove(staged_path);

    PASS();
}

void test_gc_get_diff_modified(void) {
    TEST("git_get_diff: non-empty output for modified unstaged file");

    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/file1.txt", s_repo);
    ASSERT_TRUE(gct_append_file(file_path, "extra\n"));

    git_repo_t *r = git_repo_open(s_repo);
    ASSERT_NOT_NULL(r);

    char buf[GC_MAX_DIFF_SIZE];
    bool ok = git_get_diff(r, "file1.txt", false, buf, sizeof(buf));
    ASSERT_TRUE(ok);
    ASSERT_TRUE(buf[0] != '\0');
    // The diff should contain '+extra'.
    ASSERT_TRUE(strstr(buf, "+extra") != NULL || strstr(buf, "extra") != NULL);

    git_repo_close(r);

    // Restore.
    ASSERT_TRUE(gct_git(s_repo, "checkout -- file1.txt"));

    PASS();
}

void test_gc_run_sync_rev_parse(void) {
    TEST("git_run_sync: arbitrary git command succeeds and returns output");
    git_repo_t *r = git_repo_open(s_repo);
    ASSERT_NOT_NULL(r);

    char buf[256] = {0};
    const char *args[] = { "git", "rev-parse", "--git-dir", NULL };
    bool ok = git_run_sync(r, args, buf, sizeof(buf));
    ASSERT_TRUE(ok);
    ASSERT_TRUE(buf[0] != '\0');

    git_repo_close(r);
    PASS();
}

void test_gc_run_sync_failure(void) {
    TEST("git_run_sync: invalid git command returns false");
    git_repo_t *r = git_repo_open(s_repo);
    ASSERT_NOT_NULL(r);

    char buf[256] = {0};
    const char *args[] = { "git", "this-command-does-not-exist", NULL };
    bool ok = git_run_sync(r, args, buf, sizeof(buf));
    ASSERT_FALSE(ok);

    git_repo_close(r);
    PASS();
}

// ── Entry point ───────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    if (!setup_test_repo()) {
        printf("ERROR: could not create test repo (is git in PATH?)\n");
        return 1;
    }

    TEST_START("Git Client Backend");

    test_gc_open_invalid_path();
    test_gc_open_nonexistent_path();
    test_gc_open_valid_repo();
    test_gc_current_branch_main();
    test_gc_branches_count();
    test_gc_branches_names();
    test_gc_branches_current_flag();
    test_gc_log_count();
    test_gc_log_subjects();
    test_gc_log_fields_populated();
    test_gc_log_hash_format();
    test_gc_log_feature_branch();
    test_gc_status_clean();
    test_gc_status_modified_file();
    test_gc_status_staged_file();
    test_gc_get_diff_modified();
    test_gc_run_sync_rev_parse();
    test_gc_run_sync_failure();

    teardown_test_repo();

    TEST_END();
}
