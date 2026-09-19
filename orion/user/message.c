// Message queue and dispatch implementation
// Extracted from mapview/window.c

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

#include "user.h"
#include "messages.h"
#include "draw.h"
#include "scrollbar.h"
#include "toolbar.h"
#include <orion/kernel/renderer.h>

#define CONTAINS(x, y, x1, y1, w1, h1) \
((x1) <= (x) && (y1) <= (y) && (x1) + (w1) > (x) && (y1) + (h1) > (y))

// Message queue structure for Orion-posted messages only.
typedef struct {
  window_t *target;
  uint32_t msg;
  uint32_t wparam;
  void *lparam;
} msg_t;

static struct {
  uint8_t read, write;
  msg_t messages[0x100];
} queue = {0};

// Free framework-owned asynchronous payloads attached to queue messages.
// Currently only HTTP progress snapshots are queue-owned.
static void free_posted_lparam(uint32_t msg, void *lparam) {
  if (!lparam) return;
  if (msg == evHttpProgress)
    free(lparam);
}
// Window hooks
typedef struct winhook_s {
  winhook_func_t func;
  uint32_t msg;
  void *userdata;
  struct winhook_s *next;
} winhook_t;

static winhook_t *g_hooks = NULL;

// External references

// Forward declarations for kernel/event.c helpers.
// wake_event_loop() posts a sentinel to make get_message() return 0 (loop exit).
extern void wake_event_loop(void);
// dispatch_message() routes a platform or Orion event to its target window proc.
void dispatch_message(ui_event_t *evt);
// Forward declarations for kernel/init.c per-frame rendering.
extern void ui_begin_frame(void);
extern void ui_end_frame(void);

// Forward declarations
extern void draw_panel(window_t const *win);
extern void draw_window_controls(window_t *win);
extern void draw_statusbar(window_t *win, const char *text);
extern void draw_button(irect16_t r, int dx, int dy, bool pressed);
extern void set_fullscreen(void);
extern window_t *get_root_window(window_t *window);

// Forward declarations for kernel/event.c helpers.
extern void wake_event_loop(void);
void dispatch_message(ui_event_t *evt);
// Forward declarations for kernel/init.c per-frame rendering.
extern void ui_begin_frame(void);
extern void ui_end_frame(void);

extern int titlebar_height(window_t const *win);
extern int statusbar_height(window_t const *win);
// Returns win's frame rect in absolute screen coordinates.
// For root windows, frame.x/y are already screen-absolute.
// For child windows, frame.x/y are root-client-space coords; they are mapped
// to screen by adding the root's screen origin and the root's non-client height.
// root_titlebar_h should be titlebar_height(root) — callers that already have
// it pass it in to avoid recomputing.
static irect16_t win_frame_in_screen(window_t *win, window_t *root, int root_titlebar_h) {
  (void)root;
  (void)root_titlebar_h;
  return (irect16_t){
    window_screen_x(win),
    window_screen_y(win),
    win->frame.w,
    win->frame.h
  };
}

// Register a window hook
void register_window_hook(uint32_t msg, winhook_func_t func, void *userdata) {
  winhook_t *hook = malloc(sizeof(winhook_t));
  hook->func = func;
  hook->msg = msg;
  hook->userdata = userdata;
  hook->next = g_hooks;
  g_hooks = hook;
}

// De-register a window hook
void deregister_window_hook(uint32_t msg, winhook_func_t func, void *userdata) {
  if (!g_hooks) return;
  while (g_hooks && msg == g_hooks->msg && func == g_hooks->func && userdata == g_hooks->userdata) {
    winhook_t *h = g_hooks;
    g_hooks = g_hooks->next;
    free(h);
  }
  for (winhook_t *w=g_hooks?g_hooks->next:NULL,*p=g_hooks;w;w=w->next,p=p->next) {
    if (msg == w->msg && func == w->func && userdata == w->userdata) {
      winhook_t *h = w;
      p->next = w->next;
      free(h);
    }
  }
}

