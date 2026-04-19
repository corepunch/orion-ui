#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"

#define TRAY_HEIGHT (BUTTON_HEIGHT+4)
#define SPACING 4

typedef enum {
  icon16_select,
  icon16_points,
  icon16_lines,
  icon16_sectors,
  icon16_things,
  icon16_sounds,
  icon16_appicon,
  icon16_count,
} ed_icon16_t;

result_t win_button(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

void create_button(window_t *tray, window_t *window) {
  rect_t r = { tray->cursor_pos, 2, 0, 12 };
  window_t *button = create_window(window->title, 0, &r, tray, win_button, 0, window);
  tray->cursor_pos += button->frame.w + SPACING;
  button->userdata = window;
}

static void on_win_created(window_t *win, uint32_t msg, uint32_t wparam, void *lparam, void *userdata) {
  if (!win->parent && !(win->flags&WINDOW_NOTRAYBUTTON)) {
    create_button(userdata, win);
  }
}

static void on_win_destroyed(window_t *win, uint32_t msg, uint32_t wparam, void *lparam, void *userdata) {
  if (!win->parent) {
    window_t *tray = userdata;
    window_t *button = 0;
    if (!tray->children) {
      return;
    }    
    if (win == tray->children->userdata) {
      button = tray->children;
      tray->children = button->next;
    } else {
      for (window_t *b = tray->children, *n = b->next; n; b = n, n = n->next) {
        if (n->userdata == win) {
          button = n;
          b->next = n->next;
          break;
        }
      }
    }
    if (!button) {
//      printf("Can't find button for window %s\n", win->title);
      return;
    }
    for (window_t *it = button->next; it; it = it->next) {
      it->frame.x -= button->frame.w + SPACING;
    }
    tray->cursor_pos -= button->frame.w + SPACING;
    destroy_window(button);
    invalidate_window(tray);
  }
}

result_t win_tray(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case kWindowMessageCreate:
      win->cursor_pos = 22;
      win->frame = (rect_t){0,ui_get_system_metrics(kSystemMetricScreenHeight)-TRAY_HEIGHT,ui_get_system_metrics(kSystemMetricScreenWidth),TRAY_HEIGHT};
      register_window_hook(kWindowMessageCreate, on_win_created, win);
      register_window_hook(kWindowMessageDestroy, on_win_destroyed, win);
      return true;
    case kWindowMessagePaint:
      draw_icon16(icon16_appicon, 4, 1, get_sys_color(kColorDarkEdge));
      draw_icon16(icon16_appicon, 3, 0, get_sys_color(kColorTextNormal));
      return false;
    case kWindowMessageCommand:
      if (HIWORD(wparam) == kButtonNotificationClicked) {
        window_t *button = lparam;
        show_window(button->userdata, !((window_t *)button->userdata)->visible);
      }
      return true;
    case kWindowMessageDestroy:
      deregister_window_hook(kWindowMessageCreate, on_win_created, win);
      deregister_window_hook(kWindowMessageDestroy, on_win_destroyed, win);
      return true;
    default:
      break;
  }
  return false;
}
