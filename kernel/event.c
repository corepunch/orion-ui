// Platform event handling and dispatch
// Translates platform (AXmessage) events into Orion window messages.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "../user/user.h"
#include "../user/messages.h"
#include "kernel.h"

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
extern void destroy_window(window_t *win);
extern int titlebar_height(window_t const *win);
// Window-liveness check — defined in user/message.c; used to guard posted events.
extern bool is_valid_window_ptr(window_t *target, window_t *list);
// repaint_stencil() is called when evRefreshStencil is dispatched.
extern void repaint_stencil(void);

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

// Coalescing flag: true while a sentinel is already queued in the platform
// event queue so that wake_event_loop() does not post a second one.
// Cleared when get_message() consumes the sentinel.
static bool g_wakeup_pending = false;

// Current modifier state (updated on every key event)
static uint32_t g_mod_state = 0;

uint32_t ui_get_mod_state(void) {
  return g_mod_state;
}

// Drag/resize state (shared with user/window.c for destroy_window cleanup)
static int drag_anchor[2];
static int resize_anchor[2];

// Window that received evNCLeftButtonDown (toolbar press).
// Always delivered evNCLeftButtonUp on the next left-up,
// regardless of release position, so pressed state is cleared deterministically.
// Shared with user/window.c for destroy_window cleanup (stored in g_ui_runtime).

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

