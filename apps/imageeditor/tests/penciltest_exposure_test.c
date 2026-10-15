#define IMAGEEDITOR_BW 1
#define IMAGEEDITOR_BW_RETINA
#include "test_framework.h"
#include "test_env.h"
#include "apps/imageeditor/imageeditor.h"
#include "apps/imageeditor/anim/animation_db.h"
#include "anim_test_helpers.h"
#include "apps/imageeditor/windows/animation_timing.h"

app_state_t *g_app;
int g_bw_retina_scale = 1;
static result_t animation_timing_proc(window_t *, uint32_t, uint32_t, void *);
static result_t exposure_sheet_proc(window_t *, uint32_t, uint32_t, void *);

static void exposure_timing(void) {
  TEST("FPS and independent durations retime holds, reject invalid input, undo and drive playback");
  canvas_doc_t *doc = create_document(NULL, 8, 8);
  ASSERT_NOT_NULL(doc); ASSERT_EQUAL(doc->anim->frames[0].delay_ms, 83);
  ASSERT_TRUE(cmd_anim_fps(doc, 25, false));
  ASSERT_EQUAL(doc->anim->frames[0].delay_ms, 83);
  cmd_frame_add(doc, false); ASSERT_EQUAL(doc->anim->frames[1].delay_ms, 40);
  ASSERT_TRUE(cmd_frame_duration(doc, 120, false));
  ASSERT_EQUAL(doc->anim->frames[0].delay_ms, 83);
  ASSERT_TRUE(cmd_anim_fps(doc, 50, true));
  ASSERT_EQUAL(doc->anim->frames[0].delay_ms, 42); ASSERT_EQUAL(doc->anim->frames[1].delay_ms, 60);
  cmd_undo(doc); ASSERT_EQUAL(doc->anim->fps, 25); ASSERT_EQUAL(doc->anim->frames[1].delay_ms, 120);
  cmd_redo(doc); ASSERT_EQUAL(doc->anim->fps, 50); ASSERT_EQUAL(doc->anim->frames[1].delay_ms, 60);
  int history = doc->undo.count;
  ASSERT_FALSE(cmd_anim_fps(doc, 0, true)); ASSERT_FALSE(cmd_anim_fps(doc, 1001, false));
  ASSERT_FALSE(cmd_frame_duration(doc, 0, false)); ASSERT_FALSE(cmd_frame_duration(doc, 65536, true));
  ASSERT_EQUAL(doc->undo.count, history);
  ASSERT_TRUE(cmd_frame_duration(doc, 65535, true)); ASSERT_FALSE(cmd_anim_fps(doc, 1, true));
  ASSERT_EQUAL(doc->anim->fps, 50);
  ASSERT_TRUE(cmd_frame_duration(doc, 90, true));
  ASSERT_EQUAL(doc->anim->frames[0].delay_ms, 90); ASSERT_EQUAL(doc->anim->frames[1].delay_ms, 90);
  handle_menu_command(ID_ANIM_PLAY); ASSERT_TRUE(doc->anim->playing);
  ASSERT_TRUE(cmd_frame_duration(doc, 120, false)); ASSERT_FALSE(doc->anim->playing); ASSERT_EQUAL(g_app->anim_timer_id, 0);
  close_document(doc); PASS();
}

static void timing_dialogs(void) {
  TEST("FPS and duration dialogs validate input, apply scope and cancel without edits");
  canvas_doc_t *doc = create_document(NULL, 8, 8);
  animation_timing_state_t st = {.doc = doc};
  uint32_t result = 0;
  window_t *win = create_window_from_form(&imageeditor_animation_fps_form, 0, 0, NULL, animation_timing_proc, 0, &st);
  ASSERT_NOT_NULL(win); win->userdata2 = &result;
  window_t *ok = get_window_item(win, ID_ANIMATION_FPS_OK);
  set_window_item_text(win, ID_ANIMATION_FPS_VALUE, "24fps");
  send_message(win, evCommand, MAKEDWORD(ok->id, btnClicked), ok);
  ASSERT_TRUE(is_window(win)); ASSERT_EQUAL(doc->anim->fps, 12); ASSERT_EQUAL(doc->undo.count, 0);
  set_window_item_text(win, ID_ANIMATION_FPS_VALUE, "24");
  send_message(win, evCommand, MAKEDWORD(ok->id, btnClicked), ok);
  ASSERT_FALSE(is_window(win)); ASSERT_EQUAL(result, 1);
  ASSERT_EQUAL(doc->anim->fps, 24); ASSERT_EQUAL(doc->anim->frames[0].delay_ms, 42);
  cmd_frame_add(doc, false);
  st.duration = true; result = 0;
  win = create_window_from_form(&imageeditor_frame_duration_form, 0, 0, NULL, animation_timing_proc, 0, &st);
  ASSERT_NOT_NULL(win); win->userdata2 = &result;
  set_window_item_text(win, ID_FRAME_DURATION_VALUE, "250");
  send_message(get_window_item(win, ID_FRAME_DURATION_SCOPE), btnSetCheck, btnStateChecked, NULL);
  ok = get_window_item(win, ID_FRAME_DURATION_OK);
  send_message(win, evCommand, MAKEDWORD(ok->id, btnClicked), ok);
  ASSERT_FALSE(is_window(win)); ASSERT_EQUAL(doc->anim->frames[0].delay_ms, 250);
  ASSERT_EQUAL(doc->anim->frames[1].delay_ms, 250);
  int count = doc->undo.count;
  win = create_window_from_form(&imageeditor_frame_duration_form, 0, 0, NULL, animation_timing_proc, 0, &st);
  ASSERT_NOT_NULL(win); win->userdata2 = &result;
  set_window_item_text(win, ID_FRAME_DURATION_VALUE, "999");
  window_t *cancel = get_window_item(win, ID_FRAME_DURATION_CANCEL);
  send_message(win, evCommand, MAKEDWORD(cancel->id, btnClicked), cancel);
  ASSERT_EQUAL(doc->anim->frames[1].delay_ms, 250); ASSERT_EQUAL(doc->undo.count, count);
  close_document(doc); PASS();
}

