// Message queue and dispatch implementation
// Extracted from mapview/window.c

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

#include "user.h"
#include "messages.h"
#include "draw.h"
#include "gl_compat.h"

// Message queue structure
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

// Window hooks
typedef struct winhook_s {
  winhook_func_t func;
  uint32_t msg;
  void *userdata;
  struct winhook_s *next;
} winhook_t;

static winhook_t *g_hooks = NULL;

// External references
extern window_t *windows;
extern window_t *_focused;
extern bool running;  // Set to true when graphics are initialized

// Forward declarations
extern void draw_panel(window_t const *win);
extern void draw_window_controls(window_t *win);
extern void draw_statusbar(window_t *win, const char *text);
extern void draw_bevel(rect_t const *r);
extern void paint_window_stencil(window_t const *w);
extern void repaint_stencil(void);
extern void set_fullscreen(void);
extern window_t *get_root_window(window_t *window);

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

// Remove window from message queue
void remove_from_global_queue(window_t *win) {
  for (uint8_t w = queue.write, r = queue.read; r != w; r++) {
    if (queue.messages[r].target == win) {
      queue.messages[r].target = NULL;
    }
  }
}

// Send message to window (synchronous)
int send_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  if (!win) return false;
  rect_t const *frame = &win->frame;
  window_t *root = get_root_window(win);
  int value = 0;
  if (win) {
    // Call registered hooks
    for (winhook_t *hook = g_hooks; hook; hook = hook->next) {
      if (msg == hook->msg) {
        hook->func(win, msg, wparam, lparam, hook->userdata);
      }
    }
    // Handle special messages
    switch (msg) {
      case kWindowMessageNonClientPaint:
        // Skip OpenGL calls if graphics aren't initialized (e.g., in tests)
        if (running) {
          ui_set_stencil_for_window(win->id);
          set_fullscreen();
          if (!(win->flags&WINDOW_TRANSPARENT)) {
            draw_panel(win);
          }
          if (!(win->flags&WINDOW_NOTITLE)) {
            draw_window_controls(win);
            draw_text_small(win->title, frame->x+2, window_title_bar_y(win), -1);
          }
          if (win->flags&WINDOW_TOOLBAR) {
            int t = TOOLBAR_HEIGHT;
            rect_t rect = {win->frame.x+1, win->frame.y-t+1, win->frame.w-2, t-2};
            draw_bevel(&rect);
            fill_rect(COLOR_PANEL_BG, rect.x, rect.y, rect.w, rect.h);
            for (uint32_t i = 0; i < win->num_toolbar_buttons; i++) {
              toolbar_button_t const *but = &win->toolbar_buttons[i];
              uint32_t col = but->active ? COLOR_TEXT_SUCCESS : COLOR_TEXT_NORMAL;
              draw_icon16(but->icon, rect.x + i * TB_SPACING + 2, rect.y + 2, COLOR_DARK_EDGE);
              draw_icon16(but->icon, rect.x + i * TB_SPACING + 1, rect.y + 1, col);
            }
          }
          if (win->flags&WINDOW_STATUSBAR) {
            draw_statusbar(win, win->statusbar_text);
          }
        }
        break;
      case kWindowMessagePaint:
        // Skip OpenGL calls if graphics aren't initialized (e.g., in tests)
        if (running) {
          ui_set_stencil_for_root_window(get_root_window(win)->id);
          set_viewport(&root->frame);
          set_projection(root->scroll[0],
                         root->scroll[1],
                         root->frame.w + root->scroll[0],
                         root->frame.h + root->scroll[1]);
        }
        break;
      case kToolBarMessageAddButtons:
        win->num_toolbar_buttons = wparam;
        win->toolbar_buttons = malloc(sizeof(toolbar_button_t)*wparam);
        memcpy(win->toolbar_buttons, lparam, sizeof(toolbar_button_t)*wparam);
        break;
      case kWindowMessageStatusBar:
        if (lparam) {
          strncpy(win->statusbar_text, (const char*)lparam, sizeof(win->statusbar_text) - 1);
          win->statusbar_text[sizeof(win->statusbar_text) - 1] = '\0';
          invalidate_window(win);
        }
        break;
    }
    // Call window procedure
    if (!(value = win->proc(win, msg, wparam, lparam))) {
      switch (msg) {
        case kWindowMessagePaint:
          for (window_t *sub = win->children; sub; sub = sub->next) {
            sub->proc(sub, kWindowMessagePaint, wparam, lparam);
          }
          break;
        case kWindowMessageWheel:
          if (win->flags & WINDOW_HSCROLL) {
            win->scroll[0] = MIN(0, (int)win->scroll[0]+(int16_t)LOWORD(wparam));
          }
          if (win->flags & WINDOW_VSCROLL) {
            win->scroll[1] = MAX(0, (int)win->scroll[1]-(int16_t)HIWORD(wparam));
          }
          if (win->flags & (WINDOW_VSCROLL|WINDOW_HSCROLL)) {
            invalidate_window(win);
          }
          break;
        case kWindowMessagePaintStencil:
          paint_window_stencil(win);
          break;
        case kWindowMessageHitTest:
          for (window_t *item = win->children; item; item = item->next) {
            rect_t r = item->frame;
            uint16_t x = LOWORD(wparam), y = HIWORD(wparam);
            #define CONTAINS(x, y, x1, y1, w1, h1) \
            ((x1) <= (x) && (y1) <= (y) && (x1) + (w1) > (x) && (y1) + (h1) > (y))
            if (!item->notabstop && CONTAINS(x, y, r.x, r.y, r.w, r.h)) {
              *(window_t **)lparam = item;
            }
            #undef CONTAINS
          }
          break;
        case kWindowMessageNonClientLeftButtonUp:
          if (win->flags&WINDOW_TOOLBAR) {
            uint16_t x = LOWORD(wparam);
            uint16_t y = HIWORD(wparam);
            int _x = win->frame.x + 2;
            int _y = win->frame.y - TOOLBAR_HEIGHT + 2;
            #define CONTAINS(x, y, x1, y1, w1, h1) \
            ((x1) <= (x) && (y1) <= (y) && (x1) + (w1) > (x) && (y1) + (h1) > (y))
            for (uint32_t i = 0; i < win->num_toolbar_buttons; i++) {
              toolbar_button_t *but = &win->toolbar_buttons[i];
              if (CONTAINS(x, y, _x + i * TB_SPACING, _y, 16, 16)) {
                send_message(win, kToolBarMessageButtonClick, but->ident, but);
              }
            }
            #undef CONTAINS
          }
          break;
      }
    }
    // Draw disabled overlay
    if (win->disabled && msg == kWindowMessagePaint) {
      uint32_t col = (COLOR_PANEL_BG & 0x00FFFFFF) | 0x80000000;
      set_viewport(&(rect_t){ 0, 0, ui_get_system_metrics(kSystemMetricScreenWidth), ui_get_system_metrics(kSystemMetricScreenHeight)});
      set_projection(0, 0, ui_get_system_metrics(kSystemMetricScreenWidth), ui_get_system_metrics(kSystemMetricScreenHeight));
      fill_rect(col, win->frame.x, win->frame.y, win->frame.w, win->frame.h);
    }
  }
  return value;
}