// Hit-test a toolbar child using screen-space coordinates.
// Returns NULL when the point is outside the toolbar band or in a gap.
static window_t *find_toolbar_child_at(window_t *parent, int sx, int sy,
                                       int *tb_x_out, int *tb_y_out) {
  if (!parent || !(parent->flags & WINDOW_TOOLBAR)) return NULL;
  if (sy >= parent->frame.y + titlebar_height(parent)) return NULL;

  int title_h = (parent->flags & WINDOW_NOTITLE) ? 0 : TITLEBAR_HEIGHT;
  int tb_x = sx - parent->frame.x;
  int tb_y = sy - (parent->frame.y + title_h);
  if (tb_x_out) *tb_x_out = tb_x;
  if (tb_y_out) *tb_y_out = tb_y;

  for (window_t *tc = parent->toolbar_children; tc; tc = tc->next) {
    if (CONTAINS(tb_x, tb_y, tc->frame.x, tc->frame.y, tc->frame.w, tc->frame.h)) {
      return tc;
    }
  }
  return NULL;
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
  post_message(win, evRefreshStencil, 0, NULL);
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

  window_t *win;
  int px, py; // platform logical coordinates

  switch (msg->message) {

    case kEventWindowClosed:
      g_ui_runtime.running = false;
      break;

    case kEventWindowResized: {
      int new_w = (int)LOWORD(msg->wParam);
      int new_h = (int)HIWORD(msg->wParam);
      ui_update_screen_size(new_w, new_h);
      int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
      int sh = ui_get_system_metrics(kSystemMetricScreenHeight);
      // Resize every root window to match the new platform-window dimensions.
      // ALWAYSINBACK windows are backdrop / desktop fills and have always been
      // auto-resized here.  Normal root windows receive the same treatment so
      // that applications do not need to handle evDisplayChange just to resize
      // themselves; their evResize handler is responsible for re-laying out
      // children.  evDisplayChange is no longer sent.
      for (win = g_ui_runtime.windows; win; win = win->next) {
        if (!win->parent) {
          resize_window(win, sw, sh);
        }
      }
      post_message((window_t *)1, evRefreshStencil, 0, NULL);
      for (win = g_ui_runtime.windows; win; win = win->next) {
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
        send_message(g_ui_runtime.focused, evTextInput, 0, buf);
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
        }
      }
      break;
    }

    case kEventKeyUp:
      g_mod_state = (uint32_t)msg->wParam & 0xFFFF0000u;
      send_message(g_ui_runtime.focused, evKeyUp, (uint32_t)msg->keyCode, NULL);
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
      int16_t rdx = msg->dx;
      int16_t rdy = msg->dy;
      if (g_ui_runtime.dragging) {
        move_window(g_ui_runtime.dragging,
                    SCALE_POINT(px) - drag_anchor[0],
                    SCALE_POINT(py) - drag_anchor[1]);
        repost_messages();
      } else if (g_ui_runtime.resizing) {
        int new_w = SCALE_POINT(px) - resize_anchor[0] - g_ui_runtime.resizing->frame.x;
        int new_h = SCALE_POINT(py) - resize_anchor[1] - g_ui_runtime.resizing->frame.y;
        resize_window(g_ui_runtime.resizing, new_w, new_h);
        repost_messages();
      } else if (((win = g_ui_runtime.captured) ||
                  (win = find_window(SCALE_POINT(px), SCALE_POINT(py)))))
      {
        if (win->disabled) return;
        int16_t lx = (int16_t)LOCAL_X(px, py, win);
        int16_t ly = (int16_t)LOCAL_Y(px, py, win);
        if (win == g_ui_runtime.captured || (ly >= 0 && win == g_ui_runtime.focused)) {
          send_message(win, evMouseMove, MAKEDWORD(lx, ly),
                       (void*)(intptr_t)MAKEDWORD(rdx, rdy));
        }
      }
      if (g_ui_runtime.tracked && !CONTAINS(SCALE_POINT(px), SCALE_POINT(py),
                                g_ui_runtime.tracked->frame.x, g_ui_runtime.tracked->frame.y,
                                g_ui_runtime.tracked->frame.w, g_ui_runtime.tracked->frame.h))
      {
        track_mouse(NULL);
      }
      break;
    }

    case kEventScrollWheel: {
      px = (int)msg->x;
      py = (int)msg->y;
        if ((win = g_ui_runtime.captured) ||
          (win = find_window(SCALE_POINT(px), SCALE_POINT(py))))
      {
        if (win->disabled) return;
        int16_t dx = msg->dx;
        int16_t dy = msg->dy;
        send_message(win, evWheel,
                     MAKEDWORD((uint16_t)(-dx * SCROLL_SENSITIVITY),
                               (uint16_t)(dy * SCROLL_SENSITIVITY)), NULL);
      }
      break;
    }

    case kEventLeftButtonDown:
    case kEventRightButtonDown: {
      px = (int)msg->x;
      py = (int)msg->y;
        if ((win = g_ui_runtime.captured) ||
          (win = find_window(SCALE_POINT(px), SCALE_POINT(py))))
      {
        window_t *click_root = get_root_window(win);
        if (win->disabled) return;

        int sx = SCALE_POINT(px);
        int sy = SCALE_POINT(py);
        int tb_x = 0;
        int tb_y = 0;
        window_t *toolbar_hit = NULL;
        if (msg->message == kEventLeftButtonDown &&
            win != g_ui_runtime.captured)
        {
          toolbar_hit = find_toolbar_child_at(win, sx, sy, &tb_x, &tb_y);
        }

        // Unify activation focus target with normal child hit-testing: when the
        // click lands on a toolbar child, focus that child directly.
        window_t *focus_target = toolbar_hit ? toolbar_hit : win;
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
        int lx = LOCAL_X(px, py, win);
        int ly = LOCAL_Y(px, py, win);
        // Resize handle: use frame-relative y (from window top) so the check
        // naturally maps to the bottom-right corner of the total frame.
        int ly_frame = SCALE_POINT(py) - win->frame.y;
        if (lx >= win->frame.w - RESIZE_HANDLE &&
            ly_frame >= win->frame.h - RESIZE_HANDLE &&
            !win->parent &&
            !(win->flags&WINDOW_NORESIZE) &&
            win != g_ui_runtime.captured)
        {
          g_ui_runtime.resizing = win;
          resize_anchor[0] = SCALE_POINT(px) - (win->frame.x + win->frame.w);
          resize_anchor[1] = SCALE_POINT(py) - (win->frame.y + win->frame.h);
        } else if (window_in_drag_area(win, SCALE_POINT(py)) && win != g_ui_runtime.captured) {
          g_ui_runtime.dragging = win;
          drag_anchor[0] = SCALE_POINT(px) - win->frame.x;
          drag_anchor[1] = SCALE_POINT(py) - win->frame.y;
        } else {
          if (msg->message == kEventLeftButtonDown &&
              (win->flags & WINDOW_TOOLBAR) && sy < win->frame.y + titlebar_height(win)) {
            if (toolbar_hit) {
              g_ui_runtime.toolbar_down_win = toolbar_hit;
              send_message(toolbar_hit, evLeftButtonDown,
                           MAKEDWORD(tb_x - toolbar_hit->frame.x, tb_y - toolbar_hit->frame.y), NULL);
            }
            // No hit (click in gap): no action needed.
          } else {
            int wmsg = (msg->message == kEventLeftButtonDown)
                       ? evLeftButtonDown
                       : evRightButtonDown;
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
        if ((win = g_ui_runtime.captured) ||
          (win = find_window(SCALE_POINT(px), SCALE_POINT(py))))
      {
        if (win->disabled) return;
        int lx = LOCAL_X(px, py, win);
        int ly = LOCAL_Y(px, py, win);
        if (!handle_mouse(evLeftButtonDoubleClick, win, lx, ly)) {
          send_message(win, evLeftButtonDoubleClick,
                       MAKEDWORD(lx, ly), NULL);
        }
      }
      break;
    }

    case kEventLeftButtonUp:
    case kEventRightButtonUp: {
      px = (int)msg->x;
      py = (int)msg->y;
      // Always deliver NonClientLeftButtonUp to any window that received a
      // NonClientLeftButtonDown (toolbar press), even if the release is outside
      // the window.  This guarantees the pressed state is cleared deterministically.
      // The break is intentional: toolbar clicks are fully handled by the
      // NonClientLeftButtonDown/Up pair (analogous to WM_NCLBUTTONDOWN/UP),
      // so no client LeftButtonDown/Up is sent for this click sequence.
      // Focus changes already occurred on mouse-down, so no focus work is needed here.
      if (g_ui_runtime.toolbar_down_win && msg->message == kEventLeftButtonUp) {
        int sx = SCALE_POINT(px);
        int sy = SCALE_POINT(py);
        window_t *tc = g_ui_runtime.toolbar_down_win;
        g_ui_runtime.toolbar_down_win = NULL;  // clear before send: handler may open a modal loop
        // tc->parent is always non-NULL (toolbar children always have a parent).
        window_t *parent = tc->parent;
        int tb_x = 0;
        int tb_y = 0;
        window_t *hit_tc = find_toolbar_child_at(parent, sx, sy, &tb_x, &tb_y);
        bool hit = (hit_tc == tc);
        if (hit) {
          // Release inside the button: let win_toolbar_button/win_button fire
          // the click notification normally via LeftButtonUp.
          send_message(tc, evLeftButtonUp,
                       MAKEDWORD(tb_x - tc->frame.x, tb_y - tc->frame.y), NULL);
        } else {
          // Release outside: clear the pressed visual without firing a click.
          // This matches the previous hit-tested behaviour where releasing off
          // the button was a no-op.
          tc->pressed = false;
          invalidate_window(tc);
        }
        break;
      }
      if (g_ui_runtime.dragging) {
        int sx = SCALE_POINT(px);
        int sy = SCALE_POINT(py);
        int close_x = g_ui_runtime.dragging->frame.x + g_ui_runtime.dragging->frame.w
                      - CONTROL_BUTTON_WIDTH - CONTROL_BUTTON_PADDING;
        int title_y  = window_title_bar_y(g_ui_runtime.dragging) - 2;
        bool on_close = !(g_ui_runtime.dragging->flags & WINDOW_NOTITLE)
                        && sx >= close_x && sx < close_x + CONTROL_BUTTON_WIDTH
                        && sy >= title_y && sy < title_y + TITLEBAR_HEIGHT;
        if (on_close) {
          // Clear dragging BEFORE the send: evClose may open a modal
          // dialog that pumps events, and a live dragging pointer would cause the
          // window to follow the mouse during that dialog.  Same pattern as
          // toolbar_down_win which is cleared before its send above.
          window_t *closing = g_ui_runtime.dragging;
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
        if (win->disabled) return;
        // Deliver to client area only if mouse is at or below the title bar / toolbar.
        if (SCALE_POINT(py) >= win->frame.y + titlebar_height(win) || win == g_ui_runtime.captured) {
          int lx = LOCAL_X(px, py, win);
          int ly = LOCAL_Y(px, py, win);
          int wmsg = (msg->message == kEventLeftButtonUp)
                     ? evLeftButtonUp
                     : evRightButtonUp;
          if (!handle_mouse(wmsg, win, lx, ly)) {
            send_message(win, wmsg, MAKEDWORD(lx, ly), NULL);
          }
        } else {
          int sx = SCALE_POINT(px);
          int sy = SCALE_POINT(py);
          if (msg->message == kEventLeftButtonUp)
            send_message(win, evNCLeftButtonUp,
                         MAKEDWORD(sx, sy), NULL);
        }
      }
      break;
    }

    case evRefreshStencil:
      // evRefreshStencil rebuilds the compositing stencil for all windows.
      // The target may be the dummy value (window_t*)1 used by show_window and
      // theme-change callers, so it has its own case rather than going through
      // the window-validity check in the default branch.
      if (g_ui_runtime.running) repaint_stencil();
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
  g_wakeup_pending = true;
  axPostMessageW(&g_wakeup_sentinel, kEventWindowPaint, 0, NULL);
}