// Remove window from hooks
void remove_from_global_hooks(window_t *win) {
  if (!g_hooks) return;
  while (g_hooks && win == g_hooks->userdata) {
    winhook_t *h = g_hooks;
    g_hooks = g_hooks->next;
    free(h);
  }
  for (winhook_t *w=g_hooks?g_hooks->next:NULL,*p=g_hooks;w;w=w->next,p=p->next) {
    if (w->userdata == win) {
      winhook_t *h = w;
      p->next = w->next;
      free(h);
    }
  }
}

// Clean up all hooks (called on shutdown)
void cleanup_all_hooks(void) {
  while (g_hooks) {
    winhook_t *next = g_hooks->next;
    free(g_hooks);
    g_hooks = next;
  }
  g_hooks = NULL;  // Ensure it's NULL for idempotency
}

void reset_message_queue(void) {
  memset(&queue, 0, sizeof(queue));
}

// Remove window from message queue
void remove_from_global_queue(window_t *win) {
  for (uint8_t w = queue.write, r = queue.read; r != w; r++) {
    if (queue.messages[r].target == win) {
      queue.messages[r].target = NULL;
    }
  }
}

static bool parent_notify_message(uint32_t msg) {
  switch (msg) {
    case evLeftButtonDown:
    case evLeftButtonDoubleClick:
    case evLeftButtonUp:
    case evRightButtonDown:
    case evRightButtonUp:
    case evMouseMove:
    case evKeyDown:
    case evKeyUp:
    case evTextInput:
      return true;
    default:
      return false;
  }
}

// Bind the root's offscreen target. evPaint cannot assume the matching
// evNCPaint just ran: another root (menu popup, tooltip) can paint in between
// and leave its FBO bound. Children share this same root target.
static void bind_root_surface(window_t *root) {
  int scale = (int)axGetScaling();
  if (scale < 1) scale = 1;
  R_EnsureWindowTarget(&root->surface_fbo, &root->surface_tex,
                       &root->surface_w, &root->surface_h,
                       root->frame.w * scale, root->frame.h * scale);
  glBindFramebuffer(GL_FRAMEBUFFER, root->surface_fbo);
  set_viewport_for_fbo(root);
}

