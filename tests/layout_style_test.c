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

static const form_ctrl_def_t k_items[] = {
  { .class_name = "Label", .id = 4, .size = {30, 12}, .text = "First" },
  { .class_name = "Label", .id = 5, .size = {40, 16}, .text = "Second" },
};

static const form_ctrl_def_t k_stacks[] = {
  // Declares nothing: the sheet decides. Its sheet bits are what a form without attributes compiles to.
  { .class_name = "StackView", .id = 1, .name = "plain", .layout_spacing = 4, .layout_sheet = LAYOUT_SHEET_ALL,
    .children = k_items, .child_count = 2 },
  // Declares spacing and padding, leaves orientation to the sheet.
  { .class_name = "StackView", .id = 2, .name = "declared", .layout_spacing = 3, .padding = {1, 2, 1, 2},
    .layout_sheet = LAYOUT_SHEET_ORIENT },
  // Hand-written definition: the sheet never applies.
  { .class_name = "StackView", .id = 3, .name = "code", .layout_spacing = 5 },
};

static window_t *make_form(hinstance_t hinstance, window_t *parent) {
  form_def_t def = { .name = "Layout", .width = 300, .height = 200, .flags = WINDOW_AUTO_LAYOUT,
                     .children = k_stacks, .child_count = 3 };
  return create_window_from_form(&def, 0, 0, parent, form_proc, hinstance, NULL);
}

