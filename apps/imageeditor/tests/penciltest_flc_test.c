#define IMAGEEDITOR_BW 1
#define IMAGEEDITOR_BW_RETINA
#include "test_framework.h"
#include "test_env.h"
#include "apps/imageeditor/imageeditor.h"

app_state_t *g_app;
int g_bw_retina_scale = 1;
static char flc_test_dir[512], flc_test_path[600];
static const char *flc_picker_result;
static int flc_error_count;
static bool flc_test_save_picker(openfilename_t *ofn) {
  if (!flc_picker_result) return false;
  snprintf(ofn->lpstrFile, ofn->nMaxFile, "%s", flc_picker_result);
  return true;
}
static int flc_test_message_box(window_t *win, const char *text, const char *title, uint32_t flags) {
  (void)win; (void)text; (void)title; (void)flags;
  flc_error_count++;
  return IDOK;
}
#define get_save_filename flc_test_save_picker
#define message_box flc_test_message_box

static void flc_roundtrip(void) {
  TEST("FLC saves every frame, live pixels, timing, names, palette and background");
  canvas_doc_t *doc = create_document(NULL, 5, 3);
  ASSERT_NOT_NULL(doc);
  doc->pixels[0] = 1;
  cmd_frame_add(doc, true);
  ASSERT_EQUAL(doc->anim->frame_count, 2);
  doc->pixels[4] = 2;
  doc->anim->fps = 24; doc->anim->loop = false;
  doc->anim->frames[0]->delay_ms = 42;
  doc->anim->frames[1]->delay_ms = 210;
  strcpy(doc->anim->frames[1]->name, "Hold pose");
  doc->background.color = MAKE_COLOR(210, 220, 230, 255);
  doc->background.show = false;
  doc->ipal.entries[7] = MAKE_COLOR(1, 2, 3, 123);
  ASSERT_TRUE(image_io_save(flc_test_path, doc));
  ASSERT_EQUAL(doc->anim->active_frame, 1);
  ASSERT_TRUE(flc_is_file(flc_test_path));
  int w = 0, h = 0; uint32_t pal[256], bg; bool show;
  anim_timeline_t *tl = flc_load(flc_test_path, &w, &h, pal, &bg, &show);
  ASSERT_NOT_NULL(tl);
  ASSERT_EQUAL(w, 5); ASSERT_EQUAL(h, 3); ASSERT_EQUAL(tl->frame_count, 2);
  ASSERT_EQUAL(tl->fps, 24); ASSERT_FALSE(tl->loop);
  ASSERT_EQUAL(tl->frames[0]->delay_ms, 42); ASSERT_EQUAL(tl->frames[1]->delay_ms, 210);
  ASSERT_STR_EQUAL(tl->frames[1]->name, "Hold pose");
  ASSERT_EQUAL(tl->frames[0]->data[4], 0);
  ASSERT_EQUAL(memcmp(tl->frames[1]->data, doc->pixels, 15), 0);
  ASSERT_EQUAL(memcmp(pal, doc->ipal.entries, sizeof(pal)), 0);
  ASSERT_EQUAL(bg, doc->background.color); ASSERT_FALSE(show);
  anim_timeline_free(tl);
  // Failed writes must leave an existing file readable and unchanged.
  doc->anim->frames[1]->delay_ms = 70000;
  ASSERT_FALSE(image_io_save(flc_test_path, doc));
  tl = flc_load(flc_test_path, &w, &h, pal, &bg, &show);
  ASSERT_NOT_NULL(tl); ASSERT_EQUAL(tl->frames[1]->delay_ms, 210);
  anim_timeline_free(tl);
  close_document(doc);
  PASS();
}

static void flc_menu_and_retina(void) {
  TEST("Open preserves physical dimensions at Retina scale; File Save persists edited frames");
  g_bw_retina_scale = 2;
  ASSERT_TRUE(imageeditor_open_file_path(flc_test_path));
  canvas_doc_t *doc = g_app->active_doc;
  ASSERT_EQUAL(doc->canvas_w, 5); ASSERT_EQUAL(doc->canvas_h, 3);
  ASSERT_EQUAL(doc->anim->frame_count, 2);
  ASSERT_TRUE(cmd_frame_select(doc, 1));
  doc->pixels[8] = 1; doc->modified = true;
  handle_menu_command(ID_FILE_SAVE);
  ASSERT_FALSE(doc->modified);
  ASSERT_TRUE(imageeditor_open_file_path(flc_test_path));
  canvas_doc_t *again = g_app->active_doc;
  ASSERT_EQUAL(again->anim->frames[1]->data[8], 1);
  ASSERT_EQUAL(again->canvas_w, 5);
  close_document(again); close_document(doc);
  g_bw_retina_scale = 1;
  PASS();
}

