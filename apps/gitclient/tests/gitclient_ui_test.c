// gitclient db-driven architecture tests.
//
// Tests the three properties that matter for the db-driven / NeXTSTEP-DBKit
// cascade model:
//   1. dbLoad populates all six tables in one shot.
//   2. dbFetch with a branch_id filter returns only that branch's commits
//      (branches → commits master-detail).
//   3. dbFetch with a commit_id filter lazy-loads that commit's files
//      (commits → files master-detail, on-demand from git).
//
// A real git repository is created in /tmp for every run and removed at the
// end.  No UI windows are created; all assertions are against the database
// layer directly.

#include "test_framework.h"
#include "test_env.h"
#include "test_graphics.h"
#include "gitclient_test_helpers.h"
#include "apps/gitclient/gitclient.h"
#include "apps/gitclient/gc_actions.h"
#include "apps/gitclient/gc_agents.h"
#include "apps/gitclient/components/diff_view.h"
#include "apps/terminal/components/terminal_view.h"
#include <orion/user/toolbar.h>

#include <stdio.h>
#include <string.h>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

// ── Application state stub ────────────────────────────────────────────────────

static gc_state_t g_test_state;
gc_state_t *g_gc = &g_test_state;

// ── Test repository ───────────────────────────────────────────────────────────

static char s_repo[256];

static bool setup_repo(void) {
    if (!gct_make_temp_dir(s_repo, sizeof(s_repo), "orion_gcdb")) return false;
    if (!gct_git(s_repo, "init -b main") && !gct_git(s_repo, "init"))  return false;
    if (!gct_git(s_repo, "config user.email ci@test")) return false;
    if (!gct_git(s_repo, "config user.name CI"))       return false;

    // Commit 1: file1.txt
    char p[512];
    snprintf(p, sizeof(p), "%s/file1.txt", s_repo);
    if (!gct_write_file(p, "hello\n"))        return false;
    if (!gct_git(s_repo, "add file1.txt"))    return false;
    if (!gct_git(s_repo, "commit -m \"first commit\"")) return false;

    // Commit 2: file2.txt (still on main)
    snprintf(p, sizeof(p), "%s/file2.txt", s_repo);
    if (!gct_write_file(p, "world\n"))        return false;
    if (!gct_git(s_repo, "add file2.txt"))    return false;
    if (!gct_git(s_repo, "commit -m \"second commit\"")) return false;

    // Feature branch: feature.txt
    if (!gct_git(s_repo, "checkout -b feature")) return false;
    snprintf(p, sizeof(p), "%s/feature.txt", s_repo);
    if (!gct_write_file(p, "feat\n"))         return false;
    if (!gct_git(s_repo, "add feature.txt")) return false;
    if (!gct_git(s_repo, "commit -m \"feature commit\"")) return false;

    // Return to main
    if (!gct_git(s_repo, "checkout main") &&
        !gct_git(s_repo, "checkout master")) return false;

    return true;
}

// ── Database helpers ──────────────────────────────────────────────────────────

static int result_count(result_node_t *rows) {
    int n = 0;
    for (; rows; rows = rows->next) n++;
    return n;
}

static database_t *make_db(void) {
    DB_CLASS(gitclient_db);
    return create_database("gc-test", "gitclient_db", NULL);
}

static database_t *make_changes_db(void) {
    DB_CLASS(changes_database_proc);
    return create_database("gc-test-changes", "changes_database_proc", NULL);
}

// ── Tests ─────────────────────────────────────────────────────────────────────

// 1. dbLoad populates branches ────────────────────────────────────────────────

void test_dbload_populates_branches(void) {
    TEST("dbLoad: branches table populated from git");
    test_env_init();
    database_t *db = make_db();
    git_repo_t *repo = git_repo_open(s_repo);
    ASSERT_NOT_NULL(repo);

    send_db_message(db, dbLoad, 0, repo);

    result_node_t *rows = (result_node_t *)send_db_message(
        db, dbFetch, MAKEDWORD(ID_DB_BRANCHES, 0), (void *)(intptr_t)0);
    ASSERT_TRUE(result_count(rows) >= 2);   // main + feature

    bool found_main    = false;
    bool found_feature = false;
    int  current_count = 0;
    for (result_node_t *n = rows; n; n = n->next) {
        db_branch_t *b = *(db_branch_t **)n->data;
        if (strcmp(b->name, "main") == 0 || strcmp(b->name, "master") == 0)
            found_main = true;
        if (strcmp(b->name, "feature") == 0)
            found_feature = true;
        if (b->is_current)
            current_count++;
    }
    free_result_list(rows);

    ASSERT_TRUE(found_main);
    ASSERT_TRUE(found_feature);
    ASSERT_EQUAL(current_count, 1);

    git_repo_close(repo);
    destroy_database(db);
    test_env_shutdown();
    PASS();
}

// 2. dbLoad populates commits ─────────────────────────────────────────────────

void test_dbload_populates_commits(void) {
    TEST("dbLoad: current branch's commits table populated eagerly");
    test_env_init();
    database_t *db = make_db();
    git_repo_t *repo = git_repo_open(s_repo);
    ASSERT_NOT_NULL(repo);

    send_db_message(db, dbLoad, 0, repo);

    result_node_t *rows = (result_node_t *)send_db_message(
        db, dbFetch, MAKEDWORD(ID_DB_COMMITS, 0), (void *)(intptr_t)0);
    // Only the current branch (main) is loaded eagerly; other branches
    // lazy-load when selected. main has exactly 2 commits.
    ASSERT_EQUAL(result_count(rows), 2);

    // Verify fields are populated on the first commit
    db_commit_t *c = rows ? *(db_commit_t **)rows->data : NULL;
    ASSERT_NOT_NULL(c);
    ASSERT_TRUE(c->hash[0] != '\0');
    ASSERT_TRUE(c->author[0] != '\0');
    ASSERT_TRUE(c->subject[0] != '\0');
    ASSERT_EQUAL((int)strlen(c->hash), 40);

    free_result_list(rows);
    git_repo_close(repo);
    destroy_database(db);
    test_env_shutdown();
    PASS();
}

// 3. dbLoad populates tags ────────────────────────────────────────────────────

void test_dbload_populates_tags(void) {
    TEST("dbLoad: tags table populated after tag creation");
    ASSERT_TRUE(gct_git(s_repo, "tag -a v1.0 -m release HEAD"));

    test_env_init();
    database_t *db = make_db();
    git_repo_t *repo = git_repo_open(s_repo);
    ASSERT_NOT_NULL(repo);

    send_db_message(db, dbLoad, 0, repo);

    result_node_t *rows = (result_node_t *)send_db_message(
        db, dbFetch, MAKEDWORD(ID_DB_TAGS, 0), (void *)(intptr_t)0);
    ASSERT_EQUAL(result_count(rows), 1);
    db_tag_t *tag = rows ? *(db_tag_t **)rows->data : NULL;
    ASSERT_NOT_NULL(tag);
    ASSERT_STR_EQUAL(tag->name, "v1.0");
    ASSERT_EQUAL((int)strlen(tag->hash), 40);
    free_result_list(rows);

    git_repo_close(repo);
    destroy_database(db);
    test_env_shutdown();

    // Cleanup tag for subsequent tests
    gct_git(s_repo, "tag -d v1.0");
    PASS();
}

// 4. dbLoad populates stash ───────────────────────────────────────────────────

void test_dbload_populates_stash(void) {
    TEST("dbLoad: stash table populated after stash push");
    char p[512];
    snprintf(p, sizeof(p), "%s/file1.txt", s_repo);
    ASSERT_TRUE(gct_append_file(p, "stashed change\n"));
    ASSERT_TRUE(gct_git(s_repo, "stash push -m db-test-stash"));

    test_env_init();
    database_t *db = make_db();
    git_repo_t *repo = git_repo_open(s_repo);
    ASSERT_NOT_NULL(repo);

    send_db_message(db, dbLoad, 0, repo);

    result_node_t *rows = (result_node_t *)send_db_message(
        db, dbFetch, MAKEDWORD(ID_DB_STASH, 0), (void *)(intptr_t)0);
    ASSERT_EQUAL(result_count(rows), 1);
    db_stash_t *st = rows ? *(db_stash_t **)rows->data : NULL;
    ASSERT_NOT_NULL(st);
    ASSERT_STR_EQUAL(st->ref, "stash@{0}");
    ASSERT_TRUE(strstr(st->message, "db-test-stash") != NULL);
    free_result_list(rows);

    git_repo_close(repo);
    destroy_database(db);
    test_env_shutdown();

    // Cleanup stash for subsequent tests
    gct_git(s_repo, "stash drop stash@{0}");
    PASS();
}

// 5. dbLoad clear-and-reload is idempotent ────────────────────────────────────

void test_dbload_is_idempotent(void) {
    TEST("dbLoad: calling twice does not duplicate rows");
    test_env_init();
    database_t *db = make_db();
    git_repo_t *repo = git_repo_open(s_repo);
    ASSERT_NOT_NULL(repo);

    send_db_message(db, dbLoad, 0, repo);
    send_db_message(db, dbLoad, 0, repo);   // second call must clear first

    result_node_t *rows = (result_node_t *)send_db_message(
        db, dbFetch, MAKEDWORD(ID_DB_BRANCHES, 0), (void *)(intptr_t)0);
    int count = result_count(rows);
    free_result_list(rows);
    ASSERT_TRUE(count >= 2);    // same count as after one load

    result_node_t *commits = (result_node_t *)send_db_message(
        db, dbFetch, MAKEDWORD(ID_DB_COMMITS, 0), (void *)(intptr_t)0);
    int commit_count_1 = result_count(commits);
    free_result_list(commits);

    send_db_message(db, dbLoad, 0, repo);   // third load
    commits = (result_node_t *)send_db_message(
        db, dbFetch, MAKEDWORD(ID_DB_COMMITS, 0), (void *)(intptr_t)0);
    int commit_count_2 = result_count(commits);
    free_result_list(commits);

    ASSERT_EQUAL(commit_count_1, commit_count_2);

    git_repo_close(repo);
    destroy_database(db);
    test_env_shutdown();
    PASS();
}

// 6. Master-detail: branch → commits filtered by branch_id ───────────────────

void test_master_detail_branch_to_commits(void) {
    TEST("master-detail: dbFetch(commits, branch_id) returns only that branch's commits");
    test_env_init();
    database_t *db = make_db();
    git_repo_t *repo = git_repo_open(s_repo);
    ASSERT_NOT_NULL(repo);

    send_db_message(db, dbLoad, 0, repo);

    // Find the 'feature' branch row to get its id
    result_node_t *branches = (result_node_t *)send_db_message(
        db, dbFetch, MAKEDWORD(ID_DB_BRANCHES, 0), (void *)(intptr_t)0);
    int feature_id = -1;
    int main_id    = -1;
    for (result_node_t *n = branches; n; n = n->next) {
        db_branch_t *b = *(db_branch_t **)n->data;
        if (strcmp(b->name, "feature") == 0)  feature_id = b->id;
        if (strcmp(b->name, "main") == 0 ||
            strcmp(b->name, "master") == 0)   main_id    = b->id;
    }
    free_result_list(branches);
    ASSERT_TRUE(feature_id > 0);
    ASSERT_TRUE(main_id    > 0);

    // main has exactly 2 commits
    result_node_t *main_commits = (result_node_t *)send_db_message(
        db, dbFetch,
        MAKEDWORD(ID_DB_COMMITS, ID_DB_COMMITS_BRANCH_ID),
        (void *)(intptr_t)main_id);
    int main_count = result_count(main_commits);
    free_result_list(main_commits);
    ASSERT_EQUAL(main_count, 2);

    // feature has exactly 3 commits (inherits main)
    result_node_t *feat_commits = (result_node_t *)send_db_message(
        db, dbFetch,
        MAKEDWORD(ID_DB_COMMITS, ID_DB_COMMITS_BRANCH_ID),
        (void *)(intptr_t)feature_id);
    int feat_count = result_count(feat_commits);
    free_result_list(feat_commits);
    ASSERT_EQUAL(feat_count, 3);

    git_repo_close(repo);
    destroy_database(db);
    test_env_shutdown();
    PASS();
}

// 7. Master-detail: commit → files lazy-loaded on first dbFetch ───────────────

