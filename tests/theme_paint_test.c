#include "test_framework.h"
#include <orion/user/theme.h>
#include <orion/user/draw.h>
#include <math.h>

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
static int recorded_radius;
static void paint_rounded(uint32_t color, irect16_t r, int radius) {
  recorded_radius = radius;
  float rad = MIN(radius, MIN(r.w, r.h) * 0.5f);
  for (int y = MAX(0, r.y); y < MIN(64, r.y + r.h); y++) {
    for (int x = MAX(0, r.x); x < MIN(128, r.x + r.w); x++) {
      float qx = fabsf(x + 0.5f - r.x - r.w * 0.5f) - r.w * 0.5f + rad;
      float qy = fabsf(y + 0.5f - r.y - r.h * 0.5f) - r.h * 0.5f + rad;
      if (hypotf(MAX(qx, 0), MAX(qy, 0)) <= rad) pixels[y][x] = color;
    }
  }
}
#define fill_rounded_rect paint_rounded
#define fill_rect paint_fill
#define draw_wire_rect paint_wire
#define theme_classic_instance paint_classic_instance
#define theme_modern_instance paint_modern_instance
theme_t *paint_modern_instance(void);
#include <orion/user/theme_classic.c>
#include <orion/user/theme_modern.c>
#undef fill_rect
#undef draw_wire_rect
#undef fill_rounded_rect
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
  ASSERT_EQUAL(pixels[25][50], get_sys_color(brControlBg));
  PASS();
}

static void test_modern_button_states(void) {
  TEST("Modern secondary button stays neutral when pressed");
  theme_t *theme = paint_modern_instance();
  theme->apply_palette();
  memset(pixels, 0, sizeof(pixels));
  theme->draw_part(THEME_PART_BUTTON, R(12, 10, 80, 30), CTRL_PRESSED);
  ASSERT_EQUAL(pixels[25][12], MODERN_SECONDARY_BORDER);
  ASSERT_EQUAL(pixels[25][50], get_sys_color(brButtonHover));
  ASSERT_NOT_EQUAL(pixels[25][50], get_sys_color(brAccent));
  memset(pixels, 0, sizeof(pixels));
  theme->draw_part(THEME_PART_BUTTON, R(12, 10, 80, 30), CTRL_DEFAULT | CTRL_PRESSED);
  ASSERT_EQUAL(pixels[25][50], get_sys_color(brAccent));
  PASS();
}

static void test_translated_separator(void) {
  TEST("Classic toolbar separators respect caller bounds; Modern toolbar separators draw nothing");
  theme_t *themes[] = {paint_classic_instance(), paint_modern_instance()};
  for (int i = 0; i < 2; i++) {
    themes[i]->apply_palette();
    memset(pixels, 0, sizeof(pixels));
    themes[i]->draw_part(THEME_PART_TOOLBAR_SEPARATOR, R(40, 20, 10, 24), CTRL_NORMAL);
    if (i == 0) { ASSERT_NOT_EQUAL(pixels[32][45], 0); }
    else { ASSERT_EQUAL(pixels[32][45], 0); }
    ASSERT_EQUAL(pixels[12][5], 0);
    ASSERT_EQUAL(pixels[19][45], 0);
  }
  PASS();
}

static void test_independent_parts(void) {
  TEST("Modern dropdowns match field bounds and fills in every state");
  theme_t *theme = paint_modern_instance();
  theme->apply_palette();
  ctrl_state_t states[] = {CTRL_NORMAL, CTRL_HOVER, CTRL_PRESSED, CTRL_FOCUSED, CTRL_DISABLED};
  uint32_t field[64][128];
  for (int i = 0; i < ARRAY_LEN(states); i++) {
    memset(pixels, 0, sizeof(pixels));
    theme->draw_part(THEME_PART_FIELD, R(10, 10, 80, 30), states[i]);
    memcpy(field, pixels, sizeof(field));
    memset(pixels, 0, sizeof(pixels));
    theme->draw_part(THEME_PART_COMBOBOX, R(10, 10, 80, 30), states[i]);
    ASSERT_EQUAL(memcmp(field, pixels, sizeof(field)), 0);
    ASSERT_EQUAL(pixels[25][50], modern_surface_midpoint(get_sys_color(brControlBg),
                                                      get_sys_color(brWindowDarkBg)));
  }
  memset(pixels, 0, sizeof(pixels));
  theme->draw_part(THEME_PART_SCROLLBAR_THUMB, R(10, 10, SCROLLBAR_WIDTH, 30), CTRL_NORMAL);
  ASSERT_EQUAL(pixels[10][10], 0);
  ASSERT_NOT_EQUAL(pixels[25][13], 0);
  PASS();
}

