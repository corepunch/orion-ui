#define IMAGEEDITOR_BW 1
#define IMAGEEDITOR_BW_RETINA
#include "test_framework.h"
#include "test_env.h"
#include "apps/imageeditor/imageeditor.h"

app_state_t *g_app;
int g_bw_retina_scale = 1;
static char layer_test_dir[512];

static canvas_doc_t *layers_setup(void) {
  test_env_init();
  g_app = calloc(1, sizeof(*g_app));
  g_app->current_tool = g_app->brush_tool = ID_TOOL_PENCIL;
  g_app->fg_color = IE_INK_COLOR;
  create_tool_palette_window();
  return create_document(NULL, 16, 16);
}

static void layers_teardown(void) {
  while (g_app->docs) close_document(g_app->docs);
  test_env_shutdown(); free(g_app); g_app = NULL;
}

static bool layer_pixel(canvas_doc_t *doc, int layer, int x, int y, int swatch) {
  if (!cmd_pencil_layer(doc, layer) || !ie_doc_begin_op(doc, "Layer Stroke")) return false;
  canvas_set_pixel(doc, x, y, k_pencil_palette[swatch]);
  ie_doc_commit_op(doc, true);
  return true;
}

static int color_button_count(void) {
  toolbar_state_t *tb = window_toolbar_state(g_app->tool_win);
  int count = 0;
  for (int i = 0; i < tb->item_count; i++)
    if (tb->items[i].ident >= IE_PENCIL_PALETTE_BASE && tb->items[i].ident < IE_PENCIL_PALETTE_BASE + IE_PENCIL_COLORS) count++;
  return count;
}

static void test_layer_sidebar(void) {
  TEST("2x2 sidebar layer buttons switch the target and show colors only on painting layers");
  canvas_doc_t *doc = layers_setup();
  ASSERT_NOT_NULL(doc);
  ASSERT_EQUAL(doc->layer.count, 4);
  ASSERT_EQUAL(doc->layer.active, IE_LAYER_PENCIL);
  ASSERT_EQUAL(color_button_count(), 0);
  const int order[] = {IE_LAYER_BG, IE_LAYER_PENCIL, IE_LAYER_COLOR, IE_LAYER_FX};
  for (int i = 0; i < 4; i++) {
    toolbar_state_t *tb = window_toolbar_state(g_app->tool_win);
    ASSERT_EQUAL(tb->items[i].ident, IE_PENCIL_LAYER_BASE + order[i]);
    ASSERT_EQUAL(tb->item_rects[0].y, tb->item_rects[1].y);
    ASSERT_EQUAL(tb->item_rects[2].y, tb->item_rects[3].y);
    irect16_t r = tb->item_rects[i];
    uint32_t point = MAKEDWORD(r.x + r.w / 2, r.y + r.h / 2);
    send_message(g_app->tool_win->toolbar, evLeftButtonDown, point, NULL);
    send_message(g_app->tool_win->toolbar, evLeftButtonUp, point, NULL);
    ASSERT_EQUAL(doc->layer.active, order[i]);
    ASSERT_TRUE(doc->pixels == doc->layer.stack[order[i]]->pixels);
    ASSERT_EQUAL(color_button_count(), order[i] == IE_LAYER_PENCIL ? 0 : 16);
  }
  ASSERT_TRUE(cmd_pencil_color(doc, 12));
  ASSERT_TRUE(cmd_pencil_layer(doc, IE_LAYER_PENCIL));
  ASSERT_EQUAL(g_app->fg_color, IE_INK_COLOR);
  ASSERT_TRUE(cmd_pencil_layer(doc, IE_LAYER_FX));
  ASSERT_EQUAL(g_app->fg_color, k_pencil_palette[12]);
  ASSERT_EQUAL(doc->undo.count, 0);
  layers_teardown(); PASS();
}

