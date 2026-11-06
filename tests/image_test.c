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
#include <orion/user/image.c>

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
//   img = chunk(b'IHDR', struct.pack('>IIBBBBB',1,1,8,6,0,0,0))
//   img += chunk(b'IDAT', zlib.compress(raw))
//   img += chunk(b'IEND', b'')
//   print(list(b'\x89PNG\r\n\x1a\n'+img))
// "
// ============================================================
static const uint8_t k_red_1x1_png[] = {
  0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
  0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
  0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
  0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
  0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x89, 0x99, 0x3d, 0x1d, 0x00, 0x00,
  0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
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

static bool png_has_srgb_chunk(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) return false;
  uint8_t signature[8];
  static const uint8_t png_signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  bool found = false;
  if (fread(signature, 1, sizeof(signature), fp) != sizeof(signature) ||
      memcmp(signature, png_signature, sizeof(signature)) != 0) {
    fclose(fp);
    return false;
  }
  for (;;) {
    uint8_t header[8];
    if (fread(header, 1, sizeof(header), fp) != sizeof(header)) break;
    uint32_t length = ((uint32_t)header[0] << 24) | ((uint32_t)header[1] << 16) |
                      ((uint32_t)header[2] << 8) | header[3];
    if (memcmp(header + 4, "sRGB", 4) == 0) {
      found = length == 1;
      break;
    }
    if (fseek(fp, (long)length + 4, SEEK_CUR) != 0 ||
        memcmp(header + 4, "IEND", 4) == 0)
      break;
  }
  fclose(fp);
  return found;
}

static void set_pixel(uint8_t *px, int w, int x, int y,
                      uint8_t r, uint8_t g, uint8_t b) {
  uint8_t *p = px + ((size_t)y * w + x) * 4;
  p[0] = r;
  p[1] = g;
  p[2] = b;
  p[3] = 0xff;
}

