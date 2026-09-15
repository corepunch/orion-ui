// tests/tool_handler_test.c — Tests for tool handler system (Phase 3)

#include "test_framework.h"
#include "test_env.h"
#include "apps/imageeditor/imageeditor.h"
#include "apps/imageeditor/tools/tools.h"

app_state_t *g_app = NULL;

// ── Test tool handler registry ─────────────────────────────────────────────

void test_get_tool_handler_valid(void) {
  TEST("get_tool_handler returns valid handler for implemented tools");
  
  test_env_init();
  g_app = calloc(1, sizeof(app_state_t));
  register_builtin_tools();
  
  const tool_handler_t *pencil = get_tool_handler(ID_TOOL_PENCIL);
  ASSERT_NOT_NULL(pencil);
  ASSERT_EQUAL(pencil->id, ID_TOOL_PENCIL);
  ASSERT_NOT_NULL(pencil->name);
  ASSERT_NOT_NULL(pencil->begin);
  
  const tool_handler_t *brush = get_tool_handler(ID_TOOL_BRUSH);
  ASSERT_NOT_NULL(brush);
  ASSERT_EQUAL(brush->id, ID_TOOL_BRUSH);
  
  const tool_handler_t *line = get_tool_handler(ID_TOOL_LINE);
  ASSERT_NOT_NULL(line);
  ASSERT_EQUAL(line->id, ID_TOOL_LINE);
  
  free(g_app);
  test_env_shutdown();
  PASS();
}

void test_get_tool_handler_stub(void) {
  TEST("get_tool_handler returns stub handler for unimplemented tools");
  
  test_env_init();
  g_app = calloc(1, sizeof(app_state_t));
  register_builtin_tools();
  
  const tool_handler_t *crop = get_tool_handler(ID_TOOL_CROP);
  ASSERT_NOT_NULL(crop);
  ASSERT_EQUAL(crop->id, ID_TOOL_CROP);
  ASSERT_NOT_NULL(crop->begin);  // Stub has no-op functions
  
  free(g_app);
  test_env_shutdown();
  PASS();
}

void test_get_tool_handler_invalid(void) {
  TEST("get_tool_handler returns NULL for invalid tool ID");
  
  test_env_init();
  g_app = calloc(1, sizeof(app_state_t));
  register_builtin_tools();
  
  const tool_handler_t *invalid = get_tool_handler(9999);
  ASSERT_NULL(invalid);
  
  free(g_app);
  test_env_shutdown();
  PASS();
}

// ── Test tool lifecycle ────────────────────────────────────────────────────

void test_pencil_begin_end_lifecycle(void) {
  TEST("Pencil tool: begin/drag/end lifecycle completes successfully");
  
  test_env_init();
  g_app = calloc(1, sizeof(app_state_t));
  g_app->fg_color = MAKE_COLOR(0xFF, 0x00, 0x00, 0xFF);
  register_builtin_tools();
  
  canvas_doc_t *doc = create_document(NULL, 32, 32);
  ASSERT_NOT_NULL(doc);
  
  canvas_win_state_t state = {.doc = doc};
  
  const tool_handler_t *pencil = get_tool_handler(ID_TOOL_PENCIL);
  ASSERT_NOT_NULL(pencil);
  
  // Begin stroke
  pencil->begin(doc, &state, (ipoint16_t){10, 10});
  ASSERT_EQUAL(doc->undo.count, 1);  // Undo pushed
  
  // Drag
  if (pencil->drag) {
    pencil->drag(doc, &state, (ipoint16_t){20, 20});
  }
  
  // End stroke
  if (pencil->end) {
    pencil->end(doc, &state, (ipoint16_t){20, 20});
  }
  
  ASSERT_TRUE(doc->modified);  // Document marked dirty
  
  close_document(doc);
  free(g_app);
  test_env_shutdown();
  PASS();
}

void test_line_preview_lifecycle(void) {
  TEST("Line tool: begin/drag (preview)/end (commit) lifecycle");
  
  test_env_init();
  g_app = calloc(1, sizeof(app_state_t));
  g_app->fg_color = MAKE_COLOR(0xFF, 0x00, 0x00, 0xFF);
  register_builtin_tools();
  
  canvas_doc_t *doc = create_document(NULL, 32, 32);
  ASSERT_NOT_NULL(doc);
  
  canvas_win_state_t state = {.doc = doc};
  
  const tool_handler_t *line = get_tool_handler(ID_TOOL_LINE);
  ASSERT_NOT_NULL(line);
  
  // Begin (saves snapshot)
  line->begin(doc, &state, (ipoint16_t){5, 5});
  ASSERT_NOT_NULL(doc->shape.snapshot);  // Snapshot saved for preview
  
  // Drag (shows preview, restores snapshot repeatedly)
  if (line->drag) {
    line->drag(doc, &state, (ipoint16_t){15, 15});
    line->drag(doc, &state, (ipoint16_t){20, 20});
  }
  
  // End (commits final line)
  if (line->end) {
    line->end(doc, &state, (ipoint16_t){25, 25});
  }
  
  ASSERT_TRUE(doc->modified);
  // Snapshot may or may not be freed depending on implementation
  // Just verify end() completed without crash
  
  close_document(doc);
  free(g_app);
  test_env_shutdown();
  PASS();
}

void test_tool_cancel_discards(void) {
  TEST("Tool cancel handler discards operation");
  
  test_env_init();
  g_app = calloc(1, sizeof(app_state_t));
  g_app->fg_color = MAKE_COLOR(0xFF, 0x00, 0x00, 0xFF);
  register_builtin_tools();
  
  canvas_doc_t *doc = create_document(NULL, 32, 32);
  ASSERT_NOT_NULL(doc);
  
  canvas_win_state_t state = {.doc = doc};
  
  const tool_handler_t *pencil = get_tool_handler(ID_TOOL_PENCIL);
  ASSERT_NOT_NULL(pencil);
  
  // Begin stroke
  pencil->begin(doc, &state, (ipoint16_t){10, 10});
  int undo_count = doc->undo.count;
  ASSERT_TRUE(undo_count > 0);
  
  // Cancel
  if (pencil->cancel) {
    pencil->cancel(doc, &state);
  }
  
  ASSERT_EQUAL(doc->undo.count, 0);  // Undo discarded
  // Modified state may persist depending on when pixels were changed
  
  close_document(doc);
  free(g_app);
  test_env_shutdown();
  PASS();
}

// ── Test suite ──────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
  (void)argc; (void)argv;
  TEST_START("Tool Handler System Tests");
  
  test_get_tool_handler_valid();
  test_get_tool_handler_stub();
  test_get_tool_handler_invalid();
  test_pencil_begin_end_lifecycle();
  test_line_preview_lifecycle();
  test_tool_cancel_discards();
  
  TEST_END();
}