static void flc_save_as(void) {
  TEST("Save As cancellation/failure preserves name and edits; success updates both");
  ASSERT_TRUE(imageeditor_open_file_path(flc_test_path));
  canvas_doc_t *doc = g_app->active_doc;
  doc->pixels[9] = 1; doc->modified = true;
  flc_picker_result = NULL;
  handle_menu_command(ID_FILE_SAVEAS);
  ASSERT_TRUE(doc->modified); ASSERT_STR_EQUAL(doc->filename, flc_test_path);
  char missing[600]; snprintf(missing, sizeof(missing), "%s/missing/save.flc", flc_test_dir);
  flc_picker_result = missing;
  int errors = flc_error_count;
  handle_menu_command(ID_FILE_SAVEAS);
  ASSERT_EQUAL(flc_error_count, errors + 1);
  ASSERT_TRUE(doc->modified); ASSERT_STR_EQUAL(doc->filename, flc_test_path);
  char saved[600]; snprintf(saved, sizeof(saved), "%s/save-as.flc", flc_test_dir);
  flc_picker_result = saved;
  handle_menu_command(ID_FILE_SAVEAS);
  ASSERT_FALSE(doc->modified); ASSERT_STR_EQUAL(doc->filename, saved);
  ASSERT_TRUE(imageeditor_open_file_path(saved));
  ASSERT_EQUAL(g_app->active_doc->pixels[9], 1);
  close_document(g_app->active_doc); close_document(doc); remove(saved);
  flc_picker_result = NULL;
  PASS();
}

static void fixture_u16(uint8_t *p, unsigned n) { p[0] = n; p[1] = n >> 8; }
static void fixture_u32(uint8_t *p, unsigned n) { fixture_u16(p, n); fixture_u16(p + 2, n >> 16); }
static size_t fixture_frame(uint8_t *out, int type, const uint8_t *data, int length) {
  size_t size = 22 + length;
  memset(out, 0, size);
  fixture_u32(out, size); fixture_u16(out + 4, 0xf1fa); fixture_u16(out + 6, 1);
  fixture_u32(out + 16, 6 + length); fixture_u16(out + 20, type);
  memcpy(out + 22, data, length);
  return size;
}

static void flc_external_compression(void) {
  TEST("external FLC COLOR256, BRUN, SS2, LC, BLACK, COPY and repeated frames decode");
  uint8_t file[1024] = {0}; size_t size = 128;
  // 5 x 3, odd widths exercise SS2's last-pixel opcode and line skips.
  uint8_t palette[] = {1,0, 0,3, 0,0,0, 255,255,255, 255,0,0};
  uint8_t brun[] = {1,0xfb,0,0,0,0,0, 1,5,1, 1,5,2};
  uint8_t ss2[] = {1,0, 0xff,0xff, 2,0x80, 2,0, 1,0xff,2,0, 0,1,1,2};
  uint8_t lc[] = {2,0, 1,0, 2, 0,1,0, 0,0xfe,1};
  uint8_t copy[] = {0,1,2,0,1, 2,0,1,2,0, 1,2,0,1,2};
  size += fixture_frame(file + size, 4, palette, sizeof(palette));
  size += fixture_frame(file + size, 15, brun, sizeof(brun));
  size += fixture_frame(file + size, 7, ss2, sizeof(ss2));
  size += fixture_frame(file + size, 12, lc, sizeof(lc));
  size += fixture_frame(file + size, 13, copy, 0);
  size += fixture_frame(file + size, 16, copy, sizeof(copy));
  fixture_u32(file + size, 16); fixture_u16(file + size + 4, 0xf1fa); size += 16;
  fixture_u32(file, size); fixture_u16(file + 4, 0xaf12); fixture_u16(file + 6, 7);
  fixture_u16(file + 8, 5); fixture_u16(file + 10, 3); fixture_u16(file + 12, 8); fixture_u32(file + 16, 100);
  char path[600]; snprintf(path, sizeof(path), "%s/external.flc", flc_test_dir);
  FILE *fp = fopen(path, "wb"); ASSERT_NOT_NULL(fp);
  ASSERT_EQUAL(fwrite(file, 1, size, fp), size); fclose(fp);
  int w, h; uint32_t pal[256], bg; bool show;
  anim_timeline_t *tl = flc_load(path, &w, &h, pal, &bg, &show);
  ASSERT_NOT_NULL(tl); ASSERT_EQUAL(tl->frame_count, 7); ASSERT_EQUAL(tl->fps, 10);
  uint32_t black = MAKE_COLOR(0,0,0,255), red = MAKE_COLOR(255,0,0,255), white = MAKE_COLOR(255,255,255,255);
  ASSERT_EQUAL(pal[tl->frames[1]->data[0]], black);
  ASSERT_EQUAL(pal[tl->frames[1]->data[5]], white);
  ASSERT_EQUAL(pal[tl->frames[1]->data[10]], red);
  ASSERT_EQUAL(pal[tl->frames[2]->data[6]], red);
  ASSERT_EQUAL(pal[tl->frames[2]->data[7]], black);
  ASSERT_EQUAL(pal[tl->frames[2]->data[9]], red);
  ASSERT_EQUAL(pal[tl->frames[3]->data[11]], white);
  ASSERT_EQUAL(pal[tl->frames[3]->data[12]], white);
  ASSERT_EQUAL(pal[tl->frames[4]->data[12]], black);
  ASSERT_EQUAL(memcmp(tl->frames[5]->data, tl->frames[6]->data, 15), 0);
  ASSERT_EQUAL(tl->frames[6]->delay_ms, 100);
  anim_timeline_free(tl);
  // Same fixture with legacy FLI magic, 70 Hz time units and 6-bit palette.
  fixture_u16(file + 4, 0xaf11); fixture_u32(file + 16, 7);
  fixture_u16(file + 148, 11);
  for (int i = 0; i < 9; i++) file[154 + i] >>= 2;
  fp = fopen(path, "wb"); fwrite(file, 1, size, fp); fclose(fp);
  tl = flc_load(path, &w, &h, pal, &bg, &show);
  ASSERT_NOT_NULL(tl); ASSERT_EQUAL(tl->frames[0]->delay_ms, 100);
  ASSERT_EQUAL(pal[tl->frames[1]->data[5]], MAKE_COLOR(252,252,252,255));
  anim_timeline_free(tl); remove(path);
  PASS();
}

