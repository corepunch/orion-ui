// gitclient db-driven architecture tests.
// Tests updated for compact application toolbar (no page strips).

#include "test_framework.h"
#include "test_env.h"
#include "gitclient_test_helpers.h"
#include "apps/gitclient/gitclient.h"
#include "apps/gitclient/gc_actions.h"

#include <stdio.h>
#include <string.h>

static gc_state_t g_test_state;
gc_state_t *g_gc = &g_test_state;

static char s_repo[256];

static bool setup_repo(void) {
    if (!gct_make_temp_dir(s_repo, sizeof(s_repo), "orion_gcdb")) return false;
    if (!gct_git(s_repo, "init -b main") && !gct_git(s_repo, "init"))  return false;
    if (!gct_git(s_repo, "config user.email ci@test")) return false;
    if (!gct_git(s_repo, "config user.name CI"))       return false;
    char p[512];
    snprintf(p, sizeof(p), "%s/file1.txt", s_repo);
    if (!gct_write_file(p, "hello\n"))        return false;
    if (!gct_git(s_repo, "add file1.txt"))    return false;
    if (!gct_git(s_repo, "commit -m \"first commit\"")) return false;
    snprintf(p, sizeof(p), "%s/file2.txt", s_repo);
    if (!gct_write_file(p, "world\n"))        return false;
    if (!gct_git(s_repo, "add file2.txt"))    return false;
    if (!gct_git(s_repo, "commit -m \"second commit\"")) return false;
    if (!gct_git(s_repo, "checkout -b feature")) return false;
    snprintf(p, sizeof(p), "%s/feature.txt", s_repo);
    if (!gct_write_file(p, "feat\n"))         return false;
    if (!gct_git(s_repo, "add feature.txt")) return false;
    if (!gct_git(s_repo, "commit -m \"feature commit\"")) return false;
    if (!gct_git(s_repo, "checkout main") && !gct_git(s_repo, "checkout master")) return false;
    return true;
}

static int result_count(result_node_t *rows) {
    int n = 0; for (; rows; rows = rows->next) n++; return n;
}

static database_t *make_db(void) {
    DB_CLASS(gitclient_db);
    return create_database("gc-test", "gitclient_db", NULL);
}

static database_t *make_changes_db(void) {
    DB_CLASS(changes_database_proc);
    return create_database("gc-test-changes", "changes_database_proc", NULL);
}

void test_dbload_populates_branches(void) {
    TEST("dbLoad: branches table populated from git");
    test_env_init();
    database_t *db = make_db();
    git_repo_t *repo = git_repo_open(s_repo);
    ASSERT_NOT_NULL(repo);
    send_db_message(db, dbLoad, 0, repo);
    result_node_t *rows = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(ID_DB_BRANCHES, 0), (void *)(intptr_t)0);
    ASSERT_TRUE(result_count(rows) >= 2);
    bool found_main = false, found_feature = false; int current_count = 0;
    for (result_node_t *n = rows; n; n = n->next) {
        db_branche_t *b = *(db_branche_t **)n->data;
        if (strcmp(b->name, "main") == 0 || strcmp(b->name, "master") == 0) found_main = true;
        if (strcmp(b->name, "feature") == 0) found_feature = true;
        if (b->is_current) current_count++;
    }
    free_result_list(rows);
    ASSERT_TRUE(found_main); ASSERT_TRUE(found_feature); ASSERT_EQUAL(current_count, 1);
    git_repo_close(repo); destroy_database(db); test_env_shutdown(); PASS();
}

void test_dbload_populates_commits(void) {
    TEST("dbLoad: current branch's commits table populated eagerly");
    test_env_init();
    database_t *db = make_db();
    git_repo_t *repo = git_repo_open(s_repo);
    ASSERT_NOT_NULL(repo);
    send_db_message(db, dbLoad, 0, repo);
    result_node_t *rows = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(ID_DB_COMMITS, 0), (void *)(intptr_t)0);
    ASSERT_EQUAL(result_count(rows), 2);
    db_commit_t *c = rows ? *(db_commit_t **)rows->data : NULL;
    ASSERT_NOT_NULL(c);
    ASSERT_TRUE(c->hash[0] != '\0');
    ASSERT_TRUE(c->author[0] != '\0');
    ASSERT_TRUE(c->subject[0] != '\0');
    ASSERT_EQUAL((int)strlen(c->hash), 40);
    free_result_list(rows); git_repo_close(repo); destroy_database(db); test_env_shutdown(); PASS();
}