static void exposure_sharing(void) {
  TEST("one cel used three times shares edits; Make Unique and history preserve identity");
  canvas_doc_t *doc = create_document(NULL, 8, 8);
  uint32_t id = doc->anim->frames[0].cel->id;
  ASSERT_TRUE(cmd_frame_reuse(doc, id)); ASSERT_TRUE(cmd_frame_reuse(doc, id));
  ASSERT_EQUAL(doc->anim->cel_count, 1); ASSERT_EQUAL(anim_cel_uses(doc->anim, id), 3);
  ASSERT_TRUE(ie_doc_begin_op(doc, "Shared stroke")); doc->pixels[0] = 71; ie_doc_commit_op(doc, true);
  ASSERT_TRUE(cmd_frame_select(doc, 0)); ASSERT_EQUAL(doc->pixels[0], 71);
  ASSERT_TRUE(cmd_frame_duration(doc, 230, false));
  ASSERT_EQUAL(doc->anim->frames[1].delay_ms, 83);
  ASSERT_TRUE(cmd_frame_unique(doc)); uint32_t unique = doc->anim->frames[0].cel->id;
  ASSERT_TRUE(unique != id); ASSERT_EQUAL(doc->anim->cel_count, 2);
  ASSERT_TRUE(ie_doc_begin_op(doc, "Unique stroke")); doc->pixels[0] = 200; ie_doc_commit_op(doc, true);
  ASSERT_TRUE(cmd_frame_select(doc, 1)); ASSERT_EQUAL(doc->pixels[0], 71);
  ASSERT_TRUE(cmd_frame_select(doc, 0)); ASSERT_EQUAL(doc->pixels[0], 200);
  cmd_undo(doc); ASSERT_EQUAL(doc->pixels[0], 71);
  cmd_undo(doc); ASSERT_EQUAL(doc->anim->frames[0].cel->id, id); ASSERT_EQUAL(doc->anim->cel_count, 1);
  cmd_redo(doc); ASSERT_EQUAL(doc->anim->frames[0].cel->id, unique);
  ASSERT_FALSE(cmd_cel_delete(doc, id));
  ASSERT_TRUE(cmd_exposure_edit(doc, id, 333, "held pose"));
  ASSERT_EQUAL(anim_cel_uses(doc->anim, unique), 0);
  ASSERT_TRUE(cmd_cel_delete(doc, unique)); ASSERT_EQUAL(doc->anim->cel_count, 1);
  cmd_undo(doc); ASSERT_NOT_NULL(anim_find_cel(doc->anim, unique));
  ASSERT_TRUE(cmd_cel_rename(doc, id, "Shared pose"));
  cmd_frame_move(doc, 0, 2);
  ASSERT_STR_EQUAL(doc->anim->frames[2].note, "held pose"); ASSERT_EQUAL(doc->anim->frames[2].delay_ms, 333);
  cmd_frame_delete(doc); ASSERT_NOT_NULL(anim_find_cel(doc->anim, id));
  close_document(doc); PASS();
}

