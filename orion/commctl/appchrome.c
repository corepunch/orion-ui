#include <stdlib.h>

#include "appchrome.h"
#include <orion/user/draw.h>
#include <orion/user/toolbar.h>

typedef struct {
  window_t *menubar;
  toolbar_presentation_t presentation;
} app_chrome_state_t;

typedef struct {
  winproc_t         menubar_proc;
  const menu_def_t *menus;
  int               menu_count;
  winproc_t         toolbar_proc;
} app_chrome_create_t;

static void app_chrome_resize_children(window_t *win) {
  app_chrome_state_t *st = (app_chrome_state_t *)win->userdata;
  if (!st) return;
  if (st->menubar) resize_window(st->menubar, win->frame.w, MENUBAR_HEIGHT);
  window_t *bar = app_chrome_toolbar(win);
  if (bar && st->presentation == TOOLBAR_PRESENTATION_COMPACT) {
    send_message(bar, tbSetStyle, TOOLBAR_STYLE_COMPACT, NULL);
    send_message(bar, tbSetButtonSize, MENUBAR_HEIGHT - 2 * TOOLBAR_COMPACT_PADDING, NULL);
    toolbar_state_t *tb = toolbar_get_state(bar);
    int width = 2 * TOOLBAR_COMPACT_PADDING;
    for (int i = 0; tb && tb->item_rects && i < tb->item_count; i++)
      width = MAX(width, tb->item_rects[i].x + tb->item_rects[i].w + TOOLBAR_COMPACT_PADDING);
    int menu_width = st->menubar ? send_message(st->menubar, kMenuBarMessageGetContentWidth, 0, NULL) : win->frame.w;
    bool compact = width + menu_width + MENUBAR_HEIGHT + 8 <= win->frame.w;
    toolbar_dock_t dock = compact ? TOOLBAR_DOCK_MENU : TOOLBAR_DOCK_TOP;
    if (bar->toolbar_dock != dock) {
      fprintf(stderr, "[chrome] toolbar layout win=%u toolbar=%u compact=%d width=%d menu_width=%d\n",
              win->id, bar->id, compact, win->frame.w, menu_width);
      fflush(stderr);
    }
    bar->toolbar_dock = dock;
    if (compact) {
      irect16_t row = rect_split_top(get_client_rect(win), MENUBAR_HEIGHT);
      bar->frame = rect_split_right(rect_trim_right(row, MENUBAR_HEIGHT), width);
      invalidate_window(bar);
    } else {
      send_message(bar, tbSetStyle, 0, NULL);
      send_message(bar, tbSetButtonSize, 0, NULL);
    }
  }
  irect16_t area = layout_docked_toolbars(win,
      rect_trim_top(get_client_rect(win), st->menubar ? MENUBAR_HEIGHT : 0));
  area = rect_offset(area, window_screen_x(win), window_screen_y(win));
  set_application_workspace(win, &area);
}

static result_t win_app_chrome(window_t *win, uint32_t msg,
                               uint32_t wparam, void *lparam) {
  app_chrome_state_t *st = (app_chrome_state_t *)win->userdata;
  switch (msg) {
    case evCreate: {
      app_chrome_create_t *cfg = (app_chrome_create_t *)lparam;
      st = allocate_window_data(win, sizeof(*st));
      if (!cfg || !cfg->toolbar_proc) return false;
      if (cfg->menubar_proc)
        st->menubar = create_window("menubar", WINDOW_NOTITLE | WINDOW_NORESIZE,
                                    MAKERECT(0, 0, win->frame.w, MENUBAR_HEIGHT),
                                    win, cfg->menubar_proc, 0, NULL);
      window_t *toolbar = create_docked_toolbar(win, TOOLBAR_DOCK_TOP, cfg->toolbar_proc);
      app_chrome_resize_children(win);
      if (st->menubar)
        send_message(st->menubar, kMenuBarMessageSetMenus,
                     (uint32_t)cfg->menu_count, (void *)cfg->menus);
      return toolbar != NULL;
    }
    case evHitTest: {
      ipoint16_t point = {(int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)};
      if (!lparam) return false;
      *(window_t **)lparam = NULL;
      window_t *bar = app_chrome_toolbar(win);
      if (bar && bar->toolbar_dock == TOOLBAR_DOCK_MENU && window_has_state(bar, WINDOW_STATE_VISIBLE) &&
          rect_contains_point(bar->frame, point)) {
        *(window_t **)lparam = bar;
        return true;
      }
      for (window_t *child = win->children; child; child = child->next) {
        if (!window_has_state(child, WINDOW_STATE_VISIBLE) || !rect_contains_point(child->frame, point)) continue;
        *(window_t **)lparam = child;
        send_message(child, evHitTest, MAKEDWORD(point.x - child->frame.x, point.y - child->frame.y), lparam);
        break;
      }
      return true;
    }
    case evPaint:
      return false;
    case evResize:
      app_chrome_resize_children(win);
      return true;
    case tbButtonClick: {
      window_t *src = (window_t *)lparam;
      window_t *bar = NULL;
      for (window_t *child = win->children; child; child = child->next) {
        if (!(child->flags & WINDOW_TOOLBAR)) continue;
        if (src == child || (src && src->parent == child)) {
          bar = child;
          break;
        }
      }
      if (!bar) bar = app_chrome_toolbar(win);
      fprintf(stderr, "[chrome] click ident=%u bar=%u\n",
              (unsigned)wparam, bar ? bar->id : 0);
      fflush(stderr);
      return bar ? send_message(bar, msg, wparam, lparam) : false;
    }
    case evDisplayChange: {
      resize_window(win, LOWORD(wparam), MAX(1, (int)HIWORD(wparam) - win->frame.y));
      return true;
    }
    case evDestroy:
      free(win->userdata);
      win->userdata = NULL;
      return false;
    default:
      return false;
  }
}

