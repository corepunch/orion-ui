#include "test_framework.h"
#include "test_env.h"
#include "test_graphics.h"
#include <orion/user/toolbar.h>
#include <orion/commctl/menubar.h>
#include <math.h>
#ifndef _WIN32
#include <errno.h>
#include <sys/wait.h>
#endif

// Exercise the same startup and host procedure as the standalone app.
#define main terminal_standalone_main
#include "apps/terminal/main.c"
#undef main

static void click_workspace(window_t *bar) {
  irect16_t item;
  send_message(bar, tbGetItemRect, ID_VIEW_WORKSPACE, &item);
  uint32_t point = MAKEDWORD(item.x + item.w / 2, item.y + item.h / 2);
  set_focus(bar);
  send_message(bar->toolbar, evLeftButtonDown, point, NULL);
  send_message(bar->toolbar, evLeftButtonUp, point, NULL);
}

static double terminal_split_ratio(window_t *left, window_t *right) {
  return (double)left->frame.w / (left->frame.w + right->frame.w);
}

static void test_terminal_page_host(void) {
  TEST("Terminal pane tabs retain independent sessions under the Workspace split");
  test_env_init();
  ASSERT_TRUE(test_graphics_init(1024, 768));
  char *args[] = {"terminal", NULL};
  ASSERT_TRUE(gem_init(1, args, 0));
  window_t *host = g_terminal.host, *page = g_terminal.page, *bar = g_terminal.activity;
  window_t *split = get_window_item(page, ID_WORKSPACE_PAGE_TERMINALS);
  window_t *left_tabs = get_window_item(page, ID_WORKSPACE_PAGE_LEFT_TABS);
  window_t *right_tabs = get_window_item(page, ID_WORKSPACE_PAGE_RIGHT_TABS);
  window_t *left = terminal_workspace_terminal(page, 0), *right = terminal_workspace_terminal(page, 1);
  ASSERT_EQUAL(page_host_page(host, ID_VIEW_WORKSPACE), page);
  ASSERT_EQUAL(window_active_page(host), page);
  window_t *pane_bar = (window_t *)send_message(right_tabs, tcGetToolbar, 0, NULL);
  ASSERT_NOT_NULL(pane_bar);
  toolbar_state_t *pane_actions = toolbar_get_state(pane_bar);
  ASSERT_EQUAL(pane_actions->item_count, 3);
  ASSERT_EQUAL(pane_actions->items[0].ident, ID_TAB_NEW);
  ASSERT_EQUAL(pane_actions->items[1].alt_ident, ID_TAB_SPLIT_DOWN);
  ASSERT_EQUAL(pane_actions->items[2].ident, ID_TAB_CLOSE);
  window_t *panes = page;
  ASSERT_EQUAL(split->parent, panes);
  ASSERT_EQUAL(page->parent, host);
  ASSERT_EQUAL(splitview_get_left(split), left_tabs);
  ASSERT_EQUAL(splitview_get_right(split), right_tabs);
  ASSERT_EQUAL(left->parent->parent, left_tabs);
  ASSERT_EQUAL(right->parent->parent, right_tabs);
  ASSERT_EQUAL(send_message(left_tabs, tcGetCount, 0, NULL), 1);
  ASSERT_EQUAL(send_message(right_tabs, tcGetCount, 0, NULL), 1);
  ASSERT_EQUAL(g_ui_runtime.focused, left);
  toolbar_state_t *tb = toolbar_get_state(bar);
  ASSERT_EQUAL(tb->item_count, 1);
  ASSERT_STR_EQUAL(tb->items[0].checked_icon, "workspace-fill");
  ASSERT_TRUE(toolbar_is_button_checked(bar, ID_VIEW_WORKSPACE));

  vgat_state_t *ls = left->userdata, *rs = right->userdata;
  send_message(left, evTextInput, 0, "left draft");
  send_message(right, evTextInput, 0, "right draft");
  set_focus(right);
  click_workspace(bar);
  ASSERT_EQUAL(g_ui_runtime.focused, right);
  send_message(host, evCommand, MAKEDWORD(ID_VIEW_WORKSPACE, kMenuBarNotificationItemClick), NULL);
  ASSERT_EQUAL(g_ui_runtime.focused, right);

  int before = left_tabs->frame.w;
  send_message(split, evCommand, MAKEDWORD(0, spnDragStart), (void *)(uintptr_t)MAKEDWORD(before, 10));
  send_message(split, evMouseMove, MAKEDWORD(before + 90, 10), NULL);
  send_message(split, evLeftButtonUp, 0, NULL);
  ASSERT_TRUE(left_tabs->frame.w > before);
  double ratio = terminal_split_ratio(left_tabs, right_tabs);

  // Pane actions target their own pane even when the opposite pane has focus.
  toolbar_state_t *actions = toolbar_get_state(host);
  ASSERT_NOT_NULL(actions); ASSERT_EQUAL(actions->item_count, 5);
  ASSERT_EQUAL(page->children, split); ASSERT_NULL(split->next);
  ASSERT_EQUAL(page->pages->toolbar_count, actions->item_count);
  ASSERT_FALSE(page->flags & WINDOW_TOOLBAR);
  irect16_t new_tab;
  ASSERT_TRUE(send_message(pane_bar, tbGetItemRect, ID_TAB_NEW, &new_tab));
  uint32_t point = MAKEDWORD(new_tab.x + new_tab.w / 2, new_tab.y + new_tab.h / 2);
  set_focus(left);
  send_message(pane_bar->toolbar, evLeftButtonDown, point, NULL);
  send_message(pane_bar->toolbar, evLeftButtonUp, point, NULL);
  ASSERT_EQUAL(send_message(left_tabs, tcGetCount, 0, NULL), 1);
  ASSERT_EQUAL(send_message(right_tabs, tcGetCount, 0, NULL), 2);
  window_t *right_second = terminal_workspace_terminal(page, 1);
  ASSERT_NOT_EQUAL(right_second, right);
  ASSERT_EQUAL(terminal_workspace_terminal(page, 0), left);
  ASSERT_EQUAL(g_ui_runtime.focused, right_second);
  ASSERT_FALSE(window_has_state(right->parent, WINDOW_STATE_VISIBLE));
  ASSERT_TRUE(window_has_state(left->parent, WINDOW_STATE_VISIBLE));
#ifndef _WIN32
  send_message(right_second, evTextInput, 0, "run /bin/cat");
  send_message(right_second, evKeyDown, AX_KEY_ENTER, NULL);
  vgat_state_t *process = right_second->userdata;
  ASSERT_EQUAL(process->mode, VGAT_MODE_PTY);
  int pid = process->pty_pid, fd = process->pty_fd;
  vgat_pty_watch_t *watch = process->pty_watch;
  ASSERT_TRUE(pid > 0 && fd >= 0); ASSERT_NOT_NULL(watch);
#endif
  irect16_t tab_rect;
  ASSERT_TRUE(send_message(right_tabs, tcGetTabRect, 0, &tab_rect));
  send_message(right_tabs, evLeftButtonDown, MAKEDWORD(tab_rect.x + 8, tab_rect.y + 4), NULL);
  ASSERT_EQUAL(terminal_workspace_terminal(page, 1), right);
  ASSERT_EQUAL(g_ui_runtime.focused, right);
  ASSERT_STR_EQUAL(rs->input_buf, "right draft");

  // Keyboard navigation wraps in the focused pane without switching the other.
  ui_event_t next_tab = {.message = kEventKeyDown, .keyCode = AX_KEY_TAB};
#ifdef __APPLE__
  next_tab.modflags = AX_MOD_CMD >> 16;
#else
  next_tab.modflags = AX_MOD_CTRL >> 16;
#endif
  ASSERT_TRUE(translate_accelerator(host, &next_tab, g_terminal.accel));
  ASSERT_EQUAL(g_ui_runtime.focused, right_second);
  ASSERT_EQUAL(terminal_workspace_terminal(page, 0), left);
  set_focus(left);
  send_message(host, evCommand, MAKEDWORD(ID_TAB_NEW, kAcceleratorNotification), NULL);
  window_t *left_second = terminal_workspace_terminal(page, 0);
  ASSERT_NOT_EQUAL(left_second, left);
  ASSERT_EQUAL(terminal_workspace_terminal(page, 1), right_second);
  send_message(left_second, evTextInput, 0, "second left draft");
  send_message(host, evCommand, MAKEDWORD(ID_TAB_PREVIOUS, kAcceleratorNotification), NULL);
  ASSERT_EQUAL(g_ui_runtime.focused, left);
  ASSERT_STR_EQUAL(ls->input_buf, "left draft");
  ASSERT_TRUE(fabs(terminal_split_ratio(left_tabs, right_tabs) - ratio) < 0.01);

  host->frame.w = 420; host->frame.h = 700;
  send_message(host, evResize, 0, NULL);
  ASSERT_EQUAL(bar->dock->side, DOCK_BOTTOM);
  for (int i = 0; i < actions->item_count; i++) {
    if (actions->items[i].type != TOOLBAR_ITEM_BUTTON) continue;
    ASSERT_EQUAL(actions->item_rects[i].y, actions->item_rects[0].y);
    ASSERT_TRUE(actions->item_rects[i].x + actions->item_rects[i].w <= host->frame.w);
  }
  ASSERT_EQUAL(tb->orientation, TOOLBAR_HORIZONTAL);
  ASSERT_EQUAL(page->frame.w, get_client_rect(host).w);
  ASSERT_EQUAL(page->frame.h, get_client_rect(host).h - bar->frame.h);
  ASSERT_TRUE(fabs(terminal_split_ratio(left_tabs, right_tabs) - ratio) < 0.01);
  click_workspace(bar);
  ASSERT_EQUAL(g_ui_runtime.focused, left);
  send_message(host, evCommand, MAKEDWORD(ID_TAB_NEXT, kAcceleratorNotification), NULL);
  ASSERT_STR_EQUAL(((vgat_state_t *)left_second->userdata)->input_buf, "second left draft");
  ASSERT_EQUAL(g_ui_runtime.focused, left_second);

  // Closing selected and inactive tabs leaves the opposite pane intact.
  window_t *left_bar = (window_t *)send_message(left_tabs, tcGetToolbar, 0, NULL);
  irect16_t close_tab;
  ASSERT_TRUE(send_message(left_bar, tbGetItemRect, ID_TAB_CLOSE, &close_tab));
  point = MAKEDWORD(close_tab.x + close_tab.w / 2, close_tab.y + close_tab.h / 2);
  set_focus(right_second);
  send_message(left_bar->toolbar, evLeftButtonDown, point, NULL);
  send_message(left_bar->toolbar, evLeftButtonUp, point, NULL);
  ASSERT_EQUAL(send_message(left_tabs, tcGetCount, 0, NULL), 1);
  ASSERT_EQUAL(g_ui_runtime.focused, left);
  ASSERT_EQUAL(terminal_workspace_terminal(page, 1), right_second);
  ASSERT_TRUE(send_message(right_tabs, tcDeletePage, 0, NULL));
  ASSERT_EQUAL(g_ui_runtime.focused, left);
  ASSERT_EQUAL(terminal_workspace_terminal(page, 1), right_second);
#ifndef _WIN32
  ASSERT_EQUAL(process->pty_pid, pid);
  ASSERT_EQUAL(process->pty_fd, fd);
  ASSERT_EQUAL(process->pty_watch, watch);
  ASSERT_EQUAL(process->mode, VGAT_MODE_PTY);
#endif
  host->frame.w = 1000; host->frame.h = 650;
  send_message(host, evResize, 0, NULL);
  ASSERT_EQUAL(bar->dock->side, DOCK_LEFT);
  ASSERT_EQUAL(tb->orientation, TOOLBAR_VERTICAL);
  ASSERT_TRUE(fabs(terminal_split_ratio(left_tabs, right_tabs) - ratio) < 0.01);

  // Move the live PTY into a new bottom pane. Its empty old pane collapses.
  set_focus(right_second);
  ASSERT_TRUE(page_move_tab(panes, right_second->parent, left_tabs, PANE_BOTTOM, -1));
  ASSERT_EQUAL(page_pane_count(panes), 2);
  ASSERT_EQUAL(terminal_workspace_terminal(page, 0), left);
  ASSERT_EQUAL(terminal_workspace_terminal(page, 1), right_second);
  ASSERT_EQUAL(g_ui_runtime.focused, right_second);
  ASSERT_STR_EQUAL(ls->input_buf, "left draft");
#ifndef _WIN32
  ASSERT_EQUAL(process->pty_pid, pid); ASSERT_EQUAL(process->pty_fd, fd);
  ASSERT_EQUAL(process->pty_watch, watch); ASSERT_EQUAL(process->mode, VGAT_MODE_PTY);
#endif
  click_workspace(bar);
  ASSERT_EQUAL(g_ui_runtime.focused, right_second);
  ASSERT_TRUE(page_move_tab(panes, right_second->parent, left_tabs, PANE_CENTER, 0));
  ASSERT_EQUAL(page_pane_count(panes), 1);
  ASSERT_EQUAL(g_ui_runtime.focused, right_second);
  send_message(host, evCommand, MAKEDWORD(ID_TAB_CLOSE, kAcceleratorNotification), NULL);
  ASSERT_EQUAL(page_pane_count(panes), 1);
  ASSERT_EQUAL(terminal_workspace_terminal(page, 0), left);
#ifndef _WIN32
  int status = 0;
  ASSERT_EQUAL(waitpid(pid, &status, WNOHANG), -1);
  ASSERT_EQUAL(errno, ECHILD);
#endif
  ASSERT_STR_EQUAL(ls->input_buf, "left draft");
  send_message(host, evCommand, MAKEDWORD(ID_TAB_CLOSE, kAcceleratorNotification), NULL);
  window_t *fresh = terminal_workspace_terminal(page, 0);
  ASSERT_NOT_NULL(fresh); ASSERT_NOT_EQUAL(fresh, left);
  ASSERT_STR_EQUAL(((vgat_state_t *)fresh->userdata)->input_buf, "");
  // Commands use the same reusable split/move implementation as dragging.
  ASSERT_TRUE(terminal_workspace_command(page, ID_TAB_SPLIT_RIGHT));
  ASSERT_EQUAL(page_pane_count(panes), 2);
  ASSERT_TRUE(terminal_workspace_command(page, ID_TAB_SPLIT_DOWN));
  ASSERT_EQUAL(page_pane_count(panes), 3);
  ASSERT_TRUE(terminal_workspace_command(page, ID_TAB_FOCUS_NEXT));
  ASSERT_EQUAL(g_ui_runtime.focused, fresh);
  ASSERT_TRUE(terminal_workspace_command(page, ID_TAB_MOVE));
  ASSERT_EQUAL(page_pane_count(panes), 2);
  ASSERT_EQUAL(g_ui_runtime.focused, fresh);
  host->frame.w = 420; host->frame.h = 700;
  send_message(host, evResize, 0, NULL);
  ASSERT_EQUAL(bar->dock->side, DOCK_BOTTOM);
  for (int i = 0; i < actions->item_count; i++) {
    if (actions->items[i].type != TOOLBAR_ITEM_BUTTON) continue;
    ASSERT_EQUAL(actions->item_rects[i].y, actions->item_rects[0].y);
    ASSERT_TRUE(actions->item_rects[i].x + actions->item_rects[i].w <= host->frame.w);
  }
  click_workspace(bar);
  ASSERT_EQUAL(g_ui_runtime.focused, fresh);
  window_t *active_pane = page_active_pane(page);
  pane_bar = (window_t *)send_message(active_pane, tcGetToolbar, 0, NULL);
  ASSERT_NOT_NULL(pane_bar);
  irect16_t action;
  ASSERT_TRUE(send_message(pane_bar, tbGetItemRect, ID_TAB_SPLIT_RIGHT, &action));
  ui_event_t alt = {.message = kEventModifiersChanged, .wParam = AX_MOD_ALT};
  dispatch_message(&alt);
  int pane_count = page_pane_count(page);
  uint32_t split_point = MAKEDWORD(action.x + action.w / 2, action.y + action.h / 2);
  send_message(pane_bar->toolbar, evLeftButtonDown, split_point, NULL);
  send_message(pane_bar->toolbar, evLeftButtonUp, split_point, NULL);
  ASSERT_EQUAL(page_pane_count(page), pane_count + 1);
  window_t *new_pane = page_active_pane(page);
  ASSERT_TRUE(new_pane->frame.y > active_pane->frame.y);
  pane_bar = (window_t *)send_message(new_pane, tcGetToolbar, 0, NULL);
  ASSERT_NOT_NULL(pane_bar);
  ASSERT_EQUAL(toolbar_get_state(pane_bar)->item_count, 3);
  alt.wParam = 0; dispatch_message(&alt);
  // Closing the sole tab safely destroys its pane and the clicked toolbar.
  ASSERT_TRUE(send_message(pane_bar, tbGetItemRect, ID_TAB_CLOSE, &close_tab));
  point = MAKEDWORD(close_tab.x + close_tab.w / 2, close_tab.y + close_tab.h / 2);
  send_message(pane_bar->toolbar, evLeftButtonDown, point, NULL);
  send_message(pane_bar->toolbar, evLeftButtonUp, point, NULL);
  ASSERT_EQUAL(page_pane_count(page), pane_count);
  gem_shutdown();
  ASSERT_NULL(g_terminal.host); ASSERT_NULL(g_ui_runtime.focused);
  test_env_shutdown(); test_graphics_shutdown();
  PASS();
}

int main(void) {
  TEST_START("Terminal pane tabs");
  test_terminal_page_host();
  TEST_END();
}
