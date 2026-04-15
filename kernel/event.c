// Platform event handling and dispatch
// Translates platform (AXmessage) events into Orion window messages.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "../user/user.h"
#include "../user/messages.h"
#include "kernel.h"

// External references
extern bool running;
extern window_t *windows;
extern window_t *_focused;
extern window_t *_tracked;
extern window_t *_captured;

// Macros for coordinate conversion (platform logical → Orion logical)
#define SCALE_POINT(x) ((x)/UI_WINDOW_SCALE)

// Absolute screen origin of the client area for a window.  For top-level windows
// the client area starts at frame.y + titlebar_height().  For child windows
// frame.x/y is root-client-local, so we add the root window's client origin.
// This mirrors WinAPI ChildWindowFromPoint semantics: child procs always
// receive coordinates in their own client coordinate system.
static inline int win_abs_x(window_t *w) {
  if (!w->parent) return w->frame.x;
  window_t *root = get_root_window(w);
  return root->frame.x + w->frame.x;
}
static inline int win_abs_y(window_t *w) {
  window_t *root = get_root_window(w);
  return root->frame.y + titlebar_height(root) + (w->parent ? w->frame.y : 0);
}

#define LOCAL_X(px, py, WIN) (SCALE_POINT(px) - win_abs_x(WIN) + (WIN)->scroll[0])
#define LOCAL_Y(px, py, WIN) (SCALE_POINT(py) - win_abs_y(WIN) + (WIN)->scroll[1])
#define CONTAINS(x, y, x1, y1, w1, h1) \
((x1) <= (x) && (y1) <= (y) && (x1) + (w1) > (x) && (y1) + (h1) > (y))

// Sentinel object — any event posted with this target is a wakeup-only event
// and should be silently discarded by dispatch_message.
static int g_wakeup_sentinel;

// Current modifier state (updated on every key event)
static uint32_t g_mod_state = 0;

uint32_t ui_get_mod_state(void) {
  return g_mod_state;
}

// External functions
extern int send_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
extern void post_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
extern void move_window(window_t *win, int x, int y);
extern void resize_window(window_t *win, int new_w, int new_h);
extern window_t *find_window(int x, int y);
extern void set_focus(window_t* win);
extern void track_mouse(window_t *win);
extern void show_window(window_t *win, bool visible);
extern void end_dialog(window_t *win, uint32_t code);
extern void invalidate_window(window_t *win);
extern int titlebar_height(window_t const *win);

// Drag/resize state (shared with user/window.c for destroy_window cleanup)
window_t *_dragging = NULL;
window_t *_resizing = NULL;
static int drag_anchor[2];
static int resize_anchor[2];

// Window that received kWindowMessageNonClientLeftButtonDown (toolbar press).
// Always delivered kWindowMessageNonClientLeftButtonUp on the next left-up,
// regardless of release position, so pressed state is cleared deterministically.
// Shared with user/window.c for destroy_window cleanup (extern declared there).
window_t *_toolbar_down_win = NULL;

// Handle mouse events on child windows.
// x, y are in the parent window's client coordinate system.
// Each child receives coords in its own client coordinate system (WinAPI style).
static int handle_mouse(int msg, window_t *win, int x, int y) {
  for (window_t *c = win->children; c; c = c->next) {
    if (CONTAINS(x, y, c->frame.x, c->frame.y, c->frame.w, c->frame.h) &&
        c->proc(c, msg, MAKEDWORD(x - c->frame.x, y - c->frame.y), NULL))
    {
      return true;
    }
  }
  return false;
}

