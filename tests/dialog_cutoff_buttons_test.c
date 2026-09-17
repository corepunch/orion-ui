// Test for dialog button cutoff issue - grid expanding too much in vertical stack
// This reproduces the filter gallery layout where buttons are cut off at the bottom.
//
// Root cause: When a grid with WINDOW_VSCROLL children is measured, it was claiming
// ALL available height by expanding flex rows during measurement. This starved
// sibling elements (like button stacks) in the parent stack layout.
//
// Fix: Grid measurement now returns minimum height needed (before flex expansion).
// Flex expansion only happens during arrange, not measure.

#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/commctl/commctl.h>

// Minimal reportview for testing
static result_t test_reportview_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      // Simulate a reportview with lots of content (20 items * 20px = 400px)
      return true;
    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (m) {
        // Request minimum height (header + one entry), not full content
        m->desired_w = MAX(m->desired_w, 200);
        m->desired_h = MAX(m->desired_h, 40);  // Small minimum
      }
      return true;
    }
    case evPaint:
      return false;
    default:
      return false;
  }
}

static result_t test_panel_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)wparam; (void)lparam;
  return msg == evCreate || msg == evPaint || msg == evDestroy;
}

// Test that buttons aren't cut off when grid has scrollable content
void test_dialog_grid_buttons_not_cutoff(void) {
  TEST("dialog: buttons stack not cut off when grid has scrollable reportview");
  
  test_env_init();
  
  // Create a dialog form similar to filter_gallery:
  // - Dialog 560x360 with 8px padding
  // - Vertical stack with:
  //   - Grid with two columns (preview and reportview)
  //   - Horizontal stack with buttons at bottom
  
  // The client area after padding: 560-16 = 544 width, 360-16 = 344 height
  // Title bar: ~20px
  // So actual content area: ~324px height
  
  window_t *dialog = create_window("Test Dialog",
    WINDOW_DIALOG | WINDOW_NOTRAYBUTTON,
    MAKERECT(0, 0, 560, 360),
    NULL, test_panel_proc, 0, NULL);
  ASSERT_NOT_NULL(dialog);
  
  dialog->flags |= WINDOW_AUTO_LAYOUT;
  dialog->flags &= ~WINDOW_STACK_HORIZONTAL;
  dialog->layout.layout_spacing = 4;
  dialog->layout.layout_padding = (irect16_t){8, 8, 8, 8};
  
  // Create grid container (two columns)
  window_t *grid = create_window("",
    WINDOW_NOTITLE | WINDOW_NOFILL,
    MAKERECT(0, 0, 1, 1),
    dialog, "gridview", 0, NULL);
  ASSERT_NOT_NULL(grid);
  grid->layout.layout_spacing = 8;
  
  // Column 1: preview area
  window_t *col1 = create_window("",
    WINDOW_NOTITLE | WINDOW_NOFILL,
    MAKERECT(0, 0, 1, 1),
    grid, "column", 0, NULL);
  ASSERT_NOT_NULL(col1);
  
  window_t *preview = create_window("Preview",
    WINDOW_NOTITLE | WINDOW_NOFILL,
    MAKERECT(0, 0, 200, 200),
    col1, test_panel_proc, 0, NULL);
  ASSERT_NOT_NULL(preview);
  preview->layout.layout_fixed_w = 200;
  preview->layout.layout_fixed_h = 200;
  
  // Column 2: reportview with scrollbar
  window_t *col2 = create_window("",
    WINDOW_NOTITLE | WINDOW_NOFILL,
    MAKERECT(0, 0, 1, 1),
    grid, "column", 0, NULL);
  ASSERT_NOT_NULL(col2);
  
  window_t *reportview = create_window("",
    WINDOW_NOTITLE | WINDOW_VSCROLL,
    MAKERECT(0, 0, 1, 1),
    col2, test_reportview_proc, 0, NULL);
  ASSERT_NOT_NULL(reportview);
  
  // Button stack at bottom
  window_t *btn_stack = create_window("",
    WINDOW_NOTITLE | WINDOW_NOFILL,
    MAKERECT(0, 0, 1, 1),
    dialog, "stackview", 0, NULL);
  ASSERT_NOT_NULL(btn_stack);
  btn_stack->flags |= WINDOW_STACK_HORIZONTAL;
  btn_stack->layout.layout_spacing = 6;
  
  window_t *ok_btn = create_window("OK",
    BUTTON_DEFAULT,
    MAKERECT(0, 0, 60, 19),
    btn_stack, win_button, 0, NULL);
  ASSERT_NOT_NULL(ok_btn);
  ok_btn->layout.layout_fixed_w = 60;
  ok_btn->layout.layout_fixed_h = 19;
  
  window_t *cancel_btn = create_window("Cancel",
    0,
    MAKERECT(0, 0, 60, 19),
    btn_stack, win_button, 0, NULL);
  ASSERT_NOT_NULL(cancel_btn);
  cancel_btn->layout.layout_fixed_w = 60;
  cancel_btn->layout.layout_fixed_h = 19;
  
  // Sync layout
  window_layout_sync(dialog);
  
  // Get the button stack's final position and check it's visible in the dialog
  irect16_t dialog_client = get_client_rect(dialog);
  
  // Buttons should be at the bottom but still within the client rect
  int btn_stack_bottom = btn_stack->frame.y + btn_stack->frame.h;
  int dialog_bottom = dialog_client.y + dialog_client.h;
  
  fprintf(stderr, "Dialog client rect: y=%d h=%d (bottom=%d)\n",
          dialog_client.y, dialog_client.h, dialog_bottom);
  fprintf(stderr, "Grid rect: y=%d h=%d (bottom=%d)\n",
          grid->frame.y, grid->frame.h, grid->frame.y + grid->frame.h);
  fprintf(stderr, "Button stack rect: y=%d h=%d (bottom=%d)\n",
          btn_stack->frame.y, btn_stack->frame.h, btn_stack_bottom);
  fprintf(stderr, "OK button rect: y=%d h=%d\n",
          ok_btn->frame.y, ok_btn->frame.h);
  
  // The button stack bottom should be <= dialog bottom (within client area)
  if (btn_stack_bottom > dialog_bottom) {
    fprintf(stderr, "FAIL: Button stack bottom (%d) extends beyond dialog bottom (%d)\n",
            btn_stack_bottom, dialog_bottom);
  }
  ASSERT_TRUE(btn_stack_bottom <= dialog_bottom);
  
  // Buttons should be at least partially visible (not completely cut off)
  ASSERT_TRUE(btn_stack->frame.y < dialog_bottom);
  
  // Button stack should have reasonable height (buttons are 19px)
  ASSERT_TRUE(btn_stack->frame.h >= 19);
  
  destroy_window(dialog);
  test_env_shutdown();
  PASS();
}

