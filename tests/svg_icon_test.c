#include "test_framework.h"
#include <orion/user/svg_icon_loader.h>
#include <orion/user/bmp_icon_loader.h>
#include <orion/kernel/renderer.h>
#include <platform/platform.h>

static float test_density = 1.0f;
static uint8_t *uploaded_pixels;
static int uploaded_w, uploaded_h, upload_count, delete_count;
static R_TextureFilter uploaded_filter;

static float test_scaling(void) { return test_density; }
static uint32_t test_upload(int w, int h, const void *pixels,
                            R_TextureFilter filter, R_TextureWrap wrap) {
  free(uploaded_pixels);
  uploaded_pixels = malloc((size_t)w * h * 4);
  if (!uploaded_pixels) return 0;
  memcpy(uploaded_pixels, pixels, (size_t)w * h * 4);
  uploaded_w = w; uploaded_h = h; uploaded_filter = filter;
  return (uint32_t)++upload_count;
}
static void test_delete(uint32_t tex) { delete_count++; }
static bool test_bmp_resolve(const char *name, sysicon_resolved_t *out) { return false; }

#define axGetScaling test_scaling
#define R_CreateTextureSRGBA8 test_upload
#define R_DeleteTexture test_delete
#define bmp_icon_resolve test_bmp_resolve
#include <orion/user/svg_icon_loader.c>
#undef axGetScaling
#undef R_CreateTextureSRGBA8
#undef R_DeleteTexture
#undef bmp_icon_resolve

static bool tile_has_coverage(int x, int y, int size) {
  for (int row = y; row < y + size; row++)
    for (int col = x; col < x + size; col++)
      if (uploaded_pixels[((size_t)row * uploaded_w + col) * 4 + 3]) return true;
  return false;
}

static void test_strip_density(void) {
  TEST("SVG strips preserve logical sizes and UV tiles at 1x, 1.5x and 2x");
  const char *names[] = {"zoom-in", NULL, "undo"};
  const float densities[] = {1.0f, 1.5f, 2.0f};
  for (int i = 0; i < 3; i++) {
    test_density = densities[i];
    bitmap_strip_t strip = {0};
    ASSERT_TRUE(svg_build_strip("share/icons", names, 3, 24, 2, &strip, NULL));
    int raster = (int)ceilf(24 * test_density * UI_WINDOW_SCALE);
    ASSERT_EQUAL(uploaded_w, raster * 2);
    ASSERT_EQUAL(uploaded_h, raster * 2);
    ASSERT_EQUAL(uploaded_filter, R_FILTER_LINEAR);
    ASSERT_EQUAL(strip.icon_w, 24);
    ASSERT_EQUAL(strip.icon_h, 24);
    ASSERT_EQUAL(strip.sheet_w, 48);
    ASSERT_EQUAL(strip.sheet_h, 48);
    ASSERT_EQUAL(strip.cols, 2);
    ASSERT_TRUE(tile_has_coverage(0, 0, raster));
    ASSERT_FALSE(tile_has_coverage(raster, 0, raster));
    ASSERT_TRUE(tile_has_coverage(0, raster, raster));
    ASSERT_FALSE(tile_has_coverage(raster, raster, raster));
  }
  PASS();
}

static void test_named_icon_density(void) {
  TEST("named SVG icons rerasterize at Retina density and reuse the cache");
  svg_set_icons_dir("share/icons");
  test_density = 1.0f;
  sysicon_resolved_t normal, retina;
  ASSERT_TRUE(sysicon_resolve("zoom-in", &normal));
  ASSERT_EQUAL(uploaded_w, SYSICON_SIZE * UI_WINDOW_SCALE);
  int uploads_before = upload_count, deletes_before = delete_count;
  test_density = 2.0f;
  ASSERT_TRUE(sysicon_resolve("zoom-in", &retina));
  ASSERT_EQUAL(upload_count, uploads_before + 1);
  ASSERT_EQUAL(delete_count, deletes_before + 1);
  ASSERT_EQUAL(uploaded_w, 2 * SYSICON_SIZE * UI_WINDOW_SCALE);
  ASSERT_EQUAL(uploaded_h, uploaded_w);
  ASSERT_EQUAL(retina.w, normal.w);
  ASSERT_EQUAL(retina.h, normal.h);
  ASSERT_EQUAL(retina.w, SYSICON_SIZE);
  ASSERT_EQUAL(retina.u0, 0.0f);
  ASSERT_EQUAL(retina.v0, 0.0f);
  ASSERT_EQUAL(retina.u1, 1.0f);
  ASSERT_EQUAL(retina.v1, 1.0f);
  ASSERT_TRUE(sysicon_resolve("zoom-in", &retina));
  ASSERT_EQUAL(upload_count, uploads_before + 1);

  // A stretched 1x bitmap would have identical coverage throughout every 2x2 block.
  bool has_retina_detail = false;
  for (int y = 0; y < uploaded_h; y += 2) {
    for (int x = 0; x < uploaded_w; x += 2) {
      size_t p = ((size_t)y * uploaded_w + x) * 4 + 3;
      if (uploaded_pixels[p] != uploaded_pixels[p + 4] ||
          uploaded_pixels[p] != uploaded_pixels[p + uploaded_w * 4])
        has_retina_detail = true;
    }
  }
  ASSERT_TRUE(has_retina_detail);
  PASS();
}

int main(void) {
  TEST_START("SVG Retina rasterization");
  test_strip_density();
  test_named_icon_density();
  free(uploaded_pixels);
  TEST_END();
}