void test_master_detail_commit_to_files_lazy_load(void) {
    TEST("master-detail: dbFetch(files, commit_id) lazy-loads files from git");
    test_env_init();
    database_t *db = make_db();
    git_repo_t *repo = git_repo_open(s_repo);
    ASSERT_NOT_NULL(repo);

    send_db_message(db, dbLoad, 0, repo);

    // Get main branch id, then find the "second commit" (adds file2.txt)
    result_node_t *branches = (result_node_t *)send_db_message(
        db, dbFetch, MAKEDWORD(ID_DB_BRANCHES, 0), (void *)(intptr_t)0);
    int main_id = -1;
    for (result_node_t *n = branches; n; n = n->next) {
        db_branch_t *b = *(db_branch_t **)n->data;
        if (strcmp(b->name, "main") == 0 || strcmp(b->name, "master") == 0)
            main_id = b->id;
    }
    free_result_list(branches);
    ASSERT_TRUE(main_id > 0);

    result_node_t *commits = (result_node_t *)send_db_message(
        db, dbFetch,
        MAKEDWORD(ID_DB_COMMITS, ID_DB_COMMITS_BRANCH_ID),
        (void *)(intptr_t)main_id);
    // newest first — pick the second commit (adds file2.txt)
    result_node_t *second_node = commits ? commits->next : NULL;
    ASSERT_NOT_NULL(second_node);
    db_commit_t *second = *(db_commit_t **)second_node->data;
    int commit_id = second->id;
    free_result_list(commits);
    ASSERT_TRUE(commit_id > 0);

    // First access: no files cached → lazy-load triggers git show
    result_node_t *files = (result_node_t *)send_db_message(
        db, dbFetch,
        MAKEDWORD(ID_DB_FILES, ID_DB_FILES_COMMIT_ID),
        (void *)(intptr_t)commit_id);
    int file_count = result_count(files);
    free_result_list(files);
    ASSERT_TRUE(file_count >= 1);   // at least one file touched in that commit

    // Second access: same commit_id → returns cached rows, no re-run
    files = (result_node_t *)send_db_message(
        db, dbFetch,
        MAKEDWORD(ID_DB_FILES, ID_DB_FILES_COMMIT_ID),
        (void *)(intptr_t)commit_id);
    ASSERT_EQUAL(result_count(files), file_count);
    free_result_list(files);

    git_repo_close(repo);
    destroy_database(db);
    test_env_shutdown();
    PASS();
}

// 8. Files for different commits are independent ───────────────────────────────

void test_files_per_commit_are_independent(void) {
    TEST("master-detail: files filtered by commit_id do not bleed across commits");
    test_env_init();
    database_t *db = make_db();
    git_repo_t *repo = git_repo_open(s_repo);
    ASSERT_NOT_NULL(repo);

    send_db_message(db, dbLoad, 0, repo);

    // Get main branch commits
    result_node_t *branches = (result_node_t *)send_db_message(
        db, dbFetch, MAKEDWORD(ID_DB_BRANCHES, 0), (void *)(intptr_t)0);
    int main_id = -1;
    for (result_node_t *n = branches; n; n = n->next) {
        db_branch_t *b = *(db_branch_t **)n->data;
        if (strcmp(b->name, "main") == 0 || strcmp(b->name, "master") == 0)
            main_id = b->id;
    }
    free_result_list(branches);
    ASSERT_TRUE(main_id > 0);

    result_node_t *commits = (result_node_t *)send_db_message(
        db, dbFetch,
        MAKEDWORD(ID_DB_COMMITS, ID_DB_COMMITS_BRANCH_ID),
        (void *)(intptr_t)main_id);
    ASSERT_NOT_NULL(commits);
    ASSERT_NOT_NULL(commits->next);
    result_node_t *node_a = commits;
    result_node_t *node_b = commits->next;
    int id_a = (*(db_commit_t **)node_a->data)->id;
    int id_b = (*(db_commit_t **)node_b->data)->id;
    free_result_list(commits);

    // Trigger lazy-load for both commits
    result_node_t *fa = (result_node_t *)send_db_message(
        db, dbFetch, MAKEDWORD(ID_DB_FILES, ID_DB_FILES_COMMIT_ID), (void *)(intptr_t)id_a);
    result_node_t *fb = (result_node_t *)send_db_message(
        db, dbFetch, MAKEDWORD(ID_DB_FILES, ID_DB_FILES_COMMIT_ID), (void *)(intptr_t)id_b);

    // Neither set must be empty; verify commit_id stamps are correct
    ASSERT_TRUE(result_count(fa) >= 1);
    ASSERT_TRUE(result_count(fb) >= 1);
    for (result_node_t *n = fa; n; n = n->next)
        ASSERT_EQUAL((*(db_file_t **)n->data)->commit_id, id_a);
    for (result_node_t *n = fb; n; n = n->next)
        ASSERT_EQUAL((*(db_file_t **)n->data)->commit_id, id_b);

    free_result_list(fa);
    free_result_list(fb);

    git_repo_close(repo);
    destroy_database(db);
    test_env_shutdown();
    PASS();
}

// 9. Working-tree files have commit_id == 0 ───────────────────────────────────

void test_working_tree_files_have_zero_commit_id(void) {
    TEST("changes dbLoad: working-tree dirty files stored with commit_id == 0");
    // Stage a new file so the working tree is dirty
    char p[512];
    snprintf(p, sizeof(p), "%s/dirty.txt", s_repo);
    ASSERT_TRUE(gct_write_file(p, "dirty\n"));
    ASSERT_TRUE(gct_git(s_repo, "add dirty.txt"));

    test_env_init();
    database_t *db = make_changes_db();
    git_repo_t *repo = git_repo_open(s_repo);
    ASSERT_NOT_NULL(repo);

    send_db_message(db, dbLoad, 0, repo);

    // commit_id == 0 means "working tree" — fetch all (no filter) to see them
    result_node_t *all_files = (result_node_t *)send_db_message(
        db, dbFetch, MAKEDWORD(TABLE_FILES, 0), (void *)(intptr_t)0);
    bool found_wt = false;
    for (result_node_t *n = all_files; n; n = n->next) {
        db_file_t *f = *(db_file_t **)n->data;
        if (f->commit_id == 0) { found_wt = true; break; }
    }
    free_result_list(all_files);
    ASSERT_TRUE(found_wt);

    git_repo_close(repo);
    destroy_database(db);
    test_env_shutdown();

    // Unstage and remove the temp file
    gct_git(s_repo, "restore --staged dirty.txt");
    remove(p);
    PASS();
}

// 10. Orion-generated context menu fixtures ───────────────────────────────────

// Controls now live in page sub-forms, so search recursively across all of them.
static const form_ctrl_def_t *find_in(const form_ctrl_def_t *children,
                                       int count, uint32_t id) {
    for (int i = 0; i < count; i++) {
        if (children[i].id == id) return &children[i];
        const form_ctrl_def_t *found =
            find_in(children[i].children, children[i].child_count, id);
        if (found) return found;
    }
    return NULL;
}

static const form_ctrl_def_t *generated_control(uint32_t id) {
    const form_ctrl_def_t *f;
    f = find_in(gc_changes_page_form.children, gc_changes_page_form.child_count, id);
    if (f) return f;
    f = find_in(gc_history_page_form.children, gc_history_page_form.child_count, id);
    if (f) return f;
    return NULL;
}

void test_generated_context_menus_attach_to_expected_controls(void) {
    TEST("gitclient Orion: generated context menus attach to all four tables");
    const form_ctrl_def_t *branches = generated_control(ID_HISTORY_PAGE_BRANCHES);
    const form_ctrl_def_t *tags     = generated_control(ID_HISTORY_PAGE_TAGS);
    const form_ctrl_def_t *stash    = generated_control(ID_HISTORY_PAGE_STASH_LIST);
    const form_ctrl_def_t *files    = generated_control(ID_CHANGES_PAGE_CHANGES_FILES);
    const form_ctrl_def_t *history  = generated_control(ID_HISTORY_PAGE_HISTORY_FILES);
    ASSERT_NOT_NULL(branches); ASSERT_NOT_NULL(tags);
    ASSERT_NOT_NULL(stash);    ASSERT_NOT_NULL(files); ASSERT_NOT_NULL(history);
    ASSERT_TRUE(branches->context_menu == CONTEXT_MENU_BRANCHES_ITEMS);
    ASSERT_TRUE(tags->context_menu     == CONTEXT_MENU_TAGS_ITEMS);
    ASSERT_TRUE(stash->context_menu    == CONTEXT_MENU_STASH_ITEMS);
    ASSERT_TRUE(files->context_menu    == CONTEXT_MENU_FILES_ITEMS);
    ASSERT_TRUE(history->context_menu == NULL);
    ASSERT_EQUAL(branches->context_menu_count, 4);
    ASSERT_EQUAL(tags->context_menu_count, 1);
    ASSERT_EQUAL(stash->context_menu_count, 2);
    ASSERT_EQUAL(files->context_menu_count, 8);
    ASSERT_TRUE(changes_page_changes_files_tableview_params.check_field != NULL);
    ASSERT_TRUE(strcmp(changes_page_changes_files_tableview_params.check_field, "staged") == 0);
    ASSERT_TRUE(changes_page_changes_files_tableview_params.master_id == 0);
    ASSERT_TRUE(history_page_history_files_tableview_params.check_field == NULL);
    ASSERT_EQUAL(history_page_history_files_tableview_params.master_id, ID_HISTORY_PAGE_LOG);
    ASSERT_TRUE(history_page_branches_tableview_params.check_field == NULL);
    ASSERT_TRUE(history_page_tags_tableview_params.check_field == NULL);
    ASSERT_TRUE(history_page_stash_list_tableview_params.check_field == NULL);
    PASS();
}

void test_generated_context_menus_reuse_shared_commands(void) {
    TEST("gitclient Orion: context entries reuse normal command IDs");
    ASSERT_EQUAL(CONTEXT_MENU_BRANCHES_ITEMS[0].id, ID_BRANCH_CHECKOUT);
    ASSERT_EQUAL(CONTEXT_MENU_BRANCHES_ITEMS[1].id, ID_BRANCH_MERGE);
    ASSERT_EQUAL(CONTEXT_MENU_BRANCHES_ITEMS[3].id, ID_BRANCH_DELETE);
    ASSERT_EQUAL(CONTEXT_MENU_TAGS_ITEMS[0].id, ID_TAG_DELETE);
    ASSERT_EQUAL(CONTEXT_MENU_STASH_ITEMS[0].id, ID_COMMIT_STASH_POP);
    ASSERT_EQUAL(CONTEXT_MENU_STASH_ITEMS[1].id, ID_COMMIT_STASH_DROP);
    ASSERT_EQUAL(CONTEXT_MENU_FILES_ITEMS[0].id, ID_FILES_STAGE);
    ASSERT_EQUAL(CONTEXT_MENU_FILES_ITEMS[1].id, ID_FILES_UNSTAGE);
    ASSERT_EQUAL(CONTEXT_MENU_FILES_ITEMS[3].id, ID_FILES_STAGE_ALL);
    ASSERT_EQUAL(CONTEXT_MENU_FILES_ITEMS[4].id, ID_FILES_UNSTAGE_ALL);
    ASSERT_EQUAL(CONTEXT_MENU_FILES_ITEMS[6].id, ID_FILES_REVEAL);
    ASSERT_EQUAL(CONTEXT_MENU_FILES_ITEMS[7].id, ID_FILES_DISCARD);
    PASS();
}

// 11. The compact application toolbar reuses menu command IDs; pages have no strips ─

static bool app_toolbar_has_action(int ident) {
    const toolbar_item_t *items = gitclient_application_toolbar.items;
    for (int i = 0; i < gitclient_application_toolbar.count; i++)
        if (items[i].type == TOOLBAR_ITEM_BUTTON && items[i].ident == ident)
            return true;
    return false;
}

void test_toolbar_buttons_reference_menu_command_ids(void) {
    TEST("gitclient Orion: compact chrome toolbar reuses menu command IDs");
    ASSERT_EQUAL(gitclient_main_window_form.role, WINDOW_ROLE_HOST);
    ASSERT_EQUAL(gitclient_changes_page_form.role, WINDOW_ROLE_PAGE);
    ASSERT_EQUAL(gitclient_history_page_form.role, WINDOW_ROLE_PAGE);
    ASSERT_EQUAL(gitclient_github_page_form.role, WINDOW_ROLE_PAGE);
    ASSERT_EQUAL(gitclient_main_window_form.toolbar_count, 0);
    ASSERT_EQUAL(gitclient_changes_page_form.toolbar_count, 0);
    ASSERT_EQUAL(gitclient_history_page_form.toolbar_count, 0);
    ASSERT_EQUAL(gitclient_github_page_form.toolbar_count, 0);
    ASSERT_EQUAL(gitclient_overview_page_form.toolbar_count, 0);
    ASSERT_EQUAL(gitclient_application_toolbar.presentation, TOOLBAR_PRESENTATION_COMPACT);
    ASSERT_TRUE(gitclient_application_toolbar.count > 0);
    ASSERT_NOT_NULL(gitclient_application_toolbar.items);
    ASSERT_TRUE(app_toolbar_has_action(ID_REMOTE_SYNC));
    ASSERT_TRUE(app_toolbar_has_action(ID_REPO_REFRESH));
    ASSERT_FALSE(app_toolbar_has_action(ID_VIEW_OVERVIEW));
    ASSERT_TRUE(app_toolbar_has_action(ID_REMOTE_PRUNE));
    ASSERT_FALSE(app_toolbar_has_action(ID_FILES_STAGE_ALL));
    const toolbar_item_t *items = gitclient_application_toolbar.items;
    for (int i = 0; i < gitclient_application_toolbar.count; i++) {
        if (items[i].type != TOOLBAR_ITEM_BUTTON) continue;
        ASSERT_TRUE(items[i].ident != 0);
        for (int j = i + 1; j < gitclient_application_toolbar.count; j++) {
            if (items[j].type != TOOLBAR_ITEM_BUTTON) continue;
            ASSERT_TRUE(items[i].ident != items[j].ident);
        }
    }
    PASS();
}

