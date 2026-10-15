#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>

static result_t source_probe_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)wparam;
  if (msg == evSetDatabase) { win->userdata = lparam; return true; }
  return false;
}

static database_t source_a = {.name = "form_source_a"};
static database_t source_b = {.name = "form_source_b"};
static database_t source_default = {.name = "form_source_default"};

static void source_binding(bool flat) {
  TEST(flat ? "flat declarative sources override inherited database independently" :
              "nested declarative sources override inherited database independently");
  test_env_init();
  static const fe_component_desc_t probe = {.class_name = "SourceProbe", .proc = source_probe_proc};
  ASSERT_TRUE(register_window_class(&probe));
  ASSERT_TRUE(register_database("form_source_a", &source_a));
  ASSERT_TRUE(register_database("form_source_b", &source_b));
  ui_set_database(&source_default);
  form_ctrl_def_t children[] = {
    {.class_name = "SourceProbe", .id = 12, .source = "form_source_a.records"},
    {.class_name = "SourceProbe", .id = 13, .source = "form_source_b.records"},
    {.class_name = "SourceProbe", .id = 14}
  };
  form_ctrl_def_t controls[] = {
    {.class_name = "StackView", .id = 11}, children[0], children[1], children[2]
  };
  form_def_t form = {.name = "Sources", .width = 320, .height = 150,
    .flags = WINDOW_AUTO_LAYOUT, .children = controls, .child_count = flat ? 4 : 1};
  if (flat) for (int i = 1; i < 4; i++) controls[i].parent = 11;
  else { controls[0].children = children; controls[0].child_count = 3; }
  window_t *win = create_window_from_form(&form, 0, 0, NULL, source_probe_proc, 0, NULL);
  ASSERT_NOT_NULL(win);
  ASSERT_TRUE(get_window_item(win, 12)->userdata == &source_a);
  ASSERT_TRUE(get_window_item(win, 13)->userdata == &source_b);
  ASSERT_TRUE(get_window_item(win, 14)->userdata == &source_default);
  destroy_window(win); ui_set_database(NULL); test_env_shutdown(); PASS();
}

int main(void) {
  TEST_START("Declarative form database sources");
  source_binding(true); source_binding(false);
  TEST_END();
}
