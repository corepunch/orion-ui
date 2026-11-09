// App style sheets and per-window selectors: a control's form name is its
// #id, class= its classes, and an app instance's sheet styles only its windows.

#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/user/ui_style.h>

static result_t form_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)msg; (void)wparam; (void)lparam;
  return false;
}

static const form_ctrl_def_t k_table = {
  .class_name = "ReportView", .id = 1, .flags = WINDOW_NOTITLE | WINDOW_VSCROLL, .text = "",
  .name = "checkouts", .style_class = "compact dense",
};
static const form_ctrl_def_t k_plain = { .class_name = "ReportView", .id = 2, .flags = WINDOW_NOTITLE, .text = "" };

static window_t *make_form(const form_ctrl_def_t *child, hinstance_t hinstance, const char *css) {
  form_def_t def = { .name = "Sheet", .width = 300, .height = 200, .flags = WINDOW_AUTO_LAYOUT,
                     .children = child, .child_count = 1, .stylesheet = css, .stylesheet_origin = "test.css" };
  return create_window_from_form(&def, 0, 0, NULL, form_proc, hinstance, NULL);
}

static void test_selector_composition(void) {
  TEST("a control's selector carries its classes and #id; an unstyled one is the element's");
  test_env_init();
  window_t *form = make_form(&k_table, 0, NULL), *table = form->children;
  ASSERT_STR_EQUAL(table->style_id, "checkouts");
  ASSERT_STR_EQUAL(table->style_class, "compact dense");
  const char *header = ui_selector(table, "TableView::header");
  ASSERT_STR_EQUAL(header, "TableView.compact.dense#checkouts::header");
  ASSERT_TRUE(ui_selector(table, "TableView::header") == header);   // interned
  ASSERT_STR_EQUAL(ui_selector(table, "Label.caption"), "Label.compact.dense#checkouts.caption");
  window_t *other = make_form(&k_plain, 0, NULL);
  const char *base = "TableView::header";
  ASSERT_TRUE(ui_selector(other->children, base) == base);
  destroy_window(other); destroy_window(form);
  test_env_shutdown();
  PASS();
}

static void test_id_and_class_rules(void) {
  TEST("#id and .class rules reach one table's metrics and parts");
  test_env_init();
  ui_set_app_stylesheet("TableView#checkouts::header { min-height: 40px; color: #FF0000; }"
                        "TableView.dense::row { min-height: 30px; }", "app.css");
  window_t *form = make_form(&k_table, 0, NULL), *table = form->children;
  ASSERT_EQUAL(ui_px(ui_selector(table, "TableView::header"), "min-height"), 40);
  ASSERT_EQUAL(ui_px(ui_selector(table, "TableView::row"), "min-height"), 30);
  ASSERT_TRUE(ui_px("TableView::header", "min-height") != 40);
  ASSERT_EQUAL(theme_foreground_in(table, THEME_PART_HEADER, CTRL_NORMAL), 0xFF0000FFu);
  ASSERT_TRUE(theme_foreground(THEME_PART_HEADER, CTRL_NORMAL) != 0xFF0000FFu);
  destroy_window(form);
  ui_set_app_stylesheet(NULL, NULL);
  test_env_shutdown();
  PASS();
}

static void test_instance_sheet(void) {
  TEST("a form's stylesheet belongs to its app instance");
  test_env_init();
  window_t *mine = make_form(&k_plain, 7, "TableView::header { min-height: 33px; }");
  window_t *theirs = make_form(&k_plain, 8, NULL);
  ASSERT_TRUE(ui_has_instance_stylesheet(7));
  ASSERT_FALSE(ui_has_instance_stylesheet(8));
  ASSERT_EQUAL(ui_px(ui_selector(mine->children, "TableView::header"), "min-height"), 33);
  ASSERT_TRUE(ui_px(ui_selector(theirs->children, "TableView::header"), "min-height") != 33);
  ASSERT_TRUE(ui_px("TableView::header", "min-height") != 33);
  destroy_window(mine); destroy_window(theirs);
  ui_set_instance_stylesheet(7, NULL, NULL);
  ASSERT_FALSE(ui_has_instance_stylesheet(7));
  test_env_shutdown();
  PASS();
}

static void test_window_set_style(void) {
  TEST("window_set_style restyles a window");
  test_env_init();
  ui_set_app_stylesheet("Label.muted { color: #00FF00; }", "app.css");
  window_t *label = create_window("Hi", 0, MAKERECT(0, 0, 50, 20), NULL, win_label, 0, NULL);
  ASSERT_TRUE(ui_color(ui_selector(label, "Label"), "color") != 0xFF00FF00u);
  window_set_style(label, NULL, "muted");
  ASSERT_EQUAL(ui_color(ui_selector(label, "Label"), "color"), 0xFF00FF00u);
  destroy_window(label);
  ui_set_app_stylesheet(NULL, NULL);
  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("App style sheets");
  test_selector_composition();
  test_id_and_class_rules();
  test_instance_sheet();
  test_window_set_style();
  TEST_END();
}