// 12. Action metadata and accelerators enumerate the menu-declared manifest ───

static bool gc_meta_has(uint16_t id) {
    for (int i = 0; i < gitclient_action_meta_count; i++)
        if (gitclient_action_meta[i].id == id) return true;
    return false;
}

static const accel_t *gc_find_accel(uint16_t cmd) {
    for (int i = 0; i < gitclient_default_accel_count; i++)
        if (gitclient_default_accels[i].cmd == cmd) return &gitclient_default_accels[i];
    return NULL;
}

void test_action_metadata_and_accelerators(void) {
    TEST("gitclient Orion: metadata + accels enumerate menu shortcuts");
    ASSERT_EQUAL(gitclient_action_meta_count, 74);

    for (int i = 0; i < gitclient_action_meta_count; i++) {
        ASSERT_TRUE(gitclient_action_meta[i].name[0] != '\0');
        ASSERT_TRUE(gitclient_action_meta[i].label[0] != '\0');
        ASSERT_TRUE(gitclient_action_meta[i].category[0] != '\0');
        ASSERT_TRUE(gitclient_action_meta[i].id >= ID_COMMAND_BASE);
    }

    // Every application-toolbar button references an action in the manifest.
    for (int i = 0; i < gitclient_application_toolbar.count; i++)
        if (gitclient_application_toolbar.items[i].type == TOOLBAR_ITEM_BUTTON)
            ASSERT_TRUE(gc_meta_has((uint16_t)gitclient_application_toolbar.items[i].ident));

    // Every context-menu entry that is not a separator references the manifest.
    for (int i = 0; i < CONTEXT_MENU_BRANCHES_COUNT; i++)
        if (CONTEXT_MENU_BRANCHES_ITEMS[i].id) ASSERT_TRUE(gc_meta_has((uint16_t)CONTEXT_MENU_BRANCHES_ITEMS[i].id));
    for (int i = 0; i < CONTEXT_MENU_FILES_COUNT; i++)
        if (CONTEXT_MENU_FILES_ITEMS[i].id) ASSERT_TRUE(gc_meta_has((uint16_t)CONTEXT_MENU_FILES_ITEMS[i].id));
    for (int i = 0; i < CONTEXT_MENU_STASH_COUNT; i++)
        if (CONTEXT_MENU_STASH_ITEMS[i].id) ASSERT_TRUE(gc_meta_has((uint16_t)CONTEXT_MENU_STASH_ITEMS[i].id));

    // Every accelerator targets an action that declares a hotkey, and the
    // hotkey-bearing actions match the accelerator table one-to-one.
    int hotkey_count = 0;
    for (int i = 0; i < gitclient_action_meta_count; i++)
        if (gitclient_action_meta[i].hotkey[0]) hotkey_count++;
    ASSERT_EQUAL(hotkey_count, gitclient_default_accel_count);
    for (int i = 0; i < gitclient_default_accel_count; i++)
        ASSERT_TRUE(gc_meta_has(gitclient_default_accels[i].cmd));

    // Spot-check the documented defaults: F5 → refresh, Ctrl+K → commit.
    const accel_t *refresh = gc_find_accel(ID_REPO_REFRESH);
    const accel_t *commit  = gc_find_accel(ID_COMMIT_COMMIT);
    ASSERT_NOT_NULL(refresh);
    ASSERT_NOT_NULL(commit);
    ASSERT_EQUAL(refresh->key, AX_KEY_F5);
    ASSERT_EQUAL(refresh->fVirt & FCONTROL, 0);
    ASSERT_EQUAL(commit->key, AX_KEY_K);
    ASSERT_TRUE((commit->fVirt & FCONTROL) != 0);
    PASS();
}

// 13. Every menu-declared action has a concrete dispatch handler -------------

void test_every_menu_action_has_handler(void) {
    TEST("gitclient actions: every menu declaration has a handler");
    for (int i = 0; i < gitclient_action_meta_count; i++) {
        const char *name = NULL;
        ASSERT_TRUE(gc_action_handler_for(gitclient_action_meta[i].id, &name));
        ASSERT_NOT_NULL(name);
        ASSERT_STR_EQUAL(name, gitclient_action_meta[i].name);
    }
    ASSERT_EQUAL(gc_action_handler_for(0, NULL), false);
    PASS();
}

// ── Entry point ───────────────────────────────────────────────────────────────

void test_reload_history_log_invalidates_diff_and_reports_failure(void) {
    TEST("gc_reload_history_log: swaps commits, drops diff cache; bad ref keeps log and fails");
    test_env_init();
    database_t *db = make_db();
    git_repo_t *repo = git_repo_open(s_repo);
    ASSERT_NOT_NULL(repo);
    send_db_message(db, dbLoad, 0, repo);
    gc_state_t saved = g_test_state;
    g_test_state.repo = repo;
    g_test_state.history_db = db;
    g_test_state.diff_cache_valid = true;

    ASSERT_TRUE(gc_reload_history_log("feature"));
    ASSERT_FALSE(g_test_state.diff_cache_valid);
    result_node_t *rows = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(ID_DB_COMMITS, 0), (void *)(intptr_t)0);
    ASSERT_EQUAL(result_count(rows), 3);
    free_result_list(rows);

    ASSERT_FALSE(gc_reload_history_log("no-such-ref"));
    rows = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(ID_DB_COMMITS, 0), (void *)(intptr_t)0);
    ASSERT_EQUAL(result_count(rows), 3);
    free_result_list(rows);

    g_test_state = saved;
    git_repo_close(repo); destroy_database(db); test_env_shutdown(); PASS();
}

static result_t overview_test_host(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win;
  if (msg == evCommand) return gc_overview_handle_command(wparam, lparam);
  return msg == evCreate || msg == evDestroy;
}

static int table_rows(window_t *list) {
  int n = 0;
  while (send_message(list, tvGetRecord, (uint32_t)n, NULL)) n++;
  return n;
}

void test_overview_table_filters_and_removes(void) {
  TEST("overview: one row per checkout with launch targets; filter and Remove from Workspace act on the table");
  test_env_init();
  gc_state_t saved = g_test_state;
  memset(&g_test_state, 0, sizeof(g_test_state));
  g_test_state.workspace_db = gc_workspace_db_create();
  ASSERT_NOT_NULL(g_test_state.workspace_db);
  window_t *host = test_env_create_window("overview", 0, 0, 640, 480, overview_test_host, NULL);
  g_test_state.main_win = host;
  g_test_state.tab = GC_TAB_OVERVIEW;
  g_test_state.overview_page_win = create_window_from_form(&gitclient_overview_page_form, 0, 0, host, gc_page_overview_proc, 0, NULL);
  ASSERT_NOT_NULL(g_test_state.checkouts_win);
  ASSERT_NOT_NULL(g_test_state.targets_win);

  char launch[700];
  snprintf(launch, sizeof(launch), "%s/" GC_LAUNCH_FILE, s_repo);
  ASSERT_TRUE(gct_write_file(launch,
    "<launch project=\"demo\">\n"
    "  <target name=\"Tests\" kind=\"test\" run=\"make test\" />\n"
    "  <target name=\"Game\" default=\"true\" run=\"make run LEVEL=${level}\">\n"
    "    <param name=\"level\" choices=\"3 4\" />\n"
    "  </target>\n"
    "</launch>\n"));
  snprintf(g_test_state.workspace[0], 512, "%s/missing-a", s_repo);
  snprintf(g_test_state.workspace[1], 512, "%s", s_repo);
  g_test_state.workspace_count = 2;
  gc_overview_refresh_sync();
  ASSERT_EQUAL(g_test_state.checkout_count, 2);
  ASSERT_EQUAL(table_rows(g_test_state.checkouts_win), 2);

  int repo_row = -1;
  for (int i = 0; i < 2; i++) {
    const db_checkout_t *row = (const db_checkout_t *)send_message(g_test_state.checkouts_win, tvGetRecord, (uint32_t)i, NULL);
    ASSERT_NOT_NULL(row);
    if (!strcmp(row->tone, "missing")) { ASSERT_STR_EQUAL(row->state, "Folder missing"); continue; }
    repo_row = i;
    ASSERT_STR_EQUAL(row->name, "Main checkout");
    ASSERT_STR_EQUAL(row->target, "Game");
  }
  ASSERT_TRUE(repo_row >= 0);
  send_message(g_test_state.checkouts_win, RVM_SETSELECTION, (uint32_t)repo_row, NULL);
  tableview_handle_master_selection(get_root_window(g_test_state.checkouts_win), g_test_state.checkouts_win);
  ASSERT_EQUAL(table_rows(g_test_state.targets_win), 2);
  const db_target_t *game = NULL;
  for (int i = 0; i < 2; i++) {
    const db_target_t *t = (const db_target_t *)send_message(g_test_state.targets_win, tvGetRecord, (uint32_t)i, NULL);
    if (t && !strcmp(t->name, "Game")) game = t;
  }
  ASSERT_NOT_NULL(game);
  ASSERT_STR_EQUAL(game->kind, "run");
  ASSERT_STR_EQUAL(game->command, "make run LEVEL=3");

  // Only the missing folder needs attention once the repository counts as published.
  for (int i = 0; i < g_test_state.checkout_count; i++)
    if (!g_test_state.checkouts[i].missing) g_test_state.checkouts[i].no_upstream = false, g_test_state.checkouts[i].untracked = 0;
  window_t *filter = g_test_state.filter_win;   // centred in its bar, hugging its labels
  ASSERT_EQUAL(2 * filter->frame.y + filter->frame.h, filter->parent->frame.h);
  ASSERT_TRUE(filter->frame.w < filter->parent->frame.w);
  send_message(g_test_state.filter_win, sgSetSelection, 1, NULL);
  ASSERT_TRUE(gc_overview_handle_command(MAKEDWORD(g_test_state.filter_win->id, sgnSelChange), g_test_state.filter_win));
  ASSERT_TRUE(g_test_state.attention_only);
  ASSERT_EQUAL(table_rows(g_test_state.checkouts_win), 1);
  send_message(g_test_state.checkouts_win, RVM_SETSELECTION, 0, NULL);
  ASSERT_TRUE(gc_overview_remove_selected());
  ASSERT_EQUAL(g_test_state.workspace_count, 1);
  ASSERT_STR_EQUAL(g_test_state.workspace[0], s_repo);
  ASSERT_TRUE(g_test_state.workspace_dirty);

  send_message(g_test_state.filter_win, sgSetSelection, 0, NULL);
  ASSERT_TRUE(gc_overview_handle_command(MAKEDWORD(g_test_state.filter_win->id, sgnSelChange), g_test_state.filter_win));
  ASSERT_FALSE(g_test_state.attention_only);
  ASSERT_EQUAL(table_rows(g_test_state.checkouts_win), 1);
  send_message(g_test_state.checkouts_win, RVM_SETSELECTION, 0, NULL);
  ASSERT_TRUE(gc_overview_remove_selected());
  ASSERT_EQUAL(g_test_state.workspace_count, 0);
  ASSERT_EQUAL(g_test_state.checkout_count, 0);
  ASSERT_EQUAL(table_rows(g_test_state.checkouts_win), 0);
  ASSERT_FALSE(gc_overview_remove_selected());
  ASSERT_TRUE(axPathExists(s_repo));
  remove(launch);
  test_env_shutdown();
  destroy_database(g_test_state.workspace_db);
  g_test_state = saved;
  PASS();
}

static const db_target_t *select_target(const char *name) {
  for (int i = 0; ; i++) {
    const db_target_t *t = (const db_target_t *)send_message(g_test_state.targets_win, tvGetRecord, (uint32_t)i, NULL);
    if (!t) return NULL;
    if (strcmp(t->name, name)) continue;
    send_message(g_test_state.targets_win, RVM_SETSELECTION, (uint32_t)i, NULL);
    return t;
  }
}

