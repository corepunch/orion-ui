// Platform event handling and dispatch
// Translates platform (AXmessage) events into Orion window messages.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>

#include "user.h"
#include "messages.h"
#include "rect.h"
#include "toolbar.h"
#include <orion/kernel/kernel.h>

// External functions
extern intptr_t send_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
extern void post_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
extern void move_window(window_t *win, int x, int y);
extern void resize_window(window_t *win, int new_w, int new_h);
extern window_t *find_window(int x, int y);
extern void set_focus(window_t* win);
extern void track_mouse(window_t *win);
extern void show_window(window_t *win, bool visible);
extern void end_dialog(window_t *win, uint32_t code);
extern void invalidate_window(window_t *win);
extern void destroy_window(window_t *win);
extern int titlebar_height(window_t const *win);
// Window-liveness check — defined in user/message.c; used to guard posted events.
extern bool is_valid_window_ptr(window_t *target, window_t *list);

// Macros for coordinate conversion (platform logical → Orion logical)
#define SCALE_POINT(x) ((x)/UI_WINDOW_SCALE)

// Absolute screen origin of the client area for a window. Child frames are
// parent-relative, so nested windows must include every ancestor offset.
static inline int win_abs_x(window_t *w) {
  return w->parent ? window_screen_x(w) : w->frame.x;
}
static inline int win_abs_y(window_t *w) {
  if (!w->parent) return w->frame.y + titlebar_height(w);
  return window_screen_y(w);
}

#define LOCAL_X(px, py, WIN) (SCALE_POINT(px) - win_abs_x(WIN) + (WIN)->hscroll.pos)
#define LOCAL_Y(px, py, WIN) (SCALE_POINT(py) - win_abs_y(WIN) + (WIN)->vscroll.pos)
#define CONTAINS(x, y, x1, y1, w1, h1) \
((x1) <= (x) && (y1) <= (y) && (x1) + (w1) > (x) && (y1) + (h1) > (y))

// Sentinel object — any event posted with this target is a wakeup-only event
// and should be silently discarded by dispatch_message.
static int g_wakeup_sentinel;

// Coalescing flag: true while a sentinel is already queued in the platform
// event queue so that wake_event_loop() does not post a second one.
// Cleared when get_message() consumes the sentinel.
static bool g_wakeup_pending = false;

// Current modifier state (updated on key and modifier-only events)
static uint32_t g_mod_state = 0;
static bool g_key_state[256];

static uint32_t normalize_key_code(uint32_t key) {
  return key >= 'a' && key <= 'z' ? key - ('a' - 'A') : key;
}

uint32_t ui_get_mod_state(void) {
  return g_mod_state;
}

bool ui_is_key_down(uint32_t key) {
  key = normalize_key_code(key);
  return key < ARRAY_LEN(g_key_state) && g_key_state[key];
}

static void update_key_state(ui_event_t *msg) {
  uint32_t key;
  switch (msg->message) {
    case kEventKeyDown:
    case kEventKeyUp:
      key = normalize_key_code((uint32_t)msg->keyCode);
      if (key < ARRAY_LEN(g_key_state)) g_key_state[key] = msg->message == kEventKeyDown;
      g_mod_state = (uint32_t)msg->wParam & 0xFFFF0000u;
      break;
    case kEventModifiersChanged:
      g_mod_state = (uint32_t)msg->wParam & 0xFFFF0000u;
      break;
    case kEventKillFocus:
      memset(g_key_state, 0, sizeof(g_key_state));
      g_mod_state = 0;
      return;
    default:
      return;
  }
  g_key_state[AX_KEY_SHIFT] = (g_mod_state & AX_MOD_SHIFT) != 0;
  g_key_state[AX_KEY_CTRL]  = (g_mod_state & AX_MOD_CTRL)  != 0;
  g_key_state[AX_KEY_ALT]   = (g_mod_state & AX_MOD_ALT)   != 0;
}

// Drag/resize state (shared with user/window.c for destroy_window cleanup)
static int drag_anchor[2];
static int resize_anchor[2];
static window_t *gesture_target;
static uint32_t gesture_target_id;
static bool gesture_scroll;
static window_t *pointer_target;
static uint32_t pointer_target_id;

// Window that received evNCLeftButtonDown (toolbar press).
// Always delivered evNCLeftButtonUp on the next left-up,
// regardless of release position, so pressed state is cleared deterministically.
// Shared with user/window.c for destroy_window cleanup (stored in g_ui_runtime).

// Handle mouse events on child windows.
result_t send_pointer_message(window_t *win, uint32_t msg, uint32_t point, void *lparam) {
  if (win && win->view.enabled && (msg == evMouseMove || msg == evLeftButtonDown ||
      msg == evLeftButtonUp || msg == evLeftButtonDoubleClick || msg == evRightButtonDown || msg == evRightButtonUp)) {
    ipoint16_t client = {(int16_t)LOWORD(point), (int16_t)HIWORD(point)};
    win->view.pointer = client;
    ipoint16_t content = window_client_to_content(win, client);
    point = MAKEDWORD(content.x, content.y);
    if (msg == evMouseMove && lparam) {
      ipoint16_t previous = {client.x - (int16_t)LOWORD((uintptr_t)lparam), client.y - (int16_t)HIWORD((uintptr_t)lparam)};
      previous = window_client_to_content(win, previous);
      lparam = (void *)(intptr_t)MAKEDWORD(content.x - previous.x, content.y - previous.y);
    }
  }
  return send_message(win, msg, point, lparam);
}

