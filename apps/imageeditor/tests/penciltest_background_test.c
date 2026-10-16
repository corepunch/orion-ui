#define IMAGEEDITOR_BW 1
#define IMAGEEDITOR_BW_RETINA
#include "test_framework.h"
#include "test_env.h"
#include "apps/imageeditor/imageeditor.h"
#include "apps/imageeditor/commands/commands.h"

app_state_t *g_app;
int g_bw_retina_scale = 1;
static char bg_test_dir[512];

#define STOCK_DIR "apps/imageeditor/penciltest/share"

static canvas_doc_t *bg_setup(void) {
  test_env_init();
  g_app = calloc(1, sizeof(*g_app));
  g_app->current_tool = ID_TOOL_PENCIL;
  g_app->fg_color = IE_INK_COLOR;
  create_tool_palette_window();
  return create_document(NULL, 16, 16);
}

static void bg_teardown(void) {
  while (g_app->docs) close_document(g_app->docs);
  test_env_shutdown(); free(g_app); g_app = NULL;
}

// 32x16 picture: red left half, blue right half; covering 16x16 keeps the center.
static bg_image_t *bg_two_tone(int w, int h) {
  uint8_t src[32 * 16 * 4];
  for (int i = 0; i < 32 * 16; i++) {
    bool left = i % 32 < 16;
    uint8_t px[4] = {left ? 200 : 10, 30, left ? 10 : 220, 255};
    memcpy(src + i * 4, px, 4);
  }
  return bg_image_cover(src, 32, 16, w, h);
}

static void test_resample(void) {
  TEST("resampler averages in linear light, ignores transparent color and crops a centered cover");
  uint8_t bw[8] = {0, 0, 0, 255, 255, 255, 255, 255};
  int all[4] = {0, 0, 2, 1};
  uint8_t *out = image_resample_srgba8(bw, 2, 1, all, 1, 1);
  ASSERT_NOT_NULL(out);
  ASSERT_EQUAL(out[0], 188); ASSERT_EQUAL(out[3], 255); free(out);
  uint8_t ghost[8] = {255, 0, 0, 255, 0, 255, 0, 0};
  out = image_resample_srgba8(ghost, 2, 1, all, 1, 1);
  ASSERT_EQUAL(out[0], 255); ASSERT_EQUAL(out[1], 0); ASSERT_EQUAL(out[3], 128); free(out);
  int rect[4];
  image_cover_rect(1280, 720, 1, 1, rect);
  ASSERT_EQUAL(rect[0], 280); ASSERT_EQUAL(rect[1], 0); ASSERT_EQUAL(rect[2], 720); ASSERT_EQUAL(rect[3], 720);
  image_cover_rect(1280, 720, 4, 3, rect);
  ASSERT_EQUAL(rect[0], 160); ASSERT_EQUAL(rect[2], 960); ASSERT_EQUAL(rect[3], 720);
  int bad[4] = {1, 0, 2, 1};
  ASSERT_NULL(image_resample_srgba8(bw, 2, 1, bad, 1, 1));
  PASS();
}

static void test_command_history(void) {
  TEST("setting a picture clears the Background plane in one undoable command shared by checkpoints");
  canvas_doc_t *doc = bg_setup();
  bg_image_t *img = bg_two_tone(16, 16);
  ASSERT_NOT_NULL(img);
  ASSERT_EQUAL(img->pixels[0], 200); ASSERT_EQUAL(img->pixels[(16 * 15 + 15) * 4 + 2], 220);
  ASSERT_EQUAL(doc->layer.stack[IE_LAYER_BG]->pixels[0], 1);
  ASSERT_TRUE(cmd_background_set_image(doc, img));
  ASSERT_TRUE(doc->background.image == img);
  ASSERT_EQUAL(img->refs, 2);
  ASSERT_EQUAL(doc->layer.stack[IE_LAYER_BG]->pixels[0], 255);
  ASSERT_EQUAL(doc->undo.count, 1);
  cmd_undo(doc);
  ASSERT_NULL(doc->background.image);
  ASSERT_EQUAL(doc->layer.stack[IE_LAYER_BG]->pixels[0], 1);
  cmd_redo(doc);
  ASSERT_TRUE(doc->background.image == img);
  ASSERT_EQUAL(doc->layer.stack[IE_LAYER_BG]->pixels[0], 255);
  cmd_background_remove(doc);
  ASSERT_NULL(doc->background.image);
  ASSERT_EQUAL(doc->undo.count, 2);
  cmd_background_remove(doc);
  ASSERT_EQUAL(doc->undo.count, 2);
  cmd_undo(doc);
  ASSERT_TRUE(doc->background.image == img);
  bg_image_t *wrong = bg_image_new(8, 8);
  ASSERT_FALSE(cmd_background_set_image(doc, wrong));
  ASSERT_TRUE(doc->background.image == img);
  bg_image_release(wrong);
  bg_image_release(img);
  bg_teardown(); PASS();
}

