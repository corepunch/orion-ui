#ifndef __IE_HISTORY_CASES_H__
#define __IE_HISTORY_CASES_H__
#include "test_framework.h"
#include "test_env.h"
#include "apps/imageeditor/imageeditor.h"
#include "apps/imageeditor/tools/tools.h"
#include "apps/imageeditor/commands/commands.h"
#include <orion/user/toolbar.h>

app_state_t *g_app;
#if IMAGEEDITOR_BW
int g_bw_retina_scale = 1;
#endif

static void history_shape_roundtrips(void) {
  TEST("every shape uses a document checkpoint before preview, through both input paths");
  int shapes[] = {ID_TOOL_LINE, ID_TOOL_RECT, ID_TOOL_ELLIPSE, ID_TOOL_ROUNDED_RECT};
  for (int path = 0; path < 2; path++) {
    for (int i = 0; i < ARRAY_LEN(shapes); i++) {
      canvas_doc_t *doc = create_document(NULL, 32, 32);
      ASSERT_NOT_NULL(doc);
      size_t size = (size_t)doc->canvas_w * doc->canvas_h * DOC_BPP;
      uint8_t *before = malloc(size), *after = malloc(size);
      memcpy(before, doc->pixels, size);
      g_app->current_tool = shapes[i];
      const tool_handler_t *tool = get_tool_handler(shapes[i]);
      if (path) {
        tool->begin(doc, NULL, (ipoint16_t){4, 4});
        tool->drag(doc, NULL, (ipoint16_t){24, 24});
        tool->end(doc, NULL, (ipoint16_t){24, 24});
      } else {
        send_message(doc->canvas_win, evLeftButtonDown, MAKEDWORD(4, 4), NULL);
        send_message(doc->canvas_win, evMouseMove, MAKEDWORD(24, 24), NULL);
        send_message(doc->canvas_win, evLeftButtonUp, MAKEDWORD(24, 24), NULL);
      }
      memcpy(after, doc->pixels, size);
      ASSERT_TRUE(memcmp(before, after, size) != 0);
      ASSERT_EQUAL(doc->undo.count, 1);
      cmd_undo(doc);
      ASSERT_EQUAL(memcmp(before, doc->pixels, size), 0);
      cmd_redo(doc);
      ASSERT_EQUAL(memcmp(after, doc->pixels, size), 0);
      cmd_undo(doc);
      send_message(doc->canvas_win, evLeftButtonDown, MAKEDWORD(3, 3), NULL);
      send_message(doc->canvas_win, evMouseMove, MAKEDWORD(20, 20), NULL);
      send_message(doc->canvas_win, evPointerCancel, 0, NULL);
      ASSERT_EQUAL(memcmp(before, doc->pixels, size), 0);
      ASSERT_EQUAL(doc->redo.count, 1);
      cmd_redo(doc);
      ASSERT_EQUAL(memcmp(after, doc->pixels, size), 0);
      free(before); free(after);
      close_document(doc);
    }
  }
  PASS();
}