// Delivery coordinates include the owner's scroll offset. Child frames are
// fixed in its viewport, so remove that offset before descending.
static int handle_mouse(int msg, window_t *win, int x, int y, void *lparam) {
  if (win == g_ui_runtime.captured) return false;
  x -= win->hscroll.pos;
  y -= win->vscroll.pos;
  if (!rect_contains_point(get_client_rect(win), (ipoint16_t){x, y})) return false;
  for (window_t *c = win->children; c; c = c->next) {
    if (!window_has_state(c, WINDOW_STATE_VISIBLE)) continue;
    if (!CONTAINS(x, y, c->frame.x, c->frame.y, c->frame.w, c->frame.h))
      continue;
    // Hit-test in viewport space, then deliver in the child's content space.
    // Controls should not need to know whether an event came directly from a
    // root window or through one or more nested layout containers.
    int lx = x - c->frame.x + (int)c->hscroll.pos;
    int ly = y - c->frame.y + (int)c->vscroll.pos;
    ax_gesture_t local;
    void *payload = lparam;
    if (msg == evGesture && lparam) {
      local = *(ax_gesture_t *)lparam;
      float dx = -win->hscroll.pos - c->frame.x + c->hscroll.pos;
      float dy = -win->vscroll.pos - c->frame.y + c->vscroll.pos;
      local.x += dx; local.previous_x += dx;
      local.y += dy; local.previous_y += dy;
      payload = &local;
    }
    if (handle_mouse(msg, c, lx, ly, payload))
      return true;
    if (send_pointer_message(c, msg, MAKEDWORD(lx, ly), payload)) {
      if (msg == evGesture) { gesture_target = c; gesture_target_id = c->id; }
      if (msg == evLeftButtonDown) { pointer_target = c; pointer_target_id = c->id; }
      return true;
    }
  }
  return false;
}

// Hit-test a toolbar child using screen-space coordinates.
// Returns NULL when the point is outside the toolbar band or in a gap.
// Toolbar mouse handling: the toolbar host window (win->toolbar) processes
// owner-drawn item clicks instead of delegating to child windows.
// This function returns a pointer to the toolbar host window if the click
// was over the toolbar strip, or NULL if not.
static window_t *find_toolbar_host_at(window_t *parent, int sx, int sy) {
  if (!parent || !(parent->flags & WINDOW_TOOLBAR)) return NULL;
  if (sy < window_screen_y(parent) ||
      sy >= window_screen_y(parent) + titlebar_height(parent)) return NULL;
  // Toolbar is above the titlebar height line — hit is valid.
  // Return the toolbar host so the click can be routed to its mouse handler.
  return parent->toolbar;
}

// Find next tab stop
window_t* find_next_tab_stop(window_t *win, bool allow_current) {
  if (!win) return false;
  window_t *next;
  if ((next = find_next_tab_stop(win->children, true))) return next;
  if (!(win->flags & WINDOW_NOTABSTOP) &&
      (win->parent || window_has_state(win, WINDOW_STATE_VISIBLE)) &&
      allow_current) return win;
  if ((next = find_next_tab_stop(win->next, true))) return next;
  return allow_current ? NULL : find_next_tab_stop(win->parent, false);
}

// Find previous tab stop
window_t* find_prev_tab_stop(window_t* win) {
  window_t *it = (win = (win->parent ? win : find_next_tab_stop(win, false)));
  for (window_t *next = find_next_tab_stop(it, false); next != win;
       it = next, next = find_next_tab_stop(next, false));
  return it;
}

