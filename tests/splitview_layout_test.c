#include <orion/ui.h>
#include "test_env.h"
#include "test_framework.h"

static result_t nop_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)msg; (void)wparam; (void)lparam;
  return false;
}

static window_t *find_splitter(window_t *win) {
  // NOTE: Must compare by class name, not by proc pointer.
  // On Windows (MinGW DLLs), a function pointer obtained from outside the DLL
  // points to the import stub, while the same pointer inside the DLL points to
  // the real function.  Direct child->proc == win_splitter comparison would
  // fail.  Using find_window_class_desc_by_proc() avoids this because both the
  // stored registry pointer and the child->proc are the real function address.
  for (window_t *child = win ? win->children : NULL; child; child = child->next) {
    const fe_component_desc_t *desc = find_window_class_desc_by_proc(child->proc);
    if (desc && strcmp(desc->class_name, "Splitter") == 0)
      return child;
  }
  return NULL;
}

static void dispatch_mouse(int x, int y, uint32_t message) {
  ui_event_t event = { .message = message,
                       .x = (uint16_t)(x * UI_WINDOW_SCALE),
                       .y = (uint16_t)(y * UI_WINDOW_SCALE) };
  dispatch_message(&event);
}

static void test_form_splitview_uses_arranged_frame(void) {
  TEST("SplitView: form layout assigns a non-zero frame and arranges both panes");
  test_env_init();
  static const form_ctrl_def_t panes[] = {
    { .class_name = "Label", .id = 2, .text = "left" },
    { .class_name = "Label", .id = 3, .text = "right" },
  };
  static const form_ctrl_def_t children[] = {
    { .class_name = "SplitView", .id = 1, .flags = WINDOW_FLEXSPACE,
      .children = panes, .child_count = 2, .lparam = (void *)SPLIT_VERT },
  };
  static const form_def_t def = {
    .name = "split", .width = 300, .height = 120, .flags = WINDOW_AUTO_LAYOUT,
    .children = children, .child_count = 1,
  };

  window_t *root = create_window_from_form(&def, 0, 0, NULL, nop_proc, 0, NULL);
  window_t *split = get_window_item(root, 1);
  window_t *left = splitview_get_left(split), *right = splitview_get_right(split);
  ASSERT_NOT_NULL(root); ASSERT_NOT_NULL(split); ASSERT_NOT_NULL(left); ASSERT_NOT_NULL(right);
  ASSERT_TRUE(split->frame.w > 0); ASSERT_TRUE(split->frame.h > 0);
  ASSERT_TRUE(left->frame.w > 0); ASSERT_TRUE(right->frame.w > 0);
  ASSERT_EQUAL(left->frame.y, right->frame.y);
  ASSERT_TRUE(right->frame.x > left->frame.x);
  ASSERT_EQUAL(right->frame.x - left->frame.x - left->frame.w, get_theme()->splitter.spacing);

  destroy_window(root);
  test_env_shutdown();
  PASS();
}

static void test_form_splitview_honors_horizontal_divider(void) {
  TEST("SplitView: form lparam selects top/bottom panes");
  test_env_init();
  static const form_ctrl_def_t panes[] = {
    { .class_name = "Label", .id = 12, .text = "top" },
    { .class_name = "Label", .id = 13, .text = "bottom" },
  };
  static const form_ctrl_def_t children[] = {
    { .class_name = "SplitView", .id = 11, .flags = WINDOW_FLEXSPACE,
      .children = panes, .child_count = 2, .lparam = (void *)SPLIT_HORZ },
  };
  static const form_def_t def = {
    .name = "split", .width = 300, .height = 120, .flags = WINDOW_AUTO_LAYOUT,
    .children = children, .child_count = 1,
  };

  window_t *root = create_window_from_form(&def, 0, 0, NULL, nop_proc, 0, NULL);
  window_t *split = get_window_item(root, 11);
  window_t *top = splitview_get_left(split), *bottom = splitview_get_right(split);
  ASSERT_NOT_NULL(root); ASSERT_NOT_NULL(split); ASSERT_NOT_NULL(top); ASSERT_NOT_NULL(bottom);
  ASSERT_TRUE(top->frame.h > 0); ASSERT_TRUE(bottom->frame.h > 0);
  ASSERT_EQUAL(top->frame.x, bottom->frame.x);
  ASSERT_TRUE(bottom->frame.y > top->frame.y);
  ASSERT_EQUAL(bottom->frame.y - top->frame.y - top->frame.h, get_theme()->splitter.spacing);

  destroy_window(root);
  test_env_shutdown();
  PASS();
}