// Send message to window (synchronous)
intptr_t send_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  if (!win) return false;
  irect16_t const *frame = &win->frame;
  window_t *root = get_root_window(win);
  intptr_t value = 0;
  // Call registered hooks
  for (winhook_t *hook = g_hooks; hook; hook = hook->next) {
    if (msg == hook->msg) {
      hook->func(win, msg, wparam, lparam, hook->userdata);
    }
  }
  // Handle special messages
  switch (msg) {
    case evResize:
      layout_docked_toolbars(win, get_client_rect(win));
      break;
    case evNCPaint:
      // Skip OpenGL calls if graphics aren't initialized (e.g., in tests)
      if (g_ui_runtime.running) {
        bind_root_surface(root);
        if (win == root && (win->flags & WINDOW_TRANSPARENT)) R_ClearWindowTarget(root->surface_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, root->surface_fbo);
        set_viewport_for_fbo(root);
        if (!(win->flags&WINDOW_TRANSPARENT) && wparam == 0) {
          draw_panel(win);
        }
        if (!(win->flags&WINDOW_NOTITLE)) {
          draw_window_controls(win);
        }
        toolbar_draw_non_client(win);
        if (win->flags&WINDOW_STATUSBAR) {
          draw_statusbar(win, win->statusbar_text);
        }
      }
      break;
    case evPaint:
      // Skip OpenGL calls if graphics aren't initialized (e.g., in tests)
      if (g_ui_runtime.running) {
        bind_root_surface(root);
        if (win->parent && (win->flags & WINDOW_TOOLBAR)) toolbar_draw_non_client(win);
        int t = titlebar_height(root);
        // Shift projection so that (0,0) in drawing space maps to the top-left
        // of the window's own client area.  For root windows (no parent),
        // cx=cy=0 and the projection is unchanged (backward compat).  For child
        // windows, cx/cy equal the child's frame.x/y so that drawing at (0,0)
        // appears at the child's screen position rather than at the root's
        // client origin.
        int cx = 0;
        int cy = 0;
        if (win->parent) {
          cx = window_screen_x(win) - window_screen_x(root);
          cy = window_screen_y(win) - (window_screen_y(root) + t);
        }
        int scroll_x = win->parent ? 0 : win->hscroll.pos;
        int scroll_y = win->parent ? 0 : win->vscroll.pos;
        set_projection(scroll_x - cx, -t - cy + scroll_y,
                       root->frame.w + scroll_x - cx,
                       root->frame.h - t - cy + scroll_y);
        // Every child is clipped to its own client area and every ancestor's
        // viewport, including children without built-in scrollbars.
        irect16_t clip = R(0, 0, root->frame.w, root->frame.h);
        for (window_t *owner = win; owner; owner = owner->parent) {
          irect16_t cr = get_client_rect(owner);
          cr = rect_offset(cr, window_screen_x(owner) - root->frame.x,
                           window_screen_y(owner) - root->frame.y + titlebar_height(owner));
          int left = MAX(clip.x, cr.x), top = MAX(clip.y, cr.y);
          int right = MIN(clip.x + clip.w, cr.x + cr.w);
          int bottom = MIN(clip.y + clip.h, cr.y + cr.h);
          clip = R(left, top, MAX(0, right - left), MAX(0, bottom - top));
        }
        set_scissor_fbo(root, clip);
      }
      break;
    case tbSetItems:
    case tbSetStrip:
    case tbSetActiveButton:
    case tbSetButtonSize:
    case tbSetOrientation:
    case tbSetStyle:
    case tbLoadStrip:
      (void)toolbar_handle_message(win, msg, wparam, lparam);
      break;
    case evStatusBar:
      if (lparam) {
        strncpy(win->statusbar_text, (const char*)lparam, sizeof(win->statusbar_text) - 1);
        win->statusbar_text[sizeof(win->statusbar_text) - 1] = '\0';
        invalidate_window(win);
      }
      break;
  }
  // Intercept mouse events for built-in scrollbars before calling win->proc
  if ((win->flags & (WINDOW_HSCROLL | WINDOW_VSCROLL)) &&
      (msg == evLeftButtonDown ||
       msg == evLeftButtonDoubleClick ||
       msg == evMouseMove ||
       msg == evLeftButtonUp)) {
    if (scrollbar_handle_builtin_mouse(win, msg, wparam, lparam)) return true;
  }
  // Non-client input must never reach the document or its child controls.
  if (msg == evLeftButtonDown || msg == evLeftButtonDoubleClick ||
      msg == evLeftButtonUp || msg == evRightButtonDown ||
      msg == evRightButtonUp || msg == evMouseMove || msg == evWheel) {
    ipoint16_t point = {(int16_t)LOWORD(wparam) - win->hscroll.pos,
                       (int16_t)HIWORD(wparam) - win->vscroll.pos};
    if ((win->flags & (WINDOW_HSCROLL | WINDOW_VSCROLL | WINDOW_STATUSBAR)) &&
        g_ui_runtime.captured != win &&
        !rect_contains_point(get_client_rect(win), point)) return true;
  }
  // Scrollbar timers do not consume the window's own timer notifications.
  if ((win->flags & (WINDOW_HSCROLL | WINDOW_VSCROLL)) && msg == evTimer)
    scrollbar_handle_builtin_timer(win, (uint32_t)wparam);
  if (win->parent && parent_notify_message(msg)) {
    parent_notify_t pn = {
      .child = win,
      .child_msg = msg,
      .child_wparam = wparam,
      .child_lparam = lparam,
    };
    if (send_message(win->parent, evParentNotify, 0, &pn))
      return true;
  }
  // The same window-owned matrix defines painting and pointer delivery.
  float saved_projection[16];
  bool view_paint = msg == evPaint && g_ui_runtime.running && win->view.enabled;
  if (view_paint) begin_draw_transform(&win->view.matrix, saved_projection);
  value = win->proc(win, msg, wparam, lparam);
  if (view_paint) {
    end_draw_transform(saved_projection);
    win->proc(win, evPaint, WINDOW_PAINT_OVERLAY, NULL);
  }
  if (!value) {
    switch (msg) {
      case evPointerCancel:
        // Existing controls release their pressed state without an inside click.
        win->proc(win, evLeftButtonUp, MAKEDWORD(-1, -1), NULL);
        return true;
      case evPaint:
        for (window_t *sub = win->children; sub; sub = sub->next) {
          if (window_has_state(sub, WINDOW_STATE_VISIBLE))
            send_message(sub, evPaint, wparam, lparam);
        }
        break;
      case evWheel:
        // Only drive built-in scrollbars when they are actually visible.
        // If this window can't handle wheel events, bubble to parent (WinAPI behavior).
        // wparam = mouse position MAKEDWORD(x,y), lparam = scroll deltas MAKEDWORD(dx,dy)
        if ((win->flags & (WINDOW_HSCROLL | WINDOW_VSCROLL)) &&
            (win->hscroll.visible || win->vscroll.visible)) {
          scrollbar_handle_builtin_wheel(win, lparam);
        } else if (win->parent) {
          // Bubble wheel event to parent, translating window-local mouse
          // coords from the child's client space into the parent's client space.
          int16_t clx = (int16_t)LOWORD(wparam);
          int16_t cly = (int16_t)HIWORD(wparam);
          uint32_t parent_wp = MAKEDWORD(
            (uint16_t)(clx + win->frame.x - win->hscroll.pos + win->parent->hscroll.pos),
            (uint16_t)(cly + win->frame.y - win->vscroll.pos + win->parent->vscroll.pos));
          send_message(win->parent, evWheel, parent_wp, lparam);
        }
        break;
      case evPaintStencil:
        paint_window_stencil(win);
        break;
      case evMeasure: {
        layout_measure_t *m = (layout_measure_t *)lparam;
        // If window has auto-layout, measure its children
        if (m && (win->flags & WINDOW_AUTO_LAYOUT)) {
          layout_measure_window(win, m);
        } else if (m) {
          // Fallback: use existing frame dimensions
          if (m->desired_w <= 0) m->desired_w = frame->w > 0 ? frame->w : 1;
          if (m->desired_h <= 0) m->desired_h = frame->h > 0 ? frame->h : 1;
        }
        if (m) {
          if (m->desired_w <= 0) m->desired_w = frame->w > 0 ? frame->w : 1;
          if (m->desired_h <= 0) m->desired_h = frame->h > 0 ? frame->h : 1;
          value = MAKEDWORD((uint16_t)m->desired_w, (uint16_t)m->desired_h);
        }
        break;
      }
      case evArrange: {
        layout_arrange_t *a = (layout_arrange_t *)lparam;
        if (a) {
          irect16_t r = a->rect;
          if (r.w < 1) r.w = 1;
          if (r.h < 1) r.h = 1;
          win->frame = r;
          send_message(win, evResize, 0, NULL);
          // If this window has auto-layout, sync its children
          window_layout_sync(win);
        }
        value = MAKEDWORD((uint16_t)MAX(1, win->frame.w),
                          (uint16_t)MAX(1, win->frame.h));
        break;
      }
      case evHitTest:
        {
          int x = (int16_t)LOWORD(wparam), y = (int16_t)HIWORD(wparam);
          if (!rect_contains_point(get_client_rect(win), (ipoint16_t){x, y})) break;
          for (window_t *item = win->children; item; item = item->next) {
            if (!window_has_state(item, WINDOW_STATE_VISIBLE)) continue;
            irect16_t r = item->frame;
            if (CONTAINS(x, y, r.x, r.y, r.w, r.h)) {
              *(window_t **)lparam = item;
              send_message(item, evHitTest,
                           MAKEDWORD((uint16_t)(x - r.x),
                                     (uint16_t)(y - r.y)),
                           lparam);
            }
          }
        }
        break;
      case evNCLeftButtonUp:
        (void)toolbar_handle_notitle_nc_left_button_up(win, wparam);
        break;
      case evCommand:
        break;
    }
  }
  if (msg == evMeasure) {
    layout_measure_t *m = (layout_measure_t *)lparam;
    if (value == true)
      value = 0;
    if (m && m->desired_w > 0 && m->desired_h > 0) {
      value = MAKEDWORD((uint16_t)m->desired_w, (uint16_t)m->desired_h);
    } else if (value) {
      int w = (int)LOWORD((uint32_t)value);
      int h = (int)HIWORD((uint32_t)value);
      if (w < 1) w = 1;
      if (h < 1) h = 1;
      if (m) {
        m->desired_w = w;
        m->desired_h = h;
      }
      value = MAKEDWORD((uint16_t)w, (uint16_t)h);
    } else {
      int w = frame->w > 0 ? frame->w : 1;
      int h = frame->h > 0 ? frame->h : 1;
      if (m) {
        m->desired_w = w;
        m->desired_h = h;
      }
      value = MAKEDWORD((uint16_t)w, (uint16_t)h);
    }
  } else if (msg == evArrange) {
    value = MAKEDWORD((uint16_t)MAX(1, win->frame.w),
                      (uint16_t)MAX(1, win->frame.h));
  }
  // Draw disabled overlay
  if (window_has_state(win, WINDOW_STATE_DISABLED) &&
      msg == evPaint && win != g_ui_runtime.modal_overlay_parent) {
    uint32_t col = (get_sys_color(brControlBg) & 0x00FFFFFF) | 0x80000000;
    int root_t = titlebar_height(root);
    irect16_t wf = win_frame_in_screen(win, root, root_t);
    // Render in FBO-local coordinates.
    set_viewport_for_fbo(root);
    fill_rect(col, R(wf.x - root->frame.x, wf.y - root->frame.y, wf.w, wf.h));
  }
  if (msg == evPaint && win == g_ui_runtime.modal_overlay_parent) {
    int root_t = titlebar_height(root);
    irect16_t wf = win_frame_in_screen(win, root, root_t);
    set_viewport_for_fbo(root);
    fill_rect(get_sys_color(brModalOverlay), R(wf.x - root->frame.x, wf.y - root->frame.y, wf.w, wf.h));
  }
  // Draw built-in scrollbars on top of window content.
  // Restore the FBO paint state: the overlay above may have changed the
  // projection.  The bars are drawn in the root-relative coordinate space
  // established by paint setup.
  if (msg == evPaint && g_ui_runtime.running &&
      (win->flags & (WINDOW_HSCROLL | WINDOW_VSCROLL))) {
    int root_t = titlebar_height(root);
    irect16_t wf = win_frame_in_screen(win, root, root_t);
    int scroll_x = 0;
    int scroll_y = 0;
    set_viewport_for_fbo(root);
    set_projection(scroll_x, -root_t + scroll_y,
                   root->frame.w + scroll_x, root->frame.h - root_t + scroll_y);
    set_scissor_fbo(root, (irect16_t){
      wf.x - root->frame.x, wf.y - root->frame.y, wf.w, wf.h
    });
    draw_builtin_scrollbars(win);
  }
  return value;
}

