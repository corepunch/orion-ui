#define IMAGEEDITOR_INDEXED 1
#include "test_framework.h"
#include "apps/imageeditor/imageeditor.h"
#include <unistd.h>

bool flc_save(const char *path, const canvas_doc_t *doc) { (void)path; (void)doc; return false; }
#include "apps/imageeditor/io/image_io.c"

static const char *test_temp_dir(void) {
  const char *dir = getenv("TEMP");
  if (!dir) dir = getenv("TMP");
  if (!dir) dir = getenv("TMPDIR");
  return dir ? dir : "/tmp";
}

static void test_indexed_bmp_roundtrip(void) {
  TEST("indexed BMP round trip preserves palette indices, including duplicate colours");
  char path[512]; snprintf(path, sizeof(path), "%s/orion-indexed-%d.bmp", test_temp_dir(), (int)getpid());
  uint8_t src[] = {1, 2, 3, 4, 2, 1};
  layer_t layer = {.pixels = src}; layer_t *stack[] = {&layer};
  canvas_doc_t doc = {0}; doc.canvas_w = 3; doc.canvas_h = 2;
  doc.layer.stack = stack; doc.layer.count = 1; doc.ipal.count = 256;
  doc.ipal.entries[1] = MAKE_COLOR(42, 90, 135, 255);
  doc.ipal.entries[2] = doc.ipal.entries[1];
  doc.ipal.entries[3] = MAKE_COLOR(12, 34, 56, 255);
  doc.ipal.entries[4] = MAKE_COLOR(78, 91, 23, 255);
  ASSERT_TRUE(image_io_save(path, &doc));
  int w = 0, h = 0, count = 0; uint32_t palette[256];
  uint8_t *pixels = image_io_load(path, &w, &h, palette, &count);
  ASSERT_NOT_NULL(pixels);
  ASSERT_EQUAL(w, 3); ASSERT_EQUAL(h, 2); ASSERT_EQUAL(count, 256);
  ASSERT_TRUE(memcmp(src, pixels, sizeof(src)) == 0);
  ASSERT_EQUAL(palette[1], doc.ipal.entries[1]); ASSERT_EQUAL(palette[2], doc.ipal.entries[2]);
  ASSERT_EQUAL(palette[3], doc.ipal.entries[3]); ASSERT_EQUAL(palette[4], doc.ipal.entries[4]);
  ASSERT_EQUAL(COLOR_A(palette[IMAGEEDITOR_TRANSPARENT_INDEX]), 0);
  free(pixels); remove(path); PASS();
}

static void test_top_down_bmp(void) {
  TEST("indexed BMP imports top-down rows and rejects truncated data");
  char path[512]; snprintf(path, sizeof(path), "%s/orion-top-down-%d.bmp", test_temp_dir(), (int)getpid());
  uint8_t palette[256 * 4] = {0}, src[] = {3, 2, 1, 1, 2, 3};
  for (int i = 0; i < 256; i++) palette[i * 4] = (uint8_t)i;
  FILE *file = fopen(path, "wb"); ASSERT_NOT_NULL(file);
  BMPHANDLE bmp = bmpwrite_new(file); ASSERT_NOT_NULL(bmp);
  ASSERT_EQUAL(bmpwrite_set_dimensions(bmp, 3, 2, 1, 8), BMP_RESULT_OK);
  ASSERT_EQUAL(bmpwrite_set_palette(bmp, 256, palette), BMP_RESULT_OK);
  ASSERT_EQUAL(bmpwrite_set_orientation(bmp, BMP_ORIENT_TOPDOWN), BMP_RESULT_OK);
  ASSERT_EQUAL(bmpwrite_save_image(bmp, src), BMP_RESULT_OK);
  bmp_free(bmp); ASSERT_EQUAL(fclose(file), 0);
  int w = 0, h = 0, count = 0; uint32_t colors[256];
  uint8_t *pixels = image_io_load(path, &w, &h, colors, &count);
  ASSERT_NOT_NULL(pixels); ASSERT_EQUAL(w, 3); ASSERT_EQUAL(h, 2);
  ASSERT_TRUE(memcmp(src, pixels, sizeof(src)) == 0); free(pixels);
  uint8_t header[32]; file = fopen(path, "rb"); ASSERT_NOT_NULL(file);
  ASSERT_EQUAL(fread(header, 1, sizeof(header), file), sizeof(header)); fclose(file);
  file = fopen(path, "wb"); ASSERT_NOT_NULL(file);
  ASSERT_EQUAL(fwrite(header, 1, sizeof(header), file), sizeof(header)); fclose(file);
  ASSERT_NULL(image_io_load(path, &w, &h, colors, &count));
  file = fopen(path, "wb"); ASSERT_NOT_NULL(file);
  bmp = bmpwrite_new(file); ASSERT_NOT_NULL(bmp);
  ASSERT_EQUAL(bmpwrite_set_dimensions(bmp, 3, 2, 1, 8), BMP_RESULT_OK);
  ASSERT_EQUAL(bmpwrite_set_palette(bmp, 256, palette), BMP_RESULT_OK);
  ASSERT_EQUAL(bmpwrite_set_rle(bmp, BMP_RLE_RLE8), BMP_RESULT_OK);
  ASSERT_EQUAL(bmpwrite_save_image(bmp, src), BMP_RESULT_OK);
  bmp_free(bmp); ASSERT_EQUAL(fclose(file), 0);
  pixels = image_io_load(path, &w, &h, colors, &count);
  ASSERT_NOT_NULL(pixels); ASSERT_TRUE(memcmp(src, pixels, sizeof(src)) == 0); free(pixels);
  remove(path); PASS();
}

int main(void) {
  TEST_START("indexed Image Editor BMP");
  test_indexed_bmp_roundtrip();
  test_top_down_bmp();
  TEST_END();
}