static bool click_target_icon(const char *name) {
  if (!select_target(name)) return false;
  int row = (int)send_message(g_test_state.targets_win, RVM_GETSELECTION, 0, NULL);
  return gc_overview_handle_command(MAKEDWORD(row, RVN_ICONCLICK), g_test_state.targets_win);
}

// Deliver the PTY watcher's readiness posts until the target is no longer running.
static bool wait_target_stopped(const char *name) {
  for (int i = 0; i < 3000; i++) {
    ui_event_t e;
    while (axPeekMessage(&e)) dispatch_message(&e);
    if (!gc_run_is_running(select_target(name)->checkout, name)) return true;
    usleep(1000);
  }
  return false;
}

void test_overview_runs_targets_in_terminal(void) {
  TEST("overview: a target runs in a hidden TerminalView; its icon stops it; a failure shows the terminal");
#ifdef _WIN32
  SKIP("TerminalView has no PTY on Windows");
#endif
  test_env_init();
  char plugin[1024];
  snprintf(plugin, sizeof(plugin), "%s/../lib/terminal_components%s", ui_get_exe_dir(), AX_DYNLIB_EXT);
  ASSERT_TRUE(fe_load_component_plugin(plugin));
  gc_state_t saved = g_test_state;
  memset(&g_test_state, 0, sizeof(g_test_state));
  g_test_state.workspace_db = gc_workspace_db_create();
  window_t *host = test_env_create_window("overview", 0, 0, 800, 600, overview_test_host, NULL);
  g_test_state.main_win = host;
  g_test_state.tab = GC_TAB_OVERVIEW;
  g_test_state.overview_page_win = create_window_from_form(&gitclient_overview_page_form, 0, 0, host, gc_page_overview_proc, 0, NULL);
  ASSERT_NOT_NULL(g_test_state.terminal_tabs_win);
  ASSERT_FALSE(window_has_state(g_test_state.terminal_tabs_win, WINDOW_STATE_VISIBLE));

  char launch[700];
  snprintf(launch, sizeof(launch), "%s/" GC_LAUNCH_FILE, s_repo);
  ASSERT_TRUE(gct_write_file(launch,
    "<launch project=\"demo\">\n"
    "  <target name=\"Serve\" run=\"pwd &gt; served.txt; sleep 30\" />\n"
    "  <target name=\"Broken\" kind=\"test\" run=\"exit 3\" />\n"
    "</launch>\n"));
  snprintf(g_test_state.workspace[0], 512, "%s", s_repo);
  g_test_state.workspace_count = 1;
  gc_overview_refresh_sync();
  send_message(g_test_state.checkouts_win, RVM_SETSELECTION, 0, NULL);
  tableview_handle_master_selection(get_root_window(g_test_state.checkouts_win), g_test_state.checkouts_win);

  // The icon starts the target in the checkout; the row turns into a stop icon.
  ASSERT_TRUE(click_target_icon("Serve"));
  ASSERT_TRUE(gc_run_is_running(select_target("Serve")->checkout, "Serve"));
  ASSERT_TRUE(select_target("Serve")->running);
  ASSERT_STR_EQUAL(g_test_state.run_button_win->title, "Stop");
  ASSERT_EQUAL(g_test_state.run_count, 1);
  ASSERT_EQUAL(g_test_state.runs[0].tab->parent, g_test_state.terminal_tabs_win);
  ASSERT_FALSE(g_test_state.terminal_shown);
  char served[700];
  snprintf(served, sizeof(served), "%s/served.txt", s_repo);
  for (int i = 0; i < 2000 && !axPathExists(served); i++) usleep(1000);
  ASSERT_TRUE(axPathExists(served));

  // Clicking the stop icon ends the process group quietly: the terminal stays hidden.
  ASSERT_TRUE(click_target_icon("Serve"));
  ASSERT_TRUE(wait_target_stopped("Serve"));
  ASSERT_FALSE(select_target("Serve")->running);
  ASSERT_STR_EQUAL(g_test_state.run_button_win->title, "Run");
  ASSERT_FALSE(g_test_state.terminal_shown);

  // A failing target opens the terminal on its own tab.
  ASSERT_TRUE(click_target_icon("Broken"));
  ASSERT_TRUE(wait_target_stopped("Broken"));
  ASSERT_EQUAL(g_test_state.run_count, 2);
  ASSERT_EQUAL(send_message(g_test_state.runs[1].term, termGetExitCode, 0, NULL), 3);
  ASSERT_TRUE(g_test_state.terminal_shown);
  ASSERT_TRUE(window_has_state(g_test_state.terminal_tabs_win, WINDOW_STATE_VISIBLE));
  ASSERT_EQUAL(send_message(g_test_state.terminal_tabs_win, tcGetSelection, 0, NULL), 1);

  // The Terminal button hides it again; running the same target reuses its tab.
  ASSERT_TRUE(gc_overview_handle_command(MAKEDWORD(g_test_state.terminal_button_win->id, btnClicked), g_test_state.terminal_button_win));
  ASSERT_FALSE(window_has_state(g_test_state.terminal_tabs_win, WINDOW_STATE_VISIBLE));
  ASSERT_TRUE(click_target_icon("Broken"));
  ASSERT_TRUE(wait_target_stopped("Broken"));
  ASSERT_EQUAL(g_test_state.run_count, 2);

  remove(served); remove(launch);
  destroy_window(host);
  fe_unload_component_plugins();
  test_env_shutdown();
  destroy_database(g_test_state.workspace_db);
  g_test_state = saved;
  PASS();
}

#ifndef _WIN32
static void agent_test_pump(void) {
#ifdef __APPLE__
  // macOS platform timers are scheduled on the native run loop.
  CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false);
#else
  axWaitMessage(10);
#endif
  ui_event_t event;
  while (axPeekMessage(&event)) dispatch_message(&event);
  repost_messages();
}

static bool agent_test_wait(gc_agent_phase_t phase) {
  for (int i = 0; i < 1200; i++) {
    int matching = 0;
    for (const gc_agent_task_t *task = gc_agents_tasks(); task; task = task->next)
      matching += task->phase == phase;
    if (matching == GC_AGENT_BATCH_SIZE) return true;
    agent_test_pump();
  }
  return false;
}
#endif

void test_agents_review_and_launch(void) {
  TEST("agents: three PTYs, isolated worktrees, per-task diffs and launch from the pinned review");
#ifdef _WIN32
  SKIP("TerminalView has no PTY on Windows");
#else
  static const char runner[] =
    "#!/bin/sh\n"
    "for arg do prompt=$arg; done\n"
    "case \"$prompt\" in\n"
    "  Alpha*) [ \"${0##*/}:$1:$2:$3:$4:$5\" = 'codex:-a:never:exec:-s:workspace-write' ] || exit 2; text=alpha ;;\n"
    "  Beta*) [ \"${0##*/}:$1:$2:$3\" = 'claude:--print:--permission-mode:acceptEdits' ] || exit 2; text=beta ;;\n"
    "  Gamma*) text=gamma ;;\n"
    "  *) exit 2 ;;\n"
    "esac\n"
    "printf '%s\\n' \"$text\" > app.txt\n"
    "if [ \"$text\" = 'alpha' ]; then printf 'new file\\n' > extra.txt; fi\n"
    "if [ \"$text\" = 'beta' ]; then git add app.txt && git commit -qm beta || exit 2; fi\n"
    "while [ ! -f \"$GC_AGENT_TEST_RELEASE\" ]; do sleep 0.05; done\n";

  char root[256], path[700], codex[700], claude[700], release[700], env[8192];
  ASSERT_TRUE(gct_make_temp_dir(root, sizeof(root), "orion_agents"));
  ASSERT_TRUE(gct_git(root, "init -b main"));
  ASSERT_TRUE(gct_git(root, "config user.email fixture@example.test"));
  ASSERT_TRUE(gct_git(root, "config user.name Fixture"));
  snprintf(path, sizeof(path), "%s/app.txt", root);
  ASSERT_TRUE(gct_write_file(path, "base\n"));
  snprintf(path, sizeof(path), "%s/.gitignore", root);
  ASSERT_TRUE(gct_write_file(path, "compiled.tmp\n"));
  snprintf(path, sizeof(path), "%s/launch.xml", root);
  ASSERT_TRUE(gct_write_file(path, "<launch><target name=\"Demo\" default=\"true\" build=\"cp app.txt compiled.tmp\" "
                                  "run=\"cat compiled.tmp &gt; ../launched.txt; git rev-parse HEAD &gt; ../revision.txt\" /></launch>\n"));
  ASSERT_TRUE(gct_git(root, "add . && git commit -qm base"));
  snprintf(codex, sizeof(codex), "%s/codex", root);
  snprintf(claude, sizeof(claude), "%s/claude", root);
  ASSERT_TRUE(gct_write_file(codex, runner) && chmod(codex, 0700) == 0);
  ASSERT_TRUE(gct_write_file(claude, runner) && chmod(claude, 0700) == 0);
  const char *previous_path = getenv("PATH");
  char *saved_path = previous_path ? strdup(previous_path) : NULL;
  snprintf(env, sizeof(env), "%s:%s", root, saved_path ? saved_path : "/usr/bin:/bin");
  setenv("PATH", env, 1);
  snprintf(release, sizeof(release), "%s/release", root);
  setenv("GC_AGENT_TEST_RELEASE", release, 1);

  test_env_init();
  ASSERT_TRUE(test_graphics_init(960, 640));
  gc_state_t saved = g_test_state;
  database_t *saved_db = ui_get_database();
  memset(&g_test_state, 0, sizeof(g_test_state));
  g_test_state.history_db = make_db();
  g_test_state.changes_db = make_changes_db();
  DB_CLASS(github_database_proc);
  g_test_state.github_db = create_database("gc-test-github", "github_database_proc", NULL);
  ui_set_database(g_test_state.history_db);
  register_database("db", g_test_state.history_db);
  register_database("github_db", g_test_state.github_db);
  g_test_state.workspace_db = gc_workspace_db_create();
  g_test_state.sync_loads = true;
  char plugin[1024];
  snprintf(plugin, sizeof(plugin), "%s/../lib/gitclient_components%s", ui_get_exe_dir(), AX_DYNLIB_EXT);
  ASSERT_TRUE(fe_load_component_plugin(plugin));
  snprintf(plugin, sizeof(plugin), "%s/../lib/terminal_components%s", ui_get_exe_dir(), AX_DYNLIB_EXT);
  ASSERT_TRUE(fe_load_component_plugin(plugin));
  window_t *host = create_window_from_form(&gitclient_main_window_form, 0, 0, NULL, gc_main_proc, 0, NULL);
  ASSERT_NOT_NULL(host);
  g_test_state.repo = git_repo_open(root);
  ASSERT_NOT_NULL(g_test_state.repo);
  gc_set_view_mode(GC_TAB_AGENTS);
  gc_agent_request_t requests[] = {
    {GC_AGENT_CODEX, "Alpha's change costs $5."},
    {GC_AGENT_CLAUDE, "Beta commits its change."},
    {GC_AGENT_CODEX, "Gamma edits the same file."}
  };
  ASSERT_TRUE(gc_agents_start(root, root, requests));
  ASSERT_TRUE(agent_test_wait(GC_AGENT_RUNNING));
  ASSERT_EQUAL(g_test_state.run_count, GC_AGENT_BATCH_SIZE);
  ASSERT_TRUE(gct_write_file(release, "ready\n"));
  ASSERT_TRUE(agent_test_wait(GC_AGENT_READY));

  window_t *page = g_test_state.agents_page_win;
  window_t *list = get_window_item(page, ID_AGENTS_PAGE_TASKS);
  window_t *diff = get_window_item(page, ID_AGENTS_PAGE_REVIEW_DIFF);
  window_t *run = get_window_item(page, ID_AGENTS_RUN_REVIEWED);
  ASSERT_NOT_NULL(list);
  ASSERT_NOT_NULL(diff);
  ASSERT_NOT_NULL(run);
  window_t *summary = get_window_item(page, ID_AGENTS_PAGE_SUMMARY), *badge = get_window_item(page, ID_AGENTS_PAGE_STATE);
  ASSERT_NOT_NULL(summary);
  ASSERT_NOT_NULL(badge);
  ASSERT_STR_EQUAL(summary->title, "0 in progress, 3 to review, 0 done");
  const gc_agent_task_t *first = gc_agents_tasks(), *task = first;
  const char *expected[] = {"alpha", "beta", "gamma"};
  const char *before = NULL;
  for (int i = 0; i < GC_AGENT_BATCH_SIZE; i++, task = task->next) {
    ASSERT_NOT_NULL(task);
    ASSERT_STR_EQUAL(task->base, first->base);
    ASSERT_TRUE(strcmp(task->base, task->revision) != 0);
    ASSERT_TRUE(!before || strcmp(before, task->checkout) != 0);
    before = task->checkout;
    send_message(list, RVM_SETSELECTION, i, NULL);
    send_message(host, evCommand, MAKEDWORD(list->id, RVN_SELCHANGE), list);
    ASSERT_STR_EQUAL(badge->title, "Ready for review");
    ASSERT_TRUE(window_has_state(badge, WINDOW_STATE_VISIBLE));
    gc_diff_state_t *st = diff->userdata;
    bool found = false;
    for (int j = 0; j < st->line_count; j++) found |= strstr(st->lines[j].text, expected[i]) != NULL;
    ASSERT_TRUE(found);
  }
  ASSERT_NULL(task);

  snprintf(path, sizeof(path), "%s/app.txt", first->checkout);
  ASSERT_TRUE(gct_write_file(path, "edited after review\n"));
  snprintf(path, sizeof(path), "%s/launch.xml", first->checkout);
  ASSERT_TRUE(gct_write_file(path, "<launch><target name=\"Wrong\" run=\"exit 4\"/></launch>"));
  send_message(list, RVM_SETSELECTION, 0, NULL);
  send_message(host, evCommand, MAKEDWORD(list->id, RVN_SELCHANGE), list);
  ASSERT_FALSE(window_has_state(run, WINDOW_STATE_DISABLED));
  send_message(host, evCommand, MAKEDWORD(ID_AGENTS_RUN_REVIEWED, btnClicked), run);

  snprintf(path, sizeof(path), "%s/../revision.txt", first->preview);
  for (int i = 0; i < 1200 && (!axPathExists(path) || gc_run_is_running(first->preview, "Demo")); i++) agent_test_pump();
  FILE *file = fopen(path, "r");
  ASSERT_NOT_NULL(file);
  char actual[80];
  ASSERT_NOT_NULL(fgets(actual, sizeof(actual), file));
  fclose(file);
  actual[strcspn(actual, "\r\n")] = 0;
  ASSERT_STR_EQUAL(actual, first->revision);
  snprintf(path, sizeof(path), "%s/../launched.txt", first->preview);
  file = fopen(path, "r");
  ASSERT_NOT_NULL(file);
  ASSERT_NOT_NULL(fgets(actual, sizeof(actual), file));
  fclose(file);
  ASSERT_STR_EQUAL(actual, "alpha\n");
  snprintf(path, sizeof(path), "%s/app.txt", root);
  file = fopen(path, "r");
  ASSERT_NOT_NULL(file);
  ASSERT_NOT_NULL(fgets(actual, sizeof(actual), file));
  fclose(file);
  ASSERT_STR_EQUAL(actual, "base\n");

  destroy_window(host);
  fe_unload_component_plugins();
  ui_set_database(saved_db);
  destroy_database(g_test_state.history_db);
  destroy_database(g_test_state.changes_db);
  destroy_database(g_test_state.github_db);
  destroy_database(g_test_state.workspace_db);
  test_graphics_shutdown();
  test_env_shutdown();
  g_test_state = saved;
  if (saved_path) setenv("PATH", saved_path, 1); else unsetenv("PATH");
  free(saved_path);
  unsetenv("GC_AGENT_TEST_RELEASE");
  gct_remove_dir(root);
#endif
  PASS();
}

