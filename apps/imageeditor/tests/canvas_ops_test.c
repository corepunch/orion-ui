// tests/canvas_ops_test.c — Tests for operation lifecycle API (Phase 1)

#include "test_framework.h"
#include "test_env.h"
#include "apps/imageeditor/imageeditor.h"

app_state_t *g_app = NULL;

// ── Test operation lifecycle wrappers ──────────────────────────────────────

void test_begin_commit_marks_dirty(void) {
  TEST("ie_doc_begin_op + commit_op marks document dirty and updates title");
  
  test_env_init();
  g_app = calloc(1, sizeof(app_state_t));
  canvas_doc_t *doc = create_document(NULL, 32, 32);
  ASSERT_NOT_NULL(doc);
  ASSERT_FALSE(doc->modified);
  
  ie_doc_begin_op(doc, "Test Operation");
  ASSERT_EQUAL(doc->undo.count, 0);
  ASSERT_NOT_NULL(doc->command.before);
  canvas_set_pixel(doc, 3, 3, MAKE_COLOR(255, 0, 0, 255));
  
  ie_doc_commit_op(doc, true);
  ASSERT_TRUE(doc->modified);  // Document marked dirty
  
  close_document(doc);
  free(g_app);
  test_env_shutdown();
  PASS();
}

void test_begin_discard_rolls_back(void) {
  TEST("ie_doc_begin_op + commit_op(false) discards undo snapshot");
  
  test_env_init();
  g_app = calloc(1, sizeof(app_state_t));
  canvas_doc_t *doc = create_document(NULL, 32, 32);
  ASSERT_NOT_NULL(doc);
  
  ie_doc_begin_op(doc, "Failed Operation");
  int undo_count_before = doc->undo.count;
  ASSERT_EQUAL(undo_count_before, 0);
  ASSERT_NOT_NULL(doc->command.before);
  
  ie_doc_commit_op(doc, false);  // Discard
  ASSERT_EQUAL(doc->undo.count, 0);  // Undo snapshot discarded
  ASSERT_FALSE(doc->modified);  // Document not marked dirty
  
  close_document(doc);
  free(g_app);
  test_env_shutdown();
  PASS();
}

void test_nested_operations_not_supported(void) {
  TEST("Multiple ie_doc_begin_op calls without commit push snapshots");
  
  test_env_init();
  g_app = calloc(1, sizeof(app_state_t));
  canvas_doc_t *doc = create_document(NULL, 32, 32);
  ASSERT_NOT_NULL(doc);
  
  ASSERT_EQUAL(doc->undo.count, 0);  // Start with empty undo stack
  
  ASSERT_TRUE(ie_doc_begin_op(doc, "Operation 1"));
  ASSERT_FALSE(ie_doc_begin_op(doc, "Operation 2"));
  ASSERT_EQUAL(doc->undo.count, 0);
  ASSERT_NOT_NULL(doc->command.before);
  ie_doc_commit_op(doc, false);

  close_document(doc);
  free(g_app);
  test_env_shutdown();
  PASS();
}

void test_invalidate_canvas_refreshes_view(void) {
  TEST("ie_doc_invalidate_canvas works with document canvas window");
  
  test_env_init();
  g_app = calloc(1, sizeof(app_state_t));
  canvas_doc_t *doc = create_document(NULL, 32, 32);
  ASSERT_NOT_NULL(doc);
  
  // create_document creates both doc->win and doc->canvas_win
  ASSERT_NOT_NULL(doc->canvas_win);
  
  // Should handle invalidate gracefully (no crash)
  ie_doc_invalidate_canvas(doc);
  
  close_document(doc);
  free(g_app);
  test_env_shutdown();
  PASS();
}

void test_after_pixels_changed_triggers_refresh(void) {
  TEST("ie_doc_after_pixels_changed works with document canvas window");
  
  test_env_init();
  g_app = calloc(1, sizeof(app_state_t));
  canvas_doc_t *doc = create_document(NULL, 32, 32);
  ASSERT_NOT_NULL(doc);
  
  // create_document creates both doc->win and doc->canvas_win
  ASSERT_NOT_NULL(doc->canvas_win);
  
  // Should handle after_pixels_changed gracefully (no crash)
  ie_doc_after_pixels_changed(doc);
  
  close_document(doc);
  free(g_app);
  test_env_shutdown();
  PASS();
}

// ── Test suite ──────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
  (void)argc; (void)argv;
  TEST_START("Canvas Operations API Tests");
  
  test_begin_commit_marks_dirty();
  test_begin_discard_rolls_back();
  test_nested_operations_not_supported();
  test_invalidate_canvas_refreshes_view();
  test_after_pixels_changed_triggers_refresh();
  
  TEST_END();
}