static void test_layer_frames(void) {
  TEST("shared background and three cels survive blank/duplicate/move/delete, playback and undo");
  canvas_doc_t *doc = layers_setup();
  ASSERT_TRUE(layer_pixel(doc, IE_LAYER_BG, 0, 0, 10));
  ASSERT_TRUE(layer_pixel(doc, IE_LAYER_COLOR, 1, 0, 4));
  ASSERT_TRUE(layer_pixel(doc, IE_LAYER_PENCIL, 2, 0, 0));
  ASSERT_TRUE(layer_pixel(doc, IE_LAYER_FX, 3, 0, 12));
  cmd_frame_add(doc, true);
  ASSERT_EQUAL(doc->layer.stack[IE_LAYER_COLOR]->pixels[1], 5);
  ASSERT_EQUAL(doc->layer.stack[IE_LAYER_PENCIL]->pixels[2], IE_PENCIL_MAX_OPACITY);
  ASSERT_EQUAL(doc->layer.stack[IE_LAYER_FX]->pixels[3], 13);
  ASSERT_TRUE(layer_pixel(doc, IE_LAYER_FX, 3, 0, 14));
  cmd_undo(doc); ASSERT_EQUAL(doc->pixels[3], 13);
  cmd_redo(doc); ASSERT_EQUAL(doc->pixels[3], 15);
  cmd_frame_add(doc, false);
  ASSERT_EQUAL(doc->layer.stack[IE_LAYER_BG]->pixels[0], 11);
  for (int i = 1; i < 4; i++) for (int p = 0; p < 4; p++) ASSERT_EQUAL(doc->layer.stack[i]->pixels[p], 0);
  cmd_frame_move(doc, 1, 0);
  ASSERT_TRUE(cmd_frame_select(doc, 0));
  ASSERT_EQUAL(doc->layer.stack[IE_LAYER_FX]->pixels[3], 15);
  doc->anim->playing = true; doc->anim->playback_start_frame = 0;
  anim_tick(doc);
  ASSERT_EQUAL(doc->layer.stack[IE_LAYER_FX]->pixels[3], 13);
  anim_stop_playback(doc);
  ASSERT_EQUAL(doc->layer.stack[IE_LAYER_FX]->pixels[3], 15);
  cmd_frame_delete(doc);
  ASSERT_EQUAL(doc->layer.stack[IE_LAYER_FX]->pixels[3], 13);
  ASSERT_EQUAL(doc->layer.stack[IE_LAYER_BG]->pixels[0], 11);
  cmd_undo(doc);
  ASSERT_EQUAL(doc->anim->frame_count, 3);
  ASSERT_EQUAL(doc->layer.stack[IE_LAYER_FX]->pixels[3], 15);
  layers_teardown(); PASS();
}

static void test_underpaint_fill(void) {
  TEST("color fill uses hard ink outlines without changing pencil or shared background");
  canvas_doc_t *doc = layers_setup();
  ASSERT_TRUE(cmd_pencil_layer(doc, IE_LAYER_COLOR));
  ASSERT_TRUE(ie_doc_begin_op(doc, "Outline"));
  for (int i = 3; i <= 12; i++) {
    canvas_set_pixel(doc, i, 3, IE_INK_COLOR); canvas_set_pixel(doc, i, 12, IE_INK_COLOR);
    canvas_set_pixel(doc, 3, i, IE_INK_COLOR); canvas_set_pixel(doc, 12, i, IE_INK_COLOR);
  }
  ie_doc_commit_op(doc, true);
  uint8_t ink_top = doc->pixels[3 * 16 + 6], ink_left = doc->pixels[6 * 16 + 3];
  ASSERT_TRUE(ie_doc_begin_op(doc, "Underpaint"));
  ASSERT_TRUE(pencil_color_fill(doc, 6, 6, k_pencil_palette[4], 2));
  ie_doc_commit_op(doc, true);
  ASSERT_EQUAL(canvas_get_pixel(doc, 6, 6), k_pencil_palette[4]);
  ASSERT_EQUAL(doc->pixels[0], 0);
  ASSERT_EQUAL(doc->layer.stack[IE_LAYER_COLOR]->pixels[3 * 16 + 6], ink_top);
  ASSERT_EQUAL(doc->layer.stack[IE_LAYER_COLOR]->pixels[6 * 16 + 3], ink_left);
  ASSERT_EQUAL(doc->layer.stack[IE_LAYER_BG]->pixels[6 * 16 + 6], 0);
  cmd_undo(doc); ASSERT_EQUAL(doc->pixels[6 * 16 + 6], 0);
  cmd_redo(doc); ASSERT_EQUAL(doc->pixels[6 * 16 + 6], 5);
  ASSERT_TRUE(cmd_pencil_layer(doc, IE_LAYER_PENCIL));
  ASSERT_TRUE(ie_doc_begin_op(doc, "Monochrome Stroke"));
  canvas_set_pixel(doc, 6, 6, k_pencil_palette[12]);
  ie_doc_commit_op(doc, true);
  ASSERT_EQUAL(COLOR_A(canvas_get_pixel(doc, 6, 6)), IE_PENCIL_MAX_OPACITY);
  ASSERT_EQUAL(COLOR_R(canvas_get_pixel(doc, 6, 6)), COLOR_R(pencil_configured_color()));
  ASSERT_TRUE(ie_doc_begin_op(doc, "Erase Pencil"));
  canvas_set_pixel(doc, 6, 6, IE_PAPER_COLOR);
  ie_doc_commit_op(doc, true);
  ASSERT_EQUAL(doc->pixels[6 * 16 + 6], 0);
  uint8_t composite[256];
  ASSERT_TRUE(pencil_composite_frame(doc, 0, composite));
  ASSERT_EQUAL(composite[6 * 16 + 6], 5);
  layers_teardown(); PASS();
}

