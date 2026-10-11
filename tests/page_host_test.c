#include <orion/ui.h>
#include <orion/user/toolbar.h>
#include <orion/commctl/menubar.h>
#include "test_env.h"
#include "test_framework.h"

enum { ACT_FIRST = 2100, ACT_SECOND, RAIL = 1000, EDIT_FIRST, EDIT_SECOND };
static int activity_changes, activity_creates, activity_destroys;

static result_t test_activity_page_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:  activity_creates++; return true;
    case evDestroy: activity_destroys++; return true;
    case evPaint: return false;
    default: return false;
  }
}

static result_t test_page_host_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: case evDestroy: return true;
    case evPageChanged: activity_changes++; return true;
    case evPaint: return false;
    default: return false;
  }
}

static const toolbar_item_t activity_buttons[] = {
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ACT_FIRST,  .style = TBSTYLE_CHECKGROUP, .text = "First"},
  {.type = TOOLBAR_ITEM_BUTTON, .ident = ACT_SECOND, .style = TBSTYLE_CHECKGROUP, .text = "Second"},
};
static const activitybar_params_t activity_params = {activity_buttons, ARRAY_LEN(activity_buttons)};
static const form_ctrl_def_t activity_controls[] = {
  {.class_name = "ActivityBar", .id = RAIL, .lparam = &activity_params},
};
static const form_ctrl_def_t activity_edits[] = {
  {.class_name = "TextEdit", .id = EDIT_FIRST},
  {.class_name = "TextEdit", .id = EDIT_SECOND},
};
static const form_def_t activity_page_form = {
  .name = "Page", .width = 400, .height = 200, .role = WINDOW_ROLE_PAGE,
  .flags = WINDOW_AUTO_LAYOUT, .children = activity_edits, .child_count = ARRAY_LEN(activity_edits),
};
static const page_binding_t page_bindings[] = {
  {ACT_FIRST,  RAIL, "first",  &activity_page_form, EDIT_FIRST},
  {ACT_SECOND, RAIL, "second", &activity_page_form, EDIT_FIRST},
};
static const form_def_t page_host_form = {
  .name = "Host", .width = 500, .height = 300, .role = WINDOW_ROLE_HOST,
  .flags = WINDOW_AUTO_LAYOUT, .children = activity_controls, .child_count = ARRAY_LEN(activity_controls),
  .page_bindings = page_bindings, .page_binding_count = ARRAY_LEN(page_bindings), .initial_page = ACT_SECOND,
};
static const page_handler_t activity_handlers[] = {
  {"first", test_activity_page_proc}, {"second", test_activity_page_proc},
};

static window_t *make_page_host(void) {
  return create_window_from_form(&page_host_form, 0, 0, NULL, test_page_host_proc, 0, NULL);
}

