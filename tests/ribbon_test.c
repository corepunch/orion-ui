// Ribbon: tabs, command state, the three-row and single-row layouts, shrinking
// groups, and the application chrome that hosts it in place of a menu bar.

#include "test_framework.h"
#include "test_env.h"
#include <orion/ui.h>
#include <orion/commctl/commctl.h>
#include <orion/user/toolbar.h>

enum { ID_NEW = 101, ID_OPEN, ID_SAVE, ID_BOLD, ID_FIND, ID_EXIT, ID_ZOOM };

static const menu_item_t kFileMenu[] = { { "Exit", ID_EXIT, NULL, 0 } };
static const ribbon_item_t kClipboard[] = {
  { RIBBON_ITEM_BUTTON, ID_NEW,  "page-plus",   "New Document", NULL, RIBBON_ITEM_LARGE },
  { RIBBON_ITEM_BUTTON, ID_OPEN, "folder",      "Open",         "Open a document" },
  { RIBBON_ITEM_BUTTON, ID_SAVE, "floppy-disk", "Save" },
  { RIBBON_ITEM_BUTTON, ID_BOLD, "text",        "Bold",         NULL, RIBBON_ITEM_CHECK },
};
static const ribbon_item_t kEditing[] = {
  { RIBBON_ITEM_BUTTON,   ID_FIND, "search", "Find", NULL, RIBBON_ITEM_LARGE },
  { RIBBON_ITEM_SEPARATOR },
  { RIBBON_ITEM_DROPDOWN, 0, "folder", "More", NULL, 0, kFileMenu, ARRAY_LEN(kFileMenu) },
};
static const ribbon_item_t kView[] = { { RIBBON_ITEM_BUTTON, ID_ZOOM, "zoom-in", "Zoom", NULL, RIBBON_ITEM_LARGE } };
static const ribbon_group_t kHomeGroups[] = { { "Clipboard", kClipboard, ARRAY_LEN(kClipboard) }, { "Editing", kEditing, ARRAY_LEN(kEditing) } };
static const ribbon_group_t kViewGroups[] = { { "Zoom", kView, ARRAY_LEN(kView) }, { "Again", kClipboard, ARRAY_LEN(kClipboard) } };
static const ribbon_tab_t kTabs[] = { { "Home", kHomeGroups, ARRAY_LEN(kHomeGroups) }, { "View", kViewGroups, ARRAY_LEN(kViewGroups) } };
#define CONTEXT_TABLE (1u << 0)
static const ribbon_tab_t kContextTabs[] = { { "Home", kHomeGroups, ARRAY_LEN(kHomeGroups) }, { "View", kViewGroups, ARRAY_LEN(kViewGroups) },
                                             { "Table", kViewGroups, ARRAY_LEN(kViewGroups), CONTEXT_TABLE } };
static const ribbon_def_t kContextRibbon = { .tabs = kContextTabs, .tab_count = ARRAY_LEN(kContextTabs) };
static const ribbon_def_t kRibbon = { .tabs = kTabs, .tab_count = ARRAY_LEN(kTabs), .menu_label = "File",
                                      .menu_items = kFileMenu, .menu_count = ARRAY_LEN(kFileMenu) };

static int g_clicks, g_last_command;

static result_t ribbon_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  if (msg == evCommand && HIWORD(wparam) == btnClicked) { g_clicks++; g_last_command = LOWORD(wparam); return true; }
  return win_ribbon(win, msg, wparam, lparam);
}

static window_t *make_ribbon(int width) {
  window_t *ribbon = create_window("ribbon", WINDOW_NOTITLE | WINDOW_NORESIZE, MAKERECT(0, 0, width, 200), NULL, ribbon_proc, 0, (void *)&kRibbon);
  if (ribbon) show_window(ribbon, true);
  return ribbon;
}

static int ribbon_height(window_t *ribbon) {
  ipoint16_t size = {0};
  send_message(ribbon, evDockMeasure, 0, &size);
  return size.y;
}

static void click_at(int x, int y) {
  ui_event_t ev = {.message = kEventLeftButtonDown, .x = x * UI_WINDOW_SCALE, .y = y * UI_WINDOW_SCALE};
  dispatch_message(&ev);
  ev.message = kEventLeftButtonUp;
  dispatch_message(&ev);
}

static void click_rect(window_t *ribbon, irect16_t r) {
  click_at(window_screen_x(ribbon) + r.x + r.w / 2, window_screen_y(ribbon) + r.y + r.h / 2);
}