static void test_nested_splitter_hit_hover_and_drag(void) {
  TEST("SplitView: nested divider owns hover and drags in parent-local coordinates");
  test_env_init();
  static const form_ctrl_def_t inner_panes[] = {
    { .class_name = "Label", .id = 23, .text = "middle" },
    { .class_name = "Label", .id = 24, .text = "right" },
  };
  static const form_ctrl_def_t outer_panes[] = {
    { .class_name = "Label", .id = 21, .text = "left" },
    { .class_name = "SplitView", .id = 22, .flags = WINDOW_FLEXSPACE,
      .children = inner_panes, .child_count = 2, .lparam = (void *)SPLIT_VERT },
  };
  static const form_ctrl_def_t children[] = {
    { .class_name = "SplitView", .id = 20, .flags = WINDOW_FLEXSPACE,
      .children = outer_panes, .child_count = 2, .lparam = (void *)SPLIT_VERT },
  };
  static const form_def_t def = {
    .name = "nested split", .width = 600, .height = 160,
    .flags = WINDOW_AUTO_LAYOUT | WINDOW_NOTITLE,
    .children = children, .child_count = 1,
  };

  window_t *root = create_window_from_form(&def, 40, 30, NULL, nop_proc, 0, NULL);
  window_t *inner = get_window_item(root, 22), *splitter = find_splitter(inner);
  ASSERT_NOT_NULL(root); ASSERT_NOT_NULL(inner); ASSERT_NOT_NULL(splitter);
  splitview_get_left(inner)->layout.layout_margin = R(2, 2, 2, 2);
  splitview_get_right(inner)->layout.layout_margin = R(2, 2, 2, 2);
  send_message(inner, svSetPaneMargin, 2, NULL);
  send_message(inner, evResize, 0, NULL);
  int sx = window_screen_x(splitter) + splitter->frame.w / 2;
  int sy = window_screen_y(splitter) + splitter->frame.h / 2;
  ASSERT(find_window(sx, sy) == splitter, "nested splitter was not the hover target");

  dispatch_mouse(sx, sy, kEventMouseMoved);
  ASSERT(g_ui_runtime.tracked == splitter, "nested splitter did not retain mouse tracking");
  int old_left_w = splitview_get_left(inner)->frame.w;
  dispatch_mouse(sx, sy, kEventLeftButtonDown);
  ASSERT(g_ui_runtime.captured == inner, "nested splitview did not capture its drag");
  dispatch_mouse(sx + 30, sy, kEventLeftButtonDragged);
  dispatch_mouse(sx + 30, sy, kEventLeftButtonUp);
  ASSERT(splitview_get_left(inner)->frame.w > old_left_w, "nested divider did not move right");
  ASSERT_NULL(g_ui_runtime.captured);

  destroy_window(root);
  test_env_shutdown();
  PASS();
}

static void test_splitter_margin_hotspot(void) {
  TEST("Splitter hotspot covers both margins and theme spacing, with no extra pixels");
  test_env_init();
  theme_style_t saved_theme = get_theme()->style;
  theme_style_t styles[] = {THEME_MODERN, THEME_CLASSIC};
  for (int theme = 0; theme < ARRAY_LEN(styles); theme++) {
    set_theme(styles[theme]);
    for (int orientation = SPLIT_VERT; orientation <= SPLIT_HORZ; orientation++) {
      window_t *root = create_window("", WINDOW_NOTITLE, MAKERECT(40, 30, 300, 200), NULL, nop_proc, 0, NULL);
      window_t *split = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL, MAKERECT(10, 10, 260, 160), root,
                                      "SplitView", 0, (void *)(intptr_t)orientation);
      window_t *left = create_window("", WINDOW_NOTITLE, MAKERECT(0, 0, 1, 1), split, "Label", 0, NULL);
      window_t *right = create_window("", WINDOW_NOTITLE, MAKERECT(0, 0, 1, 1), split, "Label", 0, NULL);
      left->layout.layout_margin = right->layout.layout_margin = R(2, 2, 2, 2);
      send_message(split, svSetPaneMargin, 2, NULL);
      send_message(split, evResize, 0, NULL);
      window_t *divider = find_splitter(split);
      ASSERT_EQUAL(orientation == SPLIT_VERT ? divider->frame.w : divider->frame.h, 4 + get_theme()->splitter.spacing);
      for (int side = 0; side < 2; side++) {
        ipoint16_t local = {divider->frame.w / 2, divider->frame.h / 2};
        if (orientation == SPLIT_VERT) local.x = side ? divider->frame.w - 1 : 0;
        else local.y = side ? divider->frame.h - 1 : 0;
        ipoint16_t p = window_map_point(divider, NULL, local);
        ipoint16_t outside = p;
        if (orientation == SPLIT_VERT) outside.x += side ? 1 : -1;
        else outside.y += side ? 1 : -1;
        ASSERT_TRUE(find_window(outside.x, outside.y) != divider);
        ASSERT_EQUAL(find_window(p.x, p.y), divider);
        dispatch_mouse(p.x, p.y, kEventMouseMoved);
        ASSERT_EQUAL(g_ui_runtime.tracked, divider);
        dispatch_mouse(outside.x, outside.y, kEventMouseMoved);
        ASSERT_TRUE(g_ui_runtime.tracked != divider);
        dispatch_mouse(p.x, p.y, kEventMouseMoved);
        ASSERT_EQUAL(g_ui_runtime.tracked, divider);
        ASSERT_EQUAL(send_message(divider, evGetCursor, 0, NULL), orientation == SPLIT_VERT ? curResizeH : curResizeV);
        int before = orientation == SPLIT_VERT ? splitview_get_left(split)->frame.w : splitview_get_left(split)->frame.h;
        dispatch_mouse(p.x, p.y, kEventLeftButtonDown);
        ASSERT_EQUAL(g_ui_runtime.captured, split);
        p.x += orientation == SPLIT_VERT ? 10 : 0;
        p.y += orientation == SPLIT_HORZ ? 10 : 0;
        dispatch_mouse(p.x, p.y, kEventLeftButtonDragged);
        dispatch_mouse(p.x, p.y, kEventLeftButtonUp);
        int after = orientation == SPLIT_VERT ? splitview_get_left(split)->frame.w : splitview_get_left(split)->frame.h;
        ASSERT_TRUE(after >= before + 9 && after <= before + 11);
        ASSERT_NULL(g_ui_runtime.captured);
      }
      ipoint16_t p = window_map_point(divider, NULL, (ipoint16_t){-2, -2});
      ASSERT_TRUE(find_window(p.x, p.y) != divider);
      destroy_window(root);
    }
  }
  set_theme(saved_theme);
  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("splitview layout");
  test_form_splitview_uses_arranged_frame();
  test_form_splitview_honors_horizontal_divider();
  test_nested_splitter_hit_hover_and_drag();
  test_splitter_margin_hotspot();
  TEST_END();
}