static result_t intrinsic_cell_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  if (msg == evMeasure) {
    layout_measure_t *m = lparam;
    m->desired_w = 61;
    m->desired_h = m->avail_w < 61 ? 28 : 14;
    return true;
  }
  return msg == evCreate || msg == evPaint || msg == evDestroy;
}

static void test_content_column_width(void) {
  TEST("grid: content column fits label and leaves remaining width for inputs");
  test_env_init();
  window_t *grid = create_window("", WINDOW_NOTITLE, MAKERECT(0, 0, 240, 80),
                                 NULL, win_grid, 0, NULL);
  window_t *labels = create_window("", 0, MAKERECT(0, 0, -1, 0), grid, win_column, 0, NULL);
  window_t *inputs = create_window("", 0, MAKERECT(0, 0, 0, 0), grid, win_column, 0, NULL);
  window_t *label = create_window("Height:", 0, MAKERECT(0, 0, 0, 0), labels, intrinsic_cell_proc, 0, NULL);
  int gap = 4;
  grid->layout.layout_spacing = gap;
  window_layout_sync(grid);
  irect16_t client = get_client_rect(grid);
  fprintf(stderr, "grid frame=%dx%d client=%dx%d labels=%d,%d %dx%d inputs=%d,%d %dx%d label=%dx%d\n",
          grid->frame.w, grid->frame.h, client.w, client.h,
          labels->frame.x, labels->frame.y, labels->frame.w, labels->frame.h,
          inputs->frame.x, inputs->frame.y, inputs->frame.w, inputs->frame.h,
          label->frame.w, label->frame.h);
  ASSERT_EQUAL(labels->frame.w, 61);
  ASSERT_EQUAL(label->frame.w, 61);
  ASSERT_EQUAL(label->frame.h, 14);
  ASSERT_EQUAL(inputs->frame.x, labels->frame.x + labels->frame.w + gap);
  ASSERT_EQUAL(inputs->frame.w, 175);
  resize_window(grid, 300, 80);
  window_layout_sync(grid);
  ASSERT_EQUAL(labels->frame.w, 61);
  ASSERT_EQUAL(inputs->frame.x, labels->frame.x + labels->frame.w + gap);
  ASSERT_EQUAL(inputs->frame.w, 235);
  destroy_window(grid);
  test_env_shutdown();
  PASS();
}

int main(int argc, char *argv[]) {
  (void)argc; (void)argv;
  TEST_START("dialog layout: buttons not cut off by grid");
  test_dialog_grid_buttons_not_cutoff();
  test_content_column_width();
  TEST_END();
}