// Move window to top of Z-order.
//
// For system/unowned windows (hinstance == 0), system WINDOW_ALWAYSONTOP
// windows stay above other system windows. Non-topmost system windows are
// inserted below system (h==0) ALWAYSONTOP windows, rather than below every
// ALWAYSONTOP window globally.
//
// For app windows (hinstance != 0) the clicked window's entire app group is
// brought to front, but only up to just below any system (h==0) ALWAYSONTOP
// windows (shell menu bar, popup menus, etc.).  Within the app group, normal
// windows come first, WINDOW_ALWAYSONTOP windows come next, and WINDOW_DIALOG
// windows come last, so modal dialogs stay above their app's palettes while
// remaining below active shell menus and below any other app's windows when
// that app is active.
void move_to_top(window_t* _win) {
  extern window_t *get_root_window(window_t *window);

  window_t *win = get_root_window(_win);
  invalidate_window(win);

  if (win->flags & WINDOW_ALWAYSINBACK)
    return;

  hinstance_t h = win->hinstance;

  if (h == 0) {
    // System/unowned window — original global ALWAYSONTOP behaviour.
    window_t **head = &g_ui_runtime.windows, *p = NULL, *n = *head;

    while (n != win) {
      p = n;
      n = n->next;
      if (!n) return;
    }

    if (p) p->next = win->next;
    else *head = win->next;

    if (!*head) {
      *head = win;
      win->next = NULL;
      return;
    }

    if (win->flags & (WINDOW_ALWAYSONTOP | WINDOW_DIALOG)) {
      // Append to absolute tail — globally on top of everything. Standalone
      // apps use hinstance 0, so dialogs must also sort after their palettes.
      window_t *tail = *head;
      while (tail->next)
        tail = tail->next;
      tail->next = win;
      win->next = NULL;
    } else {
      // Insert before the first system (h==0) ALWAYSONTOP window so that
      // system ALWAYSONTOP windows always stay visually on top.
      window_t *prev = NULL, *cur = *head;
      while (cur && !(cur->hinstance == 0 && (cur->flags & WINDOW_ALWAYSONTOP))) {
        prev = cur;
        cur  = cur->next;
      }
      win->next = cur;
      if (prev) prev->next = win;
      else      *head      = win;
    }
    return;
  }

  // App window (h != 0): bring the entire app group to the front of the
  // app-window section, which sits below system (h==0) ALWAYSONTOP windows.
  //
  // The group is ordered: normals first (clicked window last = on top),
  // then ALWAYSONTOP windows, then WINDOW_DIALOG windows. Dialogs are last so
  // modal file/message pickers cover same-app floating palettes.

  // Step 1: Extract all windows of this app from the global list.
  window_t *n_head = NULL, *n_tail = NULL;  // normal windows sublist
  window_t *t_head = NULL, *t_tail = NULL;  // ALWAYSONTOP windows sublist
  window_t *d_head = NULL, *d_tail = NULL;  // dialog windows sublist

  window_t *prev = NULL, *cur = g_ui_runtime.windows;
  while (cur) {
    window_t *next = cur->next;
    if (cur->hinstance == h && !(cur->flags & WINDOW_ALWAYSINBACK)) {
      // Remove from the global list.
      if (prev) prev->next = next;
      else      g_ui_runtime.windows    = next;
      cur->next = NULL;

      if (cur != win) {
        // Append all other app windows to their respective sublists now;
        // win itself is appended last (after the loop) so it ends up on top.
        if (cur->flags & WINDOW_DIALOG) {
          if (d_tail) d_tail->next = cur; else d_head = cur;
          d_tail = cur;
        } else if (cur->flags & WINDOW_ALWAYSONTOP) {
          if (t_tail) t_tail->next = cur; else t_head = cur;
          t_tail = cur;
        } else {
          if (n_tail) n_tail->next = cur; else n_head = cur;
          n_tail = cur;
        }
      }
      // prev stays unchanged — cur was removed from the list.
    } else {
      prev = cur;
    }
    cur = next;
  }

  // Append win at the END of its sublist so it is topmost within the group.
  if (win->flags & WINDOW_DIALOG) {
    if (d_tail) d_tail->next = win; else d_head = win;
    d_tail = win;
  } else if (win->flags & WINDOW_ALWAYSONTOP) {
    if (t_tail) t_tail->next = win; else t_head = win;
    t_tail = win;
  } else {
    if (n_tail) n_tail->next = win; else n_head = win;
    n_tail = win;
  }
  win->next = NULL;

  // Chain the sublists: normals -> topmost -> dialogs.
  if (n_tail) n_tail->next = t_head;
  if (t_tail) t_tail->next = d_head;
  else if (n_tail) n_tail->next = d_head;
  window_t *group_head = n_head ? n_head : (t_head ? t_head : d_head);
  window_t *group_tail = d_tail ? d_tail : (t_tail ? t_tail : n_tail);

  if (!group_head) return;

  // Step 2: Find the insertion point — just before the first system (h==0)
  // ALWAYSONTOP window so the shell menu bar / popups stay globally on top.
  window_t *ins_prev = NULL;
  cur = g_ui_runtime.windows;
  while (cur && !(cur->hinstance == 0 && (cur->flags & WINDOW_ALWAYSONTOP))) {
    ins_prev = cur;
    cur = cur->next;
  }

  // Insert the app group at the insertion point.
  group_tail->next = cur;
  if (ins_prev) ins_prev->next = group_head;
  else          g_ui_runtime.windows        = group_head;

  // Invalidate every window in the moved group so previously-occluded windows
  // repaint correctly now that the group has come to the front.
  for (window_t *gw = group_head; gw != cur; gw = gw->next)
    invalidate_window(gw);
}