static void test_activity_lifecycle(void) {
  TEST("activity host: command routes preserve page state, focus and layout");
  test_env_init();
  activity_changes = activity_creates = activity_destroys = 0;
  window_t *host = make_page_host();
  ASSERT_TRUE(page_host_init(host, &page_host_form, activity_handlers, ARRAY_LEN(activity_handlers)));
  window_t *first = page_host_page(host, ACT_FIRST), *second = page_host_page(host, ACT_SECOND);
  window_t *bar = get_window_item(host, RAIL), *edit = get_window_item(second, EDIT_SECOND);
  ASSERT_EQUAL(activity_creates, 2);
  ASSERT_EQUAL(activity_changes, 1);
  ASSERT_EQUAL(window_active_page(host), second);
  ASSERT_EQUAL(page_host_selection(host), ACT_SECOND);
  ASSERT_FALSE(window_has_state(first, WINDOW_STATE_VISIBLE));
  ASSERT_TRUE(toolbar_is_button_checked(bar, ACT_SECOND));
  ASSERT_EQUAL(g_ui_runtime.focused, get_window_item(second, EDIT_FIRST));
  send_message(edit, edSetText, 0, "Persistent draft");
  set_focus(edit);
  // Pointer navigation focuses the rail before it dispatches the command.
  set_focus(bar);
  send_message(bar, evCommand, MAKEDWORD(ACT_FIRST, btnClicked), bar);
  ASSERT_EQUAL(window_active_page(host), first);
  ASSERT_FALSE(window_has_state(second, WINDOW_STATE_VISIBLE));
  ASSERT_TRUE(toolbar_is_button_checked(bar, ACT_FIRST));
  send_message(host, evCommand, MAKEDWORD(ACT_SECOND, kMenuBarNotificationItemClick), NULL);
  ASSERT_EQUAL(g_ui_runtime.focused, edit);
  char text[64]; send_message(edit, edGetText, sizeof(text), text);
  ASSERT_STR_EQUAL(text, "Persistent draft");
  send_message(host, evCommand, MAKEDWORD(ACT_FIRST, kAcceleratorNotification), NULL);
  ASSERT_EQUAL(page_host_selection(host), ACT_FIRST);
  ASSERT_TRUE(page_host_select(host, ACT_SECOND));
  ASSERT_EQUAL(g_ui_runtime.focused, edit);
  int changes = activity_changes;
  ASSERT_TRUE(page_host_select(host, ACT_SECOND));
  ASSERT_EQUAL(activity_changes, changes);
  ASSERT_EQUAL(g_ui_runtime.focused, edit);
  set_focus(host);
  send_message(host, evSetFocus, 0, NULL);
  ASSERT_EQUAL(g_ui_runtime.focused, edit);
  ASSERT_FALSE(page_host_select(host, 9999));
  ASSERT_EQUAL(page_host_selection(host), ACT_SECOND);
  ASSERT_FALSE(page_host_command(host, MAKEDWORD(ACT_FIRST, ednChange)));
  host->frame.w = 700; host->frame.h = 420;
  send_message(host, evResize, 0, NULL);
  ASSERT_EQUAL(second->frame.x, bar->frame.w);
  ASSERT_EQUAL(second->frame.w, get_client_rect(host).w - bar->frame.w);
  ASSERT_EQUAL(second->frame.h, get_client_rect(host).h);
  ASSERT_EQUAL(activity_creates, 2);
  destroy_window(host);
  ASSERT_EQUAL(activity_destroys, 2);
  test_env_shutdown();
  PASS();
}

static void test_activity_focus_lifetime(void) {
  TEST("activity host: independent hosts and destroyed/disabled focus targets");
  test_env_init();
  window_t *host = make_page_host();
  ASSERT_TRUE(page_host_init(host, &page_host_form, NULL, 0));
  window_t *page = page_host_page(host, ACT_SECOND), *edit = get_window_item(page, EDIT_SECOND);
  set_focus(edit);
  ASSERT_TRUE(page_host_select(host, ACT_FIRST));
  destroy_window(edit);
  ASSERT_TRUE(page_host_select(host, ACT_SECOND));
  ASSERT_EQUAL(g_ui_runtime.focused, get_window_item(page, EDIT_FIRST));
  ASSERT_TRUE(page_host_select(host, ACT_FIRST));
  enable_window(get_window_item(page, EDIT_FIRST), false);
  ASSERT_TRUE(page_host_select(host, ACT_SECOND));
  ASSERT_EQUAL(g_ui_runtime.focused, page);
  window_t *other = make_page_host();
  ASSERT_TRUE(page_host_init(other, &page_host_form, NULL, 0));
  ASSERT_TRUE(page_host_select(other, ACT_FIRST));
  ASSERT_EQUAL(page_host_selection(host), ACT_SECOND);
  ASSERT_EQUAL(page_host_selection(other), ACT_FIRST);
  destroy_window(page);
  ASSERT_EQUAL(page_host_selection(host), 0);
  ASSERT_FALSE(page_host_select(host, ACT_SECOND));
  ASSERT_TRUE(page_host_select(host, ACT_FIRST));
  destroy_window(host); destroy_window(other);
  test_env_shutdown();
  PASS();
}

static void test_activity_validation(void) {
  TEST("activity host: rejects invalid declarations without creating pages");
  test_env_init();
  activity_creates = 0;
  window_t *host = make_page_host();
  form_def_t bad = page_host_form;
  bad.initial_page = 9999;
  ASSERT_FALSE(page_host_init(host, &bad, activity_handlers, ARRAY_LEN(activity_handlers)));
  ASSERT_EQUAL(activity_creates, 0);
  ASSERT_EQUAL(page_host_selection(host), 0);
  page_binding_t bindings[2] = {page_bindings[0], page_bindings[0]};
  bad = page_host_form; bad.page_bindings = bindings;
  ASSERT_FALSE(page_host_init(host, &bad, NULL, 0));
  ASSERT_EQUAL(activity_creates, 0);
  ASSERT_TRUE(page_host_init(host, &page_host_form, activity_handlers, ARRAY_LEN(activity_handlers)));
  ASSERT_FALSE(page_host_init(host, &page_host_form, NULL, 0));
  ASSERT_EQUAL(activity_creates, 2);
  ASSERT_EQUAL(page_host_selection(host), ACT_SECOND);
  destroy_window(host);
  test_env_shutdown();
  PASS();
}