// Post message to window queue (asynchronous).
// Keeps Orion-posted lifecycle/repaint work separate from the platform's live
// input queue so repost_messages() cannot accidentally consume fresh mouse/
// keyboard events while flushing paints.
void post_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  for (uint8_t w = queue.write, r = queue.read; r != w; r++) {
    if (queue.messages[r].target == win &&
        queue.messages[r].msg == msg)
    {
      if (msg == evHttpProgress || msg == evThemeChanged) {
        // These messages carry a meaningful payload in wparam/lparam that must
        // reflect the latest value, not the value at the time of the first post.
        free_posted_lparam(msg, queue.messages[r].lparam);
        queue.messages[r].wparam = wparam;
        queue.messages[r].lparam = lparam;
      } else if (msg == evNCPaint && wparam == 0) {
        queue.messages[r].wparam = 0;
      } else {
        free_posted_lparam(msg, lparam);
      }
      return;
    }
  }

  queue.messages[queue.write++] = (msg_t) {
    .target = win,
    .msg = msg,
    .wparam = wparam,
    .lparam = lparam,
  };

  // Wake get_message() so the caller's while-loop exits and repost_messages()
  // can process the newly-queued message this iteration.
  wake_event_loop();
}

// Check whether 'target' is still a live window reachable from 'list'.
// Called by dispatch_message() before routing a posted Orion event to guard
// against dispatching to a window that was destroyed after post_message()
// was called.  O(window_count) per call; window counts are small in practice
// (typically < 50).
bool is_valid_window_ptr(window_t *target, window_t *list) {
  if (!target) return false;
  for (window_t *w = list; w; w = w->next) {
    toolbar_state_t *tb = toolbar_get_state(w);
    if (w == target || w->toolbar == target) return true;
    if (is_valid_window_ptr(target, w->children)) return true;
    if (is_valid_window_ptr(target, tb ? tb->children : NULL)) return true;
  }
  return false;
}

