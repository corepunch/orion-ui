#define IMAGEEDITOR_BW 1
#define IMAGEEDITOR_BW_RETINA
#include "test_framework.h"
#include "test_env.h"
#include "apps/imageeditor/imageeditor.h"
#include "apps/imageeditor/io/flc_codec.h"
#include "anim_test_helpers.h"

app_state_t *g_app;
int g_bw_retina_scale = 1;
static uint8_t uploaded_coverage[64 * 48];
static uint32_t uploads;
static uint32_t memory_upload(int w, int h, const void *pixels, R_TextureFilter filter, R_TextureWrap wrap) {
  if (w != 64 || h != 48 || filter != R_FILTER_NEAREST || wrap != R_WRAP_CLAMP) return 0;
  memcpy(uploaded_coverage, pixels, sizeof(uploaded_coverage));
  return ++uploads;
}
static void memory_delete_texture(uint32_t texture) { (void)texture; }
#define R_CreateTextureR8 memory_upload
#define R_DeleteTexture memory_delete_texture

static bool memory_independent(const anim_frame_t *frame) {
  if (frame->data || frame->data_size || frame->format != FRAME_FORMAT_RLE_INDEXED) return false;
  size_t offset = 0;
  for (int i = 0; i < 3; i++) {
    if (frame->cels_size - offset < 6) return false;
    const uint8_t *p = frame->cels + offset;
    size_t size = flc_codec_u32(p); int type = flc_codec_u16(p + 4);
    if (size < 6 || size > frame->cels_size - offset || (type != 13 && type != 15 && type != 16)) return false;
    offset += size;
  }
  return offset == frame->cels_size;
}

static void memory_long_animation(void) {
  TEST("150 frames retain independent compressed cels and decode in arbitrary order");
  canvas_doc_t *doc = create_document(NULL, 320, 200); ASSERT_NOT_NULL(doc);
  size_t n = 320 * 200, bytes = 0;
  uint8_t *coverage = calloc(1, n), *clear = malloc(n); ASSERT_NOT_NULL(coverage); ASSERT_NOT_NULL(clear);
  memset(clear, 255, n);
  const uint8_t *planes[] = {clear, coverage, clear};
  for (int i = 0; i < 150; i++) {
    if (i) ASSERT_EQUAL(anim_timeline_insert_frame(doc->anim, i - 1), i);
    memset(coverage, 0, n); coverage[i * 37] = i + 1;
    ASSERT_TRUE(anim_frame_pack_cels(doc->anim->frames[i], planes, 320, 200));
    ASSERT_TRUE(memory_independent(doc->anim->frames[i]));
    bytes += doc->anim->frames[i]->cels_size;
  }
  ASSERT_TRUE(bytes < 150 * 3 * n / 20);
  for (int i = 0; i < 150; i++) {
    int index = i * 73 % 150;
    ASSERT_TRUE(doc_anim_load(doc, index));
    ASSERT_EQUAL(doc->pixels[index * 37], index + 1);
    ASSERT_TRUE(memory_independent(doc->anim->frames[index]));
  }
  printf("\n    150 frames: %zu encoded bytes, %zu raw cel bytes\n", bytes, 150 * 3 * n);
  free(coverage); free(clear); close_document(doc); PASS();
}

static void memory_edit_history(void) {
  TEST("editing, duplicate, reorder, undo, resize and redo keep independent compressed storage");
  canvas_doc_t *doc = create_document(NULL, 64, 48); ASSERT_NOT_NULL(doc);
  doc->pixels[9] = 17; cmd_frame_add(doc, true);
  ASSERT_TRUE(ie_doc_begin_op(doc, "Change coverage")); doc->pixels[9] = 231; ie_doc_commit_op(doc, true);
  ASSERT_TRUE(cmd_frame_select(doc, 0)); ASSERT_EQUAL(doc->pixels[9], 17);
  ASSERT_TRUE(cmd_frame_select(doc, 1)); ASSERT_EQUAL(doc->pixels[9], 231);
  cmd_undo(doc); ASSERT_EQUAL(doc->pixels[9], 17);
  cmd_redo(doc); ASSERT_EQUAL(doc->pixels[9], 231);
  cmd_frame_move(doc, 1, 0); ASSERT_EQUAL(doc->anim->active_frame, 0);
  ASSERT_TRUE(cmd_frame_select(doc, 1)); ASSERT_EQUAL(doc->pixels[9], 17);
  cmd_resize_canvas(doc, 80, 60); ASSERT_EQUAL(doc->pixels[9], 17);
  ASSERT_TRUE(cmd_frame_select(doc, 0)); ASSERT_EQUAL(doc->pixels[9], 231);
  for (int i = 0; i < 2; i++) ASSERT_TRUE(memory_independent(doc->anim->frames[i]));
  close_document(doc); PASS();
}