// Find next tab stop
window_t* find_next_tab_stop(window_t *win, bool allow_current) {
  if (!win) return false;
  window_t *next;
  if ((next = find_next_tab_stop(win->children, true))) return next;
  if (!win->notabstop && (win->parent || win->visible) && allow_current) return win;
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
// windows come first and WINDOW_ALWAYSONTOP windows come last, so a toolbox or
// palette stays above its own document windows while remaining below the active
// shell menus and below any other app's windows when that app is active.
void move_to_top(window_t* _win) {
  extern window_t *get_root_window(window_t *window);

  window_t *win = get_root_window(_win);
  post_message(win, kWindowMessageRefreshStencil, 0, NULL);
  invalidate_window(win);

  if (win->flags & WINDOW_ALWAYSINBACK)
    return;

  hinstance_t h = win->hinstance;

  if (h == 0) {
    // System/unowned window — original global ALWAYSONTOP behaviour.
    window_t **head = &windows, *p = NULL, *n = *head;

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

    if (win->flags & WINDOW_ALWAYSONTOP) {
      // Append to absolute tail — globally on top of everything.
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
  // then ALWAYSONTOP windows (clicked window last = on top within group).

  // Step 1: Extract all windows of this app from the global list.
  window_t *n_head = NULL, *n_tail = NULL;  // normal windows sublist
  window_t *t_head = NULL, *t_tail = NULL;  // ALWAYSONTOP windows sublist

  window_t *prev = NULL, *cur = windows;
  while (cur) {
    window_t *next = cur->next;
    if (cur->hinstance == h && !(cur->flags & WINDOW_ALWAYSINBACK)) {
      // Remove from the global list.
      if (prev) prev->next = next;
      else      windows    = next;
      cur->next = NULL;

      if (cur != win) {
        // Append all other app windows to their respective sublists now;
        // win itself is appended last (after the loop) so it ends up on top.
        if (cur->flags & WINDOW_ALWAYSONTOP) {
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
  if (win->flags & WINDOW_ALWAYSONTOP) {
    if (t_tail) t_tail->next = win; else t_head = win;
    t_tail = win;
  } else {
    if (n_tail) n_tail->next = win; else n_head = win;
    n_tail = win;
  }
  win->next = NULL;

  // Chain the two sublists: normals → topmost.
  if (n_tail) n_tail->next = t_head;
  window_t *group_head = n_head ? n_head : t_head;
  window_t *group_tail = t_tail ? t_tail : n_tail;

  if (!group_head) return;

  // Step 2: Find the insertion point — just before the first system (h==0)
  // ALWAYSONTOP window so the shell menu bar / popups stay globally on top.
  window_t *ins_prev = NULL;
  cur = windows;
  while (cur && !(cur->hinstance == 0 && (cur->flags & WINDOW_ALWAYSONTOP))) {
    ins_prev = cur;
    cur = cur->next;
  }

  // Insert the app group at the insertion point.
  group_tail->next = cur;
  if (ins_prev) ins_prev->next = group_head;
  else          windows        = group_head;

  // Invalidate every window in the moved group so previously-occluded windows
  // repaint correctly now that the group has come to the front.
  for (window_t *gw = group_head; gw != cur; gw = gw->next)
    invalidate_window(gw);
}

// Dispatch a platform AXmessage to the Orion window system.
void dispatch_message(ui_event_t *msg) {
  // Wakeup events are used only to unblock axWaitEvent; discard them.
  if (msg->target == &g_wakeup_sentinel)
    return;

  window_t *win;
  int px, py; // platform logical coordinates

  switch (msg->message) {

    case kEventWindowClosed:
      running = false;
      break;

    case kEventWindowResized: {
      int new_w = (int)LOWORD(msg->wParam);
      int new_h = (int)HIWORD(msg->wParam);
      ui_update_screen_size(new_w, new_h);
      int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
      int sh = ui_get_system_metrics(kSystemMetricScreenHeight);
      for (win = windows; win; win = win->next) {
        if (!win->parent) {
          if (win->flags & WINDOW_ALWAYSINBACK) {
            resize_window(win, sw, sh);
          } else {
            send_message(win, kWindowMessageDisplayChange, MAKEDWORD(sw, sh), NULL);
          }
        }
      }
      post_message((window_t *)1, kWindowMessageRefreshStencil, 0, NULL);
      for (win = windows; win; win = win->next) {
        if (win->visible) {
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
        send_message(_focused, kWindowMessageTextInput, 0, buf);
      }
      break;
    }

    case kEventKeyDown: {
      // Track modifier state from the key event's wParam high-word bits.
      g_mod_state = (uint32_t)msg->wParam & 0xFFFF0000u;
      uint32_t key = (uint32_t)msg->keyCode;
      // Send text input for printable characters (ASCII 32–126).
      // The char bytes are stored inline in the lParam field by the platform.
      char text_ch = *(char*)&msg->lParam;
      if (text_ch >= 0x20 && text_ch != 0x7f) {
        char buf[2] = { text_ch, '\0' };
        send_message(_focused, kWindowMessageTextInput, 0, buf);
      }
      if (_focused && !send_message(_focused, kWindowMessageKeyDown, key, NULL)) {
        if (key == AX_KEY_TAB) {
          if (msg->modflags & (AX_MOD_SHIFT >> 16)) {
            set_focus(find_prev_tab_stop(_focused));
          } else {
            set_focus(find_next_tab_stop(_focused, false));
          }
        } else if (key == AX_KEY_ENTER) {
          window_t *def = find_default_button(get_root_window(_focused));
          if (def) {
            send_message(def, kWindowMessageLeftButtonDown, 0, NULL);
            send_message(def, kWindowMessageLeftButtonUp, 0, NULL);
          }
        }
      }
      break;
    }

    case kEventKeyUp:
      g_mod_state = (uint32_t)msg->wParam & 0xFFFF0000u;
      send_message(_focused, kWindowMessageKeyUp, (uint32_t)msg->keyCode, NULL);
      break;

    case kEventJoyAxisMotion:
      send_message(_focused, kWindowMessageJoyAxisMotion,
                   MAKEDWORD(msg->wParam & 0xFF, (uint16_t)(intptr_t)msg->lParam), NULL);
      break;

    case kEventJoyButtonDown:
      send_message(_focused, kWindowMessageJoyButtonDown, msg->wParam, NULL);
      break;

    case kEventMouseMoved:
    case kEventLeftMouseDragged:
    case kEventRightMouseDragged:
    case kEventOtherMouseDragged: {
      px = (int)msg->x;
      py = (int)msg->y;
      int16_t rdx = msg->dx;
      int16_t rdy = msg->dy;
      if (_dragging) {
        move_window(_dragging,
                    SCALE_POINT(px) - drag_anchor[0],
                    SCALE_POINT(py) - drag_anchor[1]);
      } else if (_resizing) {
        int new_w = SCALE_POINT(px) - resize_anchor[0] - _resizing->frame.x;
        int new_h = SCALE_POINT(py) - resize_anchor[1] - _resizing->frame.y;
        resize_window(_resizing, new_w, new_h);
      } else if (((win = _captured) ||
                  (win = find_window(SCALE_POINT(px), SCALE_POINT(py)))))
      {
        if (win->disabled) return;
        int16_t lx = (int16_t)LOCAL_X(px, py, win);
        int16_t ly = (int16_t)LOCAL_Y(px, py, win);
        if (win == _captured || (ly >= 0 && win == _focused)) {
          send_message(win, kWindowMessageMouseMove, MAKEDWORD(lx, ly),
                       (void*)(intptr_t)MAKEDWORD(rdx, rdy));
        }
      }
      if (_tracked && !CONTAINS(SCALE_POINT(px), SCALE_POINT(py),
                                _tracked->frame.x, _tracked->frame.y,
                                _tracked->frame.w, _tracked->frame.h))
      {
        track_mouse(NULL);
      }
      break;
    }

    case kEventScrollWheel: {
      px = (int)msg->x;
      py = (int)msg->y;
      if ((win = _captured) ||
          (win = find_window(SCALE_POINT(px), SCALE_POINT(py))))
      {
        if (win->disabled) return;
        int16_t dx = msg->dx;
        int16_t dy = msg->dy;
        send_message(win, kWindowMessageWheel,
                     MAKEDWORD((uint16_t)(-dx * SCROLL_SENSITIVITY),
                               (uint16_t)(dy * SCROLL_SENSITIVITY)), NULL);
      }
      break;
    }

    case kEventLeftMouseDown:
    case kEventRightMouseDown: {
      px = (int)msg->x;
      py = (int)msg->y;
      if ((win = _captured) ||
          (win = find_window(SCALE_POINT(px), SCALE_POINT(py))))
      {
        if (win->disabled) return;
        bool activating = (win != _focused);
        window_t *old_root = _focused ? get_root_window(_focused) : NULL;
        window_t *new_root = get_root_window(win);
        bool root_changing = activating && (new_root != old_root);
        if (activating) {
          send_message(win, kWindowMessageMouseActivate, 0, NULL);
          if (root_changing && old_root)
            send_message(old_root, kWindowMessageActivate, WA_INACTIVE, new_root);
        }
        if (!win->parent) {
          move_to_top(win);
        }
        if (activating) {
          set_focus(win);
          if (root_changing)
            send_message(new_root, kWindowMessageActivate, WA_CLICKACTIVE, old_root);
        }
        int lx = LOCAL_X(px, py, win);
        int ly = LOCAL_Y(px, py, win);
        // Resize handle: use frame-relative y (from window top) so the check
        // naturally maps to the bottom-right corner of the total frame.
        int ly_frame = SCALE_POINT(py) - win->frame.y;
        if (lx >= win->frame.w - RESIZE_HANDLE &&
            ly_frame >= win->frame.h - RESIZE_HANDLE &&
            !win->parent &&
            !(win->flags&WINDOW_NORESIZE) &&
            win != _captured)
        {
          _resizing = win;
          resize_anchor[0] = SCALE_POINT(px) - (win->frame.x + win->frame.w);
          resize_anchor[1] = SCALE_POINT(py) - (win->frame.y + win->frame.h);
        } else if (window_in_drag_area(win, SCALE_POINT(py)) && win != _captured) {
          _dragging = win;
          drag_anchor[0] = SCALE_POINT(px) - win->frame.x;
          drag_anchor[1] = SCALE_POINT(py) - win->frame.y;
        } else {
          int sx = SCALE_POINT(px);
          int sy = SCALE_POINT(py);
          if (msg->message == kEventLeftMouseDown &&
              (win->flags & WINDOW_TOOLBAR) && sy < win->frame.y) {
            // Non-client left button down in toolbar area: send dedicated message
            // so the toolbar can show visual pressed feedback immediately.
            // Only applies when WINDOW_TOOLBAR is set; title bar clicks are
            // handled earlier by window_in_drag_area → _dragging path.
            send_message(win, kWindowMessageNonClientLeftButtonDown,
                         MAKEDWORD(sx, sy), NULL);
            _toolbar_down_win = win;
          } else {
            int wmsg = (msg->message == kEventLeftMouseDown)
                       ? kWindowMessageLeftButtonDown
                       : kWindowMessageRightButtonDown;
            if (!handle_mouse(wmsg, win, lx, ly)) {
              send_message(win, wmsg, MAKEDWORD(lx, ly), NULL);
            }
          }
        }
      }
      break;
    }

    case kEventLeftDoubleClick: {
      px = (int)msg->x;
      py = (int)msg->y;
      if ((win = _captured) ||
          (win = find_window(SCALE_POINT(px), SCALE_POINT(py))))
      {
        if (win->disabled) return;
        int lx = LOCAL_X(px, py, win);
        int ly = LOCAL_Y(px, py, win);
        if (!handle_mouse(kWindowMessageLeftButtonDoubleClick, win, lx, ly)) {
          send_message(win, kWindowMessageLeftButtonDoubleClick,
                       MAKEDWORD(lx, ly), NULL);
        }
      }
      break;
    }

    case kEventLeftMouseUp:
    case kEventRightMouseUp: {
      px = (int)msg->x;
      py = (int)msg->y;
      // Always deliver NonClientLeftButtonUp to any window that received a
      // NonClientLeftButtonDown (toolbar press), even if the release is outside
      // the window.  This guarantees the pressed state is cleared deterministically.
      // The break is intentional: toolbar clicks are fully handled by the
      // NonClientLeftButtonDown/Up pair (analogous to WM_NCLBUTTONDOWN/UP),
      // so no client LeftButtonDown/Up is sent for this click sequence.
      // Focus changes already occurred on mouse-down, so no focus work is needed here.
      if (_toolbar_down_win && msg->message == kEventLeftMouseUp) {
        int sx = SCALE_POINT(px);
        int sy = SCALE_POINT(py);
        window_t *toolbar_win = _toolbar_down_win;
        _toolbar_down_win = NULL;  // clear before send: handler may open a modal loop
        send_message(toolbar_win, kWindowMessageNonClientLeftButtonUp,
                     MAKEDWORD(sx, sy), NULL);
        break;
      }
      if (_dragging) {
        int sx = SCALE_POINT(px);
        int sy = SCALE_POINT(py);
        int close_x = _dragging->frame.x + _dragging->frame.w
                      - CONTROL_BUTTON_WIDTH - CONTROL_BUTTON_PADDING;
        int title_y  = window_title_bar_y(_dragging) - 2;
        bool on_close = !(_dragging->flags & WINDOW_NOTITLE)
                        && sx >= close_x && sx < close_x + CONTROL_BUTTON_WIDTH
                        && sy >= title_y && sy < title_y + TITLEBAR_HEIGHT;
        if (on_close) {
          if (_dragging->flags & WINDOW_DIALOG) {
            end_dialog(_dragging, -1);
          } else {
            if (!send_message(_dragging, kWindowMessageClose, 0, NULL)) {
              show_window(_dragging, false);
            }
          }
          _dragging = NULL;
        } else {
          if (msg->message == kEventLeftMouseUp)
            send_message(_dragging, kWindowMessageNonClientLeftButtonUp,
                         MAKEDWORD(sx, sy), NULL);
          _dragging = NULL;
        }
      } else if (_resizing) {
        _resizing = NULL;
      } else if ((win = _captured) ||
                 (win = find_window(SCALE_POINT(px), SCALE_POINT(py))))
      {
        if (win->disabled) return;
        // Deliver to client area only if mouse is at or below the title bar / toolbar.
        if (SCALE_POINT(py) >= win->frame.y + titlebar_height(win) || win == _captured) {
          int lx = LOCAL_X(px, py, win);
          int ly = LOCAL_Y(px, py, win);
          int wmsg = (msg->message == kEventLeftMouseUp)
                     ? kWindowMessageLeftButtonUp
                     : kWindowMessageRightButtonUp;
          if (!handle_mouse(wmsg, win, lx, ly)) {
            send_message(win, wmsg, MAKEDWORD(lx, ly), NULL);
          }
        } else {
          int sx = SCALE_POINT(px);
          int sy = SCALE_POINT(py);
          if (msg->message == kEventLeftMouseUp)
            send_message(win, kWindowMessageNonClientLeftButtonUp,
                         MAKEDWORD(sx, sy), NULL);
        }
      }
      break;
    }

    default:
      break;
  }
}

// Get next platform event.
// Blocks with axWaitEvent on the first call per cycle (saving CPU), then
// drains any additional queued events with axPollEvent.  Returns 0 when the
// platform queue is empty, which causes the caller's while-loop to exit and
// call repost_messages() to process internal (paint/async) messages.
int get_message(ui_event_t *evt) {
  static bool s_draining_queue = false;
  if (s_draining_queue) {
    int r = axPollEvent(evt);
    if (!r) s_draining_queue = false;
    return r;
  }
  s_draining_queue = true;
  axWaitEvent(0);
  return axPollEvent(evt);
}

// Wake up axWaitEvent by posting a sentinel event to the platform queue.
// Called by post_message() whenever a new Orion internal message is enqueued.
void wake_event_loop(void) {
  axPostMessageW(&g_wakeup_sentinel, kEventWindowPaint, 0, NULL);
}
