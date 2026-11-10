// App style sheets and per-window selectors: a control's form name is its
// #id, class= its classes, and an app instance's sheet styles only its windows.

#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/user/ui_style.h>

#ifdef APP_STYLESHEET_TEST_STANDALONE
#include <orion/user/style.h>
#include <orion/user/ui_style.c>
#include <orion/user/theme_box.c>

static style_sheet_t *test_process_sheet;
static struct { hinstance_t instance; style_sheet_t *sheet; } test_instance_sheets[2];
unsigned g_theme_style_generation = 1;
uint32_t g_sys_colors[brCount];

theme_t *get_theme(void) { return NULL; }
style_sheet_t *theme_stylesheet(theme_t *theme) { (void)theme; return test_process_sheet; }
style_sheet_t *ui_instance_stylesheet(hinstance_t instance) {
  for (size_t index = 0; index < 2; index++)
    if (test_instance_sheets[index].instance == instance) return test_instance_sheets[index].sheet;
  return NULL;
}
bool ui_has_instance_stylesheet(hinstance_t instance) { return ui_instance_stylesheet(instance) != NULL; }
void ui_set_instance_stylesheet(hinstance_t instance, const char *css, const char *origin) {
  for (size_t index = 0; index < 2; index++) {
    if (test_instance_sheets[index].instance && test_instance_sheets[index].instance != instance) continue;
    style_sheet_free(test_instance_sheets[index].sheet);
    test_instance_sheets[index].instance = css ? instance : 0;
    test_instance_sheets[index].sheet = css ? style_sheet_parse(css, origin) : NULL;
    g_theme_style_generation++;
    return;
  }
  abort();
}
void test_env_init(void) { test_process_sheet = style_sheet_parse("Button { color: #000000; }", "process.css"); }
void test_env_shutdown(void) { style_sheet_free(test_process_sheet); test_process_sheet = NULL; g_theme_style_generation++; }
uint32_t theme_foreground_in(const window_t *win, theme_part_t part, ctrl_state_t state) {
  return theme_css_foreground(part, state, ui_window_scope(win));
}
irect16_t theme_content_insets_in(const window_t *win, theme_part_t part, ctrl_state_t state) {
  return theme_css_insets_vars(part, state, ui_window_scope(win), NULL);
}
#else
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

static void test_controls_read_their_selectors(void) {
  TEST("Card and TabView read their own #id rules; a restyled Card recomputes its padding");
  test_env_init();
  ui_set_app_stylesheet("Card#roomy { padding-left: 30px; padding-top: 20px; }"
                        "TabView#wide::tab { min-width: 200px; }", "app.css");
  static const form_ctrl_def_t roomy = { .class_name = "Card", .id = 3, .text = "", .name = "roomy" };
  static const form_ctrl_def_t plain = { .class_name = "Card", .id = 4, .text = "", .name = "plain" };
  window_t *a = make_form(&roomy, 0, NULL), *b = make_form(&plain, 0, NULL);
  int ring = ui_px("Card", "--ring-width");
  ASSERT_EQUAL(a->children->layout.layout_padding.y, ring + 20);
  ASSERT_TRUE(b->children->layout.layout_padding.y != ring + 20);
  window_set_style(b->children, "roomy", NULL);
  ASSERT_EQUAL(b->children->layout.layout_padding.y, ring + 20);

  window_t *host = create_window("Host", 0, MAKERECT(0, 0, 900, 200), NULL, form_proc, 0, NULL);
  window_t *tabs = create_window("", WINDOW_NOTITLE, MAKERECT(0, 0, 900, TAB_CONTROL_HEIGHT), host, "TabView", 0, NULL);
  const tab_item_t items[] = {{"A", 1}, {"B", 2}};
  ASSERT_TRUE(send_message(tabs, tcSetItems, 2, (void *)items));
  irect16_t r;
  ASSERT_TRUE(send_message(tabs, tcGetTabRect, 0, &r));
  ASSERT_TRUE(r.w < 200);
  window_set_style(tabs, "wide", NULL);
  ASSERT_TRUE(send_message(tabs, tcGetTabRect, 0, &r));
  ASSERT_EQUAL(r.w, 200);

  destroy_window(host); destroy_window(a); destroy_window(b);
  ui_set_app_stylesheet(NULL, NULL);
  test_env_shutdown();
  PASS();
}