void repost_messages(void) {
  bool frame_began = g_ui_runtime.running;
  bool frame_had_paint = false;
  if (frame_began) {
    ui_begin_frame();   // make GL context current, bind platform framebuffer
  }
  for (uint8_t write = queue.write; queue.read != write;) {
    msg_t *m = &queue.messages[queue.read++];
    if (m->target == NULL) {
      free_posted_lparam(m->msg, m->lparam);
      continue;
    }
    if (m->msg == evRefreshStencil) {
      // Stencil no longer used; discard.
      free_posted_lparam(m->msg, m->lparam);
      continue;
    }
    if (!is_valid_window_ptr(m->target, g_ui_runtime.windows)) {
      free_posted_lparam(m->msg, m->lparam);
      continue;
    }
    if (m->msg == evPaint) frame_had_paint = true;
    send_message(m->target, m->msg, m->wparam, m->lparam);
    free_posted_lparam(m->msg, m->lparam);
  }
  // Composite baked root-window FBO textures to the screen. A move can
  // request a blit without posting evPaint (the surfaces are already valid).
  bool want_composite = frame_had_paint || g_ui_runtime.needs_composite;
  g_ui_runtime.needs_composite = false;
  if (frame_began && want_composite) {
    composite_root_windows();
  }
  char screenshot_path[1024];
  int screenshot_quality = 90;
  bool quit_after_screenshot = false;
  if (frame_began && ui_dequeue_screenshot_for_frame(frame_had_paint,
        screenshot_path, sizeof(screenshot_path), &screenshot_quality,
        &quit_after_screenshot)) {
    ui_save_screenshot(screenshot_path, screenshot_quality);
  }
  // A window procedure may request quit while dispatching.  Still present any
  // frame that was begun; otherwise its completed drawing is never swapped.
  if (frame_began) {
    ui_end_frame();     // present frame (swap buffers / flushBuffer)
  }
  if (quit_after_screenshot) ui_request_quit();
}