static irect16_t item_rect(window_t *ribbon, int ident) {
  irect16_t r = {0};
  send_message(ribbon, rbGetItemRect, (uint32_t)ident, &r);
  return r;
}

static void test_tabs_and_state(void) {
  TEST("a ribbon shows one tab's commands; state belongs to the command wherever it is placed");
  test_env_init();
  window_t *ribbon = make_ribbon(900);
  ASSERT_NOT_NULL(ribbon);
  ASSERT_EQUAL(send_message(ribbon, rbGetActiveTab, 0, NULL), 0);
  irect16_t menu, home, view, r;
  ASSERT_TRUE(send_message(ribbon, rbGetTabRect, (uint32_t)-1, &menu) && send_message(ribbon, rbGetTabRect, 0, &home) &&
              send_message(ribbon, rbGetTabRect, 1, &view));
  ASSERT(menu.w > 0 && menu.x + menu.w <= home.x && home.x + home.w <= view.x, "the application menu precedes the tabs");
  ASSERT_FALSE(send_message(ribbon, rbGetTabRect, 2, &r));
  ASSERT_TRUE(send_message(ribbon, rbGetItemRect, ID_FIND, &r));
  ASSERT(!send_message(ribbon, rbGetItemRect, ID_ZOOM, &r), "another tab's command is not shown");
  click_rect(ribbon, view);
  ASSERT_EQUAL(send_message(ribbon, rbGetActiveTab, 0, NULL), 1);
  ASSERT_TRUE(send_message(ribbon, rbGetItemRect, ID_ZOOM, &r));
  ASSERT_FALSE(send_message(ribbon, rbSetActiveTab, 2, NULL));
  ASSERT_EQUAL(send_message(ribbon, rbGetState, ID_SAVE, NULL), 0);
  ASSERT_TRUE(send_message(ribbon, rbSetState, ID_SAVE, (void *)(uintptr_t)TBSTATE_DISABLED));
  ASSERT_TRUE(send_message(ribbon, rbSetActiveTab, 0, NULL));
  ASSERT(send_message(ribbon, rbGetState, ID_SAVE, NULL) == TBSTATE_DISABLED, "both placements of Save are disabled");
  ASSERT_EQUAL((int)send_message(ribbon, rbGetState, 999, NULL), -1);
  ASSERT_FALSE(send_message(ribbon, rbSetState, 999, (void *)(uintptr_t)TBSTATE_CHECKED));
  ASSERT_FALSE(send_message(ribbon, rbSetState, ID_SAVE, (void *)(uintptr_t)0x80));
  destroy_window(ribbon);
  test_env_shutdown();
  PASS();
}

static void test_commands(void) {
  TEST("a click sends the command to the ribbon's procedure; toggles check, disabled commands stay silent");
  test_env_init();
  window_t *ribbon = make_ribbon(900);
  g_clicks = 0; g_last_command = 0;
  click_rect(ribbon, item_rect(ribbon, ID_NEW));
  ASSERT(g_clicks == 1 && g_last_command == ID_NEW, "New fires once");
  ASSERT_NULL(g_ui_runtime.captured);
  click_rect(ribbon, item_rect(ribbon, ID_BOLD));
  ASSERT(g_clicks == 2 && send_message(ribbon, rbGetState, ID_BOLD, NULL) == TBSTATE_CHECKED, "a toggle checks itself");
  click_rect(ribbon, item_rect(ribbon, ID_BOLD));
  ASSERT(g_clicks == 3 && send_message(ribbon, rbGetState, ID_BOLD, NULL) == 0, "and unchecks");
  send_message(ribbon, rbSetState, ID_OPEN, (void *)(uintptr_t)TBSTATE_DISABLED);
  click_rect(ribbon, item_rect(ribbon, ID_OPEN));
  ASSERT(g_clicks == 3, "a disabled command does not fire");
  irect16_t open = item_rect(ribbon, ID_OPEN), save = item_rect(ribbon, ID_SAVE);
  ui_event_t ev = {.message = kEventLeftButtonDown, .x = (save.x + 2) * UI_WINDOW_SCALE, .y = (save.y + 2) * UI_WINDOW_SCALE};
  dispatch_message(&ev);
  ev.message = kEventLeftButtonUp; ev.y = (open.y + 2) * UI_WINDOW_SCALE;
  dispatch_message(&ev);
  ASSERT(g_clicks == 3, "releasing on another command cancels the press");
  char tip[256] = "";
  ASSERT(!send_message(ribbon, evGetTooltipText, MAKEDWORD(open.x + 2, open.y + 2), tip), "a button that shows its caption has no tooltip");
  send_message(ribbon, rbSetLayout, RIBBON_LAYOUT_SIMPLIFIED, NULL);
  open = item_rect(ribbon, ID_OPEN); save = item_rect(ribbon, ID_SAVE);
  irect16_t large = item_rect(ribbon, ID_NEW);
  ASSERT_TRUE(send_message(ribbon, evGetTooltipText, MAKEDWORD(open.x + 2, open.y + 2), tip));
  ASSERT(strcmp(tip, "Open a document") == 0, "a bare icon's tooltip is the item's");
  ASSERT_TRUE(send_message(ribbon, evGetTooltipText, MAKEDWORD(save.x + 2, save.y + 2), tip));
  ASSERT(strcmp(tip, "Save") == 0, "or its caption");
  ASSERT(!send_message(ribbon, evGetTooltipText, MAKEDWORD(large.x + 2, large.y + 2), tip), "the single row's captioned buttons have none either");
  destroy_window(ribbon);
  test_env_shutdown();
  PASS();
}