static void test_layer_resize(void) {
  TEST("resizing transforms every cel and the shared background; undo restores all planes");
  canvas_doc_t *doc = layers_setup();
  for (int i = 0; i < 4; i++) ASSERT_TRUE(layer_pixel(doc, i, 4, 4, i + 3));
  cmd_frame_add(doc, true);
  cmd_resize_image(doc, 8, 8, IMAGE_RESIZE_NEAREST);
  ASSERT_EQUAL(doc->anim->frames[0]->cels_size, 3 * 64);
  ASSERT_EQUAL(doc->anim->frames[1]->cels_size, 3 * 64);
  cmd_undo(doc);
  for (int i = 0; i < 4; i++) ASSERT_EQUAL(doc->layer.stack[i]->pixels[4 * 16 + 4], i == IE_LAYER_PENCIL ? IE_PENCIL_MAX_OPACITY : i + 4);
  ASSERT_TRUE(cmd_frame_select(doc, 0));
  for (int i = 0; i < 4; i++) ASSERT_EQUAL(doc->layer.stack[i]->pixels[4 * 16 + 4], i == IE_LAYER_PENCIL ? IE_PENCIL_MAX_OPACITY : i + 4);
  cmd_resize_canvas(doc, 20, 20);
  ASSERT_TRUE(cmd_frame_select(doc, 1));
  for (int i = 0; i < 4; i++) ASSERT_EQUAL(doc->layer.stack[i]->pixels[4 * 20 + 4], i == IE_LAYER_PENCIL ? IE_PENCIL_MAX_OPACITY : i + 4);
  layers_teardown(); PASS();
}

static void test_layer_project(void) {
  TEST("PTF preserves live background, every cel, hidden layers, active layer and atomic failure");
  canvas_doc_t *doc = layers_setup();
  for (int i = 0; i < 4; i++) ASSERT_TRUE(layer_pixel(doc, i, i, 0, i + 3));
  cmd_frame_add(doc, false);
  for (int i = 1; i < 4; i++) ASSERT_TRUE(layer_pixel(doc, i, i, 1, i + 6));
  doc->layer.stack[IE_LAYER_BG]->pixels[10] = 14; // live data, not a cached frame composite
  doc->layer.stack[IE_LAYER_COLOR]->visible = false;
  doc->pixels[12] = 16;
  char path[600]; snprintf(path, sizeof(path), "%s/layers.ptf", layer_test_dir);
  ASSERT_TRUE(image_io_save(path, doc));
  ASSERT_TRUE(imageeditor_open_file_path(path));
  canvas_doc_t *again = g_app->active_doc;
  ASSERT_EQUAL(again->layer.count, 4);
  ASSERT_EQUAL(again->layer.active, IE_LAYER_FX);
  ASSERT_FALSE(again->layer.stack[IE_LAYER_COLOR]->visible);
  ASSERT_EQUAL(again->layer.stack[IE_LAYER_BG]->pixels[10], 14);
  for (int i = 1; i < 4; i++) ASSERT_EQUAL(again->layer.stack[i]->pixels[i], i == IE_LAYER_PENCIL ? IE_PENCIL_MAX_OPACITY : i + 4);
  ASSERT_TRUE(cmd_frame_select(again, 1));
  for (int i = 1; i < 4; i++) ASSERT_EQUAL(again->layer.stack[i]->pixels[16 + i], i == IE_LAYER_PENCIL ? IE_PENCIL_MAX_OPACITY : i + 7);
  ASSERT_EQUAL(again->pixels[12], 16);
  uint8_t composite[256];
  ASSERT_TRUE(pencil_composite_frame(again, 0, composite)); ASSERT_EQUAL(composite[10], 14);
  again->anim->frames[0]->cels_size = 1;
  ASSERT_FALSE(image_io_save(path, again));
  ASSERT_TRUE(imageeditor_open_file_path(path));
  ASSERT_EQUAL(g_app->active_doc->anim->frames[0]->cels_size, 768);
  FILE *fp = fopen(path, "r+b"); ASSERT_NOT_NULL(fp);
  ASSERT_EQUAL(fseek(fp, -(16 + 256 * 7), SEEK_END), 0);
  uint8_t header[16]; ASSERT_EQUAL(fread(header, 1, 16, fp), 16);
  ASSERT_EQUAL(memcmp(header + 8, "PTL3", 4), 0);
  header[11] = '9';
  ASSERT_EQUAL(fseek(fp, -16, SEEK_CUR), 0);
  ASSERT_EQUAL(fwrite(header, 1, 16, fp), 16); fclose(fp);
  canvas_doc_t *before = g_app->docs;
  ASSERT_FALSE(imageeditor_open_file_path(path)); ASSERT_TRUE(g_app->docs == before);
  remove(path);
  layers_teardown(); PASS();
}