static void history_polygon_crop_and_limit(void) {
  TEST("polygon spans clicks, crop spans Enter, and bounded history stays valid");
  canvas_doc_t *doc = create_document(NULL, 32, 32);
  size_t bytes = (size_t)doc->canvas_w * doc->canvas_h * DOC_BPP;
  uint8_t *before = malloc(bytes);
  memcpy(before, doc->pixels, bytes);
  g_app->current_tool = ID_TOOL_POLYGON;
  ipoint16_t points[] = {{4, 4}, {24, 4}, {12, 24}};
  for (int i = 0; i < ARRAY_LEN(points); i++) {
    uint32_t point = MAKEDWORD(points[i].x, points[i].y);
    send_message(doc->canvas_win, evLeftButtonDown, point, NULL);
    send_message(doc->canvas_win, evLeftButtonUp, point, NULL);
    ASSERT_NOT_NULL(doc->command.before);
    ASSERT_EQUAL(doc->undo.count, 0);
  }
  send_message(doc->canvas_win, evRightButtonDown, MAKEDWORD(12, 24), NULL);
  ASSERT_EQUAL(doc->undo.count, 1);
  ASSERT_TRUE(memcmp(before, doc->pixels, bytes) != 0);
  cmd_undo(doc);
  ASSERT_EQUAL(memcmp(before, doc->pixels, bytes), 0);
  free(before);
  cmd_redo(doc);
  g_app->current_tool = ID_TOOL_CROP;
  send_message(doc->canvas_win, evLeftButtonDown, MAKEDWORD(2, 2), NULL);
  send_message(doc->canvas_win, evMouseMove, MAKEDWORD(17, 17), NULL);
  send_message(doc->canvas_win, evLeftButtonUp, MAKEDWORD(17, 17), NULL);
  ASSERT_NOT_NULL(doc->command.before);
  send_message(doc->canvas_win, evKeyDown, AX_KEY_ENTER, NULL);
  ASSERT_EQUAL(doc->canvas_w, 16);
  ASSERT_NULL(doc->command.before);
  cmd_undo(doc);
  ASSERT_EQUAL(doc->canvas_w, 32);
  ASSERT_FALSE(doc->sel.active);
  cmd_redo(doc);
  ASSERT_EQUAL(doc->canvas_w, 16);
  // Repeated edits exercise eviction and both history stacks at capacity.
  for (int i = 0; i < UNDO_MAX + 3; i++) {
    ASSERT_TRUE(ie_doc_begin_op(doc, "Timing"));
    doc->anim->frames[0]->delay_ms = 1000 + i;
    ie_doc_commit_op(doc, true);
  }
  ASSERT_EQUAL(doc->undo.count, UNDO_MAX);
  for (int i = 0; i < UNDO_MAX; i++) ASSERT_TRUE(doc_undo(doc));
  ASSERT_FALSE(doc_undo(doc));
  for (int i = 0; i < UNDO_MAX; i++) ASSERT_TRUE(doc_redo(doc));
  ASSERT_EQUAL(doc->anim->frames[0]->delay_ms, 1000 + UNDO_MAX + 2);
  ASSERT_FALSE(doc_redo(doc));
  close_document(doc);
  PASS();
}

static void history_frames(void) {
  TEST("frame add, duplicate, reorder and delete restore pixels, metadata and active frame");
  canvas_doc_t *doc = create_document(NULL, 32, 32);
  ASSERT_TRUE(ie_doc_begin_op(doc, "Ink"));
  canvas_set_pixel(doc, 4, 4, g_app->fg_color);
  ie_doc_commit_op(doc, true);
  uint32_t ink = canvas_get_pixel(doc, 4, 4);
  handle_menu_command(ID_ANIM_NEW_FRAME);
  ASSERT_EQUAL(doc->anim->frame_count, 2);
  ASSERT_EQUAL(doc->anim->active_frame, 1);
  uint32_t blank = canvas_get_pixel(doc, 4, 4);
  ASSERT_TRUE(blank != ink);
  cmd_undo(doc);
  ASSERT_EQUAL(doc->anim->frame_count, 1);
  ASSERT_EQUAL(canvas_get_pixel(doc, 4, 4), ink);
  cmd_redo(doc);
  ASSERT_EQUAL(doc->anim->frame_count, 2);
  ASSERT_EQUAL(canvas_get_pixel(doc, 4, 4), blank);
  int count = doc->undo.count;
  ASSERT_TRUE(cmd_frame_select(doc, 0));
  ASSERT_EQUAL(doc->undo.count, count);
  ASSERT_EQUAL(canvas_get_pixel(doc, 4, 4), ink);
  ASSERT_TRUE(ie_doc_begin_op(doc, "Frame Timing"));
  doc->anim->frames[0]->delay_ms = 170;
  strcpy(doc->anim->frames[0]->name, "Drawing");
  ie_doc_commit_op(doc, true);
  handle_menu_command(ID_ANIM_DUPLICATE_FRAME);
  ASSERT_EQUAL(doc->anim->frame_count, 3);
  ASSERT_EQUAL(doc->anim->active_frame, 1);
  ASSERT_EQUAL(canvas_get_pixel(doc, 4, 4), ink);
  cmd_undo(doc);
  ASSERT_EQUAL(doc->anim->frame_count, 2);
  cmd_redo(doc);
  ASSERT_EQUAL(doc->anim->frame_count, 3);
  cmd_frame_move(doc, 1, 2);
  ASSERT_EQUAL(doc->anim->active_frame, 2);
  cmd_undo(doc);
  ASSERT_EQUAL(doc->anim->active_frame, 1);
  cmd_redo(doc);
  ASSERT_EQUAL(doc->anim->active_frame, 2);
  handle_menu_command(ID_ANIM_DELETE_FRAME);
  ASSERT_EQUAL(doc->anim->frame_count, 2);
  ASSERT_EQUAL(canvas_get_pixel(doc, 4, 4), blank);
  cmd_undo(doc);
  ASSERT_EQUAL(doc->anim->frame_count, 3);
  ASSERT_EQUAL(doc->anim->active_frame, 2);
  ASSERT_EQUAL(canvas_get_pixel(doc, 4, 4), ink);
  ASSERT_EQUAL(doc->anim->frames[2]->delay_ms, 170);
  ASSERT_EQUAL(strcmp(doc->anim->frames[0]->name, "Drawing"), 0);
  cmd_redo(doc);
  ASSERT_EQUAL(doc->anim->frame_count, 2);
  // Editing an earlier frame then navigating elsewhere must undo that edit.
  ASSERT_TRUE(cmd_frame_select(doc, 0));
  ASSERT_TRUE(ie_doc_begin_op(doc, "Second Ink"));
  canvas_set_pixel(doc, 8, 8, g_app->fg_color);
  ie_doc_commit_op(doc, true);
  ASSERT_TRUE(cmd_frame_select(doc, 1));
  cmd_undo(doc);
  ASSERT_EQUAL(doc->anim->active_frame, 0);
  ASSERT_EQUAL(canvas_get_pixel(doc, 4, 4), ink);
  ASSERT_TRUE(canvas_get_pixel(doc, 8, 8) != ink);
  cmd_redo(doc);
  ASSERT_EQUAL(doc->anim->active_frame, 1);
  ASSERT_TRUE(cmd_frame_select(doc, 0));
  ASSERT_EQUAL(canvas_get_pixel(doc, 8, 8), ink);
  close_document(doc);
  PASS();
}