static void test_layouts(void) {
  TEST("three rows stack small buttons beside large ones; one row lays them side by side; minimizing leaves the tabs");
  test_env_init();
  window_t *ribbon = make_ribbon(900);
  irect16_t large = item_rect(ribbon, ID_NEW), open = item_rect(ribbon, ID_OPEN), save = item_rect(ribbon, ID_SAVE), bold = item_rect(ribbon, ID_BOLD);
  ASSERT(open.x == save.x && save.x == bold.x && open.y < save.y && save.y < bold.y, "small buttons share a column");
  ASSERT(open.w == save.w && save.w == bold.w, "and its width");
  ASSERT(large.h == open.h * 3 && large.x + large.w <= open.x, "a large button is as tall as the column");
  int classic = ribbon_height(ribbon);
  ASSERT_TRUE(send_message(ribbon, rbSetLayout, RIBBON_LAYOUT_SIMPLIFIED, NULL));
  ASSERT_EQUAL(send_message(ribbon, rbGetLayout, 0, NULL), RIBBON_LAYOUT_SIMPLIFIED);
  int simple = ribbon_height(ribbon);
  ASSERT(simple < classic, "one row is shorter");
  large = item_rect(ribbon, ID_NEW); open = item_rect(ribbon, ID_OPEN); save = item_rect(ribbon, ID_SAVE);
  ASSERT(large.y == open.y && open.y == save.y && large.x + large.w <= open.x && open.x + open.w <= save.x, "commands sit in one row");
  ASSERT(large.w > open.w, "a large button keeps its caption; small ones show their icon");
  ASSERT_FALSE(send_message(ribbon, rbSetLayout, 7, NULL));
  ASSERT_TRUE(send_message(ribbon, rbSetMinimized, true, NULL));
  irect16_t tab;
  send_message(ribbon, rbGetTabRect, 0, &tab);
  ASSERT(ribbon_height(ribbon) == tab.h && !send_message(ribbon, rbGetItemRect, ID_NEW, &large), "a minimized ribbon is its tab row");
  click_rect(ribbon, tab);
  ASSERT(!send_message(ribbon, rbGetMinimized, 0, NULL) && ribbon_height(ribbon) == simple, "choosing a tab opens it again");
  destroy_window(ribbon);
  test_env_shutdown();
  PASS();
}

static void test_scaling(void) {
  TEST("a narrow ribbon drops captions, then turns large buttons into small ones");
  test_env_init();
  window_t *wide = make_ribbon(900);
  irect16_t find_wide = item_rect(wide, ID_FIND), open_wide = item_rect(wide, ID_OPEN);
  int narrow = find_wide.x + 30;   // the second group no longer fits as declared
  destroy_window(wide);
  window_t *ribbon = make_ribbon(narrow);
  irect16_t find = item_rect(ribbon, ID_FIND), open = item_rect(ribbon, ID_OPEN);
  ASSERT(open.w > 0 && open.w < open_wide.w, "small buttons dropped their captions first");
  ASSERT(find.w > 0 && find.h < find_wide.h, "then the last group's large button became a small one");
  ASSERT(find.x + find.w <= narrow, "and the commands fit the ribbon");
  destroy_window(ribbon);
  test_env_shutdown();
  PASS();
}