static void flc_reject_corruption(void) {
  TEST("truncated and malformed animations fail without adding a document");
  FILE *fp = fopen(flc_test_path, "rb"); ASSERT_NOT_NULL(fp);
  fseek(fp, 0, SEEK_END); long size = ftell(fp); rewind(fp);
  uint8_t *file = malloc(size); ASSERT_NOT_NULL(file);
  ASSERT_EQUAL(fread(file, 1, size, fp), (size_t)size); fclose(fp);
  char path[600]; snprintf(path, sizeof(path), "%s/broken.flc", flc_test_dir);
  int cuts[] = {0, 5, 127, 128, 143, 149, (int)size - 1};
  canvas_doc_t *before = g_app->docs;
  for (int i = 0; i < ARRAY_LEN(cuts); i++) {
    fp = fopen(path, "wb"); fwrite(file, 1, cuts[i], fp); fclose(fp);
    ASSERT_FALSE(imageeditor_open_file_path(path)); ASSERT_TRUE(g_app->docs == before);
  }
  int offsets[] = {6, 8, 10, 12, 128, 144};
  for (int i = 0; i < ARRAY_LEN(offsets); i++) {
    uint8_t save[2]; memcpy(save, file + offsets[i], 2); memset(file + offsets[i], 0xff, 2);
    fp = fopen(path, "wb"); fwrite(file, 1, size, fp); fclose(fp);
    ASSERT_FALSE(imageeditor_open_file_path(path)); ASSERT_TRUE(g_app->docs == before);
    memcpy(file + offsets[i], save, 2);
  }
  free(file); remove(path);
  PASS();
}

static void flc_color_palette_roundtrip(void) {
  TEST("16 coloring swatches retain exact colors through FLC save and reopen");
  canvas_doc_t *doc = create_document(NULL, IE_PENCIL_COLORS, 2);
  ASSERT_NOT_NULL(doc);
  for (int i = 0; i < IE_PENCIL_COLORS; i++) {
    ASSERT_TRUE(cmd_pencil_color(doc, i));
    canvas_set_pixel(doc, i, 0, g_app->fg_color);
  }
  cmd_frame_add(doc, true);
  ASSERT_TRUE(cmd_pencil_color(doc, 4));
  canvas_set_pixel(doc, 0, 1, g_app->fg_color);
  char path[600]; snprintf(path, sizeof(path), "%s/colors.flc", flc_test_dir);
  ASSERT_TRUE(image_io_save(path, doc));
  ASSERT_TRUE(imageeditor_open_file_path(path));
  canvas_doc_t *again = g_app->active_doc;
  for (int frame = 0; frame < 2; frame++) {
    ASSERT_TRUE(cmd_frame_select(again, frame));
    for (int i = 0; i < IE_PENCIL_COLORS; i++) ASSERT_EQUAL(canvas_get_pixel(again, i, 0), k_pencil_palette[i]);
  }
  ASSERT_EQUAL(canvas_get_pixel(again, 0, 1), k_pencil_palette[4]);
  close_document(again); close_document(doc); remove(path);
  PASS();
}

int main(void) {
  TEST_START("Penciltest FLC persistence");
  snprintf(flc_test_dir, sizeof(flc_test_dir), "%s/orion-flc-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
  if (!mkdtemp(flc_test_dir)) return 1;
  snprintf(flc_test_path, sizeof(flc_test_path), "%s/animation.flc", flc_test_dir);
  test_env_init(); g_app = calloc(1, sizeof(*g_app));
  flc_roundtrip(); flc_menu_and_retina(); flc_save_as(); flc_external_compression(); flc_reject_corruption();
  flc_color_palette_roundtrip();
  while (g_app->docs) close_document(g_app->docs);
  free(g_app); g_app = NULL; test_env_shutdown();
  if (getenv("FLC_TEST_KEEP_FILES")) printf("FLC fixture: %s\n", flc_test_path);
  else { remove(flc_test_path); rmdir(flc_test_dir); }
  TEST_END();
}
