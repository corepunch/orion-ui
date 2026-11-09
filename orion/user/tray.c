// Tray bar window — shows a taskbar button for each top-level window.
// Moved from commctl/ to user/ because the tray is part of the window manager
// shell, not an application-level common control.

#include "user.h"
#include "messages.h"
#include "draw.h"

#define TRAY_HEIGHT (BUTTON_HEIGHT + 4)
#define TRAY_SPACING 4
#define TRAY_START_X 22

#define TRAY_CONTAINS(px, py, rc) \
  ((px) >= (rc).x && (py) >= (rc).y && \
   (px) < (rc).x + (rc).w && (py) < (rc).y + (rc).h)

typedef struct {
  window_t *hot;
  window_t *pressed;
} tray_state_t;

static bool tray_is_task_window(window_t *tray, window_t *w) {
  return w && !w->parent && w != tray && !(w->flags & WINDOW_NOTRAYBUTTON);
}

static int tray_button_width(const window_t *w) {
  int tw = text_strwidth("Text", w->title);
  int bw = tw + 12;
  if (bw < 56) bw = 56;
  return bw;
}

static window_t *tray_hit_test(window_t *tray, int x, int y, irect16_t *out_rect) {
  int bx = TRAY_START_X;
  irect16_t r = {0};
  for (window_t *it = g_ui_runtime.windows; it; it = it->next) {
    if (!tray_is_task_window(tray, it)) continue;
    r = (irect16_t){bx, 2, tray_button_width(it), BUTTON_HEIGHT};
    if (TRAY_CONTAINS(x, y, r)) {
      if (out_rect) *out_rect = r;
      return it;
    }
    bx += r.w + TRAY_SPACING;
  }
  if (out_rect) *out_rect = r;
  return NULL;
}

static void on_win_created(window_t *win, uint32_t msg, uint32_t wparam, void *lparam, void *userdata) {
  (void)msg; (void)wparam; (void)lparam;
  window_t *tray = userdata;
  if (tray_is_task_window(tray, win)) {
    invalidate_window(tray);
  }
}

static void on_win_destroyed(window_t *win, uint32_t msg, uint32_t wparam, void *lparam, void *userdata) {
  (void)msg; (void)wparam; (void)lparam;
  window_t *tray = userdata;
  if (tray_is_task_window(tray, win)) {
    invalidate_window(tray);
  }
}

result_t win_tray(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  tray_state_t *st = (tray_state_t *)win->userdata;
  switch (msg) {
    case evCreate: {
      allocate_window_data(win, sizeof(tray_state_t));
      st = (tray_state_t *)win->userdata;
      win->frame = (irect16_t){
        0,
        ui_get_system_metrics(kSystemMetricScreenHeight) - TRAY_HEIGHT,
        ui_get_system_metrics(kSystemMetricScreenWidth),
        TRAY_HEIGHT
      };
      register_window_hook(evCreate, on_win_created, win);
      register_window_hook(evDestroy, on_win_destroyed, win);
      return true;
    }
    case evPaint:
      draw_sysicon("orion-mark", 3, (TRAY_HEIGHT - 16) / 2, 16, get_sys_color(brAccent));   // Orion's crystal
      {
        window_t *active = g_ui_runtime.focused
            ? get_root_window(g_ui_runtime.focused) : NULL;
        int bx = TRAY_START_X;
        for (window_t *it = g_ui_runtime.windows; it; it = it->next) {
          if (!tray_is_task_window(win, it)) continue;
          int bw = tray_button_width(it);
          irect16_t r = {bx, 2, bw, BUTTON_HEIGHT};
          bool pressed = (st && st->pressed == it);
          bool down = pressed || (it == active);
          draw_button(r, 1, 1, down);
          draw_text("Text", it->title, r.x + 5, r.y + 4,
                          get_sys_color(brTextNormal));
          bx += bw + TRAY_SPACING;
        }
      }
      return true;
    case evLeftButtonDown: {
      int x = (int16_t)LOWORD(wparam);
      int y = (int16_t)HIWORD(wparam);
      window_t *hit = tray_hit_test(win, x, y, NULL);
      if (!st) return true;
      st->pressed = hit;
      st->hot = hit;
      track_mouse(win);
      invalidate_window(win);
      return true;
    }
    case evMouseMove: {
      int x = (int16_t)LOWORD(wparam);
      int y = (int16_t)HIWORD(wparam);
      window_t *hit = tray_hit_test(win, x, y, NULL);
      if (st && st->hot != hit) {
        st->hot = hit;
        invalidate_window(win);
      }
      return true;
    }
    case evMouseLeave:
      if (st && st->hot) {
        st->hot = NULL;
        invalidate_window(win);
      }
      return true;
    case evLeftButtonUp: {
      int x = (int16_t)LOWORD(wparam);
      int y = (int16_t)HIWORD(wparam);
      window_t *hit = tray_hit_test(win, x, y, NULL);
      window_t *pressed = st ? st->pressed : NULL;
      if (st) st->pressed = NULL;
      if (pressed && pressed == hit) {
        show_window(pressed, !window_has_state(pressed, WINDOW_STATE_VISIBLE));
      }
      invalidate_window(win);
      return true;
    }
    case evDestroy:
      deregister_window_hook(evCreate, on_win_created, win);
      deregister_window_hook(evDestroy, on_win_destroyed, win);
      return true;
    default:
      break;
  }
  return false;
}