static void test_contextual_tabs(void) {
  TEST("a contextual tab shows while its context is active and gives way to the tab the user came from");
  test_env_init();
  window_t *ribbon = create_window("ribbon", WINDOW_NOTITLE | WINDOW_NORESIZE, MAKERECT(0, 0, 900, 200), NULL, ribbon_proc, 0, (void *)&kContextRibbon);
  show_window(ribbon, true);
  irect16_t view, table;
  ASSERT_TRUE(send_message(ribbon, rbGetTabRect, 1, &view));
  ASSERT(!send_message(ribbon, rbGetTabRect, 2, &table) && table.w == 0, "the contextual tab is hidden");
  ASSERT_FALSE(send_message(ribbon, rbSetActiveTab, 2, NULL));
  click_rect(ribbon, view);
  send_message(ribbon, rbSetContexts, CONTEXT_TABLE, NULL);
  ASSERT(send_message(ribbon, rbGetTabRect, 2, &table) && table.x >= view.x + view.w, "it shows after the other tabs");
  ASSERT(send_message(ribbon, rbGetActiveTab, 0, NULL) == 1, "showing it does not open it");
  send_message(ribbon, rbSetContexts, 0, NULL);
  send_message(ribbon, rbSetContexts, CONTEXT_TABLE, (void *)(uintptr_t)1);
  ASSERT(send_message(ribbon, rbGetActiveTab, 0, NULL) == 2, "unless asked to");
  ASSERT_EQUAL(send_message(ribbon, rbGetContexts, 0, NULL), CONTEXT_TABLE);
  send_message(ribbon, rbSetContexts, 0, NULL);
  ASSERT(send_message(ribbon, rbGetActiveTab, 0, NULL) == 1, "when the context leaves, the tab the user came from returns");
  destroy_window(ribbon);
  test_env_shutdown();
  PASS();
}

static result_t quick_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)win; (void)msg; (void)wparam; (void)lparam;
  return false;
}

static void test_chrome(void) {
  TEST("ribbon chrome docks the ribbon in the menu row and keeps the workspace under it");
  test_env_init();
  ui_update_screen_size(1024 * UI_WINDOW_SCALE, 768 * UI_WINDOW_SCALE);
  static const toolbar_item_t quick_items[] = { { TOOLBAR_ITEM_BUTTON, ID_SAVE, "floppy-disk", 0, 0, NULL, "Save" } };
  static const application_toolbar_t quick = { quick_items, 1, TOOLBAR_PRESENTATION_COMPACT };
  ribbon_def_t def = kRibbon;
  def.quick_access = &quick;
  ASSERT_NULL(create_ribbon_chrome("Chrome", ribbon_proc, NULL, quick_proc, 0));
  ASSERT_NULL(create_ribbon_chrome("Chrome", ribbon_proc, &def, NULL, 0));
  window_t *chrome = create_ribbon_chrome("Chrome", ribbon_proc, &def, quick_proc, 0);
  ASSERT_NOT_NULL(chrome);
  window_t *ribbon = app_chrome_ribbon(chrome), *bar = app_chrome_toolbar(chrome);
  ASSERT_NOT_NULL(ribbon);
  ASSERT_NOT_NULL(bar);
  ASSERT(app_chrome_menubar(chrome) == ribbon, "the ribbon is the chrome's menu row");
  irect16_t workspace = {0}, tab;
  ASSERT_TRUE(get_application_workspace(0, &workspace));
  ASSERT(workspace.y == ribbon_height(ribbon), "the workspace starts under the ribbon");
  send_message(ribbon, rbGetTabRect, 0, &tab);
  ASSERT(toolbar_dock_hint(bar) == TOOLBAR_DOCK_MENU && bar->frame.y == 0 && bar->frame.h <= tab.h, "the Quick Access Toolbar sits in the tab row");
  int tall = workspace.y;
  send_message(ribbon, rbSetLayout, RIBBON_LAYOUT_SIMPLIFIED, NULL);
  ASSERT_TRUE(get_application_workspace(0, &workspace));
  ASSERT(workspace.y < tall && workspace.y == ribbon_height(ribbon), "a shorter ribbon returns the room to the workspace");
  destroy_window(chrome);
  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("Ribbon");
  test_tabs_and_state();
  test_commands();
  test_layouts();
  test_scaling();
  test_contextual_tabs();
  test_chrome();
  TEST_END();
}
