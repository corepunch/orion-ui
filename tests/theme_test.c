#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/user/theme.h>

static theme_part_t recorded_part;
static ctrl_state_t recorded_state;
static int draw_count;

static void record_part(theme_part_t part, irect16_t r, ctrl_state_t state) {
  recorded_part = part;
  recorded_state = state;
  draw_count++;
}

static void test_switch_validation(void) {
  TEST("theme switching rejects invalid styles, callbacks and metrics without changing palette");
  test_env_init();
  set_theme(THEME_CLASSIC);
  uint32_t classic_color = get_sys_color(brControlBg);
  ASSERT_TRUE(set_theme(THEME_MODERN));
  ASSERT_NOT_EQUAL(classic_color, get_sys_color(brControlBg));
  theme_t *modern = get_theme(), *classic = theme_classic_instance();
  uint32_t modern_color = get_sys_color(brControlBg);
  ASSERT_FALSE(set_theme((theme_style_t)-1));
  ASSERT_FALSE(set_theme((theme_style_t)255));
  ASSERT_TRUE(get_theme() == modern);
  ASSERT_EQUAL(modern_color, get_sys_color(brControlBg));

  void (*saved)(theme_part_t, irect16_t, ctrl_state_t) = classic->draw_part;
  classic->draw_part = NULL;
  bool result = set_theme(THEME_CLASSIC);
  classic->draw_part = saved;
  ASSERT_FALSE(result);
  int width = classic->scrollbar_width;
  classic->scrollbar_width = -1;
  result = set_theme(THEME_CLASSIC);
  classic->scrollbar_width = width;
  ASSERT_FALSE(result);
  ASSERT_TRUE(get_theme() == modern);
  ASSERT_EQUAL(modern_color, get_sys_color(brControlBg));
  ASSERT_FALSE(modern->scrollbar_overlay);
  ASSERT_EQUAL(modern->scrollbar_width, SCROLLBAR_WIDTH);
  ASSERT_TRUE(set_theme(THEME_CLASSIC));
  ASSERT_EQUAL(get_sys_color(brControlBg), classic_color);
  ASSERT_EQUAL(get_theme()->scrollbar_width, SCROLLBAR_WIDTH);
  int color_id = brControlBg;
  uint32_t override = 0xff123456;
  set_sys_colors(1, &color_id, &override);
  ASSERT_FALSE(set_theme(THEME_CLASSIC));
  ASSERT_EQUAL(get_sys_color(brControlBg), override);
  set_theme(THEME_MODERN);
  set_theme(THEME_CLASSIC);
  ASSERT_EQUAL(get_sys_color(brControlBg), classic_color);
  test_env_shutdown();
  PASS();
}

static void test_semantic_dispatch(void) {
  TEST("semantic dispatch preserves selection/focus, suppresses disabled transients and rejects invalid parts");
  theme_t *theme = get_theme();
  void (*saved)(theme_part_t, irect16_t, ctrl_state_t) = theme->draw_part;
  theme->draw_part = record_part;
  draw_count = 0;
  theme_draw(THEME_PART_BUTTON, R(10, 20, 60, 24),
             CTRL_DISABLED | CTRL_SELECTED | CTRL_FOCUSED | CTRL_HOVER | CTRL_PRESSED);
  ctrl_state_t state = recorded_state;
  theme_draw((theme_part_t)-1, R(0, 0, 10, 10), CTRL_NORMAL);
  theme_draw(THEME_PART_COUNT, R(0, 0, 10, 10), CTRL_NORMAL);
  theme_draw(THEME_PART_BUTTON, R(0, 0, 0, 10), CTRL_NORMAL);
  theme->draw_part = saved;
  ASSERT_EQUAL(draw_count, 1);
  ASSERT_EQUAL(recorded_part, THEME_PART_BUTTON);
  ASSERT_EQUAL(state, CTRL_DISABLED | CTRL_SELECTED | CTRL_FOCUSED);
  PASS();
}

static void test_control_parts(void) {
  TEST("fields and checked buttons send distinct semantic parts and state");
  test_env_init();
  window_t *field = test_env_create_window("field", 0, 0, 100, 24, win_textedit, NULL);
  window_t *button = test_env_create_window("toggle", 0, 0, 100, 24, win_button, NULL);
  ASSERT_NOT_NULL(field);
  ASSERT_NOT_NULL(button);
  button->flags |= BUTTON_PUSHLIKE;
  button->value = 1;
  g_ui_runtime.focused = field;
  theme_t *theme = get_theme();
  void (*saved)(theme_part_t, irect16_t, ctrl_state_t) = theme->draw_part;
  theme->draw_part = record_part;
  send_message(field, evPaint, 0, NULL);
  theme_part_t field_part = recorded_part;
  ctrl_state_t field_state = recorded_state;
  send_message(button, evPaint, 0, NULL);
  theme->draw_part = saved;
  ASSERT_EQUAL(field_part, THEME_PART_FIELD);
  ASSERT_TRUE(field_state & CTRL_FOCUSED);
  ASSERT_EQUAL(recorded_part, THEME_PART_BUTTON);
  ASSERT_TRUE(recorded_state & CTRL_SELECTED);
  ASSERT_FALSE(recorded_state & CTRL_PRESSED);
  test_env_shutdown();
  PASS();
}

static void test_all_parts(void) {
  TEST("both themes implement every part/state combination and selection foreground policy");
  test_env_init();
  for (int style = THEME_CLASSIC; style <= THEME_MODERN; style++) {
    set_theme((theme_style_t)style);
    for (int part = 0; part < THEME_PART_COUNT; part++)
      for (int state = 0; state < 64; state++)
        theme_draw((theme_part_t)part, R(5, 7, 60, 24), (ctrl_state_t)state);
    ASSERT_EQUAL(theme_foreground(THEME_PART_LIST_ITEM, CTRL_SELECTED),
                 get_sys_color(brActiveTitlebarText));
    ASSERT_EQUAL(theme_foreground(THEME_PART_BUTTON, CTRL_DISABLED | CTRL_DEFAULT), get_sys_color(brTextDisabled));
  }
  test_env_shutdown();
  PASS();
}

extern void draw_window_controls(window_t *win);
static irect16_t recorded_titlebar, recorded_caption;
static void record_chrome(irect16_t titlebar, irect16_t caption, const char *title, ctrl_state_t state, bool maximizable) {
  recorded_titlebar = titlebar;
  recorded_caption = caption;
}

static void test_titlebar_bounds(void) {
  TEST("titlebar paint excludes the toolbar band");
  test_env_init();
  window_t win = {0};
  win.frame = R(0, 0, 300, 200);
  win.flags = WINDOW_TOOLBAR;
  theme_t *theme = get_theme();
  void (*saved)(irect16_t, irect16_t, const char *, ctrl_state_t, bool) = theme->draw_window_chrome;
  theme->draw_window_chrome = record_chrome;
  draw_window_controls(&win);
  theme->draw_window_chrome = saved;
  ASSERT_EQUAL(recorded_titlebar.h, TITLEBAR_HEIGHT);
  ASSERT_EQUAL(recorded_titlebar.w, 300);
  ASSERT_EQUAL(recorded_caption.h, TITLEBAR_HEIGHT);
  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("theme semantic API");
  test_switch_validation();
  test_semantic_dispatch();
  test_control_parts();
  test_all_parts();
  test_titlebar_bounds();
  TEST_END();
}