// Post message to window queue (asynchronous)
void post_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  // Remove duplicate messages from queue
  for (uint8_t w = queue.write, r = queue.read; r != w; r++) {
    if (queue.messages[r].target == win &&
        queue.messages[r].msg == msg)
    {
      queue.messages[r].target = NULL;
    }
  }
  // Add new message
  queue.messages[queue.write++] = (msg_t) {
    .target = win,
    .msg = msg,
    .wparam = wparam,
    .lparam = lparam,
  };
}

// Check whether 'target' is still a live window (root or descendant).
// This guards against dispatching messages to windows that were destroyed
// after the message was posted (e.g. a button posting invalidate_window on
// itself after end_dialog has already freed it).
// Complexity is O(queue_len * window_count) per repost_messages call; both
// counts are small in practice (queue ≤ 256, windows typically < 50).
static bool is_valid_window_ptr(window_t *target, window_t *list) {
  for (window_t *w = list; w; w = w->next) {
    if (w == target) return true;
    if (is_valid_window_ptr(target, w->children)) return true;
  }
  return false;
}

void repost_messages(void) {
  for (uint8_t write = queue.write; queue.read != write;) {
    msg_t *m = &queue.messages[queue.read++];
    if (m->target == NULL) continue;
    if (m->msg == kWindowMessageRefreshStencil) {
      if (running) {
        repaint_stencil();
      }
      continue;
    }
    if (!is_valid_window_ptr(m->target, windows)) continue;
    send_message(m->target, m->msg, m->wparam, m->lparam);
  }
  if (running) {
    glFlush();
    // SDL_GL_SwapWindow(window);
  }
}