static void test_sheet_values(void) {
  TEST("gap, padding and flex-direction reach a container; declared values win");
  test_env_init();
  ui_set_app_stylesheet("StackView#plain { gap: 6px; padding: 10px 14px 8px 2px; flex-direction: row; }"
                        "StackView#declared { gap: 20px; padding: 30px; flex-direction: row; }"
                        "StackView#code { gap: 20px; padding: 30px; }", "app.css");
  window_t *form = make_form(0, NULL);
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
  window_t *form = make_form(0, NULL), *plain = get_window_item(form, 1);
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

static bool same_rect(irect16_t left, irect16_t right) {
  return left.x == right.x && left.y == right.y && left.w == right.w && left.h == right.h;
}

static bool same_layout(window_t *live, window_t *fresh) {
  if (!same_rect(live->frame, fresh->frame) ||
      live->layout.layout_spacing != fresh->layout.layout_spacing ||
      !same_rect(live->layout.layout_padding, fresh->layout.layout_padding) ||
      (live->flags & WINDOW_STACK_HORIZONTAL) != (fresh->flags & WINDOW_STACK_HORIZONTAL)) {
    fprintf(stderr, "Layout mismatch #%s: live=(%d,%d %dx%d) fresh=(%d,%d %dx%d), gap=%d/%d\n",
            live->style_id, live->frame.x, live->frame.y, live->frame.w, live->frame.h,
            fresh->frame.x, fresh->frame.y, fresh->frame.w, fresh->frame.h,
            live->layout.layout_spacing, fresh->layout.layout_spacing);
    return false;
  }
  window_t *child = live->children, *reference = fresh->children;
  for (; child && reference; child = child->next, reference = reference->next)
    if (!same_layout(child, reference)) return false;
  return !child && !reference;
}

static void replace_live_sheet(hinstance_t hinstance, const char *css) {
  g_ui_runtime.running = true;
  ui_set_instance_stylesheet(hinstance, css, "live.css");
  g_ui_runtime.running = false;
}

static void test_live_replacement(hinstance_t hinstance) {
  TEST(hinstance ? "live instance replacement and removal match fresh layout, leaving other instances alone"
                 : "live app replacement and removal match fresh layout without resetting palette or capture");
  test_env_init();
  ui_set_instance_stylesheet(hinstance,
    "StackView#plain { gap: 6px; padding: 2px; flex-direction: row; color: #123456; }", "live.css");
  window_t *form = make_form(hinstance, NULL), *other = make_form(8, NULL);
  window_t *plain = get_window_item(form, 1), *other_plain = get_window_item(other, 1);
  irect16_t other_frame = other_plain->children->frame;
  int other_gap = other_plain->layout.layout_spacing;
  theme_t *theme = get_theme();
  uint32_t saved_color = get_sys_color(brAccent), custom_color = WEB(0x456789);
  int color_role = brAccent;
  set_sys_colors(1, &color_role, &custom_color);
  g_ui_runtime.captured = plain;
  test_env_enable_tracking(true);
  register_window_hook(evThemeChanged, test_env_hook_callback, NULL);
  register_window_hook(evResize, test_env_hook_callback, NULL);
  register_window_hook(evArrange, test_env_hook_callback, NULL);
  const char *replacements[] = {
    "StackView#plain { gap: 13px; padding: 4px 6px 8px 10px; flex-direction: column; color: #654321; }",
    "StackView#plain { gap: 9px; padding: 3px; flex-direction: row; color: #abcdef; }",
    NULL,
  };
  for (int step = 0; step < 3; step++) {
    reset_message_queue();
    test_env_clear_events();
    int old_gap = plain->layout.layout_spacing;
    replace_live_sheet(hinstance, replacements[step]);
    ASSERT_EQUAL(plain->layout.layout_spacing, old_gap);
    ASSERT_EQUAL(test_env_get_event_count(), 0);
    ASSERT_TRUE(get_theme() == theme);
    ASSERT_EQUAL(get_sys_color(brAccent), custom_color);
    ASSERT_TRUE(g_ui_runtime.captured == plain);
    repost_messages();
    ASSERT_TRUE(test_env_was_message_sent(evThemeChanged));
    ASSERT_TRUE(test_env_was_message_sent(evResize));
    ASSERT_TRUE(test_env_was_message_sent(evPaint));
    bool arranged = false;
    for (int event = 0; event < test_env_get_event_count(); event++) {
      test_event_t *item = test_env_get_event(event);
      if (item->window != plain || item->msg != evArrange) continue;
      ASSERT_EQUAL(item->call_count, 1);
      arranged = true;
    }
    ASSERT_TRUE(arranged);
    if (hinstance) {
      ASSERT_EQUAL(other_plain->layout.layout_spacing, other_gap);
      ASSERT_TRUE(same_rect(other_plain->children->frame, other_frame));
      for (int event = 0; event < test_env_get_event_count(); event++)
        ASSERT_TRUE(test_env_get_event(event)->window->hinstance != 8);
    }
    window_t *fresh = make_form(hinstance, NULL);
    ASSERT_TRUE(same_layout(plain, get_window_item(fresh, 1)));
    ASSERT_EQUAL(theme_foreground_in(plain, THEME_PART_STACK_VIEW, CTRL_NORMAL),
                 theme_foreground_in(get_window_item(fresh, 1), THEME_PART_STACK_VIEW, CTRL_NORMAL));
    ASSERT_EQUAL(get_window_item(form, 2)->layout.layout_spacing, 3);
    ASSERT_EQUAL(get_window_item(form, 3)->layout.layout_spacing, 5);
    destroy_window(fresh);
  }
  ASSERT_EQUAL(plain->layout.layout_spacing, 4);
  ASSERT_FALSE(plain->flags & WINDOW_STACK_HORIZONTAL);
  ASSERT_TRUE(same_rect(plain->layout.layout_padding, R(0, 0, 0, 0)));
  g_ui_runtime.captured = NULL;
  set_sys_colors(1, &color_role, &saved_color);
  test_env_shutdown();
  PASS();
}

static void test_instance_descendants(void) {
  TEST("instance notification refreshes nested forms and their root");
  test_env_init();
  window_t *host = make_form(7, NULL);
  window_t *host_plain = get_window_item(host, 1);
  window_t *nested = make_form(0, host);
  window_t *plain = get_window_item(nested, 1);
  ASSERT_EQUAL(plain->hinstance, 7);
  test_env_enable_tracking(true);
  register_window_hook(evThemeChanged, test_env_hook_callback, NULL);
  register_window_hook(evResize, test_env_hook_callback, NULL);
  reset_message_queue();
  test_env_clear_events();
  replace_live_sheet(7, "StackView#plain { gap: 17px; padding: 5px; flex-direction: row; }");
  repost_messages();
  ASSERT_EQUAL(plain->layout.layout_spacing, 17);
  ASSERT_TRUE(plain->flags & WINDOW_STACK_HORIZONTAL);
  ASSERT_EQUAL(host_plain->layout.layout_spacing, 17);
  bool root_resize = false;
  for (int index = 0; index < test_env_get_event_count(); index++) {
    test_event_t *event = test_env_get_event(index);
    if (event->msg == evThemeChanged) ASSERT_EQUAL(event->window->hinstance, 7);
    if (event->msg == evResize && event->window == host) root_resize = true;
  }
  ASSERT_TRUE(root_resize);
  test_env_shutdown();
  ui_set_instance_stylesheet(7, NULL, NULL);
  PASS();
}

static void test_toolbar_notification(void) {
  TEST("hidden toolbar hosts and embedded controls receive instance restyling");
  test_env_init();
  static const toolbar_item_t items[] = {
    { .type = TOOLBAR_ITEM_TEXTEDIT, .ident = 20, .w = 80 },
  };
  form_def_t def = { .name = "Toolbar", .width = 300, .height = 200,
    .flags = WINDOW_TOOLBAR | WINDOW_HIDDEN, .toolbar_items = items, .toolbar_count = 1 };
  window_t *form = create_window_from_form(&def, 0, 0, NULL, form_proc, 7, NULL);
  ASSERT_NOT_NULL(form);
  toolbar_state_t *toolbar = window_toolbar_state(form);
  ASSERT_NOT_NULL(toolbar);
  ASSERT_NOT_NULL(toolbar->children);
  test_env_enable_tracking(true);
  register_window_hook(evThemeChanged, test_env_hook_callback, NULL);
  reset_message_queue();
  test_env_clear_events();
  replace_live_sheet(7, "Toolbar { padding: 9px; }");
  ASSERT_EQUAL(test_env_get_event_count(), 0);
  repost_messages();
  bool host_notified = false, child_notified = false;
  for (int index = 0; index < test_env_get_event_count(); index++) {
    test_event_t *event = test_env_get_event(index);
    if (event->msg != evThemeChanged) continue;
    if (event->window == form->toolbar) host_notified = true;
    if (event->window == toolbar->children) child_notified = true;
    ASSERT_EQUAL(event->call_count, 1);
  }
  ASSERT_TRUE(host_notified && child_notified);
  test_env_shutdown();
  ui_set_instance_stylesheet(7, NULL, NULL);
  PASS();
}

static result_t remove_child_on_theme(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  if (msg == evThemeChanged && win->children) destroy_window(win->children);
  return false;
}

static void test_deferred_callback(void) {
  TEST("stylesheet setters defer destructive callbacks until after tree traversal");
  test_env_init();
  window_t *form = make_form(7, NULL);
  form->proc = remove_child_on_theme;
  window_t *first = form->children;
  ASSERT_NOT_NULL(first);
  reset_message_queue();
  replace_live_sheet(7, "StackView { gap: 11px; }");
  ASSERT_TRUE(form->children == first);
  repost_messages();
  ASSERT_TRUE(form->children != first);
  test_env_shutdown();
  ui_set_instance_stylesheet(7, NULL, NULL);
  PASS();
}

int main(void) {
  TEST_START("Layout properties in the style sheet");
  test_sheet_values();
  test_defaults_and_restyle();
  test_live_replacement(0);
  test_live_replacement(7);
  test_instance_descendants();
  test_toolbar_notification();
  test_deferred_callback();
  TEST_END();
}