// Dispatch a platform AXmessage to the Orion window system.
void dispatch_message(ui_event_t *msg) {
  // Sentinel events are wakeup-only — clear the pending flag and skip.
  // get_message() already filters sentinels (returning 0); this guard handles
  // sentinels that arrive via the repost_messages() drain loop.
  if (msg->target == (void *)&g_wakeup_sentinel) {
    g_wakeup_pending = false;
    return;
  }

  update_key_state(msg);

  window_t *win;
  int px, py; // platform logical coordinates

  switch (msg->message) {

    case kEventDragDrop: {
      const char *path = (const char *)msg->lParam;
      if (path && path[0]) {
        ui_open_file(path);
      }
      if (msg->lParam) {
        free(msg->lParam);
      }
      break;
    }

    case kEventWindowClosed:
      g_ui_runtime.running = false;
      break;

    case kEventWindowResized: {
      int new_w = (int)LOWORD(msg->wParam);
      int new_h = (int)HIWORD(msg->wParam);
      ui_update_screen_size(new_w, new_h);
      int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
      int sh = ui_get_system_metrics(kSystemMetricScreenHeight);
      // ALWAYSINBACK windows (backdrop/desktop fills) are auto-resized to the
      // new screen size.  All other root windows receive evDisplayChange so they
      // can adjust their own layout/geometry without being force-resized.
      for (win = g_ui_runtime.windows; win; win = win->next) {
        if (!win->parent) {
          if (win->flags & WINDOW_ALWAYSINBACK) {
            resize_window(win, sw, sh);
          } else {
            send_message(win, evDisplayChange,
                         MAKEDWORD(sw, sh), NULL);
            update_maximized_window(win);
          }
        }
      }
      for (win = g_ui_runtime.windows; win; win = win->next) {
        if (window_has_state(win, WINDOW_STATE_VISIBLE)) {
          invalidate_window(win);
        }
      }
      break;
    }

    case kEventChar: {
      // Some platforms send kEventChar as a separate text-input event.
      char ch = *(char*)&msg->lParam;
      if (ch != '\0') {
        char buf[2] = { ch, '\0' };
        send_message(g_ui_runtime.focused, evTextInput, 0, buf);
      }
      break;
    }

    case kEventKeyDown: {
      uint32_t key = normalize_key_code((uint32_t)msg->keyCode);
      // Send text input for printable characters (ASCII 32–126).
      // The char bytes are stored inline in the lParam field by the platform.
      char text_ch = *(char*)&msg->lParam;
      if (text_ch >= 0x20 && text_ch != 0x7f) {
        char buf[2] = { text_ch, '\0' };
        send_message(g_ui_runtime.focused, evTextInput, 0, buf);
      }
      if (g_ui_runtime.focused && !send_message(g_ui_runtime.focused, evKeyDown, key, NULL)) {
        if (key == AX_KEY_TAB) {
          if (msg->modflags & (AX_MOD_SHIFT >> 16)) {
            set_focus(find_prev_tab_stop(g_ui_runtime.focused));
          } else {
            set_focus(find_next_tab_stop(g_ui_runtime.focused, false));
          }
        } else if (key == AX_KEY_ENTER) {
          window_t *def = find_default_button(get_root_window(g_ui_runtime.focused));
          if (def) {
            send_message(def, evLeftButtonDown, 0, NULL);
            send_message(def, evLeftButtonUp, 0, NULL);
          }
        } else if (key == AX_KEY_F12) {
          char path[1024];
          time_t now = time(NULL);
          struct tm *tm_now = localtime(&now);
          if (tm_now) {
            snprintf(path, sizeof(path), "%s/screenshot_%04d%02d%02d_%02d%02d%02d.jpg",
                     axSettingsDirectory(),
                     tm_now->tm_year + 1900, tm_now->tm_mon + 1, tm_now->tm_mday,
                     tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);
            ui_request_screenshot(path, 90, false);
          }
        }
      }
      break;
    }

    case kEventKeyUp:
      send_message(g_ui_runtime.focused, evKeyUp,
                   normalize_key_code((uint32_t)msg->keyCode), NULL);
      break;

    case kEventModifiersChanged:
      break;

    case kEventJoyAxisMotion:
      send_message(g_ui_runtime.focused, evJoyAxisMotion,
                   MAKEDWORD(msg->wParam & 0xFF, (uint16_t)(intptr_t)msg->lParam), NULL);
      break;

    case kEventJoyButtonDown:
      send_message(g_ui_runtime.focused, evJoyButtonDown, msg->wParam, NULL);
      break;

    case kEventMouseMoved:
    case kEventLeftButtonDragged:
    case kEventRightButtonDragged:
    case kEventOtherButtonDragged: {
      px = (int)msg->x;
      py = (int)msg->y;
      // a. Record last known pointer position so scroll handlers can resync hover.
      g_ui_runtime.last_mouse_sx = SCALE_POINT(px);
      g_ui_runtime.last_mouse_sy = SCALE_POINT(py);
      int16_t rdx = msg->dx;
      int16_t rdy = msg->dy;
      if (g_ui_runtime.dragging) {
        move_window(g_ui_runtime.dragging,
                    SCALE_POINT(px) - drag_anchor[0],
                    SCALE_POINT(py) - drag_anchor[1]);
      } else if (g_ui_runtime.resizing) {
        int new_w = SCALE_POINT(px) - resize_anchor[0] - g_ui_runtime.resizing->frame.x;
        int new_h = SCALE_POINT(py) - resize_anchor[1] - g_ui_runtime.resizing->frame.y;
        resize_window(g_ui_runtime.resizing, new_w, new_h);
      } else if (((win = g_ui_runtime.captured) ||
                  (win = find_window(SCALE_POINT(px), SCALE_POINT(py)))))
      {
        if (window_has_state(win, WINDOW_STATE_DISABLED)) return;
        int16_t lx = (int16_t)LOCAL_X(px, py, win);
        int16_t ly = (int16_t)LOCAL_Y(px, py, win);
        if (win == g_ui_runtime.captured || ly >= 0) {
          void *motion = (void*)(intptr_t)MAKEDWORD(rdx, rdy);
          if (win == g_ui_runtime.captured ||
              !handle_mouse(evMouseMove, win, lx, ly, motion))
            send_pointer_message(win, evMouseMove, MAKEDWORD(lx, ly), motion);
        }
      }
      if (g_ui_runtime.tracked && !CONTAINS(SCALE_POINT(px), SCALE_POINT(py),
                                window_screen_x(g_ui_runtime.tracked),
                                window_screen_y(g_ui_runtime.tracked),
                                g_ui_runtime.tracked->frame.w, g_ui_runtime.tracked->frame.h))
      {
        track_mouse(NULL);
      }
      // Tooltip update: only on plain mouse movement (not drag / resize / capture).
      if (msg->message == kEventMouseMoved &&
          !g_ui_runtime.dragging && !g_ui_runtime.resizing &&
          !g_ui_runtime.captured)
      {
        int sx = SCALE_POINT(px), sy = SCALE_POINT(py);
        window_t *hover = find_window(sx, sy);
        if (hover && !window_has_state(hover, WINDOW_STATE_DISABLED)) {
          // Route toolbar mousemove to the host window for hover tracking
          window_t *tb_host = find_toolbar_host_at(hover, sx, sy);
          // b. Deliver evMouseLeave to the toolbar host when the pointer leaves
          // the toolbar band (tb_host transitions from non-NULL to NULL or changes).
          // win_toolbar does not call track_mouse, so the generic track_mouse
          // mechanism cannot drive this transition automatically.
          if (tb_host != g_ui_runtime.tracked_toolbar) {
            if (g_ui_runtime.tracked_toolbar)
              send_message(g_ui_runtime.tracked_toolbar, evMouseLeave, 0, NULL);
            g_ui_runtime.tracked_toolbar = tb_host;
          }
          if (tb_host) {
            int title_h = (hover->flags & WINDOW_NOTITLE) ? 0 : TITLEBAR_HEIGHT;
            int tb_x = sx - hover->frame.x;
            int tb_y = sy - (hover->frame.y + title_h);
            send_message(tb_host, evMouseMove,
                         MAKEDWORD((uint16_t)tb_x, (uint16_t)tb_y), NULL);
            char tip_buf[256] = {0};
            if (send_message(tb_host, evGetTooltipText,
                             MAKEDWORD((uint16_t)tb_x, (uint16_t)tb_y),
                             tip_buf) && tip_buf[0]) {
              tooltip_update(tb_host, tip_buf, sx, sy);
            } else {
              tooltip_update(NULL, NULL, sx, sy);
            }
          } else {
            char tip_buf[256] = {0};
            int lx_h = (int16_t)LOCAL_X(px, py, hover);
            int ly_h = (int16_t)LOCAL_Y(px, py, hover);
            if (send_message(hover, evGetTooltipText,
                             MAKEDWORD((uint16_t)lx_h, (uint16_t)ly_h),
                             tip_buf) && tip_buf[0]) {
              tooltip_update(hover, tip_buf, sx, sy);
            } else {
              tooltip_update(NULL, NULL, sx, sy);
            }
          }
        } else {
          // Mouse is outside any interactive window; clear toolbar hover too.
          if (g_ui_runtime.tracked_toolbar) {
            send_message(g_ui_runtime.tracked_toolbar, evMouseLeave, 0, NULL);
            g_ui_runtime.tracked_toolbar = NULL;
          }
          tooltip_update(NULL, NULL, sx, sy);
        }
        // Cursor shape update: query the hovered window for the desired cursor.
        if (hover && !window_has_state(hover, WINDOW_STATE_DISABLED)) {
          window_t *root = hover;
          while (root->parent) root = root->parent;
          int root_lx = sx - root->frame.x;
          int root_ly = sy - root->frame.y;
          int cursor_id = curArrow;
          if (root_lx >= root->frame.w - SCROLLBAR_WIDTH &&
              root_ly >= root->frame.h - SCROLLBAR_WIDTH &&
              !(root->flags & WINDOW_NORESIZE) &&
              root->parent)
          {
            cursor_id = curResizeNWSE;
          } else {
            int lx_c = (int16_t)LOCAL_X(px, py, hover);
            int ly_c = (int16_t)LOCAL_Y(px, py, hover);
            cursor_id = (int)send_message(hover, evGetCursor,
                                          MAKEDWORD((uint16_t)lx_c, (uint16_t)ly_c), NULL);
          }
          axSetCursor(cursor_id);
        } else {
          axSetCursor(curArrow);
        }
      }
      break;
    }

    case kEventGesture: {
      ax_gesture_t gesture = msg->gesture;
      if (!isfinite(gesture.x) || !isfinite(gesture.y) || !isfinite(gesture.previous_x) ||
          !isfinite(gesture.previous_y) || !isfinite(gesture.scale) || gesture.scale <= 0 ||
          !isfinite(gesture.rotation) || gesture.phase > AX_GESTURE_CANCEL) {
        fprintf(stderr, "[input] invalid gesture phase=%u scale=%f rotation=%f\n", gesture.phase, gesture.scale, gesture.rotation);
        fflush(stderr);
        return;
      }
      if (gesture.phase == AX_GESTURE_BEGIN) {
        if (gesture_target && is_window(gesture_target) && gesture_target->id == gesture_target_id && !gesture_scroll) {
          ax_gesture_t cancel = {.phase = AX_GESTURE_CANCEL, .scale = 1};
          send_message(gesture_target, evGesture, 0, &cancel);
        }
        gesture_target = NULL;
        gesture_scroll = false;
        win = find_window(SCALE_POINT(gesture.x), SCALE_POINT(gesture.y));
      } else {
        win = gesture_target;
        if (!win || !is_window(win) || win->id != gesture_target_id) { gesture_target = NULL; return; }
      }
      if (!win) return;
      for (window_t *owner = win; owner; owner = owner->parent) {
        if (window_has_state(owner, WINDOW_STATE_DISABLED) || !window_has_state(owner, WINDOW_STATE_VISIBLE)) {
          if (gesture_target && !gesture_scroll) {
            ax_gesture_t cancel = {.phase = AX_GESTURE_CANCEL, .scale = 1};
            send_message(gesture_target, evGesture, 0, &cancel);
          }
          gesture_target = NULL;
          return;
        }
      }
      gesture.x = LOCAL_X(gesture.x, gesture.y, win);
      gesture.y = LOCAL_Y(msg->gesture.x, gesture.y, win);
      gesture.previous_x = LOCAL_X(gesture.previous_x, gesture.previous_y, win);
      gesture.previous_y = LOCAL_Y(msg->gesture.previous_x, gesture.previous_y, win);
      uint32_t point = MAKEDWORD((int)gesture.x, (int)gesture.y);
      if (gesture_scroll) {
        if (gesture.phase == AX_GESTURE_UPDATE) {
          int dx = (int)lroundf(gesture.x - gesture.previous_x);
          int dy = (int)lroundf(gesture.y - gesture.previous_y);
          void *delta = (void *)(intptr_t)MAKEDWORD(-dx * SCROLL_SENSITIVITY, dy * SCROLL_SENSITIVITY);
          if (!handle_mouse(evWheel, win, gesture.x, gesture.y, delta)) send_message(win, evWheel, point, delta);
        }
      } else if (gesture.phase != AX_GESTURE_BEGIN || !handle_mouse(evGesture, win, gesture.x, gesture.y, &gesture)) {
        if (send_message(win, evGesture, point, &gesture)) { gesture_target = win; gesture_target_id = win->id; }
      }
      if (gesture.phase == AX_GESTURE_BEGIN && !gesture_target) {
        gesture_target = win; gesture_target_id = win->id; gesture_scroll = true;
      }
      if (gesture.phase != AX_GESTURE_UPDATE) {
        fprintf(stderr, "[input] gesture phase=%u win=%u target=%u\n", gesture.phase, win->id, gesture_target ? gesture_target_id : 0);
        fflush(stderr);
      }
      if (gesture.phase == AX_GESTURE_END || gesture.phase == AX_GESTURE_CANCEL) gesture_target = NULL;
      break;
    }
    case kEventPointerCancel: {
      px = msg->x; py = msg->y;
      win = g_ui_runtime.captured;
      if (!win && pointer_target && is_window(pointer_target) && pointer_target->id == pointer_target_id)
        win = pointer_target;
      if (!win) win = find_window(SCALE_POINT(px), SCALE_POINT(py));
      if (win) {
        int lx = LOCAL_X(px, py, win), ly = LOCAL_Y(px, py, win);
        if ((win == pointer_target || !handle_mouse(evPointerCancel, win, lx, ly, NULL)) &&
            !send_message(win, evPointerCancel, MAKEDWORD(lx, ly), NULL))
          send_message(win, evLeftButtonUp, MAKEDWORD(-1, -1), NULL);
        fprintf(stderr, "[input] pointer cancel win=%u\n", win->id);
        fflush(stderr);
      }
      g_ui_runtime.captured = NULL;
      g_ui_runtime.dragging = NULL;
      g_ui_runtime.resizing = NULL;
      pointer_target = NULL;
      break;
    }
    case kEventScrollWheel: {
      px = (int)msg->x;
      py = (int)msg->y;
      // a. Keep last_mouse up to date; sb_try_scroll reads it to resync hover.
      g_ui_runtime.last_mouse_sx = SCALE_POINT(px);
      g_ui_runtime.last_mouse_sy = SCALE_POINT(py);
        if ((win = g_ui_runtime.captured) ||
          (win = find_window(SCALE_POINT(px), SCALE_POINT(py))))
      {
        if (window_has_state(win, WINDOW_STATE_DISABLED)) return;
        int16_t dx = msg->dx;
        int16_t dy = msg->dy;
        // Convert to window-local coordinates (same as clicks/moves)
        int16_t lx = (int16_t)LOCAL_X(px, py, win);
        int16_t ly = (int16_t)LOCAL_Y(px, py, win);
        // Use handle_mouse to find deepest child, just like clicks do
        // lparam contains scroll deltas
        void *scroll_deltas = (void*)(intptr_t)MAKEDWORD((uint16_t)(-dx * SCROLL_SENSITIVITY),
                                                          (uint16_t)(dy * SCROLL_SENSITIVITY));
        if (!handle_mouse(evWheel, win, lx, ly, scroll_deltas)) {
          send_message(win, evWheel, MAKEDWORD((uint16_t)lx, (uint16_t)ly), scroll_deltas);
        }
      }
      break;
    }

    case kEventLeftButtonDown:
    case kEventRightButtonDown: {
      if (msg->message == kEventLeftButtonDown) pointer_target = NULL;
      px = (int)msg->x;
      py = (int)msg->y;
      tooltip_cancel();
        if ((win = g_ui_runtime.captured) ||
          (win = find_window(SCALE_POINT(px), SCALE_POINT(py))))
      {
        window_t *click_root = get_root_window(win);
        if (window_has_state(win, WINDOW_STATE_DISABLED)) return;

        int sx = SCALE_POINT(px);
        int sy = SCALE_POINT(py);
        int lx = LOCAL_X(px, py, win);
        int ly = LOCAL_Y(px, py, win);
        
        window_t *toolbar_host = NULL;
        if (msg->message == kEventLeftButtonDown && win != g_ui_runtime.captured) {
          toolbar_host = find_toolbar_host_at(win, sx, sy);
        }

        // Unify activation focus target: when the click lands on the toolbar,
        // focus the window (not the toolbar host); the toolbar host is hidden and
        // just processes owner-drawn clicks.
        window_t *focus_target = win;
        bool activating = (focus_target != g_ui_runtime.focused);
        window_t *old_root = g_ui_runtime.focused ? get_root_window(g_ui_runtime.focused) : NULL;
        window_t *new_root = click_root;
        bool root_changing = activating && (new_root != old_root);
        if (activating) {
          send_message(focus_target, evMouseActivate, 0, NULL);
          if (root_changing && old_root)
            send_message(old_root, evActivate, WA_INACTIVE, new_root);
        }
        if (click_root && !click_root->parent && win != g_ui_runtime.captured) {
          move_to_top(click_root);
        }
        if (activating) {
          set_focus(focus_target);
          if (root_changing)
            send_message(new_root, evActivate, WA_CLICKACTIVE, old_root);
        }
        window_t *resize_target = (win == click_root || win->parent) ? click_root : NULL;
        int root_lx = resize_target ? sx - resize_target->frame.x : 0;
        int root_ly = resize_target ? sy - resize_target->frame.y : 0;
        if (resize_target &&
            root_lx >= resize_target->frame.w - SCROLLBAR_WIDTH &&
            root_ly >= resize_target->frame.h - SCROLLBAR_WIDTH &&
            !(resize_target->flags&WINDOW_NORESIZE) &&
            win != g_ui_runtime.captured)
        {
          g_ui_runtime.resizing = resize_target;
          resize_anchor[0] = sx - (resize_target->frame.x + resize_target->frame.w);
          resize_anchor[1] = sy - (resize_target->frame.y + resize_target->frame.h);
        } else if (!win->maximized && window_in_drag_area(win, SCALE_POINT(py)) && win != g_ui_runtime.captured) {
          // For WINDOW_NOTITLE toolbars, don't drag if the click hits a toolbar
          // button — only drag from empty space.
          bool skip_drag = false;
          if (toolbar_host && (win->flags & WINDOW_NOTITLE)) {
            toolbar_state_t *tb = window_toolbar_state(win);
            int tb_x = sx - window_screen_x(win);
            int tb_y = sy - window_screen_y(win);
            if (tb && toolbar_item_hit(tb, tb_x, tb_y) >= 0)
              skip_drag = true;
          }
          if (!skip_drag) {
            g_ui_runtime.dragging = win;
            drag_anchor[0] = SCALE_POINT(px) - win->frame.x;
            drag_anchor[1] = SCALE_POINT(py) - win->frame.y;
          } else {
            // Route to toolbar instead of dragging
            int tb_x = sx - window_screen_x(win);
            int tb_y = sy - window_screen_y(win);
            if (!toolbar_dispatch_embedded_mouse(win, evLeftButtonDown, tb_x, tb_y)) {
              send_message(toolbar_host, evLeftButtonDown,
                           MAKEDWORD((uint16_t)tb_x, (uint16_t)tb_y), NULL);
            }
          }
        } else {
          if (msg->message == kEventLeftButtonDown &&
              (win->flags & WINDOW_TOOLBAR) && toolbar_host) {
            // Route to toolbar host's mouse handler (owner-draw item dispatch)
            int title_h = (win->flags & WINDOW_NOTITLE) ? 0 : TITLEBAR_HEIGHT;
            int tb_x = sx - window_screen_x(win);
            int tb_y = sy - (window_screen_y(win) + title_h);
            if (!toolbar_dispatch_embedded_mouse(win, evLeftButtonDown, tb_x, tb_y)) {
              send_message(toolbar_host, evLeftButtonDown,
                           MAKEDWORD((uint16_t)tb_x, (uint16_t)tb_y), NULL);
            }
          } else {
            int wmsg = (msg->message == kEventLeftButtonDown)
                       ? evLeftButtonDown
                       : evRightButtonDown;
            if (!handle_mouse(wmsg, win, lx, ly, NULL)) {
              send_pointer_message(win, wmsg, MAKEDWORD(lx, ly), NULL);
              if (wmsg == evLeftButtonDown) { pointer_target = win; pointer_target_id = win->id; }
            }
          }
        }
      }
      break;
    }

    case kEventLeftDoubleClick: {
      px = (int)msg->x;
      py = (int)msg->y;
        if ((win = g_ui_runtime.captured) ||
          (win = find_window(SCALE_POINT(px), SCALE_POINT(py))))
      {
        if (window_has_state(win, WINDOW_STATE_DISABLED)) return;
        int lx = LOCAL_X(px, py, win);
        int ly = LOCAL_Y(px, py, win);
        if (!handle_mouse(evLeftButtonDoubleClick, win, lx, ly, NULL)) {
          send_pointer_message(win, evLeftButtonDoubleClick,
                       MAKEDWORD(lx, ly), NULL);
        }
      }
      break;
    }

    case kEventLeftButtonUp:
    case kEventRightButtonUp: {
      if (msg->message == kEventLeftButtonUp) pointer_target = NULL;
      px = (int)msg->x;
      py = (int)msg->y;
      // Toolbar clicks are handled entirely by the toolbar host window's
      // evLeftButtonDown/Up handlers. No legacy toolbar_down_win state needed.
      if (g_ui_runtime.dragging) {
        int sx = SCALE_POINT(px);
        int sy = SCALE_POINT(py);
        irect16_t titlebar  = rect_split_top(g_ui_runtime.dragging->frame, TITLEBAR_HEIGHT);
        irect16_t close_btn = rect_split_right(titlebar, TITLEBAR_HEIGHT);
        bool on_close = !(g_ui_runtime.dragging->flags & (WINDOW_NOTITLE | WINDOW_NOCLOSE))
                        && sx >= close_btn.x && sx < close_btn.x + close_btn.w
                        && sy >= close_btn.y && sy < close_btn.y + close_btn.h;
        window_t *dragged = g_ui_runtime.dragging;
        irect16_t max_btn = rect_split_right(rect_trim_right(titlebar, TITLEBAR_HEIGHT), TITLEBAR_HEIGHT);
        bool on_maximize = msg->message == kEventLeftButtonUp && dragged->maximizable && !dragged->parent &&
          !(dragged->flags & (WINDOW_NOTITLE | WINDOW_NORESIZE | WINDOW_DIALOG | WINDOW_ALWAYSINBACK | WINDOW_ALWAYSONTOP)) &&
          rect_contains_point(max_btn, (ipoint16_t){sx, sy});
        if (on_maximize) {
          g_ui_runtime.dragging = NULL;
          maximize_window(dragged);
        } else if (on_close) {
          // Clear dragging BEFORE the send: evClose may open a modal
          // dialog that pumps events, and a live dragging pointer would cause the
          // window to follow the mouse during that dialog.  Same pattern as
          // toolbar_down_win which is cleared before its send above.
          window_t *closing = g_ui_runtime.dragging;
          fprintf(stderr, "[win] close win=%u\n", closing->id);
          fflush(stderr);
          g_ui_runtime.dragging = NULL;
          if (closing->flags & WINDOW_DIALOG) {
            end_dialog(closing, -1);
          } else {
            if (!send_message(closing, evClose, 0, NULL)) {
              destroy_window(closing);
            }
          }
        } else {
          if (msg->message == kEventLeftButtonUp)
            send_message(g_ui_runtime.dragging, evNCLeftButtonUp,
                         MAKEDWORD(sx, sy), NULL);
          g_ui_runtime.dragging = NULL;
        }
      } else if (g_ui_runtime.resizing) {
        g_ui_runtime.resizing = NULL;
      } else if ((win = g_ui_runtime.captured) ||
                 (win = find_window(SCALE_POINT(px), SCALE_POINT(py))))
      {
        if (window_has_state(win, WINDOW_STATE_DISABLED)) return;
        // Deliver to client area only if mouse is at or below the title bar / toolbar.
        if (SCALE_POINT(py) >= win->frame.y + titlebar_height(win) || win == g_ui_runtime.captured) {
          // For WINDOW_NOTITLE toolbars, route button-up to toolbar host
          // (toolbar items are owner-drawn, not child windows).
          if ((win->flags & WINDOW_TOOLBAR) && (win->flags & WINDOW_NOTITLE) && win->toolbar) {
            int sx = SCALE_POINT(px);
            int sy = SCALE_POINT(py);
            int tb_x = sx - window_screen_x(win);
            int tb_y = sy - window_screen_y(win);
            if (!toolbar_dispatch_embedded_mouse(win, evLeftButtonUp, tb_x, tb_y)) {
              send_message(win->toolbar, evLeftButtonUp,
                           MAKEDWORD((uint16_t)tb_x, (uint16_t)tb_y), NULL);
            }
          } else {
            int lx = LOCAL_X(px, py, win);
            int ly = LOCAL_Y(px, py, win);
            int wmsg = (msg->message == kEventLeftButtonUp)
                       ? evLeftButtonUp
                       : evRightButtonUp;
            if (!handle_mouse(wmsg, win, lx, ly, NULL)) {
              send_pointer_message(win, wmsg, MAKEDWORD(lx, ly), NULL);
            }
          }
        } else {
          int sx = SCALE_POINT(px);
          int sy = SCALE_POINT(py);
          if (msg->message == kEventLeftButtonUp) {
            window_t *tb_host = find_toolbar_host_at(win, sx, sy);
            if ((win->flags & WINDOW_TOOLBAR) && tb_host) {
              int title_h = (win->flags & WINDOW_NOTITLE) ? 0 : TITLEBAR_HEIGHT;
              int tb_x = sx - window_screen_x(win);
              int tb_y = sy - (window_screen_y(win) + title_h);
              if (!toolbar_dispatch_embedded_mouse(win, evLeftButtonUp, tb_x, tb_y)) {
                send_message(tb_host, evLeftButtonUp,
                             MAKEDWORD((uint16_t)tb_x, (uint16_t)tb_y), NULL);
              }
            } else {
              send_message(win, evNCLeftButtonUp,
                           MAKEDWORD(sx, sy), NULL);
            }
          }
        }
      }
      break;
    }

    case evRefreshStencil:
      // Stencil no longer used; discard.
      break;

    case kEventTimer:
      // Translate platform timer events to the Orion evTimer message and route
      // to the target window.  axSetTimer's obj becomes msg->target; guard with
      // is_valid_window_ptr to skip events for already-destroyed windows.
      if (msg->target &&
          is_valid_window_ptr(msg->target, g_ui_runtime.windows)) {
        send_message(msg->target, evTimer, msg->wParam, msg->lParam);
      }
      break;

    default: {
      // All other posted Orion events (evPaint, evNCPaint, evResize, evSetFocus,
      // evHttpDone, evHttpProgress, …) are routed directly to the target window.
      // Validate the pointer first: the window may have been destroyed between
      // the post_message() call and now.  O(window_count) per call; window
      // counts are small in practice (typically < 50).
      if (msg->target &&
          is_valid_window_ptr(msg->target, g_ui_runtime.windows)) {
        send_message(msg->target, msg->message, msg->wParam, msg->lParam);
        // evHttpProgress lparam is a framework-owned malloc'd snapshot; free
        // it here after the window proc has had a chance to read it.
        if (msg->message == evHttpProgress && msg->lParam) {
          free(msg->lParam);
        }
      }
      break;
    }
  }
}