static bool file_starts_with(const char *dir, const char *name, const char *text) {
  char path[600], line[256] = {0};
  snprintf(path, sizeof(path), "%s/%s", dir, name);
  FILE *f = fopen(path, "r");
  if (!f) return false;
  bool ok = fgets(line, sizeof(line), f) && !strncmp(line, text, strlen(text));
  fclose(f);
  return ok;
}

static bool git_rev(const char *dir, const char *rev, char *out, size_t n) {
  struct git_repo_s *repo = git_repo_open(dir);
  if (!repo) return false;
  const char *args[] = { "git", "rev-parse", rev, NULL };
  char buf[128] = {0};
  bool ok = git_run_sync(repo, args, buf, sizeof(buf));
  git_repo_close(repo);
  buf[strcspn(buf, "\r\n")] = '\0';
  snprintf(out, n, "%s", buf);
  return ok && buf[0];
}

// The painted row that shows a diff line (line_count: the row after the last hunk), or -1.
static int diff_row_of(window_t *diff, int line) {
  send_message(diff, evPaint, 0, NULL);   // lays the lines out in rows
  gc_diff_state_t *st = diff->userdata;
  for (int r = 0; r < st->row_count; r++) if (st->rows[r].line == line) return r;
  return -1;
}

void test_rebase_onto_main_resolve_continue_and_abort(void) {
  TEST("rebase: onto the default branch, stop on a conflict, resolve with mine, continue; abort restores; merge aborts");
  char root[256], path[600];
  ASSERT_TRUE(gct_make_temp_dir(root, sizeof(root), "orion_rebase"));
  ASSERT_TRUE(gct_git(root, "init -q -b main"));
  ASSERT_TRUE(gct_git(root, "config user.email fixture@example.test"));
  ASSERT_TRUE(gct_git(root, "config user.name Fixture"));
  snprintf(path, sizeof(path), "%s/app.txt", root);
  ASSERT_TRUE(gct_write_file(path, "base\n"));
  ASSERT_TRUE(gct_git(root, "add . && git commit -qm base"));
  ASSERT_TRUE(gct_git(root, "checkout -qb feature"));
  ASSERT_TRUE(gct_write_file(path, "feature\n"));
  ASSERT_TRUE(gct_git(root, "commit -qam 'Feature edits app'"));
  snprintf(path, sizeof(path), "%s/extra.txt", root);
  ASSERT_TRUE(gct_write_file(path, "extra\n"));
  ASSERT_TRUE(gct_git(root, "add . && git commit -qm 'Feature adds extra'"));
  ASSERT_TRUE(gct_git(root, "checkout -q main"));
  snprintf(path, sizeof(path), "%s/app.txt", root);
  ASSERT_TRUE(gct_write_file(path, "main\n"));
  ASSERT_TRUE(gct_git(root, "commit -qam 'Main edits app'"));
  ASSERT_TRUE(gct_git(root, "checkout -q feature"));
  char before[64], main_head[64];
  ASSERT_TRUE(git_rev(root, "HEAD", before, sizeof(before)));
  ASSERT_TRUE(git_rev(root, "main", main_head, sizeof(main_head)));

  test_env_init();
  ASSERT_TRUE(test_graphics_init(960, 640));
  gc_state_t saved = g_test_state;
  database_t *saved_db = ui_get_database();
  memset(&g_test_state, 0, sizeof(g_test_state));
  g_test_state.history_db = make_db();
  g_test_state.changes_db = make_changes_db();
  DB_CLASS(github_database_proc);
  g_test_state.github_db = create_database("gc-test-github", "github_database_proc", NULL);
  ui_set_database(g_test_state.history_db);
  register_database("db", g_test_state.history_db);
  register_database("github_db", g_test_state.github_db);
  g_test_state.workspace_db = gc_workspace_db_create();
  g_test_state.sync_loads = true;
  char plugin[1024];
  snprintf(plugin, sizeof(plugin), "%s/../lib/gitclient_components%s", ui_get_exe_dir(), AX_DYNLIB_EXT);
  ASSERT_TRUE(fe_load_component_plugin(plugin));
  window_t *host = create_window_from_form(&gitclient_main_window_form, 0, 0, NULL, gc_main_proc, 0, NULL);
  ASSERT_NOT_NULL(host);
  g_test_state.repo = git_repo_open(root);
  ASSERT_NOT_NULL(g_test_state.repo);
  gc_refresh_all_sync();
  window_t *bar = get_window_item(host, ID_CHANGES_PAGE_OPERATION_BAR);
  ASSERT_NOT_NULL(bar);
  ASSERT_FALSE(window_has_state(bar, WINDOW_STATE_VISIBLE));

  // Rebase onto main stops on the first commit, in conflict.
  gc_handle_command(ID_BRANCH_REBASE_DEFAULT);
  gc_refresh_all_sync();
  ASSERT_EQUAL(g_test_state.op.kind, GIT_OP_REBASE);
  ASSERT_STR_EQUAL(g_test_state.op.branch, "feature");
  ASSERT_STR_EQUAL(g_test_state.op.onto, "main");
  ASSERT_EQUAL(g_test_state.op.step, 1);
  ASSERT_EQUAL(g_test_state.op.steps, 2);
  ASSERT_STR_EQUAL(g_test_state.op.subject, "Feature edits app");
  ASSERT_EQUAL(g_test_state.tab, GC_TAB_CHANGES);
  ASSERT_TRUE(window_has_state(bar, WINDOW_STATE_VISIBLE));
  window_t *cont = get_window_item(bar, ID_BRANCH_CONTINUE);
  ASSERT_NOT_NULL(cont);
  ASSERT_TRUE(window_has_state(cont, WINDOW_STATE_DISABLED));
  ASSERT_STR_EQUAL(get_window_item(bar, ID_CHANGES_PAGE_OPERATION_STATE)->title, "1 conflict");
  ASSERT_STR_EQUAL(get_window_item(bar, ID_CHANGES_PAGE_OPERATION_TITLE)->title, "Rebasing feature onto main");

  // Markers left in: Mark Resolved refuses. "Mine" mid-rebase is the feature commit, git's --theirs.
  char err[700];
  ASSERT_FALSE(gc_conflict_mark_resolved("app.txt", err, sizeof(err)));
  ASSERT_TRUE(strstr(err, "conflict markers") != NULL);
  ASSERT_TRUE(gc_conflict_use_version("app.txt", true, err, sizeof(err)));
  ASSERT_TRUE(file_starts_with(root, "app.txt", "feature"));
  gc_refresh_all_sync();
  ASSERT_FALSE(window_has_state(cont, WINDOW_STATE_DISABLED));
  ASSERT_STR_EQUAL(get_window_item(bar, ID_CHANGES_PAGE_OPERATION_STATE)->title, "Ready to continue");

  // Continue replays the second commit cleanly and finishes on top of main.
  ASSERT_TRUE(gc_operation_continue(err, sizeof(err)));
  gc_refresh_all_sync();
  ASSERT_EQUAL(g_test_state.op.kind, GIT_OP_NONE);
  ASSERT_FALSE(window_has_state(bar, WINDOW_STATE_VISIBLE));
  char base[64];
  ASSERT_TRUE(git_rev(root, "HEAD~2", base, sizeof(base)));
  ASSERT_STR_EQUAL(base, main_head);
  ASSERT_TRUE(file_starts_with(root, "extra.txt", "extra"));

  // Abort puts the branch back exactly as it was.
  ASSERT_TRUE(gct_git(root, "reset -q --hard") && gct_git(root, "checkout -q -B feature2 main~1"));
  snprintf(path, sizeof(path), "%s/app.txt", root);
  ASSERT_TRUE(gct_write_file(path, "second\n"));
  ASSERT_TRUE(gct_git(root, "commit -qam 'Second edits app'"));
  ASSERT_TRUE(git_rev(root, "HEAD", before, sizeof(before)));
  gc_handle_command(ID_BRANCH_REBASE_DEFAULT);
  gc_refresh_all_sync();
  ASSERT_EQUAL(g_test_state.op.kind, GIT_OP_REBASE);
  ASSERT_TRUE(gc_operation_abort(err, sizeof(err)));
  gc_refresh_all_sync();
  ASSERT_EQUAL(g_test_state.op.kind, GIT_OP_NONE);
  char after[64];
  ASSERT_TRUE(git_rev(root, "HEAD", after, sizeof(after)));
  ASSERT_STR_EQUAL(after, before);

  // A merge conflict is a merge: Abort runs merge --abort, not rebase --abort.
  ASSERT_TRUE(gc_merge_branch("main", err, sizeof(err)) == false);
  gc_refresh_all_sync();
  ASSERT_EQUAL(g_test_state.op.kind, GIT_OP_MERGE);
  ASSERT_STR_EQUAL(g_test_state.op.onto, "main");
  ASSERT_TRUE(gc_operation_abort(err, sizeof(err)));
  gc_refresh_all_sync();
  ASSERT_EQUAL(g_test_state.op.kind, GIT_OP_NONE);
  ASSERT_TRUE(git_rev(root, "HEAD", after, sizeof(after)));
  ASSERT_STR_EQUAL(after, before);

  // Discard keeps the work in a stash: the tree is clean, and popping the stash brings it back.
  snprintf(path, sizeof(path), "%s/app.txt", root);
  ASSERT_TRUE(gct_write_file(path, "edited\n"));
  snprintf(path, sizeof(path), "%s/scratch.txt", root);
  ASSERT_TRUE(gct_write_file(path, "untracked\n"));
  ASSERT_TRUE(gc_discard_all());
  ASSERT_TRUE(file_starts_with(root, "app.txt", "second"));
  ASSERT_FALSE(file_starts_with(root, "scratch.txt", "untracked"));
  ASSERT_TRUE(gct_git(root, "stash list | grep -q 'Discarded changes'"));
  ASSERT_TRUE(gct_git(root, "stash pop -q"));
  ASSERT_TRUE(file_starts_with(root, "app.txt", "edited"));
  ASSERT_TRUE(file_starts_with(root, "scratch.txt", "untracked"));
  ASSERT_TRUE(gct_git(root, "stash push -q --include-untracked"));   // leave the tree clean for the next step

  // On the default branch itself there is nothing to rebase onto.
  ASSERT_TRUE(gct_git(root, "checkout -q main"));
  char onto[96];
  ASSERT_FALSE(gc_rebase_onto_default(onto, sizeof(onto), err, sizeof(err)));
  ASSERT_TRUE(strstr(err, "You are on main") != NULL);

  // The diff shows every change since HEAD; a click in the gutter puts one line into the index.
  snprintf(path, sizeof(path), "%s/lines.txt", root);
  char text[512] = "", *cur = text;
  for (int i = 1; i <= 30; i++) cur += i == 2 ? sprintf(cur, "two\n") : i == 13 ? sprintf(cur, "thirteen\n") : sprintf(cur, "%d\n", i);
  ASSERT_TRUE(gct_write_file(path, text));
  ASSERT_TRUE(gct_git(root, "add lines.txt && git commit -qm lines"));
  cur = text;
  for (int i = 1; i <= 30; i++) cur += i == 2 ? sprintf(cur, "Two!\n") : i == 13 ? sprintf(cur, "THIRTEEN\n") : sprintf(cur, "%d\n", i);
  ASSERT_TRUE(gct_write_file(path, text));
  gc_set_view_mode(GC_TAB_CHANGES);
  gc_refresh_all_sync();
  window_t *changes_files = g_test_state.changes_files_win, *diff = g_test_state.changes_diff_win;
  send_message(changes_files, RVM_SETSELECTION, 0, NULL);
  send_message(host, evCommand, MAKEDWORD(0, RVN_SELCHANGE), changes_files);
  gc_diff_invalidate(); gc_diff_refresh();
  gc_diff_state_t *st = diff->userdata;
  ASSERT_STR_EQUAL(st->hunk_path, "lines.txt");
  ASSERT_EQUAL(st->hunk_count, 2);   // lines 1-5 and 10-16; 6-9 and 17-30 hidden
  ASSERT_EQUAL(st->source_count, 30);
  int del = -1, add = -1, selectable = 0;
  for (int i = 0; i < st->line_count; i++) {
    selectable += st->lines[i].selectable && !st->lines[i].included;
    if (st->lines[i].kind == GC_DIFF_DEL && del < 0) del = i;
    if (st->lines[i].kind == GC_DIFF_ADD && add < 0) add = i;
  }
  ASSERT_EQUAL(selectable, 4);
  ASSERT_STR_EQUAL(st->lines[del].text, "two"); ASSERT_EQUAL(st->lines[del].old_no, 2); ASSERT_EQUAL(st->lines[del].new_no, 0);
  ASSERT_STR_EQUAL(st->lines[add].text, "Two!"); ASSERT_EQUAL(st->lines[add].new_no, 2);
  ASSERT_EQUAL(st->lines[add].inner_start, 0); ASSERT_EQUAL(st->lines[add].inner_len, 4);   // no common prefix or suffix
  ASSERT_EQUAL(st->lines[del].inner_start, 0); ASSERT_EQUAL(st->lines[del].inner_len, 3);

  // Expanding: the short gap between the hunks opens at once and joins them; the end opens 20 lines at a time.
  irect16_t content = window_scroll_content_rect(diff);
  int row_h = text_line_height("DiffView");
  #define ROW_OF(line_index) diff_row_of(diff, (line_index))
  #define AT(x, row) MAKEDWORD((x), content.y + (row) * row_h + row_h / 2)
  #define CLICK(x, row) (send_message(diff, evLeftButtonDown, AT(x, row), NULL), send_message(diff, evLeftButtonUp, AT(x, row), NULL))
  CLICK(content.x + 2, ROW_OF(st->hunk_offsets[1]));
  ASSERT_EQUAL(st->hunk_count, 1);
  int seven = -1;
  for (int i = 0; i < st->line_count; i++) if (st->lines[i].new_no == 7) seven = i;
  ASSERT_TRUE(seven >= 0); ASSERT_EQUAL(st->lines[seven].kind, GC_DIFF_CONTEXT); ASSERT_EQUAL(st->lines[seven].old_no, 7);
  ASSERT_STR_EQUAL(st->lines[seven].text, "7");
  CLICK(content.x + 2, ROW_OF(st->line_count));   // the row after the last hunk
  ASSERT_EQUAL(st->lines[st->line_count - 1].new_no, 30);
  ASSERT_EQUAL(ROW_OF(st->line_count), -1);         // nothing left below

  CLICK(content.x + 2, ROW_OF(add));
  ASSERT_TRUE(gct_git(root, "diff --cached | grep -qx '+Two!'"));
  ASSERT_FALSE(gct_git(root, "diff --cached | grep -qx -- '-two'"));   // the removal stays out
  ASSERT_FALSE(gct_git(root, "diff --cached | grep -q THIRTEEN"));
  st = diff->userdata;
  int included = 0;
  for (int i = 0; i < st->line_count; i++) included += st->lines[i].included;
  ASSERT_EQUAL(included, 1);                        // reloaded from git: the index has exactly that line
  ASSERT_EQUAL(st->hunk_count, 1);                  // and the expanded lines stay open
  ASSERT_EQUAL(st->lines[st->line_count - 1].new_no, 30);

  // A click on the header's text takes the whole hunk; Enter on the current hunk toggles it back.
  CLICK(content.x + content.w / 2, ROW_OF(st->hunk_offsets[0]));   // the band's text, clear of the scrollbar
  ASSERT_TRUE(gct_git(root, "diff --cached | grep -qx '+THIRTEEN'"));
  ASSERT_TRUE(gct_git(root, "diff --cached | grep -qx -- '-two'"));
  ASSERT_TRUE(gct_git(root, "diff --quiet"));   // the whole file is staged
  st = diff->userdata;
  st->current_hunk = 0;
  send_message(diff, evKeyDown, AX_KEY_ENTER, NULL);
  ASSERT_TRUE(gct_git(root, "diff --cached --quiet"));

  // A drag down the gutter takes every line it crosses, and the index is written once, on release.
  int first_del = -1, last_add = -1;
  for (int i = 0; i < st->line_count; i++) {
    if (st->lines[i].kind == GC_DIFF_DEL && first_del < 0) first_del = i;
    if (st->lines[i].kind == GC_DIFF_ADD) last_add = i;
  }
  send_message(diff, evLeftButtonDown, AT(content.x + 2, ROW_OF(first_del)), NULL);
  send_message(diff, evMouseMove, AT(content.x + 2, ROW_OF(last_add)), NULL);
  ASSERT_TRUE(gct_git(root, "diff --cached --quiet"));   // nothing written mid-drag
  send_message(diff, evLeftButtonUp, AT(content.x + 2, ROW_OF(last_add)), NULL);
  ASSERT_TRUE(gct_git(root, "diff --quiet"));           // all four lines staged
  st = diff->userdata;

  // Side by side, a removed line and the added line that replaced it share a row;
  // the new half's gutter takes only the addition out.
  gc_diff_set_split(true);
  ASSERT_TRUE(st->split);
  send_message(diff, evPaint, 0, NULL);
  int pair_row = -1;
  for (int r = 0; r < st->row_count; r++)
    if (st->rows[r].split && st->rows[r].line >= 0 && !strcmp(st->lines[st->rows[r].line].text, "two") &&
        st->rows[r].right.line >= 0 && !strcmp(st->lines[st->rows[r].right.line].text, "Two!")) pair_row = r;
  ASSERT_TRUE(pair_row >= 0);
  CLICK(content.x + content.w / 2 + 2, pair_row);
  ASSERT_FALSE(gct_git(root, "diff --cached | grep -qx '+Two!'"));
  ASSERT_TRUE(gct_git(root, "diff --cached | grep -qx -- '-two'"));
  gc_diff_set_split(false);
  #undef ROW_OF
  #undef CLICK
  #undef AT
  ASSERT_TRUE(gct_git(root, "reset -q --hard"));

  destroy_window(host);
  fe_unload_component_plugins();
  ui_set_database(saved_db);
  destroy_database(g_test_state.history_db);
  destroy_database(g_test_state.changes_db);
  destroy_database(g_test_state.github_db);
  destroy_database(g_test_state.workspace_db);
  test_graphics_shutdown();
  test_env_shutdown();
  g_test_state = saved;
  gct_remove_dir(root);
  PASS();
}