static void history_transactions(void) {
  TEST("cancel, no-op, branching, nested rejection and document isolation");
  canvas_doc_t *a = create_document(NULL, 32, 32);
  uint32_t original = canvas_get_pixel(a, 2, 2);
  ASSERT_TRUE(ie_doc_begin_op(a, "Edit"));
  canvas_set_pixel(a, 2, 2, g_app->fg_color);
  ie_doc_commit_op(a, true);
  cmd_undo(a);
  ASSERT_TRUE(ie_doc_begin_op(a, "Cancel"));
  ASSERT_FALSE(ie_doc_begin_op(a, "Nested"));
  canvas_set_pixel(a, 2, 2, g_app->fg_color);
  canvas_doc_t *b = create_document(NULL, 32, 32);
  ASSERT_TRUE(ie_doc_begin_op(b, "Other Document"));
  canvas_set_pixel(b, 3, 3, g_app->fg_color);
  ie_doc_commit_op(b, true);
  ie_doc_commit_op(a, false);
  ASSERT_EQUAL(canvas_get_pixel(a, 2, 2), original);
  ASSERT_EQUAL(a->redo.count, 1);
  ASSERT_TRUE(ie_doc_begin_op(a, "No-op"));
  ie_doc_commit_op(a, true);
  ASSERT_EQUAL(a->undo.count, 0);
  ASSERT_EQUAL(a->redo.count, 1);
  ASSERT_TRUE(ie_doc_begin_op(a, "Branch"));
  canvas_set_pixel(a, 5, 5, g_app->fg_color);
  ie_doc_commit_op(a, true);
  ASSERT_EQUAL(a->redo.count, 0);
  ASSERT_EQUAL(b->undo.count, 1);
  close_document(b);
  close_document(a);
  PASS();
}

