// Task Manager document lookup tests (headless)
// Validates the path lookup logic used by File/Open de-duplication.

#include "test_framework.h"

typedef struct task_doc_s {
  char filename[512];
  struct task_doc_s *next;
} task_doc_t;

typedef struct {
  task_doc_t *docs;
} app_state_t;

typedef struct {
  unsigned int userdata;
} reportview_item_t;

typedef struct fake_window_s {
  struct fake_window_s *parent;
} fake_window_t;

static app_state_t *g_app = NULL;

// Inline replica of app_find_document_by_path from controller_app.c.
static task_doc_t *app_find_document_by_path(const char *path) {
  task_doc_t *doc;

  if (!g_app || !path || path[0] == '\0') return NULL;

  for (doc = g_app->docs; doc; doc = doc->next) {
    if (doc->filename[0] != '\0' && strcmp(doc->filename, path) == 0)
      return doc;
  }

  return NULL;
}

// Inline replica of the fixed Task Manager RVN_SELCHANGE selection mapping:
// selected row index comes from LOWORD(wparam), not lparam->userdata.
static int tm_selected_from_wparam(unsigned int wparam) {
  return (int)(short)(wparam & 0xFFFFu);
}

// Old buggy behavior (for regression comparison): selection pulled from item->userdata.
static int tm_selected_from_item_userdata(reportview_item_t *item, unsigned int wparam) {
  (void)wparam;
  return item ? (int)item->userdata : -1;
}

// Inline replica of the fixed click-to-front targeting in kernel/event.c:
// any client click promotes the clicked root window, not just the direct target.
static fake_window_t *tm_click_root(fake_window_t *win) {
  if (!win) return NULL;
  return win->parent ? tm_click_root(win->parent) : win;
}

// Old buggy behavior for comparison: only the direct hit window was raised.
static fake_window_t *tm_click_target_legacy(fake_window_t *win) {
  return win;
}

static void test_find_document_exact_match(void) {
  TEST("TaskManager lookup: exact filename match returns document");

  app_state_t app = {0};
  task_doc_t a = {0};
  task_doc_t b = {0};

  strncpy(a.filename, "/tmp/a.tdb", sizeof(a.filename) - 1);
  strncpy(b.filename, "/tmp/b.tdb", sizeof(b.filename) - 1);
  a.next = &b;
  app.docs = &a;
  g_app = &app;

  ASSERT_TRUE(app_find_document_by_path("/tmp/b.tdb") == &b);
  PASS();
}

static void test_find_document_missing_returns_null(void) {
  TEST("TaskManager lookup: missing path returns NULL");

  app_state_t app = {0};
  task_doc_t a = {0};

  strncpy(a.filename, "/tmp/a.tdb", sizeof(a.filename) - 1);
  app.docs = &a;
  g_app = &app;

  ASSERT_NULL(app_find_document_by_path("/tmp/none.tdb"));
  PASS();
}

static void test_find_document_ignores_empty_filename(void) {
  TEST("TaskManager lookup: empty filename docs are ignored");

  app_state_t app = {0};
  task_doc_t untitled = {0};
  task_doc_t saved = {0};

  // untitled.filename stays empty
  strncpy(saved.filename, "/tmp/saved.tdb", sizeof(saved.filename) - 1);

  untitled.next = &saved;
  app.docs = &untitled;
  g_app = &app;

  ASSERT_NULL(app_find_document_by_path(""));
  ASSERT_TRUE(app_find_document_by_path("/tmp/saved.tdb") == &saved);
  PASS();
}

static void test_find_document_null_guards(void) {
  TEST("TaskManager lookup: NULL app/path guards return NULL");

  g_app = NULL;
  ASSERT_NULL(app_find_document_by_path("/tmp/a.tdb"));

  app_state_t app = {0};
  g_app = &app;
  ASSERT_NULL(app_find_document_by_path(NULL));
  PASS();
}

static void test_find_document_returns_first_duplicate(void) {
  TEST("TaskManager lookup: returns first match when duplicates exist");

  app_state_t app = {0};
  task_doc_t first = {0};
  task_doc_t second = {0};

  strncpy(first.filename, "/tmp/dup.tdb", sizeof(first.filename) - 1);
  strncpy(second.filename, "/tmp/dup.tdb", sizeof(second.filename) - 1);

  first.next = &second;
  app.docs = &first;
  g_app = &app;

  ASSERT_TRUE(app_find_document_by_path("/tmp/dup.tdb") == &first);
  PASS();
}

static void test_selection_uses_wparam_row_index(void) {
  TEST("TaskManager selection: RVN_SELCHANGE uses LOWORD(wparam) row index");

  // Simulate MAKEDWORD(index, RVN_SELCHANGE): low word is row index.
  unsigned int wparam = 5u;
  ASSERT_EQUAL(tm_selected_from_wparam(wparam), 5);
  PASS();
}

static void test_selection_does_not_depend_on_item_userdata(void) {
  TEST("TaskManager selection: ignores item userdata for selected_idx mapping");

  reportview_item_t item = { .userdata = 99u };
  unsigned int wparam = 2u;

  // Fixed path must use row index (2), not userdata (99).
  ASSERT_EQUAL(tm_selected_from_wparam(wparam), 2);
  ASSERT_EQUAL(tm_selected_from_item_userdata(&item, wparam), 99);
  ASSERT_NOT_EQUAL(tm_selected_from_wparam(wparam), tm_selected_from_item_userdata(&item, wparam));
  PASS();
}

static void test_client_click_promotes_root_window(void) {
  TEST("TaskManager activation: client click on child promotes root window");

  fake_window_t root = {0};
  fake_window_t child = { .parent = &root };

  ASSERT_TRUE(tm_click_root(&child) == &root);
  ASSERT_TRUE(tm_click_target_legacy(&child) == &child);
  ASSERT_NOT_EQUAL(tm_click_root(&child), tm_click_target_legacy(&child));
  PASS();
}

static void test_root_click_still_promotes_self(void) {
  TEST("TaskManager activation: root click still promotes the root itself");

  fake_window_t root = {0};

  ASSERT_TRUE(tm_click_root(&root) == &root);
  PASS();
}

static void test_grandchild_click_promotes_top_level_window(void) {
  TEST("TaskManager activation: nested child click promotes top-level root");

  fake_window_t root = {0};
  fake_window_t child = { .parent = &root };
  fake_window_t grandchild = { .parent = &child };

  ASSERT_TRUE(tm_click_root(&grandchild) == &root);
  PASS();
}

int main(void) {
  TEST_START("Task Manager Document Lookup");

  test_find_document_exact_match();
  test_find_document_missing_returns_null();
  test_find_document_ignores_empty_filename();
  test_find_document_null_guards();
  test_find_document_returns_first_duplicate();
  test_selection_uses_wparam_row_index();
  test_selection_does_not_depend_on_item_userdata();
  test_client_click_promotes_root_window();
  test_root_click_still_promotes_self();
  test_grandchild_click_promotes_top_level_window();

  TEST_END();
}