void test_activitybar_navigation(void) {
  TEST("activity bar: clicks and View commands select one page, preserve state and resize beside the rail");
  test_env_init();
  ASSERT_TRUE(test_graphics_init(960, 640));
  gc_state_t saved = g_test_state;
  database_t *saved_db = ui_get_database();
  memset(&g_test_state, 0, sizeof(g_test_state));
  g_test_state.history_db = make_db();
  g_test_state.changes_db = make_changes_db();
  DB_CLASS(github_database_proc);
  g_test_state.github_db = create_database("gc-test-github", "github_database_proc", NULL);
  ui_set_database(g_test_state.history_db);
  register_database("db", g_test_state.history_db);
  register_database("github_db", g_test_state.github_db);
  g_test_state.workspace_db = gc_workspace_db_create();
  char plugin[4096];
  snprintf(plugin, sizeof(plugin), "%s/../lib/gitclient_components%s", ui_get_exe_dir(), AX_DYNLIB_EXT);
  ASSERT_TRUE(fe_load_component_plugin(plugin));
  window_t *host = create_window_from_form(&gitclient_main_window_form, 0, 0, NULL, gc_main_proc, 0, NULL);
  ASSERT_NOT_NULL(host);
  window_t *bar = g_test_state.activity_win;
  ASSERT_NOT_NULL(bar);
  toolbar_state_t *tb = toolbar_get_state(bar);
  ASSERT_EQUAL(tb->orientation, TOOLBAR_VERTICAL);
  ASSERT_EQUAL(tb->btn_size, 0);
  ASSERT_EQUAL(toolbar_effective_bsz(bar), ui_px("Toolbar::button", "height"));
  ASSERT_EQUAL(tb->item_count, 5);
  ASSERT_STR_EQUAL(tb->items[0].checked_icon, "gc-nav-overview-fill");
  ASSERT_STR_EQUAL(tb->items[1].checked_icon, "gc-nav-changes-fill");
  ASSERT_STR_EQUAL(tb->items[2].checked_icon, "gc-nav-history-fill");
  ASSERT_STR_EQUAL(tb->items[3].checked_icon, "gc-nav-github-fill");
  ASSERT_STR_EQUAL(tb->items[4].checked_icon, "gc-nav-agents-fill");
  ASSERT_EQUAL(bar->frame.w, toolbar_effective_bsz(bar) + 2 * toolbar_effective_padding(bar));
  ASSERT_EQUAL(g_test_state.tab, GC_TAB_CHANGES);
  window_t *summary = get_window_item(host, ID_CHANGES_PAGE_COMMIT_SUMMARY);
  send_message(summary, edSetText, 0, "Keep this draft");
  set_focus(summary);
  window_t *pages[] = {g_test_state.overview_page_win, g_test_state.changes_page_win,
                      g_test_state.history_page_win, g_test_state.github_page_win, g_test_state.agents_page_win};
  const uint16_t commands[] = {ID_VIEW_OVERVIEW, ID_VIEW_CHANGES, ID_VIEW_HISTORY, ID_VIEW_GITHUB, ID_VIEW_AGENTS};
  for (int view = 0; view < ARRAY_LEN(pages); view++) {
    irect16_t item;
    ASSERT_TRUE(send_message(bar, tbGetItemRect, commands[view], &item));
    uint32_t point = MAKEDWORD(item.x + item.w / 2, item.y + item.h / 2);
    send_message(bar->toolbar, evLeftButtonDown, point, NULL);
    send_message(bar->toolbar, evLeftButtonUp, point, NULL);
    ASSERT_EQUAL(g_test_state.tab, view);
    ASSERT_EQUAL(window_active_page(host), pages[view]);
    for (int i = 0; i < ARRAY_LEN(pages); i++) {
      ASSERT_EQUAL(window_has_state(pages[i], WINDOW_STATE_VISIBLE), i == view);
      ASSERT_EQUAL(toolbar_is_button_checked(bar, commands[i]), i == view);
    }
    ASSERT_EQUAL(pages[view]->frame.x, bar->frame.w);
    ASSERT_EQUAL(pages[view]->frame.w, get_client_rect(host).w - bar->frame.w);
    if (view == GC_TAB_CHANGES) ASSERT_EQUAL(g_ui_runtime.focused, summary);
  }
  send_message(host, evCommand, MAKEDWORD(ID_VIEW_CHANGES, kMenuBarNotificationItemClick), NULL);
  ASSERT_EQUAL(g_test_state.tab, GC_TAB_CHANGES);
  ASSERT_TRUE(toolbar_is_button_checked(bar, ID_VIEW_CHANGES));
  ASSERT_EQUAL(g_ui_runtime.focused, summary);
  send_message(host, evCommand, MAKEDWORD(ID_VIEW_HISTORY, kAcceleratorNotification), NULL);
  ASSERT_EQUAL(g_test_state.tab, GC_TAB_HISTORY);
  ASSERT_TRUE(toolbar_is_button_checked(bar, ID_VIEW_HISTORY));
  gc_set_view_mode(GC_TAB_CHANGES);
  ASSERT_EQUAL(g_ui_runtime.focused, summary);
  char draft[64];
  send_message(summary, edGetText, sizeof(draft), draft);
  ASSERT_STR_EQUAL(draft, "Keep this draft");
  host->frame.w = 420; host->frame.h = 700;
  send_message(host, evResize, 0, NULL);
  ASSERT_EQUAL(bar->dock->side, DOCK_BOTTOM);
  ASSERT_EQUAL(tb->orientation, TOOLBAR_HORIZONTAL);
  for (int view = 0; view < ARRAY_LEN(pages); view++) {
    irect16_t item;
    ASSERT_TRUE(send_message(bar, tbGetItemRect, commands[view], &item));
    uint32_t point = MAKEDWORD(item.x + item.w / 2, item.y + item.h / 2);
    send_message(bar->toolbar, evLeftButtonDown, point, NULL);
    send_message(bar->toolbar, evLeftButtonUp, point, NULL);
    ASSERT_EQUAL(window_active_page(host), pages[view]);
    ASSERT_EQUAL(pages[view]->frame.w, get_client_rect(host).w);
    ASSERT_EQUAL(pages[view]->frame.h, get_client_rect(host).h - bar->frame.h);
  }
  gc_set_view_mode(GC_TAB_CHANGES);
  ASSERT_EQUAL(g_ui_runtime.focused, summary);
  send_message(summary, edGetText, sizeof(draft), draft);
  ASSERT_STR_EQUAL(draft, "Keep this draft");
  host->frame.w = 600; host->frame.h = 400;
  send_message(host, evResize, 0, NULL);
  ASSERT_EQUAL(bar->frame.w, TB_SPACING + 2 * toolbar_effective_padding(bar));
  ASSERT_EQUAL(pages[GC_TAB_CHANGES]->frame.w, get_client_rect(host).w - bar->frame.w);
  ASSERT_EQUAL(pages[GC_TAB_CHANGES]->frame.h, get_client_rect(host).h);
  destroy_window(host);
  destroy_database(g_test_state.history_db);
  destroy_database(g_test_state.changes_db);
  destroy_database(g_test_state.github_db);
  destroy_database(g_test_state.workspace_db);
  ui_set_database(saved_db);
  test_env_shutdown();
  test_graphics_shutdown();
  g_test_state = saved;
  PASS();
}

