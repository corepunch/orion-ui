// SwatchGrid: a palette as rows of swatches, one selected, paged when it does not fit.

#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/commctl/commctl.h>

enum { ID_GRID = 77 };
static int g_changes;

static result_t host_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)lparam;
  if (msg == evCommand && wparam == MAKEDWORD(ID_GRID, swnSelChange)) { g_changes++; return true; }
  return false;
}

static void click_at(int x, int y) {
  ui_event_t ev = {.message = kEventLeftButtonDown, .x = x * UI_WINDOW_SCALE, .y = y * UI_WINDOW_SCALE};
  dispatch_message(&ev);
  ev.message = kEventLeftButtonUp;
  dispatch_message(&ev);
}

static window_t *make_grid(window_t **host, int w, int h) {
  *host = create_window("host", WINDOW_NOTITLE | WINDOW_NORESIZE, MAKERECT(0, 0, 400, 120), NULL, host_proc, 0, NULL);
  show_window(*host, true);
  window_t *grid = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL, MAKERECT(10, 10, w, h), *host, "SwatchGrid", 0, NULL);
  if (grid) grid->id = ID_GRID;
  return grid;
}

static void test_measure_and_select(void) {
  TEST("the grid fills its height with rows, measures the columns its colours need, and selects on a click");
  test_env_init();
  register_commctl_classes();
  window_t *host, *grid = make_grid(&host, 300, 60);
  ASSERT_NOT_NULL(grid);
  uint32_t colors[32];
  for (int i = 0; i < 32; i++) colors[i] = 0xFF000000u | (uint32_t)(i * 8);
  ASSERT_TRUE(send_message(grid, swSetColors, 32, colors));
  ASSERT_EQUAL(send_message(grid, swGetSel, 0, NULL), -1);
  int cell = ui_px("SwatchGrid::swatch", "width"), gap = ui_px("SwatchGrid", "gap");
  layout_measure_t one = {.avail_h = cell}, two = {.avail_h = 2 * cell + gap};
  send_message(grid, evMeasure, 0, &one);
  send_message(grid, evMeasure, 0, &two);
  int limit = ui_px("SwatchGrid", "--max-columns");
  ASSERT_EQUAL(two.desired_w, 16 * (cell + gap) - gap);
  ASSERT(one.desired_w >= limit * (cell + gap) - gap && one.desired_h == cell, "one row stops at the sheet's column limit and pages the rest");
  ASSERT_EQUAL(two.desired_h, 2 * cell + gap);

  g_changes = 0;
  click_at(10 + cell + gap + 2, 10 + (60 - (4 * (cell + gap) - gap)) / 2 + 2);   // second swatch of the first row
  ASSERT(g_changes == 1 && send_message(grid, swGetSel, 0, NULL) == 1, "a click selects the swatch and tells the parent");
  ASSERT_TRUE(send_message(grid, swSetSel, 5, NULL));
  ASSERT(g_changes == 1 && send_message(grid, swGetSel, 0, NULL) == 5, "selecting in code is silent");
  ASSERT(!send_message(grid, swSetSel, 32, NULL) && send_message(grid, swGetSel, 0, NULL) == 5, "an index past the palette is rejected");
  ASSERT_TRUE(send_message(grid, swSetColors, 3, colors));
  ASSERT_EQUAL(send_message(grid, swGetSel, 0, NULL), -1);   // the selected colour is gone
  enable_window(grid, false);
  click_at(12, 10 + (60 - (4 * (cell + gap) - gap)) / 2 + 2);
  ASSERT(g_changes == 1 && send_message(grid, swGetSel, 0, NULL) == -1, "a disabled grid selects nothing");
  destroy_window(host);
  test_env_shutdown();
  PASS();
}

static void test_paging(void) {
  TEST("a palette that does not fit pages: the pager and a selection in code turn the page");
  test_env_init();
  register_commctl_classes();
  int cell = ui_px("SwatchGrid::swatch", "width"), gap = ui_px("SwatchGrid", "gap");
  int w = 4 * (cell + gap) - gap + gap + ui_px("SwatchGrid::pager", "width");   // four columns and the pager
  window_t *host, *grid = make_grid(&host, w, 2 * cell + gap);
  uint32_t colors[20];
  for (int i = 0; i < 20; i++) colors[i] = 0xFF000000u | (uint32_t)(i * 12);
  send_message(grid, swSetColors, 20, colors);
  layout_measure_t m = {.avail_w = w, .avail_h = 2 * cell + gap};
  send_message(grid, evMeasure, 0, &m);
  ASSERT_EQUAL(m.desired_w, w);
  g_changes = 0;
  click_at(12, 12);
  ASSERT_EQUAL(send_message(grid, swGetSel, 0, NULL), 0);
  click_at(10 + w - 3, 10 + 2 * cell + gap - 3);   // the pager's lower half: next page
  click_at(12, 12);
  ASSERT(send_message(grid, swGetSel, 0, NULL) == 8 && g_changes == 2, "the first swatch of the second page is the ninth colour");
  send_message(grid, swSetSel, 17, NULL);          // on the third page
  click_at(12, 12);
  ASSERT_EQUAL(send_message(grid, swGetSel, 0, NULL), 16);
  click_at(10 + w - 3, 12);                        // the pager's upper half: back a page
  click_at(12, 12);
  ASSERT_EQUAL(send_message(grid, swGetSel, 0, NULL), 8);
  destroy_window(host);
  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("SwatchGrid");
  test_measure_and_select();
  test_paging();
  TEST_END();
}