static void test_composite(void) {
  TEST("exports blend the picture under Background paint and the animated layers");
  canvas_doc_t *doc = bg_setup();
  bg_image_t *img = bg_two_tone(16, 16);
  ASSERT_TRUE(cmd_background_set_image(doc, img));
  ASSERT_TRUE(cmd_pencil_layer(doc, IE_LAYER_BG) && ie_doc_begin_op(doc, "Paint"));
  canvas_set_pixel(doc, 1, 0, pencil_default_colors()[12]);
  ie_doc_commit_op(doc, true);
  uint8_t rgba[16 * 16 * 4];
  ASSERT_TRUE(doc_anim_rgba(doc, 0, rgba));
  ASSERT_EQUAL(memcmp(rgba, img->pixels, 4), 0);
  ASSERT_EQUAL(memcmp(rgba + 60 * 4, img->pixels + 60 * 4, 4), 0);
  ASSERT_EQUAL(rgba[4], COLOR_R(pencil_default_colors()[12]));
  doc->layer.stack[IE_LAYER_BG]->visible = false;
  ASSERT_TRUE(doc_anim_rgba(doc, 0, rgba));
  ASSERT_EQUAL(rgba[0], COLOR_R(IE_PAPER_COLOR));
  bg_image_release(img);
  bg_teardown(); PASS();
}

static void test_project(void) {
  TEST("PTF stores the picture losslessly; older layers stay intact");
  canvas_doc_t *doc = bg_setup();
  bg_image_t *img = bg_two_tone(16, 16);
  ASSERT_TRUE(cmd_background_set_image(doc, img));
  char path[600]; snprintf(path, sizeof(path), "%s/background.ptf", bg_test_dir);
  ASSERT_TRUE(image_io_save(path, doc));
  ASSERT_TRUE(imageeditor_open_file_path(path));
  canvas_doc_t *again = g_app->active_doc;
  ASSERT_TRUE(again != doc);
  ASSERT_NOT_NULL(again->background.image);
  ASSERT_EQUAL(again->background.image->w, 16);
  ASSERT_EQUAL(memcmp(again->background.image->pixels, img->pixels, 16 * 16 * 4), 0);
  ASSERT_EQUAL(again->layer.stack[IE_LAYER_BG]->pixels[0], 255);
  ASSERT_EQUAL(again->undo.count, 0);
  cmd_background_remove(again);
  ASSERT_TRUE(image_io_save(path, again));
  ASSERT_TRUE(imageeditor_open_file_path(path));
  ASSERT_NULL(g_app->active_doc->background.image);
  remove(path);
  bg_image_release(img);
  bg_teardown(); PASS();
}

static void test_resize(void) {
  TEST("image size resamples and canvas size crops the picture inside the resize command");
  canvas_doc_t *doc = bg_setup();
  bg_image_t *img = bg_two_tone(16, 16);
  ASSERT_TRUE(cmd_background_set_image(doc, img));
  cmd_resize_image(doc, 8, 4, IMAGE_RESIZE_BILINEAR);
  ASSERT_EQUAL(doc->background.image->w, 8); ASSERT_EQUAL(doc->background.image->h, 4);
  ASSERT_EQUAL(doc->background.image->pixels[0], 200);
  cmd_undo(doc);
  ASSERT_TRUE(doc->background.image == img);
  cmd_resize_canvas(doc, 20, 16);
  ASSERT_EQUAL(doc->background.image->w, 20);
  ASSERT_EQUAL(memcmp(doc->background.image->pixels, img->pixels, 16 * 4), 0);
  ASSERT_EQUAL(doc->background.image->pixels[18 * 4 + 3], 0);
  cmd_undo(doc);
  ASSERT_TRUE(doc->background.image == img);
  bg_image_release(img);
  bg_teardown(); PASS();
}

static void test_stock(void) {
  TEST("every stock plate has a square 128px thumbnail and loads as a cover-cropped picture");
  DIR *dir = opendir(STOCK_DIR);
  ASSERT_NOT_NULL(dir);
  int plates = 0;
  for (struct dirent *e; (e = readdir(dir));) {
    size_t len = strlen(e->d_name);
    if (len < 5 || strcmp(e->d_name + len - 4, ".jpg")) continue;
    char path[600]; int w = 0, h = 0;
    snprintf(path, sizeof(path), STOCK_DIR "/thumbs/%s", e->d_name);
    uint8_t *thumb = load_image(path, &w, &h);
    ASSERT_NOT_NULL(thumb); ASSERT_EQUAL(w, 128); ASSERT_EQUAL(h, 128);
    image_free(thumb);
    plates++;
  }
  closedir(dir);
  ASSERT_TRUE(plates >= 60);
  bg_image_t *img = bg_image_load(STOCK_DIR "/forest-glade.jpg", 64, 48);
  ASSERT_NOT_NULL(img); ASSERT_EQUAL(img->w, 64); ASSERT_EQUAL(img->pixels[3], 255);
  bg_image_release(img);
  ASSERT_NULL(bg_image_load(STOCK_DIR "/missing.jpg", 64, 48));
  PASS();
}

int main(void) {
  TEST_START("Pencil Test background image");
  const char *temp_dir = getenv("TMPDIR");
  if (!temp_dir) temp_dir = "/tmp";
  snprintf(bg_test_dir, sizeof(bg_test_dir), "%s/orion-background-XXXXXX", temp_dir);
  if (!mkdtemp(bg_test_dir)) { perror(bg_test_dir); return 1; }
  test_resample(); test_command_history(); test_composite();
  test_project(); test_resize(); test_stock();
  rmdir(bg_test_dir);
  TEST_END();
}