static void gc_drag_view(window_t *source, int index, window_t *target, ipoint16_t point, bool release) {
  irect16_t tab; send_message(source, tcGetTabRect, index, &tab);
  ipoint16_t start = window_map_point(source, NULL, (ipoint16_t){tab.x + 8, 8});
  ipoint16_t end = window_map_point(target, NULL, point);
  ui_event_t event = {.message = kEventLeftButtonDown, .x = start.x * UI_WINDOW_SCALE, .y = start.y * UI_WINDOW_SCALE};
  dispatch_message(&event);
  event.message = kEventMouseMoved; event.x = end.x * UI_WINDOW_SCALE; event.y = end.y * UI_WINDOW_SCALE;
  dispatch_message(&event);
  if (release) { event.message = kEventLeftButtonUp; dispatch_message(&event); }
}

static bool history_diff_contains(const char *text) {
  gc_diff_state_t *st = g_test_state.history_diff_win->userdata;
  for (int i = 0; i < st->line_count; i++)
    if (strstr(st->lines[i].text, text)) return true;
  return false;
}

static bool changes_db_has(const char *path) {
  result_node_t *rows = (result_node_t *)send_db_message(g_test_state.changes_db, dbFetch, MAKEDWORD(ID_DB_FILES, 0), (void *)(intptr_t)0);
  bool found = false;
  for (result_node_t *n = rows; n; n = n->next) if (!strcmp((*(db_file_t **)n->data)->path, path)) found = true;
  free_result_list(rows);
  return found;
}

static git_repo_snapshot_t *bogus_snapshot(unsigned generation, const char *path) {
  static char open_path[512];
  if (g_test_state.repo) snprintf(open_path, sizeof(open_path), "%s", git_repo_path(g_test_state.repo));
  git_repo_snapshot_t *snap = git_repo_snapshot_read(open_path, GIT_SNAP_ALL, generation);
  if (!snap) return NULL;
  snprintf(snap->files[snap->file_count].path, sizeof(snap->files[0].path), "%s", path);
  snap->files[snap->file_count++].status = 'M';
  return snap;
}

static void test_history_latest_commit_diff(void) {
  TEST("history: branch reload and refresh keep the latest commit diff selected");
  test_env_init();
  ASSERT_TRUE(test_graphics_init(1200, 800));
  gc_state_t saved = g_test_state;
  database_t *saved_db = ui_get_database();
  memset(&g_test_state, 0, sizeof(g_test_state));
  g_test_state.ephemeral = true; g_test_state.sync_loads = true;
  g_test_state.history_db = make_db(); g_test_state.changes_db = make_changes_db();
  DB_CLASS(github_database_proc);
  g_test_state.github_db = create_database("gc-history-github", "github_database_proc", NULL);
  ui_set_database(g_test_state.history_db);
  register_database("db", g_test_state.history_db); register_database("github_db", g_test_state.github_db);
  g_test_state.workspace_db = gc_workspace_db_create();
  char plugin[4096];
  snprintf(plugin, sizeof(plugin), "%s/../lib/gitclient_components%s", ui_get_exe_dir(), AX_DYNLIB_EXT);
  ASSERT_TRUE(fe_load_component_plugin(plugin));
  window_t *root = create_window_from_form(&gitclient_main_window_form, 0, 0, NULL, gc_main_proc, 0, NULL);
  ASSERT_NOT_NULL(root);
  ASSERT_TRUE(send_message(root, evDropFile, 0, s_repo));
  ASSERT_EQUAL(g_test_state.workspace_count, 1);
  ASSERT_NOT_NULL(g_test_state.repo);
  ASSERT_TRUE(send_message(root, evDropFile, 0, s_repo));
  ASSERT_EQUAL(g_test_state.workspace_count, 1);
  gc_set_view_mode(GC_TAB_HISTORY);
  window_t *branches = g_test_state.branches_win, *log = g_test_state.log_win;
  window_t *files = g_test_state.history_files_win;
  int branch = (int)send_message(branches, RVM_GETSELECTION, 0, NULL);
  send_message(root, evCommand, MAKEDWORD(branch, RVN_SELCHANGE), branches);
  tableview_handle_master_selection(root, branches);
  ASSERT_EQUAL(g_test_state.selected_commit, 0);
  ASSERT_TRUE(history_diff_contains("file2.txt"));
  ASSERT_TRUE(history_diff_contains("world"));

  send_message(files, RVM_SETSELECTION, 0, NULL);
  send_message(root, evCommand, MAKEDWORD(0, RVN_SELCHANGE), files);
  ASSERT_TRUE(history_diff_contains("world"));
  send_message(log, RVM_SETSELECTION, 1, NULL);
  send_message(root, evCommand, MAKEDWORD(1, RVN_SELCHANGE), log);
  tableview_handle_master_selection(root, log);
  ASSERT_TRUE(history_diff_contains("hello"));
  send_message(log, RVM_SETSELECTION, 0, NULL);
  send_message(root, evCommand, MAKEDWORD(0, RVN_SELCHANGE), log);
  tableview_handle_master_selection(root, log);
  ASSERT_TRUE(history_diff_contains("world"));

  ASSERT_TRUE(gc_reload_history_log("main"));
  ASSERT_EQUAL(g_test_state.selected_commit, 0);
  ASSERT_TRUE(history_diff_contains("world"));
  gc_refresh_all();
  ASSERT_EQUAL(g_test_state.selected_commit, 0);
  ASSERT_TRUE(history_diff_contains("world"));
  gc_set_view_mode(GC_TAB_CHANGES);
  ASSERT_EQUAL(g_test_state.selected_commit, -1);
  gc_refresh_all();
  ASSERT_EQUAL(g_test_state.selected_commit, -1);
  gc_set_view_mode(GC_TAB_HISTORY);
  ASSERT_EQUAL(g_test_state.selected_commit, 0);
  ASSERT_TRUE(history_diff_contains("world"));

  // The worker's result is applied only for the repository that is still open.
  g_test_state.repo_loading = true;
  git_repo_snapshot_t *snap = bogus_snapshot(g_test_state.repo_generation, "applied.txt");
  ASSERT_NOT_NULL(snap);
  gc_repo_loaded(snap);
  ASSERT_FALSE(g_test_state.repo_loading);
  ASSERT_TRUE(changes_db_has("applied.txt"));
  git_repo_t *open_repo = g_test_state.repo;
  g_test_state.repo = NULL; g_test_state.repo_loading = true;
  snap = bogus_snapshot(g_test_state.repo_generation, "closed.txt");
  gc_repo_loaded(snap);
  g_test_state.repo = open_repo;
  ASSERT_FALSE(g_test_state.repo_loading);
  ASSERT_FALSE(changes_db_has("closed.txt"));
  g_test_state.repo_loading = true;
  snap = bogus_snapshot(g_test_state.repo_generation - 1, "older.txt");
  gc_repo_loaded(snap);   // another repository was opened meanwhile: dropped, then the open one is read again
  ASSERT_FALSE(changes_db_has("older.txt"));
  ASSERT_FALSE(changes_db_has("applied.txt"));

  destroy_window(root);
  destroy_database(g_test_state.history_db); destroy_database(g_test_state.changes_db); destroy_database(g_test_state.github_db);
  destroy_database(g_test_state.workspace_db);
  ui_set_database(saved_db); test_env_shutdown(); test_graphics_shutdown(); g_test_state = saved;
  PASS();
}

