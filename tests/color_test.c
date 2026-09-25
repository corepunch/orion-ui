#include "test_framework.h"
#include <orion/user/color.h>
#include <orion/user/theme.h>

static void test_srgb8_roundtrip(void) {
  TEST("sRGB conversion round-trips every 8-bit channel code");
  for (int value = 0; value <= 255; value++)
    ASSERT_EQUAL(ui_linear_to_srgb8(ui_srgb8_to_linear((uint8_t)value)), value);
  PASS();
}

static void test_web_color_packing(void) {
  TEST("WEB converts CSS RGB order to the public packed color layout");
  uint32_t color = WEB(0x7357F6);
  ASSERT_EQUAL(color, 0xFFF65773u);
  ASSERT_EQUAL(color & 0xFFu, 0x73u);
  ASSERT_EQUAL((color >> 8) & 0xFFu, 0x57u);
  ASSERT_EQUAL((color >> 16) & 0xFFu, 0xF6u);
  ASSERT_EQUAL(color >> 24, 0xFFu);
  PASS();
}

static void test_linear_half_black_over_white(void) {
  TEST("half black over white encodes to sRGB #BC");
  uint8_t dst[4] = {255, 255, 255, 255};
  const uint8_t src[4] = {0, 0, 0, 255};
  ui_composite_srgba8(dst, src, 0.5f);
  ASSERT_EQUAL(dst[0], 188);
  ASSERT_EQUAL(dst[1], 188);
  ASSERT_EQUAL(dst[2], 188);
  ASSERT_EQUAL(dst[3], 255);
  PASS();
}

static void test_composite_alpha_boundaries(void) {
  TEST("transparent and opaque sRGB compositing preserves exact channel values");
  uint8_t dst[4] = {30, 60, 90, 255};
  const uint8_t transparent[4] = {210, 120, 40, 0};
  ui_composite_srgba8(dst, transparent, 1.0f);
  ASSERT_EQUAL(dst[0], 30); ASSERT_EQUAL(dst[1], 60); ASSERT_EQUAL(dst[2], 90); ASSERT_EQUAL(dst[3], 255);
  const uint8_t opaque[4] = {210, 120, 40, 255};
  ui_composite_srgba8(dst, opaque, 1.0f);
  ASSERT_EQUAL(dst[0], 210); ASSERT_EQUAL(dst[1], 120); ASSERT_EQUAL(dst[2], 40); ASSERT_EQUAL(dst[3], 255);
  dst[3] = 0;
  ui_composite_srgba8(dst, opaque, 0.5f);
  ASSERT_EQUAL(dst[0], 210); ASSERT_EQUAL(dst[1], 120); ASSERT_EQUAL(dst[2], 40); ASSERT_EQUAL(dst[3], 128);
  PASS();
}

int main(void) {
  TEST_START("sRGB color contract");
  test_srgb8_roundtrip();
  test_web_color_packing();
  test_linear_half_black_over_white();
  test_composite_alpha_boundaries();
  TEST_END();
}
