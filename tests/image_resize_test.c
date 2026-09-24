#include "test_framework.h"
#include <orion/user/image.h>
#include <stdlib.h>
#include <string.h>

static void test_thin_stroke(void) {
  TEST("downscale preserves a one-pixel diagonal at 32:1");
  uint8_t *src = calloc(512 * 512, 4);
  ASSERT_NOT_NULL(src);
  for (int i = 0; i < 512; i++) src[(i * 512 + i) * 4 + 3] = 255;
  uint8_t *out = downscale_image(src, 512, 512, 16);
  ASSERT_NOT_NULL(out);
  int visible = 0;
  for (int i = 0; i < 16; i++) {
    ASSERT_EQUAL(out[(i * 16 + i) * 4], 0);
    if (out[(i * 16 + i) * 4 + 3] > 0) visible++;
  }
  ASSERT_TRUE(visible >= 12);
  image_free(out);
  free(src);
  PASS();
}

static void test_alpha_weighting(void) {
  TEST("transparent colour does not bleed into the visible colour");
  uint8_t src[] = {255, 0, 0, 255, 0, 0, 255, 0,
                  0, 0, 255, 0,   0, 0, 255, 0};
  uint8_t *out = downscale_image(src, 2, 2, 1);
  ASSERT_NOT_NULL(out);
  ASSERT_EQUAL(out[0], 255);
  ASSERT_EQUAL(out[2], 0);
  ASSERT_EQUAL(out[3], 64);
  image_free(out);
  PASS();
}

static void test_stroke_preview(void) {
  TEST("stroke previews retain black, antialiasing, colour and source opacity");
  uint8_t src[8 * 8 * 4] = {0};
  for (int x = 0; x < 8; x++) src[(1 * 8 + x) * 4 + 3] = 255;
  uint8_t *out = downscale_image_ex(src, 8, 8, 2, IMAGE_DOWNSCALE_STROKES);
  ASSERT_NOT_NULL(out);
  ASSERT_EQUAL(out[0], 0);
  ASSERT_EQUAL(out[3], 255);
  ASSERT_EQUAL(out[7], 255);
  ASSERT_EQUAL(out[11], 0);
  image_free(out);
  memset(src, 0, sizeof(src));
  for (int x = 0; x < 2; x++) {
    src[(1 * 8 + x) * 4] = 240;
    src[(1 * 8 + x) * 4 + 3] = 128;
  }
  out = downscale_image_ex(src, 8, 8, 2, IMAGE_DOWNSCALE_STROKES);
  ASSERT_NOT_NULL(out);
  ASSERT_EQUAL(out[0], 240);
  ASSERT_EQUAL(out[3], 64);
  image_free(out);
  PASS();
}

static void test_bottom_up_preview(void) {
  TEST("rounded thumbnail upload puts the source top in the last texture row");
  uint8_t src[4 * 4 * 4] = {0};
  for (int x = 0; x < 4; x++) src[x * 4 + 3] = 255;
  uint8_t *out = downscale_image_ex(src, 4, 4, 2,
                                    IMAGE_DOWNSCALE_STROKES | IMAGE_DOWNSCALE_FLIP_Y);
  ASSERT_NOT_NULL(out);
  ASSERT_EQUAL(out[3], 0);
  ASSERT_EQUAL(out[7], 0);
  ASSERT_EQUAL(out[11], 255);
  ASSERT_EQUAL(out[15], 255);
  image_free(out);
  PASS();
}

static void test_square_crop(void) {
  TEST("landscape and portrait crop symmetrically, including half pixels");
  uint8_t src[3 * 2 * 4];
  memset(src, 255, sizeof(src));
  for (int y = 0; y < 2; y++) {
    src[(y * 3 + 0) * 4] = 0;
    src[(y * 3 + 2) * 4] = 0;
  }
  uint8_t *out = downscale_image(src, 3, 2, 2);
  ASSERT_NOT_NULL(out);
  for (int i = 0; i < 4; i++) ASSERT_EQUAL(out[i * 4], 188);
  image_free(out);
  for (int y = 0; y < 3; y++)
    for (int x = 0; x < 2; x++) src[(y * 2 + x) * 4] = y == 1 ? 255 : 0;
  out = downscale_image(src, 2, 3, 2);
  ASSERT_NOT_NULL(out);
  for (int i = 0; i < 4; i++) ASSERT_EQUAL(out[i * 4], 188);
  image_free(out);
  PASS();
}

static void test_small_and_empty(void) {
  TEST("small sources and transparent frames produce valid squares");
  uint8_t src[] = {24, 48, 96, 255};
  uint8_t *out = downscale_image(src, 1, 1, 3);
  ASSERT_NOT_NULL(out);
  for (int i = 0; i < 9; i++) ASSERT_EQUAL(memcmp(out + i * 4, src, 4), 0);
  image_free(out);
  src[3] = 0;
  out = downscale_image(src, 1, 1, 1);
  ASSERT_NOT_NULL(out);
  for (int i = 0; i < 4; i++) ASSERT_EQUAL(out[i], 0);
  image_free(out);
  ASSERT_TRUE(downscale_image(src, 1, 1, 0) == NULL);
  ASSERT_TRUE(downscale_image(NULL, 1, 1, 1) == NULL);
  PASS();
}

int main(void) {
  TEST_START("Image downscaling");
  test_thin_stroke();
  test_alpha_weighting();
  test_stroke_preview();
  test_bottom_up_preview();
  test_square_crop();
  test_small_and_empty();
  TEST_END();
}
