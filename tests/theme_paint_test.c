#include "test_framework.h"
#include <orion/user/theme.h>
#include <orion/user/draw.h>

// Exercise the real theme implementations with a software target, without GL.
static uint32_t pixels[64][128];
static void paint_fill(uint32_t color, irect16_t r) {
  for (int y = MAX(0, r.y); y < MIN(64, r.y + r.h); y++)
    for (int x = MAX(0, r.x); x < MIN(128, r.x + r.w); x++) pixels[y][x] = color;
}
static void paint_wire(irect16_t r, int expand, uint32_t color) {
  r = rect_inset(r, -expand);
  paint_fill(color, rect_split_top(r, 1));
  paint_fill(color, rect_split_bottom(r, 1));
  paint_fill(color, rect_split_left(r, 1));
  paint_fill(color, rect_split_right(r, 1));
}
#define fill_rect paint_fill
#define draw_wire_rect paint_wire
#define theme_classic_instance paint_classic_instance
#define theme_modern_instance paint_modern_instance
#include <orion/user/theme_classic.c>
#include <orion/user/theme_modern.c>
#undef fill_rect
#undef draw_wire_rect
#undef theme_classic_instance
#undef theme_modern_instance

static void test_modern_surfaces(void) {
  TEST("Modern paints a button border with a separate interior and preserves its panel fill");
  theme_t *theme = paint_modern_instance();
  theme->apply_palette();
  memset(pixels, 0, sizeof(pixels));
  theme->draw_part(THEME_PART_BUTTON, R(12, 10, 80, 30), CTRL_NORMAL);
  ASSERT_EQUAL(pixels[25][12], MODERN_SECONDARY_BORDER);
  ASSERT_EQUAL(pixels[25][50], get_sys_color(brControlBg));
  theme->draw_part(THEME_PART_PANEL, R(12, 10, 80, 30), CTRL_NORMAL);
  ASSERT_EQUAL(pixels[25][50], MODERN_PANEL_BG);
  PASS();
}

static void test_translated_separator(void) {
  TEST("toolbar separators respect caller x/y in both themes");
  theme_t *themes[] = {paint_classic_instance(), paint_modern_instance()};
  for (int i = 0; i < 2; i++) {
    themes[i]->apply_palette();
    memset(pixels, 0, sizeof(pixels));
    themes[i]->draw_part(THEME_PART_TOOLBAR_SEPARATOR, R(40, 20, 10, 24), CTRL_NORMAL);
    ASSERT_NOT_EQUAL(pixels[32][45], 0);
    ASSERT_EQUAL(pixels[12][5], 0);
    ASSERT_EQUAL(pixels[19][45], 0);
  }
  PASS();
}

static void test_independent_parts(void) {
  TEST("Modern dropdowns are idle-transparent, fields stay visible, and scrollbar thumbs are rounded");
  theme_t *theme = paint_modern_instance();
  theme->apply_palette();
  memset(pixels, 0, sizeof(pixels));
  theme->draw_part(THEME_PART_COMBOBOX, R(10, 10, 80, 30), CTRL_NORMAL);
  ASSERT_EQUAL(pixels[25][50], 0);
  theme->draw_part(THEME_PART_FIELD, R(10, 10, 80, 30), CTRL_NORMAL);
  ASSERT_NOT_EQUAL(pixels[25][50], 0);
  memset(pixels, 0, sizeof(pixels));
  theme->draw_part(THEME_PART_SCROLLBAR_THUMB, R(10, 10, 6, 30), CTRL_NORMAL);
  ASSERT_EQUAL(pixels[10][10], 0);
  ASSERT_NOT_EQUAL(pixels[25][13], 0);
  PASS();
}

int main(void) {
  TEST_START("theme paint output");
  test_modern_surfaces();
  test_translated_separator();
  test_independent_parts();
  TEST_END();
}