void test_dbload_populates_tags(void) {
    TEST("dbLoad: tags table populated after tag creation");
    ASSERT_TRUE(gct_git(s_repo, "tag -a v1.0 -m release HEAD"));
    test_env_init();
    database_t *db = make_db();
    git_repo_t *repo = git_repo_open(s_repo);
    ASSERT_NOT_NULL(repo);
    send_db_message(db, dbLoad, 0, repo);
    result_node_t *rows = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(ID_DB_TAGS, 0), (void *)(intptr_t)0);
    ASSERT_EQUAL(result_count(rows), 1);
    db_tag_t *tag = rows ? *(db_tag_t **)rows->data : NULL;
    ASSERT_NOT_NULL(tag); ASSERT_STR_EQUAL(tag->name, "v1.0"); ASSERT_EQUAL((int)strlen(tag->hash), 40);
    free_result_list(rows); git_repo_close(repo); destroy_database(db); test_env_shutdown();
    gct_git(s_repo, "tag -d v1.0"); PASS();
}

void test_dbload_populates_stash(void) {
    TEST("dbLoad: stash table populated after stash push");
    char p[512]; snprintf(p, sizeof(p), "%s/file1.txt", s_repo);
    ASSERT_TRUE(gct_append_file(p, "stashed change\n"));
    ASSERT_TRUE(gct_git(s_repo, "stash push -m db-test-stash"));
    test_env_init();
    database_t *db = make_db();
    git_repo_t *repo = git_repo_open(s_repo);
    ASSERT_NOT_NULL(repo);
    send_db_message(db, dbLoad, 0, repo);
    result_node_t *rows = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(ID_DB_STASH, 0), (void *)(intptr_t)0);
    ASSERT_EQUAL(result_count(rows), 1);
    db_stash_t *st = rows ? *(db_stash_t **)rows->data : NULL;
    ASSERT_NOT_NULL(st); ASSERT_STR_EQUAL(st->ref, "stash@{0}"); ASSERT_TRUE(strstr(st->message, "db-test-stash") != NULL);
    free_result_list(rows); git_repo_close(repo); destroy_database(db); test_env_shutdown();
    gct_git(s_repo, "stash drop stash@{0}"); PASS();
}

void test_dbload_is_idempotent(void) {
    TEST("dbLoad: calling twice does not duplicate rows");
    test_env_init();
    database_t *db = make_db();
    git_repo_t *repo = git_repo_open(s_repo);
    ASSERT_NOT_NULL(repo);
    send_db_message(db, dbLoad, 0, repo);
    send_db_message(db, dbLoad, 0, repo);
    result_node_t *rows = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(ID_DB_BRANCHES, 0), (void *)(intptr_t)0);
    int count = result_count(rows); free_result_list(rows);
    ASSERT_TRUE(count >= 2);
    result_node_t *commits = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(ID_DB_COMMITS, 0), (void *)(intptr_t)0);
    int commit_count_1 = result_count(commits); free_result_list(commits);
    send_db_message(db, dbLoad, 0, repo);
    commits = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(ID_DB_COMMITS, 0), (void *)(intptr_t)0);
    int commit_count_2 = result_count(commits); free_result_list(commits);
    ASSERT_EQUAL(commit_count_1, commit_count_2);
    git_repo_close(repo); destroy_database(db); test_env_shutdown(); PASS();
}

void test_master_detail_branch_to_commits(void) {
    TEST("master-detail: dbFetch(commits, branch_id) returns only that branch's commits");
    test_env_init();
    database_t *db = make_db();
    git_repo_t *repo = git_repo_open(s_repo);
    ASSERT_NOT_NULL(repo);
    send_db_message(db, dbLoad, 0, repo);
    result_node_t *branches = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(ID_DB_BRANCHES, 0), (void *)(intptr_t)0);
    int feature_id = -1, main_id = -1;
    for (result_node_t *n = branches; n; n = n->next) {
        db_branche_t *b = *(db_branche_t **)n->data;
        if (strcmp(b->name, "feature") == 0) feature_id = b->id;
        if (strcmp(b->name, "main") == 0 || strcmp(b->name, "master") == 0) main_id = b->id;
    }
    free_result_list(branches);
    ASSERT_TRUE(feature_id > 0); ASSERT_TRUE(main_id > 0);
    result_node_t *main_commits = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(ID_DB_COMMITS, ID_DB_COMMITS_BRANCH_ID), (void *)(intptr_t)main_id);
    int main_count = result_count(main_commits); free_result_list(main_commits);
    ASSERT_EQUAL(main_count, 2);
    result_node_t *feat_commits = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(ID_DB_COMMITS, ID_DB_COMMITS_BRANCH_ID), (void *)(intptr_t)feature_id);
    int feat_count = result_count(feat_commits); free_result_list(feat_commits);
    ASSERT_EQUAL(feat_count, 3);
    git_repo_close(repo); destroy_database(db); test_env_shutdown(); PASS();
}