static void test_flat_tool_items(void) {
  TEST("Modern tool items are transparent at rest and highlight only inside their bounds");
  theme_t *theme = paint_modern_instance();
  theme->apply_palette();
  ctrl_state_t states[] = {CTRL_NORMAL, CTRL_HOVER, CTRL_SELECTED, CTRL_PRESSED};
  for (int i = 0; i < ARRAY_LEN(states); i++) {
    memset(pixels, 0, sizeof(pixels));
    theme->draw_part(THEME_PART_TOOLBAR_BUTTON, R(10, 10, 30, 30), states[i]);
    ASSERT_EQUAL(pixels[10][10], 0);
    ASSERT_EQUAL(pixels[25][9], 0);
    ASSERT_EQUAL(pixels[25][40], 0);
    ASSERT_EQUAL(pixels[9][25], 0);
    ASSERT_EQUAL(pixels[40][25], 0);
    ASSERT_EQUAL(pixels[39][39], 0);
    ASSERT_EQUAL(pixels[25][10], pixels[25][25]);
    ASSERT_EQUAL(pixels[25][39], pixels[25][25]);
    ASSERT_EQUAL(pixels[10][25], pixels[25][25]);
    ASSERT_EQUAL(pixels[39][25], pixels[25][25]);
    if (i == 0) { ASSERT_EQUAL(pixels[25][25], 0); }
    else { ASSERT_NOT_EQUAL(pixels[25][25], 0); }
    if (i == 2) {
      ASSERT_EQUAL(pixels[25][25] >> 24, 255);
      ASSERT_NOT_EQUAL(pixels[25][25], get_sys_color(brControlBg));
      ASSERT_NOT_EQUAL(pixels[25][25], get_sys_color(theme->item_background.hover));
      ASSERT_EQUAL(recorded_radius, 4);
    }
  }
  ASSERT_EQUAL(theme->window_corner_radius, 8);
  ASSERT_EQUAL(paint_classic_instance()->window_corner_radius, 0);
  PASS();
}

static void test_palette_overrides(void) {
  TEST("Modern active parts and panels follow runtime palette overrides");
  theme_t *theme = paint_modern_instance();
  theme->apply_palette();
  g_sys_colors[brAccent] = 0xff123456;
  g_sys_colors[brControlBg] = 0xff654321;
  theme_part_t parts[] = {THEME_PART_TOOLBAR_BUTTON, THEME_PART_TAB, THEME_PART_LIST_ITEM,
                          THEME_PART_TITLEBAR, THEME_PART_BUTTON};
  ctrl_state_t states[] = {CTRL_SELECTED, CTRL_SELECTED | CTRL_HOVER, CTRL_SELECTED | CTRL_PRESSED};
  for (int i = 0; i < ARRAY_LEN(parts); i++) {
    for (int j = 0; j < ARRAY_LEN(states); j++) {
      memset(pixels, 0, sizeof(pixels));
      theme->draw_part(parts[i], R(10, 10, 80, 30), states[j] | CTRL_FOCUSED | CTRL_DEFAULT);
      ASSERT_EQUAL(pixels[25][50], get_sys_color(brAccent));
    }
  }
  theme->draw_part(THEME_PART_PANEL, R(10, 10, 80, 30), CTRL_NORMAL);
  ASSERT_EQUAL(pixels[25][50], get_sys_color(brControlBg));
  theme->apply_palette();
  PASS();
}