static void exposure_persistence(void) {
  TEST("PTF retains repeated references, independent durations, notes, unused cels and resize");
  char path[] = "/tmp/orion-exposures-XXXXXX.ptf";
  int fd = mkstemps(path, 4); ASSERT_TRUE(fd >= 0); close(fd);
  canvas_doc_t *doc = create_document(NULL, 8, 8);
  uint32_t id = doc->anim->frames[0].cel->id;
  doc->pixels[0] = 81;
  ASSERT_TRUE(cmd_frame_reuse(doc, id));
  ASSERT_TRUE(cmd_exposure_edit(doc, id, 250, "second use"));
  cmd_frame_add(doc, false); uint32_t spare = doc->anim->frames[2].cel->id;
  doc->pixels[1] = 99; ASSERT_TRUE(doc_anim_commit(doc)); cmd_frame_delete(doc);
  ASSERT_EQUAL(anim_cel_uses(doc->anim, spare), 0);
  ASSERT_TRUE(flc_save(path, doc));
  int w, h; uint32_t palette[256], bg; bool show;
  anim_timeline_t *tl = flc_load(path, &w, &h, palette, &bg, &show);
  ASSERT_NOT_NULL(tl); ASSERT_EQUAL(tl->frame_count, 2); ASSERT_EQUAL(tl->cel_count, 2);
  ASSERT_TRUE(tl->frames[0].cel == tl->frames[1].cel);
  ASSERT_EQUAL(tl->frames[0].cel->id, id); ASSERT_EQUAL(tl->frames[0].delay_ms, 83);
  ASSERT_EQUAL(tl->frames[1].delay_ms, 250); ASSERT_STR_EQUAL(tl->frames[1].note, "second use");
  ASSERT_EQUAL(test_anim_cel_pixel(anim_find_cel(tl, spare), 1, 1, 8, 8), 99);
  uint32_t next = tl->next_cel_id; ASSERT_TRUE(next > spare);
  anim_timeline_free(tl);
  cmd_resize_canvas(doc, 10, 10);
  uint8_t plane[100]; ASSERT_TRUE(anim_frame_expand_cel(anim_find_cel(doc->anim, spare), 1, plane, 10, 10));
  ASSERT_EQUAL(plane[1], 99); ASSERT_TRUE(doc->anim->frames[0].cel == doc->anim->frames[1].cel);
  cmd_undo(doc); ASSERT_EQUAL(doc->canvas_w, 8);
  // A bank reference to a nonexistent drawing must reject the entire load.
  FILE *fp = fopen(path, "r+b"); ASSERT_NOT_NULL(fp); uint8_t ch[16];
  ASSERT_EQUAL(fseek(fp, 128, SEEK_SET), 0);
  for (;;) {
    ASSERT_EQUAL(fread(ch, 1, 16, fp), 16);
    uint32_t length = ch[0] | (uint32_t)ch[1] << 8 | (uint32_t)ch[2] << 16 | (uint32_t)ch[3] << 24;
    if (ch[4] == 0x76 && ch[5] == 0x70) break;
    ASSERT_EQUAL(fseek(fp, length - 16, SEEK_CUR), 0);
  }
  ASSERT_EQUAL(fseek(fp, 12 + 2 * 36, SEEK_CUR), 0);
  uint8_t bad_id[4] = {255,255,255,127}; ASSERT_EQUAL(fwrite(bad_id, 1, 4, fp), 4); fclose(fp);
  ASSERT_NULL(flc_load(path, &w, &h, palette, &bg, &show));
  remove(path); close_document(doc); PASS();
}

static void exposure_view(void) {
  TEST("declarative cel bank and sheet bind data and dispatch row edits through commands");
  canvas_doc_t *doc = create_document(NULL, 8, 8);
  animation_db_state_t binding = {.doc = doc};
  database_t *db = animation_db_bind(&binding); ASSERT_NOT_NULL(db);
  window_t *win = create_window_from_form(&imageeditor_exposure_sheet_form, 20, 20, NULL, exposure_sheet_proc, 0, doc);
  ASSERT_NOT_NULL(win);
  window_t *bank = get_window_item(win, ID_EXPOSURE_SHEET_BANK), *sheet = get_window_item(win, ID_EXPOSURE_SHEET_SHEET);
  ASSERT_NOT_NULL(bank); ASSERT_NOT_NULL(sheet);
  ASSERT_EQUAL(send_message(bank, RVM_GETITEMCOUNT, 0, NULL), 1);
  ASSERT_EQUAL(send_message(sheet, RVM_GETITEMCOUNT, 0, NULL), 1);
  window_t *button = get_window_item(win, ID_EXPOSURE_SHEET_INSERT);
  send_message(win, evCommand, MAKEDWORD(button->id, btnClicked), button);
  ASSERT_EQUAL(doc->anim->frame_count, 2); ASSERT_EQUAL(doc->anim->cel_count, 1);
  set_window_item_text(win, ID_EXPOSURE_SHEET_DURATION, "375");
  set_window_item_text(win, ID_EXPOSURE_SHEET_NOTE, "test note");
  button = get_window_item(win, ID_EXPOSURE_SHEET_APPLY);
  send_message(win, evCommand, MAKEDWORD(button->id, btnClicked), button);
  ASSERT_EQUAL(doc->anim->frames[1].delay_ms, 375); ASSERT_STR_EQUAL(doc->anim->frames[1].note, "test note");
  ASSERT_EQUAL(send_message(sheet, RVM_GETITEMCOUNT, 0, NULL), 2);
  destroy_window(win); animation_db_unbind(db); close_document(doc); PASS();
}

int main(void) {
  TEST_START("Pencil Test exposures and cel bank");
  test_env_init(); register_commctl_classes(); g_app = calloc(1, sizeof(*g_app));
  g_app->fg_color = IE_INK_COLOR;
  g_app->current_tool = ID_TOOL_BRUSH;
  create_tool_palette_window();
  exposure_timing(); timing_dialogs(); exposure_sharing(); exposure_persistence(); exposure_view();
  while (g_app->docs) close_document(g_app->docs);
  test_env_shutdown(); free(g_app); g_app = NULL;
  TEST_END();
}