void test_master_detail_commit_to_files_lazy_load(void) {
    TEST("master-detail: dbFetch(files, commit_id) lazy-loads files from git");
    test_env_init();
    database_t *db = make_db();
    git_repo_t *repo = git_repo_open(s_repo);
    ASSERT_NOT_NULL(repo);
    send_db_message(db, dbLoad, 0, repo);
    result_node_t *branches = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(ID_DB_BRANCHES, 0), (void *)(intptr_t)0);
    int main_id = -1;
    for (result_node_t *n = branches; n; n = n->next) {
        db_branche_t *b = *(db_branche_t **)n->data;
        if (strcmp(b->name, "main") == 0 || strcmp(b->name, "master") == 0) main_id = b->id;
    }
    free_result_list(branches); ASSERT_TRUE(main_id > 0);
    result_node_t *commits = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(ID_DB_COMMITS, ID_DB_COMMITS_BRANCH_ID), (void *)(intptr_t)main_id);
    result_node_t *second_node = commits ? commits->next : NULL;
    ASSERT_NOT_NULL(second_node);
    db_commit_t *second = *(db_commit_t **)second_node->data;
    int commit_id = second->id; free_result_list(commits); ASSERT_TRUE(commit_id > 0);
    result_node_t *files = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(ID_DB_FILES, ID_DB_FILES_COMMIT_ID), (void *)(intptr_t)commit_id);
    int file_count = result_count(files); free_result_list(files);
    ASSERT_TRUE(file_count >= 1);
    files = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(ID_DB_FILES, ID_DB_FILES_COMMIT_ID), (void *)(intptr_t)commit_id);
    ASSERT_EQUAL(result_count(files), file_count); free_result_list(files);
    git_repo_close(repo); destroy_database(db); test_env_shutdown(); PASS();
}

void test_files_per_commit_are_independent(void) {
    TEST("master-detail: files filtered by commit_id do not bleed across commits");
    test_env_init();
    database_t *db = make_db();
    git_repo_t *repo = git_repo_open(s_repo);
    ASSERT_NOT_NULL(repo);
    send_db_message(db, dbLoad, 0, repo);
    result_node_t *branches = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(ID_DB_BRANCHES, 0), (void *)(intptr_t)0);
    int main_id = -1;
    for (result_node_t *n = branches; n; n = n->next) {
        db_branche_t *b = *(db_branche_t **)n->data;
        if (strcmp(b->name, "main") == 0 || strcmp(b->name, "master") == 0) main_id = b->id;
    }
    free_result_list(branches); ASSERT_TRUE(main_id > 0);
    result_node_t *commits = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(ID_DB_COMMITS, ID_DB_COMMITS_BRANCH_ID), (void *)(intptr_t)main_id);
    ASSERT_NOT_NULL(commits); ASSERT_NOT_NULL(commits->next);
    int id_a = (*(db_commit_t **)commits->data)->id;
    int id_b = (*(db_commit_t **)commits->next->data)->id;
    free_result_list(commits);
    result_node_t *fa = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(ID_DB_FILES, ID_DB_FILES_COMMIT_ID), (void *)(intptr_t)id_a);
    result_node_t *fb = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(ID_DB_FILES, ID_DB_FILES_COMMIT_ID), (void *)(intptr_t)id_b);
    ASSERT_TRUE(result_count(fa) >= 1); ASSERT_TRUE(result_count(fb) >= 1);
    for (result_node_t *n = fa; n; n = n->next) ASSERT_EQUAL((*(db_file_t **)n->data)->commit_id, id_a);
    for (result_node_t *n = fb; n; n = n->next) ASSERT_EQUAL((*(db_file_t **)n->data)->commit_id, id_b);
    free_result_list(fa); free_result_list(fb);
    git_repo_close(repo); destroy_database(db); test_env_shutdown(); PASS();
}

