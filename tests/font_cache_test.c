#include "test_framework.h"
#include <orion/kernel/renderer.h>
#include <platform/platform.h>

static float test_display_scale = 1.0f;
static float test_scaling(void) { return test_display_scale; }
static uint32_t test_create_texture(int w, int h, const void *pixels,
                                    R_TextureFilter filter, R_TextureWrap wrap) {
  return 1;
}
static bool test_update_texture(uint32_t tex, int x, int y, int w, int h,
                                const void *pixels) { return true; }
static void test_delete_texture(uint32_t tex) {}

#define axGetScaling test_scaling
#define R_CreateTextureR8 test_create_texture
#define R_UpdateTextureR8 test_update_texture
#define R_DeleteTexture test_delete_texture
#include <orion/user/font_cache.c>
#undef axGetScaling
#undef R_CreateTextureR8
#undef R_UpdateTextureR8
#undef R_DeleteTexture

static void test_font_size(void) {
  TEST("12 px UI fonts retain readable cap height at 1x and 2x");
  const char *paths[] = {
    "share/fonts/NotoSans-Medium.ttf", "share/fonts/NotoSans-Regular.ttf",
  };
  for (int i = 0; i < 2; i++) {
    for (int scale = 1; scale <= 2; scale++) {
      test_display_scale = (float)scale;
      font_cache_t *cache = font_cache_create(paths[i], 12.0f);
      ASSERT_NOT_NULL(cache);
      font_cache_glyph_t glyph = *font_cache_get_glyph(cache, 'H');
      font_cache_destroy(cache);
      ASSERT_TRUE(glyph.height / test_display_scale >= 8.5f);
      ASSERT_TRUE(glyph.advance >= 8);
    }
  }
  PASS();
}

static void test_retina_metrics(void) {
  TEST("Retina doubles raster detail while preserving logical metrics");
  const float sizes[] = {8.0f, 9.0f, 12.0f};
  for (int i = 0; i < 3; i++) {
    test_display_scale = 1.0f;
    font_cache_t *normal = font_cache_create("share/fonts/NotoSans-Regular.ttf", sizes[i]);
    test_display_scale = 2.0f;
    font_cache_t *retina = font_cache_create("share/fonts/NotoSans-Regular.ttf", sizes[i]);
    ASSERT_NOT_NULL(normal);
    ASSERT_NOT_NULL(retina);
    bool matches = font_cache_line_height(normal) == font_cache_line_height(retina);
    const char *sample = "AgjpMW09";
    for (const char *c = sample; *c; c++) {
      const font_cache_glyph_t *a = font_cache_get_glyph(normal, (uint32_t)*c);
      const font_cache_glyph_t *b = font_cache_get_glyph(retina, (uint32_t)*c);
      matches = matches && a->advance == b->advance &&
        fabsf(a->width - b->width / 2.0f) <= 1.0f &&
        fabsf(a->height - b->height / 2.0f) <= 1.0f &&
        fabsf(a->x_offset - b->x_offset / 2.0f) <= 1.0f &&
        fabsf(a->y_offset - b->y_offset / 2.0f) <= 1.0f;
    }
    font_cache_destroy(normal);
    font_cache_destroy(retina);
    ASSERT_TRUE(matches);
  }
  PASS();
}

int main(void) {
  TEST_START("UI font sizing");
  test_font_size();
  test_retina_metrics();
  TEST_END();
}
