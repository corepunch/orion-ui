// Image API unit tests
// Tests for load_image / image_free / save_image_png.
// Self-contained: pulls in user/image.c directly so no SDL/GL is required.

#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

// Pull in the image module implementation without the full static library.
// user/image.c defines STB_IMAGE_IMPLEMENTATION before including stb_image.h,
// so all stb symbols are compiled in here with no duplicate-symbol conflicts.
#include "../user/image.c"

// ============================================================
// Cross-platform temp directory helper.
// On Windows (MinGW) TEMP/TMP are set; on POSIX TMPDIR or /tmp.
// ============================================================
static const char *temp_dir(void) {
  const char *d = getenv("TEMP");
  if (!d) d = getenv("TMP");
  if (!d) d = getenv("TMPDIR");
  if (!d) d = "/tmp";
  return d;
}

// ============================================================
// Minimal 1x1 red RGBA PNG used for embedded-data tests.
// Generated with: python3 -c "
//   import zlib, struct
//   def chunk(t, d): return struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xFFFFFFFF)
//   raw = b'\x00\xff\x00\x00\xff'  # filter byte + R G B A
//   img = chunk(b'IHDR', struct.pack('>IIBBBBB',1,1,8,2,0,0,0))
//   img += chunk(b'IDAT', zlib.compress(raw))
//   img += chunk(b'IEND', b'')
//   print(list(b'\x89PNG\r\n\x1a\n'+img))
// "
// ============================================================
static const uint8_t k_red_1x1_png[] = {
  0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
  0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
  0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
  0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
  0xde, 0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41,
  0x54, 0x08, 0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0x00,
  0x00, 0x00, 0x02, 0x00, 0x01, 0xe2, 0x21, 0xbc,
  0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,
  0x44, 0xae, 0x42, 0x60, 0x82
};

// Write the embedded PNG bytes to a temp file, return path (static buffer).
static const char *write_temp_png(void) {
  static char path[512];
  snprintf(path, sizeof(path), "%s/orion_image_test_%d.png", temp_dir(), (int)getpid());
  FILE *f = fopen(path, "wb");
  if (!f) return NULL;
  fwrite(k_red_1x1_png, 1, sizeof(k_red_1x1_png), f);
  fclose(f);
  return path;
}

// ============================================================
// Tests
// ============================================================

void test_load_image_basic(void) {
  TEST("load_image: loads a valid PNG");

  const char *path = write_temp_png();
  ASSERT_NOT_NULL(path);

  int w = -1, h = -1;
  uint8_t *px = load_image(path, &w, &h);
  ASSERT_NOT_NULL(px);
  ASSERT_EQUAL(w, 1);
  ASSERT_EQUAL(h, 1);
  image_free(px);

  remove(path);
  PASS();
}

void test_load_image_null_path(void) {
  TEST("load_image: NULL path returns NULL");
  int w = 0, h = 0;
  uint8_t *px = load_image(NULL, &w, &h);
  ASSERT_NULL(px);
  PASS();
}

void test_load_image_null_out_params(void) {
  TEST("load_image: NULL out_w/out_h returns NULL");
  uint8_t *px = load_image("dummy.png", NULL, NULL);
  ASSERT_NULL(px);
  int w = 0;
  px = load_image("dummy.png", &w, NULL);
  ASSERT_NULL(px);
  PASS();
}

void test_load_image_missing_file(void) {
  TEST("load_image: missing file zeros dimensions and returns NULL");
  char path[512];
  snprintf(path, sizeof(path), "%s/orion_nonexistent_orion_%d.png", temp_dir(), (int)getpid());
  int w = 99, h = 99;
  uint8_t *px = load_image(path, &w, &h);
  ASSERT_NULL(px);
  ASSERT_EQUAL(w, 0);
  ASSERT_EQUAL(h, 0);
  PASS();
}

void test_save_and_reload(void) {
  TEST("save_image_png + load_image: round-trip 2x2 RGBA");

  // Build a 2x2 RGBA image: red, green, blue, white
  uint8_t src[2 * 2 * 4] = {
    0xFF, 0x00, 0x00, 0xFF,   // red
    0x00, 0xFF, 0x00, 0xFF,   // green
    0x00, 0x00, 0xFF, 0xFF,   // blue
    0xFF, 0xFF, 0xFF, 0xFF,   // white
  };

  char path[512];
  snprintf(path, sizeof(path), "%s/orion_roundtrip_%d.png", temp_dir(), (int)getpid());

  bool ok = save_image_png(path, src, 2, 2);
  ASSERT_TRUE(ok);

  int w = 0, h = 0;
  uint8_t *px = load_image(path, &w, &h);
  ASSERT_NOT_NULL(px);
  ASSERT_EQUAL(w, 2);
  ASSERT_EQUAL(h, 2);

  // Validate all 4 pixels survive the round-trip.
  ASSERT_EQUAL(memcmp(px, src, sizeof(src)), 0);

  image_free(px);
  remove(path);
  PASS();
}

void test_save_image_bad_path(void) {
  TEST("save_image_png: unwritable path returns false");
  uint8_t px[4] = {0xFF, 0, 0, 0xFF};
  bool ok = save_image_png("/nonexistent_dir/out.png", px, 1, 1);
  ASSERT_FALSE(ok);
  PASS();
}

int main(void) {
  TEST_START("image API");
  test_load_image_basic();
  test_load_image_null_path();
  test_load_image_null_out_params();
  test_load_image_missing_file();
  test_save_and_reload();
  test_save_image_bad_path();
  TEST_END();
}