void test_working_tree_files_have_zero_commit_id(void) {
    TEST("changes dbLoad: working-tree dirty files stored with commit_id == 0");
    char p[512]; snprintf(p, sizeof(p), "%s/dirty.txt", s_repo);
    ASSERT_TRUE(gct_write_file(p, "dirty\n")); ASSERT_TRUE(gct_git(s_repo, "add dirty.txt"));
    test_env_init();
    database_t *db = make_changes_db();
    git_repo_t *repo = git_repo_open(s_repo);
    ASSERT_NOT_NULL(repo);
    send_db_message(db, dbLoad, 0, repo);
    result_node_t *all_files = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(TABLE_FILES, 0), (void *)(intptr_t)0);
    bool found_wt = false;
    for (result_node_t *n = all_files; n; n = n->next) {
        db_file_t *f = *(db_file_t **)n->data;
        if (f->commit_id == 0) { found_wt = true; break; }
    }
    free_result_list(all_files); ASSERT_TRUE(found_wt);
    git_repo_close(repo); destroy_database(db); test_env_shutdown();
    gct_git(s_repo, "restore --staged dirty.txt"); remove(p); PASS();
}

static const form_ctrl_def_t *find_in(const form_ctrl_def_t *children, int count, uint32_t id) {
    for (int i = 0; i < count; i++) {
        if (children[i].id == id) return &children[i];
        const form_ctrl_def_t *found = find_in(children[i].children, children[i].child_count, id);
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
    ASSERT_NOT_NULL(branches); ASSERT_NOT_NULL(tags); ASSERT_NOT_NULL(stash); ASSERT_NOT_NULL(files);
    ASSERT_EQUAL(branches->context_menu, CONTEXT_MENU_BRANCHES);
    ASSERT_EQUAL(tags->context_menu, CONTEXT_MENU_TAGS);
    ASSERT_EQUAL(stash->context_menu, CONTEXT_MENU_STASH);
    ASSERT_EQUAL(files->context_menu, CONTEXT_MENU_FILES);
    PASS();
}

void test_generated_context_menus_reuse_shared_commands(void) {
    TEST("gitclient Orion: context menus reuse shared command IDs");
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
    ASSERT_TRUE(app_toolbar_has_action(ID_VIEW_OVERVIEW));
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
    ASSERT_EQUAL(gitclient_action_meta_count, 48);
    for (int i = 0; i < gitclient_action_meta_count; i++) {
        ASSERT_TRUE(gitclient_action_meta[i].name[0] != '\0');
        ASSERT_TRUE(gitclient_action_meta[i].label[0] != '\0');
        ASSERT_TRUE(gitclient_action_meta[i].category[0] != '\0');
        ASSERT_TRUE(gitclient_action_meta[i].id >= ID_COMMAND_BASE);
    }
    ASSERT_TRUE(gitclient_application_toolbar.count > 0);
    {
        const toolbar_item_t *items = gitclient_application_toolbar.items;
        for (int i = 0; i < gitclient_application_toolbar.count; i++)
            if (items[i].type == TOOLBAR_ITEM_BUTTON)
                ASSERT_TRUE(gc_meta_has((uint16_t)items[i].ident));
    }
    for (int i = 0; i < CONTEXT_MENU_BRANCHES_COUNT; i++)
        if (CONTEXT_MENU_BRANCHES_ITEMS[i].id) ASSERT_TRUE(gc_meta_has((uint16_t)CONTEXT_MENU_BRANCHES_ITEMS[i].id));
    for (int i = 0; i < CONTEXT_MENU_FILES_COUNT; i++)
        if (CONTEXT_MENU_FILES_ITEMS[i].id) ASSERT_TRUE(gc_meta_has((uint16_t)CONTEXT_MENU_FILES_ITEMS[i].id));
    for (int i = 0; i < CONTEXT_MENU_STASH_COUNT; i++)
        if (CONTEXT_MENU_STASH_ITEMS[i].id) ASSERT_TRUE(gc_meta_has((uint16_t)CONTEXT_MENU_STASH_ITEMS[i].id));
    int hotkey_count = 0;
    for (int i = 0; i < gitclient_action_meta_count; i++)
        if (gitclient_action_meta[i].hotkey[0]) hotkey_count++;
    ASSERT_EQUAL(hotkey_count, gitclient_default_accel_count);
    for (int i = 0; i < gitclient_default_accel_count; i++)
        ASSERT_TRUE(gc_meta_has(gitclient_default_accels[i].cmd));
    const accel_t *refresh = gc_find_accel(ID_REPO_REFRESH);
    const accel_t *commit  = gc_find_accel(ID_COMMIT_COMMIT);
    ASSERT_NOT_NULL(refresh); ASSERT_NOT_NULL(commit);
    ASSERT_EQUAL(refresh->key, AX_KEY_F5);
    ASSERT_EQUAL(refresh->fVirt & FCONTROL, 0);
    ASSERT_EQUAL(commit->key, AX_KEY_K);
    ASSERT_TRUE((commit->fVirt & FCONTROL) != 0);
    PASS();
}

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
    gct_remove_dir(s_repo);
    TEST_END();
}