static void test_full_row_selection(void) {
  TEST("Modern list selection is inset, rounded, and uses selected text");
  theme_t *theme = paint_modern_instance();
  theme->apply_palette();
  memset(pixels, 0, sizeof(pixels));
  theme->draw_part(THEME_PART_LIST_ITEM, R(10, 10, 80, 30), CTRL_SELECTED);
  ASSERT_EQUAL(pixels[10][10], 0);
  ASSERT_EQUAL(pixels[25][10], 0);
  ASSERT_EQUAL(pixels[10][50], 0);
  ASSERT_EQUAL(pixels[25][14], get_sys_color(brAccent));
  ASSERT_EQUAL(pixels[25][50], get_sys_color(brAccent));
  ASSERT_EQUAL(pixels[25][85], get_sys_color(brAccent));
  ASSERT_EQUAL(pixels[9][10], 0);
  ASSERT_EQUAL(pixels[40][10], 0);
  ASSERT_EQUAL(pixels[25][90], 0);
  ASSERT_EQUAL(theme->foreground(THEME_PART_LIST_ITEM, CTRL_SELECTED), get_sys_color(brActiveTitlebarText));
  ASSERT_NOT_EQUAL(theme->foreground(THEME_PART_LIST_ITEM, CTRL_SELECTED),
                   theme->foreground(THEME_PART_LIST_ITEM, CTRL_NORMAL));
  memset(pixels, 0, sizeof(pixels));
  theme->draw_part(THEME_PART_MENU_ITEM, R(10, 10, 80, 24), CTRL_HOVER);
  ASSERT_EQUAL(recorded_radius, 11);
  ASSERT_EQUAL(pixels[11][14], 0);
  ASSERT_EQUAL(pixels[11][25], get_sys_color(brAccent));
  ASSERT_EQUAL(pixels[22][14], get_sys_color(brAccent));
  ASSERT_EQUAL(pixels[22][13], 0);
  PASS();
}

static void test_scrollbar_thickness(void) {
  TEST("Both scrollbar orientations paint an 11 pixel thumb inside a 17 pixel strip");
  theme_t *theme = paint_modern_instance();
  memset(pixels, 0, sizeof(pixels));
  theme->draw_part(THEME_PART_SCROLLBAR_THUMB, R(10, 5, 17, 50), CTRL_NORMAL);
  int width = 0;
  for (int x = 10; x < 27; x++) width += pixels[30][x] != 0;
  ASSERT_EQUAL(width, 11);
  ASSERT_EQUAL(pixels[30][12], 0);
  ASSERT_EQUAL(pixels[30][24], 0);
  memset(pixels, 0, sizeof(pixels));
  theme->draw_part(THEME_PART_SCROLLBAR_THUMB, R(10, 5, 50, 17), CTRL_NORMAL);
  int height = 0;
  for (int y = 5; y < 22; y++) height += pixels[y][35] != 0;
  ASSERT_EQUAL(height, 11);
  PASS();
}

static void test_button_capsules(void) {
  TEST("Modern button ends follow the full height at desktop and touch sizes");
  theme_t *theme = paint_modern_instance();
  const int heights[] = {13, 25, 40};
  const ctrl_state_t states[] = {CTRL_NORMAL, CTRL_DEFAULT, CTRL_DISABLED, CTRL_HOVER, CTRL_PRESSED};
  for (int h = 0; h < ARRAY_LEN(heights); h++) {
    for (int st = 0; st < ARRAY_LEN(states); st++) {
      memset(pixels, 0, sizeof(pixels));
      theme->draw_part(THEME_PART_BUTTON, R(10, 10, 100, heights[h]), states[st]);
      ASSERT_EQUAL(pixels[10][12], 0);
      ASSERT_TRUE(pixels[10 + heights[h] / 2][10] != 0);
      ASSERT_TRUE(pixels[10][60] != 0);
    }
  }
  PASS();
}

int main(void) {
  TEST_START("theme paint output");
  test_modern_surfaces();
  test_button_capsules();
  test_modern_button_states();
  test_translated_separator();
  test_independent_parts();
  test_flat_tool_items();
  test_palette_overrides();
  test_full_row_selection();
  test_scrollbar_thickness();
  TEST_END();
}