static void test_gitclient_pane_layout(void) {
  TEST("Git Client pane docking preserves defaults, live views, bindings and drafts");
  test_env_init();
  ASSERT_TRUE(test_graphics_init(1200, 800));
  gc_state_t saved = g_test_state;
  database_t *saved_db = ui_get_database();
  memset(&g_test_state, 0, sizeof(g_test_state));
  g_test_state.ephemeral = true; g_test_state.sync_loads = true;
  g_test_state.history_db = make_db(); g_test_state.changes_db = make_changes_db();
  DB_CLASS(github_database_proc);
  g_test_state.github_db = create_database("gc-pane-github", "github_database_proc", NULL);
  ui_set_database(g_test_state.history_db);
  register_database("db", g_test_state.history_db); register_database("github_db", g_test_state.github_db);
  g_test_state.workspace_db = gc_workspace_db_create();
  char plugin[4096];
  snprintf(plugin, sizeof(plugin), "%s/../lib/gitclient_components%s", ui_get_exe_dir(), AX_DYNLIB_EXT);
  ASSERT_TRUE(fe_load_component_plugin(plugin));
  window_t *root = create_window_from_form(&gitclient_main_window_form, 0, 0, NULL, gc_main_proc, 0, NULL);
  ASSERT_NOT_NULL(root);
  root->frame.w = 1200; root->frame.h = 780; send_message(root, evResize, 0, NULL);
  gc_open_repo(s_repo);
  window_t *changes = g_test_state.changes_page_win;
  window_t *history = g_test_state.history_page_win;
  window_t *github = g_test_state.github_page_win;
  ASSERT_EQUAL(page_pane_count(g_test_state.overview_page_win), 3);   // checkouts, targets, target terminal
  ASSERT_EQUAL(page_pane_count(changes), 2); ASSERT_EQUAL(page_pane_count(history), 7); ASSERT_EQUAL(page_pane_count(github), 2);
  window_t *pages[] = {g_test_state.overview_page_win, changes, history, github};
  for (int p = 0; p < ARRAY_LEN(pages); p++) {
    for (int i = 0; i < page_pane_count(pages[p]); i++) {
      ASSERT_TRUE(send_message(page_pane_at(pages[p], i), tcGetStyle, 0, NULL) & TAB_STYLE_HIDE_SINGLE_TAB);
      window_t *bar = (window_t *)send_message(page_pane_at(pages[p], i), tcGetToolbar, 0, NULL);
      ASSERT_NOT_NULL(bar);
      ASSERT_EQUAL(toolbar_get_state(bar)->item_count, 1);
      ASSERT_EQUAL(toolbar_get_state(bar)->items[0].ident, ID_VIEW_CLOSE_TAB);
    }
  }
  window_t *left = page_pane_at(changes, 0), *right = page_pane_at(changes, 1);
  ASSERT_EQUAL(left->parent->id, ID_CHANGES_PAGE_CHANGES_SPLIT);
  ASSERT_EQUAL(splitview_get_left(left->parent), left);
  int margin = ui_px("TabView::pane", "--pane-margin");
  int left_width = left->frame.w + 2 * margin, right_width = right->frame.w + 2 * margin;
  ASSERT_TRUE(abs(left_width * 10 - (left_width + right_width) * 3) <= 10);
  window_t *panel = get_window_item(root, ID_CHANGES_PAGE_CHANGES_PANEL);
  window_t *summary = get_window_item(root, ID_CHANGES_PAGE_COMMIT_SUMMARY);
  window_t *files = g_test_state.changes_files_win, *diff = g_test_state.changes_diff_win;
  void *diff_state = diff->userdata, *file_state = files->userdata;
  send_message(summary, edSetText, 0, "Draft survives docking"); set_focus(summary);
  gc_drag_view(right, 0, left, (ipoint16_t){left->frame.w / 2, left->frame.h / 2}, true);
  ASSERT_EQUAL(page_pane_count(changes), 1);
  ASSERT_EQUAL(diff->parent, left); ASSERT_EQUAL(diff->userdata, diff_state); ASSERT_EQUAL(files->userdata, file_state);
  ASSERT_EQUAL(send_message(left, tcGetCount, 0, NULL), 2);
  send_message(root, evCommand, MAKEDWORD(ID_VIEW_NEXT_TAB, kAcceleratorNotification), NULL);
  ASSERT_EQUAL(g_ui_runtime.focused, summary);
  char draft[64]; send_message(summary, edGetText, sizeof(draft), draft);
  ASSERT_STR_EQUAL(draft, "Draft survives docking");
  gc_drag_view(left, 1, left, (ipoint16_t){left->frame.w / 2, left->frame.h - 4}, false);
  pane_drop_t preview;
  ASSERT_TRUE(page_get_drop_preview(changes, &preview)); ASSERT_EQUAL(preview.edge, PANE_BOTTOM);
  send_message(changes, evLeftButtonUp, MAKEDWORD(preview.rect.x + preview.rect.w / 2, preview.rect.y + preview.rect.h - 4), NULL);
  ASSERT_EQUAL(page_pane_count(changes), 2);
  ASSERT_EQUAL(panel->parent, left); ASSERT_NOT_EQUAL(diff->parent, left);
  ASSERT_EQUAL(diff->userdata, diff_state);
  set_focus(summary); gc_set_view_mode(GC_TAB_HISTORY); gc_set_view_mode(GC_TAB_CHANGES);
  ASSERT_EQUAL(g_ui_runtime.focused, summary);
  ASSERT_EQUAL(page_pane_count(changes), 2);
  send_message(summary, edSetText, 0, "Draft remains editable");
  send_message(root, evCommand, MAKEDWORD(summary->id, ednChange), summary);
  ASSERT_EQUAL(get_window_item(root, ID_CHANGES_PAGE_COMMIT_SUMMARY), summary);

  gc_set_view_mode(GC_TAB_HISTORY);
  window_t *log = g_test_state.log_win, *history_files = g_test_state.history_files_win;
  window_t *log_pane = log->parent, *files_pane = history_files->parent;
  ASSERT_EQUAL(log_pane->parent->id, ID_HISTORY_PAGE_LOG_FILES_SPLIT);
  ASSERT_EQUAL(files_pane->parent, log_pane->parent);
  ASSERT_TRUE(log_pane->frame.y < files_pane->frame.y);
  window_t *stash_pane = g_test_state.stash_win->parent;
  bool has_stash = send_message(g_test_state.stash_win, tvGetRecord, 0, NULL) != 0;
  ASSERT_EQUAL(window_has_state(stash_pane, WINDOW_STATE_VISIBLE), has_stash);
  ASSERT_TRUE(page_move_tab(history, history_files, log_pane, PANE_CENTER, -1));
  ASSERT_EQUAL(page_pane_count(history), 6);
  ASSERT_EQUAL(g_test_state.history_files_win, history_files);
  send_message(log_pane, tcSetSelection, 0, NULL);
  send_message(log, RVM_SETSELECTION, 0, NULL);
  tableview_handle_master_selection(root, log);
  ASSERT_NOT_NULL((void *)send_message(history_files, tvGetRecord, 0, NULL));
  ASSERT_EQUAL(history_files->context_menu_count, 0);
  window_t *branches = g_test_state.branches_win;
  ASSERT_EQUAL(branches->context_menu_count, CONTEXT_MENU_BRANCHES_COUNT);
  set_focus(log);
  window_t *pane_bar = (window_t *)send_message(log_pane, tcGetToolbar, 0, NULL);
  ASSERT_NOT_NULL(pane_bar);
  ASSERT_EQUAL(toolbar_get_state(pane_bar)->item_count, 1);
  ASSERT_EQUAL(toolbar_get_state(pane_bar)->items[0].ident, ID_VIEW_CLOSE_TAB);
  irect16_t action_rect;
  ASSERT_FALSE(send_message(pane_bar, tbGetItemRect, PANE_ACTION_SPLIT_RIGHT, &action_rect));
  ASSERT_TRUE(gc_pane_command(ID_VIEW_SPLIT_RIGHT));
  uint32_t point;
  ASSERT_EQUAL(page_pane_count(history), 7);
  ASSERT_NOT_EQUAL(log->parent, history_files->parent);
  ASSERT_TRUE(send_message(log->parent, tcGetStyle, 0, NULL) & TAB_STYLE_HIDE_SINGLE_TAB);
  ASSERT_TRUE(gc_pane_command(ID_VIEW_NEXT_PANE));
  ASSERT_NOT_NULL(page_active_pane(history));
  gc_refresh_all();
  ASSERT_EQUAL(g_test_state.log_win, log); ASSERT_EQUAL(g_test_state.history_files_win, history_files);
  pane_bar = (window_t *)send_message(log->parent, tcGetToolbar, 0, NULL);
  ASSERT_TRUE(send_message(pane_bar, tbGetItemRect, ID_VIEW_CLOSE_TAB, &action_rect));
  point = MAKEDWORD(action_rect.x + action_rect.w / 2, action_rect.y + action_rect.h / 2);
  set_focus(history_files);
  send_message(pane_bar->toolbar, evLeftButtonDown, point, NULL);
  send_message(pane_bar->toolbar, evLeftButtonUp, point, NULL);
  ASSERT_TRUE(is_window(log)); ASSERT_FALSE(window_has_state(log, WINDOW_STATE_VISIBLE));
  ASSERT_EQUAL(g_test_state.log_win, log);
  gc_refresh_all();
  ASSERT_FALSE(window_has_state(log, WINDOW_STATE_VISIBLE));
  ASSERT_TRUE(gc_pane_command(ID_VIEW_SHOW_CLOSED_VIEWS));
  ASSERT_TRUE(window_has_state(log, WINDOW_STATE_VISIBLE));
  ASSERT_NOT_NULL((void *)send_message(log, tvGetRecord, 0, NULL));

  // GitHub keeps the stacked default and can regroup without invoking network fetches.
  gc_set_view_mode(GC_TAB_CHANGES);
  git_repo_t *repo = g_test_state.repo; g_test_state.repo = NULL;
  gc_set_view_mode(GC_TAB_GITHUB);
  window_t *issues = g_test_state.github_issues_win, *pulls = g_test_state.github_pulls_win;
  window_t *issues_panel = issues->parent, *pulls_panel = pulls->parent;
  ASSERT_TRUE(issues_panel->parent->frame.y < pulls_panel->parent->frame.y);
  ASSERT_TRUE(page_move_tab(github, pulls_panel, issues_panel->parent, PANE_CENTER, -1));
  ASSERT_EQUAL(page_pane_count(github), 1);
  ASSERT_EQUAL(g_test_state.github_issues_win, issues); ASSERT_EQUAL(g_test_state.github_pulls_win, pulls);
  pane_bar = (window_t *)send_message(issues_panel->parent, tcGetToolbar, 0, NULL);
  ASSERT_EQUAL(toolbar_get_state(pane_bar)->item_count, 1);
  ASSERT_EQUAL(toolbar_get_state(pane_bar)->items[0].ident, ID_VIEW_CLOSE_TAB);
  ASSERT_TRUE(gc_pane_command(ID_VIEW_SPLIT_DOWN));
  ASSERT_EQUAL(page_pane_count(github), 2);
  ASSERT_TRUE(pulls_panel->parent->frame.y > issues_panel->parent->frame.y);
  g_test_state.repo = repo;
  gc_set_view_mode(GC_TAB_CHANGES);
  root->frame.w = 420; root->frame.h = 700; send_message(root, evResize, 0, NULL);
  ASSERT_EQUAL(g_test_state.activity_win->dock->side, DOCK_BOTTOM);
  ASSERT_EQUAL(page_pane_count(changes), 2);
  send_message(summary, edGetText, sizeof(draft), draft); ASSERT_STR_EQUAL(draft, "Draft remains editable");
  destroy_window(root);
  destroy_database(g_test_state.history_db); destroy_database(g_test_state.changes_db); destroy_database(g_test_state.github_db);
  destroy_database(g_test_state.workspace_db);
  ui_set_database(saved_db); test_env_shutdown(); test_graphics_shutdown(); g_test_state = saved;
  PASS();
}

static void test_push_badge_tracks_unpushed_commits(void) {
  TEST("Push shows the number of commits ahead of the current upstream");
  test_env_init();
  memset(g_gc, 0, sizeof(*g_gc));
  ASSERT_TRUE(gct_git(s_repo, "branch badge-upstream"));
  ASSERT_TRUE(gct_git(s_repo, "branch --set-upstream-to=badge-upstream"));
  g_gc->repo = git_repo_open(s_repo);
  ASSERT_NOT_NULL(g_gc->repo);
  g_gc->tab = GC_TAB_CHANGES;
  g_gc->sync_loads = true;
  gc_create_menubar();
  window_t *ribbon = app_chrome_ribbon(g_gc->chrome_win);
  ASSERT_NOT_NULL(ribbon);
  g_gc->main_win = test_env_create_window("Changes", 0, 0, 640, 480, overview_test_host, NULL);
  ASSERT_NOT_NULL(g_gc->main_win);
  gc_update_status();
  ASSERT_EQUAL(send_message(ribbon, rbGetBadge, ID_REMOTE_PUSH, NULL), 0);
  ASSERT_TRUE(gct_git(s_repo, "commit --allow-empty -m badge-test"));
  gc_update_status();
  ASSERT_EQUAL(send_message(ribbon, rbGetBadge, ID_REMOTE_PUSH, NULL), 1);
  ASSERT_TRUE(gct_git(s_repo, "push . HEAD:refs/heads/badge-upstream"));
  gc_update_status();
  ASSERT_EQUAL(send_message(ribbon, rbGetBadge, ID_REMOTE_PUSH, NULL), 0);
  git_repo_close(g_gc->repo);
  test_env_shutdown();
  free_accelerators(g_gc->accel);
  memset(g_gc, 0, sizeof(*g_gc));
  PASS();
}

int main(void) {
    if (!setup_repo()) {
        printf("ERROR: could not create test repository (is git in PATH?)\n");
        return 1;
    }

    TEST_START("Git Client DB-Driven Architecture");

    test_dbload_populates_branches();
    test_dbload_populates_commits();
    test_dbload_populates_tags();
    test_dbload_populates_stash();
    test_dbload_is_idempotent();
    test_master_detail_branch_to_commits();
    test_master_detail_commit_to_files_lazy_load();
    test_files_per_commit_are_independent();
    test_working_tree_files_have_zero_commit_id();
    test_generated_context_menus_attach_to_expected_controls();
    test_generated_context_menus_reuse_shared_commands();
    test_toolbar_buttons_reference_menu_command_ids();
    test_action_metadata_and_accelerators();
    test_every_menu_action_has_handler();
    test_reload_history_log_invalidates_diff_and_reports_failure();
    test_overview_table_filters_and_removes();
    test_overview_runs_targets_in_terminal();
    test_activitybar_navigation();
    test_agents_review_and_launch();
    test_rebase_onto_main_resolve_continue_and_abort();
    test_history_latest_commit_diff();
    test_gitclient_pane_layout();
    test_push_badge_tracks_unpushed_commits();

    gct_remove_dir(s_repo);

    TEST_END();
}