static void test_instance_variables(void) {
  TEST("window_set_style_var tones one Badge and one Card without touching the sheet");
  test_env_init();
  ui_set_app_stylesheet("Badge { --ink: 100% }", "app.css");
  window_t *a = create_window("a", 0, MAKERECT(0, 0, 40, 18), NULL, win_badge, 0, NULL);
  window_t *b = create_window("b", 0, MAKERECT(0, 0, 40, 18), NULL, win_badge, 0, NULL);
  ASSERT_TRUE(window_set_style_var_color(a, "--tone", 0xFF00FF00u));
  ASSERT_TRUE(window_set_style_var_color(b, "--tone", 0xFF0000FFu));
  ASSERT_EQUAL(theme_foreground_in(a, THEME_PART_BADGE, CTRL_NORMAL), 0xFF00FF00u);
  ASSERT_EQUAL(theme_foreground_in(b, THEME_PART_BADGE, CTRL_NORMAL), 0xFF0000FFu);
  ASSERT_FALSE(window_set_style_var(a, "tone", "#fff"));   // custom properties start with --
  send_message(b, bdSetColor, brTextSuccess, NULL);          // a role follows the palette
  ASSERT_EQUAL(theme_foreground_in(b, THEME_PART_BADGE, CTRL_NORMAL), get_sys_color(brTextSuccess));
  window_t *card = create_window("c", 0, MAKERECT(0, 0, 80, 40), NULL, win_card, 0, NULL);
  irect16_t plain = theme_content_insets_in(card, THEME_PART_CARD, CTRL_NORMAL);
  send_message(card, cdSetEdgeColor, 0, (void *)(uintptr_t)0xFF0000FFu);
  ASSERT_EQUAL(theme_content_insets_in(card, THEME_PART_CARD, CTRL_NORMAL).x, plain.x);   // the edge is reserved either way
  ASSERT_TRUE(window_style_vars(card) != NULL);
  send_message(card, cdSetEdgeColor, 0, NULL);
  ASSERT_TRUE(window_style_vars(card) == NULL);
  destroy_window(a); destroy_window(b); destroy_window(card);
  ui_set_app_stylesheet(NULL, NULL);
  test_env_shutdown();
  PASS();
}
#endif

static void test_box_cache_pressure(void) {
  TEST("painted scopes keep instance and class overrides after box cache eviction");
  test_env_init();
  ui_set_instance_stylesheet(71, "Button.pressure { color: #123456; background: #123456; border: 7px solid #123456; margin: 0; }"
                                "Button#paint0 { color: #654321; }", "pressure.css");
  window_t windows[160] = {0};
  for (int index = 0; index < 160; index++) {
    windows[index].hinstance = 71;
    snprintf(windows[index].style_id, sizeof(windows[index].style_id), "paint%d", index);
    snprintf(windows[index].style_class, sizeof(windows[index].style_class), "pressure");
    ASSERT_EQUAL(theme_foreground_in(&windows[index], THEME_PART_BUTTON, CTRL_NORMAL),
                 index ? 0xFF563412u : 0xFF214365u);
    ASSERT_EQUAL(theme_content_insets_in(&windows[index], THEME_PART_BUTTON, CTRL_NORMAL).x, 7);
  #ifdef APP_STYLESHEET_TEST_STANDALONE
    ASSERT_EQUAL(box_for(THEME_PART_BUTTON, CTRL_NORMAL, ui_window_scope(&windows[index]), NULL)->top, 0xFF563412u);
  #endif
  }
  for (int index = 0; index < 160; index++)
    ASSERT_EQUAL(theme_foreground_in(&windows[index], THEME_PART_BUTTON, CTRL_NORMAL),
                 index ? 0xFF563412u : 0xFF214365u);
  ui_set_instance_stylesheet(71, NULL, NULL);
  test_env_shutdown();
  PASS();
}

