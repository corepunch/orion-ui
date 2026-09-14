#include <stdlib.h>

#include "appchrome.h"
#include <orion/user/draw.h>
#include <orion/user/toolbar.h>

typedef struct {
  window_t *menubar;
  window_t *toolbar;
} app_chrome_state_t;

typedef struct {
  winproc_t         menubar_proc;
  const menu_def_t *menus;
  int               menu_count;
  winproc_t         toolbar_proc;
} app_chrome_create_t;

static void app_chrome_resize_children(window_t *win, int width) {
  app_chrome_state_t *st = (app_chrome_state_t *)win->userdata;
  if (!st) return;
  if (st->menubar) resize_window(st->menubar, width, MENUBAR_HEIGHT);
  if (st->toolbar) resize_window(st->toolbar, width, TOOLBAR_BAND_HEIGHT);
}

static result_t win_app_chrome(window_t *win, uint32_t msg,
                               uint32_t wparam, void *lparam) {
  app_chrome_state_t *st = (app_chrome_state_t *)win->userdata;
  switch (msg) {
    case evCreate: {
      app_chrome_create_t *cfg = (app_chrome_create_t *)lparam;
      st = allocate_window_data(win, sizeof(*st));
      if (!cfg || !cfg->menubar_proc || !cfg->toolbar_proc) return false;
      st->menubar = create_window("menubar", WINDOW_NOTITLE | WINDOW_NORESIZE,
                                  MAKERECT(0, 0, win->frame.w, MENUBAR_HEIGHT),
                                  win, cfg->menubar_proc, 0, NULL);
      st->toolbar = create_window("Toolbar", WINDOW_TOOLBAR | WINDOW_NOTITLE | WINDOW_NORESIZE,
                                  MAKERECT(0, MENUBAR_HEIGHT, win->frame.w, TOOLBAR_BAND_HEIGHT),
                                  win, cfg->toolbar_proc, 0, NULL);
      if (st->menubar)
        send_message(st->menubar, kMenuBarMessageSetMenus,
                     (uint32_t)cfg->menu_count, (void *)cfg->menus);
      return st->menubar && st->toolbar;
    }
    case evPaint:
      if (st && st->menubar) send_message(st->menubar, evPaint, wparam, lparam);
      if (st && st->toolbar) {
        toolbar_draw_non_client(st->toolbar);
      }
      return true;
    case tbButtonClick:
      return st && st->toolbar
             ? send_message(st->toolbar, msg, wparam, lparam)
             : false;
    case evDisplayChange: {
      int width = LOWORD(wparam);
      win->frame.w = width;
      app_chrome_resize_children(win, width);
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
  app_chrome_create_t cfg = {menubar_proc, menus, menu_count, toolbar_proc};
  window_t *win = create_window(title ? title : "Application Chrome",
      WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_ALWAYSONTOP |
      WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE | WINDOW_NODRAG,
      MAKERECT(0, 0, sw, MENUBAR_HEIGHT + TOOLBAR_BAND_HEIGHT),
      NULL, win_app_chrome, hinstance, &cfg);
  if (win) show_window(win, true);
  return win;
}

window_t *app_chrome_menubar(window_t *chrome) {
  app_chrome_state_t *st = chrome ? (app_chrome_state_t *)chrome->userdata : NULL;
  return st ? st->menubar : NULL;
}

window_t *app_chrome_toolbar(window_t *chrome) {
  app_chrome_state_t *st = chrome ? (app_chrome_state_t *)chrome->userdata : NULL;
  return st ? st->toolbar : NULL;
}