window_t *create_app_chrome(const char *title, winproc_t menubar_proc,
                            const menu_def_t *menus, int menu_count,
                            winproc_t toolbar_proc, hinstance_t hinstance) {
  int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
  int sh = MAX(MENUBAR_HEIGHT + TOOLBAR_BAND_HEIGHT, ui_get_system_metrics(kSystemMetricScreenHeight));
  app_chrome_create_t cfg = {menubar_proc, menus, menu_count, toolbar_proc};
  window_t *win = create_window(title ? title : "Application Chrome",
      WINDOW_NOTITLE | WINDOW_TRANSPARENT | WINDOW_NOFILL | WINDOW_ALWAYSONTOP |
      WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE | WINDOW_NODRAG,
      MAKERECT(0, menubar_proc ? 0 : MENUBAR_HEIGHT, sw, sh - (menubar_proc ? 0 : MENUBAR_HEIGHT)),
      NULL, win_app_chrome, hinstance, &cfg);
  if (win) show_window(win, true);
  return win;
}

window_t *app_chrome_menubar(window_t *chrome) {
  app_chrome_state_t *st = chrome ? (app_chrome_state_t *)chrome->userdata : NULL;
  return st ? st->menubar : NULL;
}

window_t *app_chrome_toolbar(window_t *chrome) {
  for (window_t *bar = chrome ? chrome->children : NULL; bar; bar = bar->next)
    if (bar->toolbar_dock == TOOLBAR_DOCK_TOP || bar->toolbar_dock == TOOLBAR_DOCK_MENU) return bar;
  return NULL;
}

window_t *create_application_chrome(const char *title, winproc_t menubar_proc,
                                    const menu_def_t *menus, int menu_count,
                                    winproc_t toolbar_proc, const application_toolbar_t *toolbar,
                                    hinstance_t hinstance) {
  if (!toolbar || toolbar->count < 0 || (toolbar->count && !toolbar->items) ||
      (toolbar->presentation != TOOLBAR_PRESENTATION_NORMAL && toolbar->presentation != TOOLBAR_PRESENTATION_COMPACT)) {
    fprintf(stderr, "[chrome] invalid application toolbar definition=%p\n", (void *)toolbar);
    fflush(stderr);
    return NULL;
  }
  window_t *chrome = create_app_chrome(title, menubar_proc, menus, menu_count, toolbar_proc, hinstance);
  if (!chrome) return NULL;
  app_chrome_state_t *st = chrome->userdata;
  st->presentation = toolbar->presentation;
  send_message(app_chrome_toolbar(chrome), tbSetItems, toolbar->count, (void *)toolbar->items);
  fprintf(stderr, "[chrome] application toolbar win=%u count=%d presentation=%d\n",
          chrome->id, toolbar->count, toolbar->presentation);
  fflush(stderr);
  app_chrome_resize_children(chrome);
  return chrome;
}

window_t *app_chrome_add_toolbar(window_t *chrome, toolbar_dock_t dock, winproc_t proc) {
  window_t *bar = create_docked_toolbar(chrome, dock, proc);
  if (bar) app_chrome_resize_children(chrome);
  return bar;
}