static void test_page_navigation_and_orientation(void) {
  TEST("PageHost: page metadata generates navigation and adapts without resetting pages");
  test_env_init();
  activity_creates = activity_destroys = 0;
  form_ctrl_def_t rail = {.class_name = "ActivityBar", .id = RAIL};
  form_def_t first = activity_page_form, second = activity_page_form;
  first.name = "First page"; first.tooltip = "First destination";
  second.name = "Second page";
  page_binding_t pages[] = {
    {ACT_FIRST, RAIL, "first", &first, EDIT_FIRST},
    {ACT_SECOND, RAIL, "second", &second, EDIT_FIRST},
  };
  form_def_t form = page_host_form;
  form.children = &rail; form.child_count = 1; form.page_bindings = pages;
  window_t *host = create_window_from_form(&form, 0, 0, NULL, test_page_host_proc, 0, NULL);
  ASSERT_TRUE(page_host_init(host, &form, activity_handlers, ARRAY_LEN(activity_handlers)));
  window_t *bar = get_window_item(host, RAIL), *page = page_host_page(host, ACT_SECOND);
  window_t *edit = get_window_item(page, EDIT_FIRST);
  toolbar_state_t *tb = window_toolbar_state(bar);
  ASSERT_EQUAL(tb->item_count, 2);
  ASSERT_STR_EQUAL(tb->items[0].text, "First page");
  ASSERT_STR_EQUAL(tb->items[0].tooltip, "First destination");
  ASSERT_STR_EQUAL(tb->items[1].tooltip, "Second page");
  ASSERT_EQUAL(bar->dock->side, DOCK_LEFT);
  send_message(edit, edSetText, 0, "Draft survives rotation");
  host->frame.w = 300; host->frame.h = 600;
  send_message(host, evResize, 0, NULL);
  ASSERT_EQUAL(bar->dock->side, DOCK_BOTTOM);
  ASSERT_EQUAL(tb->orientation, TOOLBAR_HORIZONTAL);
  ASSERT_EQUAL(page->frame.x, 0);
  ASSERT_EQUAL(page->frame.w, get_client_rect(host).w);
  ASSERT_EQUAL(page->frame.h, get_client_rect(host).h - bar->frame.h);
  ASSERT_EQUAL(bar->frame.y, page->frame.h);
  ASSERT_EQUAL(g_ui_runtime.focused, edit);
  ASSERT_EQUAL(page_host_selection(host), ACT_SECOND);
  host->frame.w += get_client_rect(host).h - get_client_rect(host).w;
  send_message(host, evResize, 0, NULL);
  ASSERT_EQUAL(bar->dock->side, DOCK_BOTTOM);
  send_message(bar, evCommand, MAKEDWORD(ACT_FIRST, btnClicked), bar);
  ASSERT_EQUAL(page_host_selection(host), ACT_FIRST);
  ASSERT_TRUE(page_host_select(host, ACT_SECOND));
  host->frame.w = 700; host->frame.h = 400;
  send_message(host, evResize, 0, NULL);
  ASSERT_EQUAL(bar->dock->side, DOCK_LEFT);
  ASSERT_EQUAL(tb->orientation, TOOLBAR_VERTICAL);
  ASSERT_EQUAL(page->frame.x, bar->frame.w);
  char text[64]; send_message(edit, edGetText, sizeof(text), text);
  ASSERT_STR_EQUAL(text, "Draft survives rotation");
  ASSERT_EQUAL(page_host_page(host, ACT_SECOND), page);
  ASSERT_EQUAL(activity_creates, 2);
  destroy_window(host);
  ASSERT_EQUAL(activity_destroys, 2);
  test_env_shutdown();
  PASS();
}

int main(void) {
  TEST_START("Activity page host");
  test_activity_lifecycle();
  test_activity_focus_lifetime();
  test_activity_validation();
  test_page_navigation_and_orientation();
  TEST_END();
}