// Get next event from the unified platform queue — canonical WinAPI GetMessage
// equivalent.  post_message() now routes Orion events (evPaint, evNCPaint, …)
// through axPostMessageW into the same platform queue as hardware events, so
// both kinds are returned here.
//
// Blocks until an event arrives (returns 1).  Returns 0 on quit or when a
// sentinel (wakeup-only) event is received; the sentinel case causes the
// caller's while-loop to exit so that repost_messages() can flush any events
// that were posted during the inner loop before resuming.
int get_message(ui_event_t *evt) {
  static bool s_draining_queue = false;
  int r;

  if (s_draining_queue) {
    r = axPeekMessage(evt);
    if (!r) {
      s_draining_queue = false;
      return 0;
    }
  } else {
    r = axGetMessage(evt);
    if (!r) return 0;
    s_draining_queue = true;
  }

  // Sentinel events only wake the loop to trigger repost_messages(); they
  // carry no UI data.  Clear the pending flag and return 0 so the caller
  // exits the while-loop.  The pending flag is what keeps wake_event_loop()
  // from posting multiple sentinels for a single burst of post_message() calls.
  if (evt->target == (void *)&g_wakeup_sentinel) {
    g_wakeup_pending = false;
    s_draining_queue = false;
    return 0;
  }

  update_key_state(evt);

  return 1;
}

// Post a sentinel event to the platform queue to wake get_message().
// Called by post_message() whenever a new Orion message is enqueued so that
// the main loop's inner while exits and repost_messages() runs this cycle.
// The g_wakeup_pending flag ensures at most one sentinel is queued at a time,
// preventing redundant repost_messages() cycles when post_message() is called
// in rapid succession (e.g., invalidate_window() posts three messages at once).
void wake_event_loop(void) {
  if (g_wakeup_pending) return;
  if (!g_ui_runtime.running) return;
  g_wakeup_pending = true;
  axPostMessageW(&g_wakeup_sentinel, kEventWindowPaint, 0, NULL);
}