static bool write_exif_oriented_jpeg(const char *plain_path,
                                     const char *oriented_path,
                                     int orientation) {
  FILE *in = fopen(plain_path, "rb");
  if (!in) return false;
  FILE *out = fopen(oriented_path, "wb");
  if (!out) {
    fclose(in);
    return false;
  }

  uint8_t soi[2];
  bool ok = fread(soi, 1, 2, in) == 2 && soi[0] == 0xff && soi[1] == 0xd8;
  if (ok) {
    static const uint8_t app1_prefix[] = {
      0xff, 0xe1, 0x00, 0x22,
      'E', 'x', 'i', 'f', 0x00, 0x00,
      'M', 'M', 0x00, 0x2a,
      0x00, 0x00, 0x00, 0x08,
      0x00, 0x01,
      0x01, 0x12, 0x00, 0x03,
      0x00, 0x00, 0x00, 0x01,
      0x00
    };
    uint8_t app1_suffix[] = {
      (uint8_t)orientation, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00
    };
    ok = fwrite(soi, 1, 2, out) == 2 &&
         fwrite(app1_prefix, 1, sizeof(app1_prefix), out) == sizeof(app1_prefix) &&
         fwrite(app1_suffix, 1, sizeof(app1_suffix), out) == sizeof(app1_suffix);
  }

  uint8_t buf[4096];
  while (ok) {
    size_t n = fread(buf, 1, sizeof(buf), in);
    if (n > 0 && fwrite(buf, 1, n, out) != n)
      ok = false;
    if (n < sizeof(buf)) {
      if (ferror(in)) ok = false;
      break;
    }
  }

  fclose(in);
  fclose(out);
  return ok;
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
  ASSERT_TRUE(png_has_srgb_chunk(path));

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

void test_exif_orientation_transform_pixels(void) {
  TEST("EXIF orientation: transforms RGBA pixels exactly");

  uint8_t src_template[2 * 3 * 4] = {0};
  set_pixel(src_template, 2, 0, 0, 10, 0, 0);
  set_pixel(src_template, 2, 1, 0, 20, 0, 0);
  set_pixel(src_template, 2, 0, 1, 30, 0, 0);
  set_pixel(src_template, 2, 1, 1, 40, 0, 0);
  set_pixel(src_template, 2, 0, 2, 50, 0, 0);
  set_pixel(src_template, 2, 1, 2, 60, 0, 0);

  struct {
    int orientation;
    int w, h;
    uint8_t r[6];
  } cases[] = {
    {2, 2, 3, {20, 10, 40, 30, 60, 50}},
    {3, 2, 3, {60, 50, 40, 30, 20, 10}},
    {4, 2, 3, {50, 60, 30, 40, 10, 20}},
    {5, 3, 2, {10, 30, 50, 20, 40, 60}},
    {6, 3, 2, {50, 30, 10, 60, 40, 20}},
    {7, 3, 2, {60, 40, 20, 50, 30, 10}},
    {8, 3, 2, {20, 40, 60, 10, 30, 50}},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    uint8_t *src = malloc(sizeof(src_template));
    ASSERT_NOT_NULL(src);
    memcpy(src, src_template, sizeof(src_template));
    int w = 2, h = 3;
    uint8_t *dst = image_apply_orientation(src, &w, &h, cases[i].orientation);
    ASSERT_NOT_NULL(dst);
    ASSERT_EQUAL(w, cases[i].w);
    ASSERT_EQUAL(h, cases[i].h);
    for (int p = 0; p < 6; p++)
      ASSERT_EQUAL(dst[p * 4], cases[i].r[p]);
    image_free(dst);
  }

  PASS();
}

static void test_bmp_color_metadata(void) {
  TEST("BMP with calibrated colour metadata loads through file and memory APIs");
  uint8_t bmp[126] = {0};
  bmp[0] = 'B'; bmp[1] = 'M'; bmp[2] = sizeof(bmp); bmp[10] = 122;
  bmp[14] = 108; bmp[18] = 1; bmp[22] = 1; bmp[26] = 1; bmp[28] = 24;
  bmp[122] = 3; bmp[123] = 2; bmp[124] = 1;
  char path[512]; snprintf(path, sizeof(path), "%s/orion_bmp_metadata_%d.bmp", temp_dir(), (int)getpid());
  FILE *file = fopen(path, "wb"); ASSERT_NOT_NULL(file);
  ASSERT_EQUAL(fwrite(bmp, 1, sizeof(bmp), file), sizeof(bmp)); ASSERT_EQUAL(fclose(file), 0);
  int w = 0, h = 0;
  uint8_t *memory = load_image_memory(bmp, sizeof(bmp), &w, &h);
  ASSERT_NOT_NULL(memory); ASSERT_EQUAL(w, 1); ASSERT_EQUAL(h, 1);
  uint8_t *from_file = load_image(path, &w, &h);
  ASSERT_NOT_NULL(from_file); ASSERT_EQUAL(w, 1); ASSERT_EQUAL(h, 1);
  ASSERT_EQUAL(from_file[0], 1); ASSERT_EQUAL(from_file[1], 2); ASSERT_EQUAL(from_file[2], 3);
  ASSERT_TRUE(memcmp(memory, from_file, 4) == 0);
  image_free(memory); image_free(from_file); remove(path); PASS();
}

static void test_memory_png(void) {
  TEST("in-memory PNG round trip preserves RGBA and rejects truncation");
  const uint8_t src[] = {255, 0, 10, 255, 3, 4, 5, 90};
  size_t size = 0;
  uint8_t *encoded = encode_image_png(src, 2, 1, &size);
  ASSERT_NOT_NULL(encoded);
  int w = 0, h = 0;
  uint8_t *decoded = load_image_memory(encoded, size, &w, &h);
  ASSERT_NOT_NULL(decoded);
  ASSERT_EQUAL(w, 2); ASSERT_EQUAL(h, 1);
  ASSERT_TRUE(memcmp(src, decoded, sizeof(src)) == 0);
  image_free(decoded);
  ASSERT_NULL(load_image_memory(encoded, size / 2, &w, &h));
  free(encoded);
  PASS();
}

void test_load_image_applies_jpeg_exif_orientation(void) {
  TEST("load_image: applies JPEG EXIF orientation");

  uint8_t rgba[2 * 3 * 4] = {
    255, 0, 0, 255,     0, 255, 0, 255,
    0, 0, 255, 255,     255, 255, 0, 255,
    0, 255, 255, 255,   255, 0, 255, 255,
  };

  char plain_path[512];
  char oriented_path[512];
  snprintf(plain_path, sizeof(plain_path), "%s/orion_plain_%d.jpg", temp_dir(), (int)getpid());
  snprintf(oriented_path, sizeof(oriented_path), "%s/orion_oriented_%d.jpg", temp_dir(), (int)getpid());

  ASSERT_TRUE(save_image_jpg(plain_path, rgba, 2, 3, 100));
  FILE *jpeg = fopen(plain_path, "rb");
  ASSERT_NOT_NULL(jpeg);
  ASSERT_TRUE(fseek(jpeg, 0, SEEK_END) == 0);
  long size = ftell(jpeg);
  ASSERT_TRUE(size > 0 && fseek(jpeg, 0, SEEK_SET) == 0);
  uint8_t *encoded = malloc((size_t)size);
  ASSERT_NOT_NULL(encoded);
  ASSERT_TRUE(fread(encoded, 1, (size_t)size, jpeg) == (size_t)size);
  fclose(jpeg);
  int memory_w = 0, memory_h = 0;
  uint8_t *memory_pixels = load_image_memory(encoded, (size_t)size, &memory_w, &memory_h);
  ASSERT_NOT_NULL(memory_pixels);
  ASSERT_EQUAL(memory_w, 2); ASSERT_EQUAL(memory_h, 3);
  image_free(memory_pixels); free(encoded);
  ASSERT_TRUE(write_exif_oriented_jpeg(plain_path, oriented_path, 6));

  int w = 0, h = 0;
  uint8_t *px = load_image(oriented_path, &w, &h);
  ASSERT_NOT_NULL(px);
  ASSERT_EQUAL(w, 3);
  ASSERT_EQUAL(h, 2);

  image_free(px);
  remove(plain_path);
  remove(oriented_path);
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
  test_memory_png();
  test_bmp_color_metadata();
  test_exif_orientation_transform_pixels();
  test_load_image_applies_jpeg_exif_orientation();
  TEST_END();
}
