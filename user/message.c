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
// Custom SDL event type (defined in kernel/event.c, declared in kernel/kernel.h).
// Listed here to avoid a user→kernel header dependency.
extern Uint32 g_ui_repaint_event;

// Forward declarations
extern void draw_panel(window_t const *win);
extern void draw_window_controls(window_t *win);
extern void draw_statusbar(window_t *win, const char *text);
extern void draw_bevel(rect_t const *r);
extern void draw_button(rect_t const *r, int dx, int dy, bool pressed);
extern void draw_sprite_region(int tex, int x, int y, int w, int h,
                               float u0, float v0, float u1, float v1, float alpha);
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

// ---- Built-in scrollbar mouse handling --------------------------------------
//
// Computes window-local mouse coordinates from a mouse-message wparam.
// Mouse wparams for child windows arrive in root-relative coords:
//   screen_x - root->frame.x.  For top-level windows they are already
//   window-local (screen_x - win->frame.x = screen_x - root->frame.x).
// Subtract only root->frame to get root-client coords (= canvas-local when
// the child sits at root-client offset (0,0), which is the common case).
static void sb_local_coords(window_t *win, uint32_t wparam, int *cx, int *cy) {
  window_t *root = get_root_window(win);
  int adj_x = 0;
  int adj_y = 0;

  /* For child windows, mouse wparam is in root-relative coords.
   * Subtract only the root frame offset to get root-client coords.
   * Top-level windows already receive window-local coords. */
  if (win->parent && root) {
    adj_x = root->frame.x;
    adj_y = root->frame.y;
  }
  *cx = (int16_t)LOWORD(wparam) - adj_x;
  *cy = (int16_t)HIWORD(wparam) - adj_y;
}

static int builtin_sb_thumb_len_msg(win_sb_t const *sb, int track) {
  int range = sb->max_val - sb->min_val;
  if (range <= 0 || sb->page >= range) return track;
  int tl = track * sb->page / range;
  return tl < 8 ? 8 : tl;
}

static int builtin_sb_thumb_off_msg(win_sb_t const *sb, int track, int tl) {
  int travel = sb->max_val - sb->min_val - sb->page;
  if (travel <= 0) return 0;
  int tt = track - tl;
  if (tt <= 0) return 0;
  return (sb->pos - sb->min_val) * tt / travel;
}

static int sb_clamp_msg(win_sb_t const *sb, int pos) {
  int max_pos = sb->max_val - sb->page;
  if (max_pos < sb->min_val) max_pos = sb->min_val;
  if (pos < sb->min_val) return sb->min_val;
  if (pos > max_pos)     return max_pos;
  return pos;
}