static void history_selection_resize_layers(void) {
  TEST("selection, resize and layers restore a complete editable document");
  canvas_doc_t *doc = create_document(NULL, 32, 32);
  uint32_t blank = canvas_get_pixel(doc, 4, 4);
  cmd_layer_fill(doc, g_app->fg_color);
  ASSERT_TRUE(canvas_get_pixel(doc, 4, 4) != blank);
  cmd_undo(doc);
  ASSERT_EQUAL(canvas_get_pixel(doc, 4, 4), blank);
  cmd_redo(doc);
  ASSERT_TRUE(canvas_get_pixel(doc, 4, 4) != blank);
  cmd_select_all(doc);
  ASSERT_TRUE(doc->sel.active);
  cmd_deselect(doc);
  cmd_undo(doc);
  ASSERT_TRUE(doc->sel.active);
  cmd_redo(doc);
  ASSERT_FALSE(doc->sel.active);
  cmd_frame_add(doc, true);
  cmd_resize_image(doc, 16, 20, IMAGE_RESIZE_NEAREST);
  ASSERT_TRUE(cmd_frame_select(doc, 0));
  ASSERT_EQUAL(doc->anim->frames[0]->data_size, (size_t)16 * 20 * DOC_BPP);
  ASSERT_EQUAL(doc->canvas_w, 16);
  cmd_undo(doc);
  ASSERT_EQUAL(doc->canvas_w, 32);
  ASSERT_EQUAL(doc->canvas_h, 32);
  cmd_redo(doc);
  ASSERT_EQUAL(doc->canvas_w, 16);
  ASSERT_EQUAL(doc->canvas_h, 20);
#if !IMAGEEDITOR_SINGLE_LAYER
  cmd_layer_new(doc, 0);
  ASSERT_EQUAL(doc->layer.count, 2);
  cmd_layer_set_visibility(doc, doc->layer.active, false);
  cmd_undo(doc);
  ASSERT_TRUE(doc->layer.stack[doc->layer.active]->visible);
  cmd_undo(doc);
  ASSERT_EQUAL(doc->layer.count, 1);
  cmd_redo(doc);
  ASSERT_EQUAL(doc->layer.count, 2);
#endif
#if IMAGEEDITOR_INDEXED
  uint32_t color = doc->ipal.entries[1];
  cmd_invert_colors(doc);
  uint32_t inverted = doc->ipal.entries[1];
  cmd_undo(doc);
  ASSERT_EQUAL(doc->ipal.entries[1], color);
  cmd_redo(doc);
  ASSERT_EQUAL(doc->ipal.entries[1], inverted);
#endif
  close_document(doc);
  PASS();
}

static void history_availability(void) {
  TEST("menu and toolbar undo/redo availability follows active document history");
  canvas_doc_t *doc = create_document(NULL, 32, 32);
  create_main_toolbar_window();
  imageeditor_sync_main_toolbar();
  toolbar_state_t *tb = toolbar_get_state(g_app->main_toolbar_win);
  ASSERT_NOT_NULL(tb);
  int undo = -1, redo = -1;
  for (int i = 0; i < tb->item_count; i++) {
    if (tb->items[i].ident == ID_EDIT_UNDO) undo = i;
    if (tb->items[i].ident == ID_EDIT_REDO) redo = i;
  }
  ASSERT_TRUE(undo >= 0 && redo >= 0);
  ASSERT_TRUE(tb->items[undo].flags & TOOLBAR_ITEM_FLAG_DISABLED);
  ASSERT_TRUE(tb->items[redo].flags & TOOLBAR_ITEM_FLAG_DISABLED);
  ASSERT_TRUE(ie_doc_begin_op(doc, "Ink"));
  canvas_set_pixel(doc, 4, 4, g_app->fg_color);
  ie_doc_commit_op(doc, true);
  ASSERT_FALSE(tb->items[undo].flags & TOOLBAR_ITEM_FLAG_DISABLED);
  cmd_undo(doc);
  ASSERT_TRUE(tb->items[undo].flags & TOOLBAR_ITEM_FLAG_DISABLED);
  ASSERT_FALSE(tb->items[redo].flags & TOOLBAR_ITEM_FLAG_DISABLED);
  for (int i = 0; i < kNumMenus; i++) {
    for (int j = 0; j < kMenus[i].item_count; j++) {
      const menu_item_t *item = &kMenus[i].items[j];
      if (item->id == ID_EDIT_UNDO) ASSERT_TRUE(item->disabled);
      if (item->id == ID_EDIT_REDO) ASSERT_FALSE(item->disabled);
    }
  }
  cmd_redo(doc);
  ASSERT_TRUE(tb->items[redo].flags & TOOLBAR_ITEM_FLAG_DISABLED);
  close_document(doc);
  ASSERT_TRUE(tb->items[undo].flags & TOOLBAR_ITEM_FLAG_DISABLED);
  ASSERT_TRUE(tb->items[redo].flags & TOOLBAR_ITEM_FLAG_DISABLED);
  destroy_window(g_app->chrome_win);
  g_app->chrome_win = NULL;
  PASS();
}

int main(void) {
  TEST_START("Document command history");
  test_env_init();
  g_app = calloc(1, sizeof(*g_app));
  g_app->fg_color = IE_INK_COLOR;
  register_builtin_tools();
  history_shape_roundtrips();
  history_polygon_crop_and_limit();
  history_frames();
  history_transactions();
  history_selection_resize_layers();
  history_availability();
  free(g_app); g_app = NULL;
  test_env_shutdown();
  TEST_END();
}
#endif