static void test_selector_cache_pressure(void) {
  TEST("scopes, selector attribution and pseudo-classes survive registry pressure and churn");
  test_env_init();
  ui_set_instance_stylesheet(71, "Button.pressure { min-height: 37px; color: #123456; }"
                                "Button.pressure:hover { min-height: 41px; }"
                                "Button#scope0 { min-height: 43px; }", "first.css");
  ui_set_instance_stylesheet(72, "Button.pressure { min-height: 53px; color: #654321; }"
                                "Button.pressure:hover { min-height: 59px; }"
                                "Button#scope0 { min-height: 61px; }", "second.css");
  enum { SCOPE_COUNT = 600, SELECTOR_COUNT = 8 };
  const char *selectors[SCOPE_COUNT][SELECTOR_COUNT], *hover[SCOPE_COUNT][SELECTOR_COUNT];
  const ui_scope_t *scopes[SCOPE_COUNT];
  window_t window = {0};
  snprintf(window.style_class, sizeof(window.style_class), "pressure");
  for (int index = 0; index < SCOPE_COUNT; index++) {
    window.hinstance = index % 2 ? 72 : 71;
    snprintf(window.style_id, sizeof(window.style_id), "scope%d", index / 2);
    window.style_scope = NULL;
    scopes[index] = ui_window_scope(&window);
    ASSERT_NOT_NULL(scopes[index]);
    for (int selector = 0; selector < SELECTOR_COUNT; selector++) {
      char base[64];
      snprintf(base, sizeof(base), "Button.variant%d", selector);
      selectors[index][selector] = ui_selector(&window, base);
      hover[index][selector] = ui_state_selector(selectors[index][selector], CTRL_HOVER);
      ASSERT_EQUAL(ui_px(selectors[index][selector], "min-height"), index < 2 ? (index ? 61 : 43) : (index % 2 ? 53 : 37));
      ASSERT_EQUAL(ui_px(hover[index][selector], "min-height"), index < 2 ? (index ? 61 : 43) : (index % 2 ? 59 : 41));
    }
  }
  for (int index = 0; index < SCOPE_COUNT; index++) {
    window.hinstance = index % 2 ? 72 : 71;
    snprintf(window.style_id, sizeof(window.style_id), "scope%d", index / 2);
    window.style_scope = NULL;
    ASSERT_TRUE(ui_window_scope(&window) == scopes[index]);
    ASSERT_EQUAL(theme_foreground_in(&window, THEME_PART_BUTTON, CTRL_NORMAL), index % 2 ? 0xFF214365u : 0xFF563412u);
    for (int selector = 0; selector < SELECTOR_COUNT; selector++) {
      char base[64];
      snprintf(base, sizeof(base), "Button.variant%d", selector);
      ASSERT_TRUE(ui_selector(&window, base) == selectors[index][selector]);
      ASSERT_TRUE(ui_state_selector(selectors[index][selector], CTRL_HOVER) == hover[index][selector]);
      ASSERT_EQUAL(ui_px(selectors[index][selector], "min-height"), index < 2 ? (index ? 61 : 43) : (index % 2 ? 53 : 37));
      ASSERT_EQUAL(ui_px(hover[index][selector], "min-height"), index < 2 ? (index ? 61 : 43) : (index % 2 ? 59 : 41));
    }
  }
  const char *many[40];
  for (int index = 0; index < 40; index++) {
    char base[64];
    snprintf(base, sizeof(base), "Button.many%d", index);
    many[index] = ui_selector(&window, base);
    ASSERT_EQUAL(ui_px(many[index], "min-height"), 53);
  }
  ui_set_instance_stylesheet(72, "Button.pressure { min-height: 67px; color: #ABCDEF; }", "replacement.css");
  ASSERT_EQUAL(ui_px(selectors[599][0], "min-height"), 67);
  ASSERT_EQUAL(ui_px(hover[599][0], "min-height"), 67);
  ASSERT_EQUAL(ui_px(many[0], "min-height"), 67);
  ASSERT_EQUAL(theme_foreground_in(&window, THEME_PART_BUTTON, CTRL_NORMAL), 0xFFEFCDABu);
  ASSERT_EQUAL(ui_px(selectors[598][0], "min-height"), 37);
  ui_set_instance_stylesheet(71, NULL, NULL);
  ui_set_instance_stylesheet(72, NULL, NULL);
  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("App style sheets");
#ifndef APP_STYLESHEET_TEST_STANDALONE
  test_instance_variables();
  test_selector_composition();
  test_id_and_class_rules();
  test_instance_sheet();
  test_window_set_style();
  test_controls_read_their_selectors();
#endif
  test_box_cache_pressure();
  test_selector_cache_pressure();
  TEST_END();
}