static void test_layer_exports(void) {
  TEST("compositing and PNG/GIF exports include shared background and all animated layers");
  canvas_doc_t *doc = layers_setup();
  for (int i = 0; i < 4; i++) ASSERT_TRUE(layer_pixel(doc, i, i, 0, i + 3));
  cmd_frame_add(doc, true);
  ASSERT_TRUE(layer_pixel(doc, IE_LAYER_BG, 0, 0, 12));
  char path[600]; snprintf(path, sizeof(path), "%s/sheet.png", layer_test_dir);
  ASSERT_TRUE(anim_export_spritesheet(doc, path));
  int w, h; uint8_t *rgba = load_image(path, &w, &h);
  ASSERT_NOT_NULL(rgba); ASSERT_EQUAL(w, 32); ASSERT_EQUAL(h, 16);
  for (int f = 0; f < 2; f++) for (int i = 0; i < 4; i++) {
    uint32_t color = k_pencil_palette[i ? i + 3 : 12];
    if (i == IE_LAYER_PENCIL) {
      uint32_t paper = IE_PAPER_COLOR, pencil = pencil_configured_color();
      color = MAKE_COLOR((COLOR_R(pencil) * IE_PENCIL_MAX_OPACITY + COLOR_R(paper) * (255 - IE_PENCIL_MAX_OPACITY) + 127) / 255,
                         (COLOR_G(pencil) * IE_PENCIL_MAX_OPACITY + COLOR_G(paper) * (255 - IE_PENCIL_MAX_OPACITY) + 127) / 255,
                         (COLOR_B(pencil) * IE_PENCIL_MAX_OPACITY + COLOR_B(paper) * (255 - IE_PENCIL_MAX_OPACITY) + 127) / 255, 255);
    }
    uint8_t *p = rgba + (f * 16 + i) * 4;
    ASSERT_EQUAL(p[0], COLOR_R(color)); ASSERT_EQUAL(p[1], COLOR_G(color)); ASSERT_EQUAL(p[2], COLOR_B(color));
  }
  image_free(rgba); remove(path);
  snprintf(path, sizeof(path), "%s/animation.png", layer_test_dir);
  ASSERT_TRUE(anim_export_apng(doc, path));
  FILE *apng = fopen(path, "rb"); ASSERT_NOT_NULL(apng);
  uint8_t png_signature[8];
  static const uint8_t expected_signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  ASSERT_EQUAL(fread(png_signature, 1, sizeof(png_signature), apng), sizeof(png_signature));
  ASSERT_EQUAL(memcmp(png_signature, expected_signature, sizeof(expected_signature)), 0);
  bool has_srgb = false;
  uint8_t chunk_header[8];
  while (fread(chunk_header, 1, sizeof(chunk_header), apng) == sizeof(chunk_header)) {
    uint32_t chunk_size = ((uint32_t)chunk_header[0] << 24) |
                          ((uint32_t)chunk_header[1] << 16) |
                          ((uint32_t)chunk_header[2] << 8) | chunk_header[3];
    if (memcmp(chunk_header + 4, "sRGB", 4) == 0) {
      has_srgb = chunk_size == 1;
      break;
    }
    if (fseek(apng, (long)chunk_size + 4, SEEK_CUR) != 0 ||
        memcmp(chunk_header + 4, "IEND", 4) == 0)
      break;
  }
  fclose(apng); ASSERT_TRUE(has_srgb);
  rgba = load_image(path, &w, &h); ASSERT_NOT_NULL(rgba);
  ASSERT_EQUAL(rgba[0], COLOR_R(k_pencil_palette[12])); image_free(rgba); remove(path);
  snprintf(path, sizeof(path), "%s/animation.gif", layer_test_dir);
  ASSERT_TRUE(anim_export_gif(doc, path));
  if (getenv("LAYERS_KEEP_EXPORT")) printf("GIF export: %s\n", path);
  else remove(path);
  layers_teardown(); PASS();
}

int main(void) {
  TEST_START("Pencil Test fixed layers");
  snprintf(layer_test_dir, sizeof(layer_test_dir), "%s/orion-layers-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
  if (!mkdtemp(layer_test_dir)) return 1;
  test_layer_sidebar(); test_layer_frames(); test_underpaint_fill();
  test_layer_resize(); test_layer_project(); test_layer_exports();
  if (!getenv("LAYERS_KEEP_EXPORT")) rmdir(layer_test_dir);
  TEST_END();
}