// Handle mouse events for a window's built-in scrollbars.
// Returns non-zero if the event was consumed by a scrollbar.
static int handle_builtin_scrollbars(window_t *win, uint32_t msg, uint32_t wparam) {
  bool has_h = (win->flags & WINDOW_HSCROLL) && win->hscroll.visible;
  bool has_v = (win->flags & WINDOW_VSCROLL) && win->vscroll.visible;

  // Handle ongoing drag (captured move / button-up) regardless of enabled state
  if (msg == kWindowMessageMouseMove || msg == kWindowMessageLeftButtonUp) {
    if (win->hscroll.dragging) {
      int cx, cy; sb_local_coords(win, wparam, &cx, &cy);
      int track = win->frame.w - (has_v ? SCROLLBAR_WIDTH : 0);
      int tl    = builtin_sb_thumb_len_msg(&win->hscroll, track);
      if (msg == kWindowMessageMouseMove) {
        int tp = track - tl;
        int tr = win->hscroll.max_val - win->hscroll.min_val - win->hscroll.page;
        if (tp > 0 && tr > 0) {
          int new_pos = sb_clamp_msg(&win->hscroll,
              win->hscroll.drag_start_pos + (cx - win->hscroll.drag_start_mouse) * tr / tp);
          if (new_pos != win->hscroll.pos) {
            win->hscroll.pos = new_pos;
            send_message(win, kWindowMessageHScroll, (uint32_t)new_pos, NULL);
            invalidate_window(win);
          }
        }
      } else {
        win->hscroll.dragging = false;
        set_capture(NULL);
      }
      return 1;
    }
    if (win->vscroll.dragging) {
      int cx, cy; sb_local_coords(win, wparam, &cx, &cy);
      int track = win->frame.h - (has_h ? SCROLLBAR_WIDTH : 0);
      int tl    = builtin_sb_thumb_len_msg(&win->vscroll, track);
      if (msg == kWindowMessageMouseMove) {
        int tp = track - tl;
        int tr = win->vscroll.max_val - win->vscroll.min_val - win->vscroll.page;
        if (tp > 0 && tr > 0) {
          int new_pos = sb_clamp_msg(&win->vscroll,
              win->vscroll.drag_start_pos + (cy - win->vscroll.drag_start_mouse) * tr / tp);
          if (new_pos != win->vscroll.pos) {
            win->vscroll.pos = new_pos;
            send_message(win, kWindowMessageVScroll, (uint32_t)new_pos, NULL);
            invalidate_window(win);
          }
        }
      } else {
        win->vscroll.dragging = false;
        set_capture(NULL);
      }
      return 1;
    }
    return 0;
  }

  if (msg != kWindowMessageLeftButtonDown) return 0;
  if (!has_h && !has_v) return 0;

  int cx, cy;
  sb_local_coords(win, wparam, &cx, &cy);

  // Horizontal scrollbar hit — always consume geometry even when disabled
  if (has_h && cy >= win->frame.h - SCROLLBAR_WIDTH && cy < win->frame.h &&
      cx >= 0 && cx < win->frame.w) {
    if (!win->hscroll.enabled) return 1; // consume click but do nothing
    int track = win->frame.w - (has_v ? SCROLLBAR_WIDTH : 0);
    if (cx >= track) return 1; // corner square
    int tl = builtin_sb_thumb_len_msg(&win->hscroll, track);
    int to = builtin_sb_thumb_off_msg(&win->hscroll, track, tl);
    if (cx >= to && cx < to + tl) {
      win->hscroll.dragging         = true;
      win->hscroll.drag_start_mouse = cx;
      win->hscroll.drag_start_pos   = win->hscroll.pos;
      set_capture(win);
    } else {
      int new_pos = sb_clamp_msg(&win->hscroll,
          win->hscroll.pos + (cx < to ? -win->hscroll.page : win->hscroll.page));
      if (new_pos != win->hscroll.pos) {
        win->hscroll.pos = new_pos;
        send_message(win, kWindowMessageHScroll, (uint32_t)new_pos, NULL);
        invalidate_window(win);
      }
    }
    return 1;
  }

  // Vertical scrollbar hit — always consume geometry even when disabled
  if (has_v && cx >= win->frame.w - SCROLLBAR_WIDTH && cx < win->frame.w &&
      cy >= 0 && cy < win->frame.h) {
    if (!win->vscroll.enabled) return 1; // consume click but do nothing
    int track = win->frame.h - (has_h ? SCROLLBAR_WIDTH : 0);
    if (cy >= track) return 1; // corner square
    int tl = builtin_sb_thumb_len_msg(&win->vscroll, track);
    int to = builtin_sb_thumb_off_msg(&win->vscroll, track, tl);
    if (cy >= to && cy < to + tl) {
      win->vscroll.dragging         = true;
      win->vscroll.drag_start_mouse = cy;
      win->vscroll.drag_start_pos   = win->vscroll.pos;
      set_capture(win);
    } else {
      int new_pos = sb_clamp_msg(&win->vscroll,
          win->vscroll.pos + (cy < to ? -win->vscroll.page : win->vscroll.page));
      if (new_pos != win->vscroll.pos) {
        win->vscroll.pos = new_pos;
        send_message(win, kWindowMessageVScroll, (uint32_t)new_pos, NULL);
        invalidate_window(win);
      }
    }
    return 1;
  }

  return 0;
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
            int bpr = (win->num_toolbar_buttons > 0 && win->frame.w > 0)
                ? MAX(1, win->frame.w / TB_SPACING) : 1;
            int nrows = (win->num_toolbar_buttons > 0)
                ? (int)((win->num_toolbar_buttons + (uint32_t)bpr - 1) / (uint32_t)bpr)
                : 1;
            int total_h = nrows * TOOLBAR_HEIGHT;
            rect_t rect = {win->frame.x+1, win->frame.y-total_h+1, win->frame.w-2, total_h-2};
            draw_bevel(&rect);
            fill_rect(COLOR_PANEL_BG, rect.x, rect.y, rect.w, rect.h);
            bitmap_strip_t *strip = (win->toolbar_strip.tex != 0) ? &win->toolbar_strip : NULL;
            for (uint32_t i = 0; i < win->num_toolbar_buttons; i++) {
              toolbar_button_t const *but = &win->toolbar_buttons[i];
              int row = (int)i / bpr;
              int col = (int)i % bpr;
              int bx = rect.x + col * TB_SPACING + 2;
              int by = rect.y + row * TOOLBAR_HEIGHT + 2;
              if (strip) {
                // Draw button background (pressed/unpressed)
                rect_t btn_r = {bx - 2, by - 2, TB_SPACING - 2, TOOLBAR_HEIGHT - 2};
                draw_button(&btn_r, 1, 1, but->active);
                int px = but->active ? 1 : 0;
                int icon_index = but->icon;
                if (strip->cols > 0) {
                  int scol = icon_index % strip->cols;
                  int srow = icon_index / strip->cols;
                  float u0 = (float)(scol * strip->icon_w) / (float)strip->sheet_w;
                  float v0 = (float)(srow * strip->icon_h) / (float)strip->sheet_h;
                  float u1 = u0 + (float)strip->icon_w / (float)strip->sheet_w;
                  float v1 = v0 + (float)strip->icon_h / (float)strip->sheet_h;
                  draw_sprite_region((int)strip->tex, bx + px, by + px,
                                     strip->icon_w, strip->icon_h, u0, v0, u1, v1, 1.0f);
                }
              } else {
                uint32_t col = but->active ? COLOR_TEXT_SUCCESS : COLOR_TEXT_NORMAL;
                draw_icon16(but->icon, bx, by, COLOR_DARK_EDGE);
                draw_icon16(but->icon, bx-1, by-1, col);
              }
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
        if (win->toolbar_buttons) free(win->toolbar_buttons);
        win->num_toolbar_buttons = wparam;
        win->toolbar_buttons = malloc(sizeof(toolbar_button_t)*wparam);
        memcpy(win->toolbar_buttons, lparam, sizeof(toolbar_button_t)*wparam);
        break;
      case kToolBarMessageSetStrip:
        if (lparam) {
          memcpy(&win->toolbar_strip, lparam, sizeof(bitmap_strip_t));
        } else {
          memset(&win->toolbar_strip, 0, sizeof(bitmap_strip_t));
        }
        invalidate_window(win);
        break;
      case kToolBarMessageSetActiveButton: {
        uint32_t ident = wparam;
        for (uint32_t i = 0; i < win->num_toolbar_buttons; i++) {
          win->toolbar_buttons[i].active = (win->toolbar_buttons[i].ident == (int)ident);
        }
        invalidate_window(win);
        break;
      }
      case kWindowMessageStatusBar:
        if (lparam) {
          strncpy(win->statusbar_text, (const char*)lparam, sizeof(win->statusbar_text) - 1);
          win->statusbar_text[sizeof(win->statusbar_text) - 1] = '\0';
          invalidate_window(win);
        }
        break;
    }
    // Intercept mouse events for built-in scrollbars before calling win->proc
    if ((win->flags & (WINDOW_HSCROLL | WINDOW_VSCROLL)) &&
        (msg == kWindowMessageLeftButtonDown ||
         msg == kWindowMessageMouseMove ||
         msg == kWindowMessageLeftButtonUp)) {
      if (handle_builtin_scrollbars(win, msg, wparam)) return 1;
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
          // When built-in scrollbars are active, drive them directly instead
          // of using the legacy win->scroll[] projection.
          if ((win->flags & (WINDOW_HSCROLL | WINDOW_VSCROLL)) &&
              (win->hscroll.visible || win->vscroll.visible)) {
            bool scrolled = false;
            if ((win->flags & WINDOW_HSCROLL) && win->hscroll.visible &&
                win->hscroll.enabled) {
              int delta = (int16_t)LOWORD(wparam);
              int new_pos = sb_clamp_msg(&win->hscroll, win->hscroll.pos + delta);
              if (new_pos != win->hscroll.pos) {
                win->hscroll.pos = new_pos;
                send_message(win, kWindowMessageHScroll, (uint32_t)new_pos, NULL);
                scrolled = true;
              }
            }
            if ((win->flags & WINDOW_VSCROLL) && win->vscroll.visible &&
                win->vscroll.enabled) {
              int delta = -(int16_t)HIWORD(wparam);
              int new_pos = sb_clamp_msg(&win->vscroll, win->vscroll.pos + delta);
              if (new_pos != win->vscroll.pos) {
                win->vscroll.pos = new_pos;
                send_message(win, kWindowMessageVScroll, (uint32_t)new_pos, NULL);
                scrolled = true;
              }
            }
            if (scrolled) invalidate_window(win);
          } else {
            if (win->flags & WINDOW_HSCROLL) {
              win->scroll[0] = MIN(0, (int)win->scroll[0]+(int16_t)LOWORD(wparam));
            }
            if (win->flags & WINDOW_VSCROLL) {
              win->scroll[1] = MAX(0, (int)win->scroll[1]-(int16_t)HIWORD(wparam));
            }
            if (win->flags & (WINDOW_VSCROLL|WINDOW_HSCROLL)) {
              invalidate_window(win);
            }
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
            int bpr = (win->num_toolbar_buttons > 0 && win->frame.w > 0)
                ? MAX(1, win->frame.w / TB_SPACING) : 1;
            int nrows = (win->num_toolbar_buttons > 0)
                ? (int)((win->num_toolbar_buttons + (uint32_t)bpr - 1) / (uint32_t)bpr)
                : 1;
            int total_h = nrows * TOOLBAR_HEIGHT;
            int base_x = win->frame.x + 2;
            int base_y = win->frame.y - total_h + 2;
            #define CONTAINS(x, y, x1, y1, w1, h1) \
            ((x1) <= (x) && (y1) <= (y) && (x1) + (w1) > (x) && (y1) + (h1) > (y))
            for (uint32_t i = 0; i < win->num_toolbar_buttons; i++) {
              toolbar_button_t *but = &win->toolbar_buttons[i];
              int row = (int)i / bpr;
              int col = (int)i % bpr;
              int bx = base_x + col * TB_SPACING;
              int by = base_y + row * TOOLBAR_HEIGHT;
              if (CONTAINS(x, y, bx, by, TB_SPACING, TOOLBAR_HEIGHT)) {
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
    // Draw built-in scrollbars on top of window content
    if (msg == kWindowMessagePaint && running &&
        (win->flags & (WINDOW_HSCROLL | WINDOW_VSCROLL))) {
      draw_builtin_scrollbars(win);
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
  // Wake up SDL_WaitEvent in get_message() so the main loop calls
  // repost_messages() and processes this newly-queued message.
  if (g_ui_repaint_event != (Uint32)-1) {
    SDL_Event peek;
    if (SDL_PeepEvents(&peek, 1, SDL_PEEKEVENT,
                       g_ui_repaint_event, g_ui_repaint_event) == 0) {
      SDL_Event e;
      SDL_memset(&e, 0, sizeof(e));
      e.type = g_ui_repaint_event;
      SDL_PushEvent(&e);
    }
  }
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