static void memory_large_project(void) {
  TEST("180 full-resolution frames save and load beyond the former decoded-movie limit");
  canvas_doc_t *doc = create_document(NULL, 1168, 784); ASSERT_NOT_NULL(doc);
  doc->pixels[0] = 64; ASSERT_TRUE(doc_anim_commit(doc));
  for (int i = 1; i < 180; i++) ASSERT_EQUAL(anim_timeline_duplicate_frame(doc->anim, i - 1), i);
  ASSERT_TRUE((uint64_t)1168 * 784 * (1 + 4 * 180) > 512u * 1024u * 1024u);
  char path[] = "/tmp/orion-memory-XXXXXX";
  int fd = mkstemp(path); ASSERT_TRUE(fd >= 0); close(fd);
  ASSERT_TRUE(flc_save(path, doc));
  int w, h; uint32_t palette[256], background; bool show;
  anim_timeline_t *loaded = flc_load(path, &w, &h, palette, &background, &show);
  ASSERT_NOT_NULL(loaded); ASSERT_EQUAL(loaded->frame_count, 180);
  ASSERT_TRUE(memory_independent(loaded->frames[179]));
  ASSERT_EQUAL(test_anim_cel_pixel(loaded->frames[179], 1, 0, w, h), 64);
  anim_timeline_free(loaded); close_document(doc); unlink(path); PASS();
}

static void memory_onion(void) {
  TEST("onion textures decode temporary coverage without changing stored bytes or working layers");
  canvas_doc_t *doc = create_document(NULL, 64, 48); ASSERT_NOT_NULL(doc);
  doc->pixels[11] = 93; cmd_frame_add(doc, false); doc->pixels[11] = 201;
  anim_frame_t *previous = doc->anim->frames[0];
  uint8_t *stored = malloc(previous->cels_size); ASSERT_NOT_NULL(stored);
  memcpy(stored, previous->cels, previous->cels_size);
  uint32_t texture = 0;
  for (int i = 0; i < 20; i++) {
    ASSERT_TRUE(anim_render_pencil_onion(previous, 64, 48, &texture));
    ASSERT_TRUE(texture != 0); ASSERT_EQUAL(doc->pixels[11], 201);
    ASSERT_EQUAL(uploaded_coverage[11], 93);
    ASSERT_EQUAL(memcmp(stored, previous->cels, previous->cels_size), 0);
    ASSERT_TRUE(memory_independent(previous));
  }
  ASSERT_EQUAL(uploads, 20);
  size_t valid = previous->cels_size;
  previous->cels_size = 1;
  ASSERT_FALSE(anim_render_pencil_onion(previous, 64, 48, &texture));
  uint8_t current = doc->pixels[11]; int index = doc->anim->active_frame;
  ASSERT_FALSE(doc_anim_load(doc, 0)); ASSERT_EQUAL(doc->pixels[11], current); ASSERT_EQUAL(doc->anim->active_frame, index);
  previous->cels_size = valid;
  R_DeleteTexture(texture); free(stored); close_document(doc); PASS();
}

static void memory_example(const char *path) {
  TEST("full MP4 example opens with compressed frames and no retained composite planes");
  ASSERT_TRUE(imageeditor_open_file_path(path));
  canvas_doc_t *doc = g_app->active_doc;
  size_t bytes = 0, n = (size_t)doc->canvas_w * doc->canvas_h;
  for (int i = 0; i < doc->anim->frame_count; i++) {
    ASSERT_TRUE(memory_independent(doc->anim->frames[i]));
    bytes += doc->anim->frames[i]->cels_size;
  }
  ASSERT_TRUE(bytes < n * 3 * doc->anim->frame_count / 2);
  printf("\n    %d frames: RLE storage %.2f MiB, working layers %.2f MiB, previous raw frame storage %.2f MiB\n",
         doc->anim->frame_count, bytes / 1048576.0, 4 * n / 1048576.0, 4.0 * n * doc->anim->frame_count / 1048576.0);
  ASSERT_TRUE(doc_anim_load(doc, doc->anim->frame_count - 1));
  ASSERT_TRUE(doc_anim_load(doc, 0));
  close_document(doc); PASS();
}

int main(void) {
  TEST_START("Penciltest compressed frame memory");
  test_env_init(); g_app = calloc(1, sizeof(*g_app));
  if (!g_app) return 1;
  memory_long_animation(); memory_edit_history(); memory_large_project(); memory_onion();
  const char *example = getenv("PENCILTEST_MEMORY_EXAMPLE");
  if (example) memory_example(example);
  while (g_app->docs) close_document(g_app->docs);
  free(g_app); g_app = NULL; test_env_shutdown();
  TEST_END();
}
