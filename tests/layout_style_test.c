// Layout properties in the sheet: a container's gap, padding and flex-direction come
// from its computed style, a form's spacing=, padding= and orientation= override
// them, and a restyled container takes the new values.

#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/user/ui_style.h>

static result_t form_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)msg; (void)wparam; (void)lparam;
  return false;
}

static const form_ctrl_def_t k_stacks[] = {
  // Declares nothing: the sheet decides. Its sheet bits are what a form without attributes compiles to.
  { .class_name = "StackView", .id = 1, .name = "plain", .layout_spacing = 4, .layout_sheet = LAYOUT_SHEET_ALL },
  // Declares spacing and padding, leaves orientation to the sheet.
  { .class_name = "StackView", .id = 2, .name = "declared", .layout_spacing = 3, .padding = {1, 2, 1, 2},
    .layout_sheet = LAYOUT_SHEET_ORIENT },
  // Hand-written definition: the sheet never applies.
  { .class_name = "StackView", .id = 3, .name = "code", .layout_spacing = 5 },
};

static window_t *make_form(const char *css) {
  form_def_t def = { .name = "Layout", .width = 300, .height = 200, .flags = WINDOW_AUTO_LAYOUT,
                     .children = k_stacks, .child_count = 3, .stylesheet = css, .stylesheet_origin = "test.css" };
  return create_window_from_form(&def, 0, 0, NULL, form_proc, 0, NULL);
}

static void test_sheet_values(void) {
  TEST("gap, padding and flex-direction reach a container; declared values win");
  test_env_init();
  ui_set_app_stylesheet("StackView#plain { gap: 6px; padding: 10px 14px 8px 2px; flex-direction: row; }"
                        "StackView#declared { gap: 20px; padding: 30px; flex-direction: row; }"
                        "StackView#code { gap: 20px; padding: 30px; }", "app.css");
  window_t *form = make_form(NULL);
  window_t *plain = get_window_item(form, 1), *declared = get_window_item(form, 2), *code = get_window_item(form, 3);
  ASSERT_EQUAL(plain->layout.layout_spacing, 6);
  ASSERT_TRUE(plain->layout.layout_padding.x == 2 && plain->layout.layout_padding.y == 10 &&
              plain->layout.layout_padding.w == 14 && plain->layout.layout_padding.h == 8);
  ASSERT_TRUE(plain->flags & WINDOW_STACK_HORIZONTAL);
  ASSERT_EQUAL(declared->layout.layout_spacing, 3);
  ASSERT_TRUE(declared->layout.layout_padding.x == 1 && declared->layout.layout_padding.y == 2);
  ASSERT_TRUE(declared->flags & WINDOW_STACK_HORIZONTAL);   // not declared: the sheet's row
  ASSERT_EQUAL(code->layout.layout_spacing, 5);
  ASSERT_FALSE(code->flags & WINDOW_STACK_HORIZONTAL);
  destroy_window(form);
  ui_set_app_stylesheet(NULL, NULL);
  test_env_shutdown();
  PASS();
}

static void test_defaults_and_restyle(void) {
  TEST("without a rule a container takes base.css's defaults; restyling takes the new rule");
  test_env_init();
  ui_set_app_stylesheet("StackView.wide { gap: 12px 7px; flex-direction: row; }", "app.css");
  window_t *form = make_form(NULL), *plain = get_window_item(form, 1);
  ASSERT_EQUAL(plain->layout.layout_spacing, 4);
  ASSERT_TRUE(plain->layout.layout_padding.x == 0 && plain->layout.layout_padding.w == 0);
  ASSERT_FALSE(plain->flags & WINDOW_STACK_HORIZONTAL);
  window_set_style(plain, "plain", "wide");
  ASSERT_EQUAL(plain->layout.layout_spacing, 7);   // a row's gap is its column-gap
  ASSERT_TRUE(plain->flags & WINDOW_STACK_HORIZONTAL);
  window_set_style(plain, "plain", "");
  ASSERT_EQUAL(plain->layout.layout_spacing, 4);
  ASSERT_FALSE(plain->flags & WINDOW_STACK_HORIZONTAL);
  window_set_layout(plain, WINDOW_STACK_HORIZONTAL, 9, (irect16_t){0, 0, 0, 0});   // code declares: the sheet stops
  window_set_style(plain, "plain", "wide");
  ASSERT_EQUAL(plain->layout.layout_spacing, 9);
  destroy_window(form);
  ui_set_app_stylesheet(NULL, NULL);
  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("Layout properties in the style sheet");
  test_sheet_values();
  test_defaults_and_restyle();
  TEST_END();
}
