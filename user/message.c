// Message queue and dispatch implementation
// Extracted from mapview/window.c

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

// Forward declaration for kernel/event.c wake-up helper.
extern void wake_event_loop(void);
// Forward declarations for kernel/init.c per-frame rendering.
extern void ui_begin_frame(void);
extern void ui_end_frame(void);

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

// ---- Built-in scrollbar mouse handling --------------------------------------
//
// Computes window-local mouse coordinates from a mouse-message wparam.
// Mouse wparams for child windows arrive in root-relative coords
// (screen_x - root->frame.x); for top-level windows they are already
// window-local.  Subtract only root->frame to get root-client coords,
// which equals canvas-local when the child sits at root-client offset (0,0).
static void sb_local_coords(window_t *win, uint32_t wparam, int *cx, int *cy) {
  window_t *root = get_root_window(win);
  int adj_x = (win->parent && root) ? root->frame.x : 0;
  int adj_y = (win->parent && root) ? root->frame.y : 0;
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

  // When WINDOW_STATUSBAR and WINDOW_HSCROLL are both set, the horizontal bar
  // is merged into the status-bar row.  In that case its geometry is:
  //   y range : [frame.h, frame.h + STATUSBAR_HEIGHT)
  //   x range : [frame.w*20/100, frame.w)        (right 80 %)
  //   track   : (frame.w - h_x_min) - vscroll_width
  bool h_merged = has_h && (win->flags & WINDOW_STATUSBAR);
  int h_x_min   = h_merged ? SB_STATUS_SPLIT_X(win->frame.w) : 0;
  int h_y_min   = h_merged ? win->frame.h : win->frame.h - SCROLLBAR_WIDTH;
  int h_y_max   = h_merged ? win->frame.h + STATUSBAR_HEIGHT : win->frame.h;
  // In merged mode the resize corner is always at the far right (SCROLLBAR_WIDTH wide),
  // so always exclude it from the hscroll track.  In non-merged mode only exclude when vscroll is present.
  int h_track   = (win->frame.w - h_x_min) - (h_merged ? SCROLLBAR_WIDTH : (has_v ? SCROLLBAR_WIDTH : 0));

  // When merged, the vscroll is not shortened by the hscroll row (which lives
  // in the status bar, outside the content area).
  int v_track = win->frame.h - (has_h && !h_merged ? SCROLLBAR_WIDTH : 0);

  // Handle ongoing drag (captured move / button-up) regardless of enabled state
  if (msg == kWindowMessageMouseMove || msg == kWindowMessageLeftButtonUp) {
    if (win->hscroll.dragging) {
      int cx, cy; sb_local_coords(win, wparam, &cx, &cy);
      // Effective track between arrow buttons
      int eff_track = h_track - 2 * SCROLLBAR_WIDTH;
      int lx_eff = (eff_track > 0) ? (cx - h_x_min) - SCROLLBAR_WIDTH : (cx - h_x_min);
      int tl  = builtin_sb_thumb_len_msg(&win->hscroll, eff_track > 0 ? eff_track : h_track);
      if (msg == kWindowMessageMouseMove) {
        int tp = (eff_track > 0 ? eff_track : h_track) - tl;
        int tr = win->hscroll.max_val - win->hscroll.min_val - win->hscroll.page;
        if (tp > 0 && tr > 0) {
          int new_pos = sb_clamp_msg(&win->hscroll,
              win->hscroll.drag_start_pos + (lx_eff - win->hscroll.drag_start_mouse) * tr / tp);
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
      int eff_track = v_track - 2 * SCROLLBAR_WIDTH;
      int cy_eff    = (eff_track > 0) ? cy - SCROLLBAR_WIDTH : cy;
      int tl    = builtin_sb_thumb_len_msg(&win->vscroll, eff_track > 0 ? eff_track : v_track);
      if (msg == kWindowMessageMouseMove) {
        int tp = (eff_track > 0 ? eff_track : v_track) - tl;
        int tr = win->vscroll.max_val - win->vscroll.min_val - win->vscroll.page;
        if (tp > 0 && tr > 0) {
          int new_pos = sb_clamp_msg(&win->vscroll,
              win->vscroll.drag_start_pos + (cy_eff - win->vscroll.drag_start_mouse) * tr / tp);
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
  if (has_h && cy >= h_y_min && cy < h_y_max &&
      cx >= h_x_min && cx < win->frame.w) {
    if (!win->hscroll.enabled) return 1; // consume click but do nothing
    int lx = cx - h_x_min;  // position within the hscroll strip
    if (lx >= h_track) return 1; // corner square
    // Arrow buttons
    if (h_track >= 2 * SCROLLBAR_WIDTH) {
      if (lx < SCROLLBAR_WIDTH) {
        // Left arrow — scroll by one unit
        int new_pos = sb_clamp_msg(&win->hscroll, win->hscroll.pos - 1);
        if (new_pos != win->hscroll.pos) {
          win->hscroll.pos = new_pos;
          send_message(win, kWindowMessageHScroll, (uint32_t)new_pos, NULL);
          invalidate_window(win);
        }
        return 1;
      }
      if (lx >= h_track - SCROLLBAR_WIDTH) {
        // Right arrow — scroll by one unit
        int new_pos = sb_clamp_msg(&win->hscroll, win->hscroll.pos + 1);
        if (new_pos != win->hscroll.pos) {
          win->hscroll.pos = new_pos;
          send_message(win, kWindowMessageHScroll, (uint32_t)new_pos, NULL);
          invalidate_window(win);
        }
        return 1;
      }
      // Thumb drag in effective track between buttons
      int eff_track = h_track - 2 * SCROLLBAR_WIDTH;
      int lx_eff = lx - SCROLLBAR_WIDTH;
      if (eff_track > 0) {
        int tl = builtin_sb_thumb_len_msg(&win->hscroll, eff_track);
        int to = builtin_sb_thumb_off_msg(&win->hscroll, eff_track, tl);
        if (lx_eff >= to && lx_eff < to + tl) {
          win->hscroll.dragging         = true;
          win->hscroll.drag_start_mouse = lx_eff;
          win->hscroll.drag_start_pos   = win->hscroll.pos;
          set_capture(win);
        } else {
          int new_pos = sb_clamp_msg(&win->hscroll,
              win->hscroll.pos + (lx_eff < to ? -win->hscroll.page : win->hscroll.page));
          if (new_pos != win->hscroll.pos) {
            win->hscroll.pos = new_pos;
            send_message(win, kWindowMessageHScroll, (uint32_t)new_pos, NULL);
            invalidate_window(win);
          }
        }
      }
    } else {
      // Narrow track — no buttons, plain thumb behaviour
      int tl = builtin_sb_thumb_len_msg(&win->hscroll, h_track);
      int to = builtin_sb_thumb_off_msg(&win->hscroll, h_track, tl);
      if (lx >= to && lx < to + tl) {
        win->hscroll.dragging         = true;
        win->hscroll.drag_start_mouse = lx;
        win->hscroll.drag_start_pos   = win->hscroll.pos;
        set_capture(win);
      } else {
        int new_pos = sb_clamp_msg(&win->hscroll,
            win->hscroll.pos + (lx < to ? -win->hscroll.page : win->hscroll.page));
        if (new_pos != win->hscroll.pos) {
          win->hscroll.pos = new_pos;
          send_message(win, kWindowMessageHScroll, (uint32_t)new_pos, NULL);
          invalidate_window(win);
        }
      }
    }
    return 1;
  }

  // Vertical scrollbar hit — always consume geometry even when disabled
  if (has_v && cx >= win->frame.w - SCROLLBAR_WIDTH && cx < win->frame.w &&
      cy >= 0 && cy < win->frame.h) {
    if (!win->vscroll.enabled) return 1; // consume click but do nothing
    if (cy >= v_track) return 1; // corner square
    // Arrow buttons
    if (v_track >= 2 * SCROLLBAR_WIDTH) {
      if (cy < SCROLLBAR_WIDTH) {
        // Up arrow — scroll by one unit
        int new_pos = sb_clamp_msg(&win->vscroll, win->vscroll.pos - 1);
        if (new_pos != win->vscroll.pos) {
          win->vscroll.pos = new_pos;
          send_message(win, kWindowMessageVScroll, (uint32_t)new_pos, NULL);
          invalidate_window(win);
        }
        return 1;
      }
      if (cy >= v_track - SCROLLBAR_WIDTH) {
        // Down arrow — scroll by one unit
        int new_pos = sb_clamp_msg(&win->vscroll, win->vscroll.pos + 1);
        if (new_pos != win->vscroll.pos) {
          win->vscroll.pos = new_pos;
          send_message(win, kWindowMessageVScroll, (uint32_t)new_pos, NULL);
          invalidate_window(win);
        }
        return 1;
      }
      // Thumb drag in effective track between buttons
      int eff_track = v_track - 2 * SCROLLBAR_WIDTH;
      int cy_eff = cy - SCROLLBAR_WIDTH;
      if (eff_track > 0) {
        int tl = builtin_sb_thumb_len_msg(&win->vscroll, eff_track);
        int to = builtin_sb_thumb_off_msg(&win->vscroll, eff_track, tl);
        if (cy_eff >= to && cy_eff < to + tl) {
          win->vscroll.dragging         = true;
          win->vscroll.drag_start_mouse = cy_eff;
          win->vscroll.drag_start_pos   = win->vscroll.pos;
          set_capture(win);
        } else {
          int new_pos = sb_clamp_msg(&win->vscroll,
              win->vscroll.pos + (cy_eff < to ? -win->vscroll.page : win->vscroll.page));
          if (new_pos != win->vscroll.pos) {
            win->vscroll.pos = new_pos;
            send_message(win, kWindowMessageVScroll, (uint32_t)new_pos, NULL);
            invalidate_window(win);
          }
        }
      }
    } else {
      // Narrow track — no buttons, plain thumb behaviour
      int tl = builtin_sb_thumb_len_msg(&win->vscroll, v_track);
      int to = builtin_sb_thumb_off_msg(&win->vscroll, v_track, tl);
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
            bool active = (_focused == win);
            draw_text_small(win->title, frame->x+2, window_title_bar_y(win),
                            get_sys_color(active ? kColorActiveTitlebarText : kColorInactiveTitlebarText));
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
            fill_rect(get_sys_color(kColorWindowBg), rect.x, rect.y, rect.w, rect.h);
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
                uint32_t col = but->active ? get_sys_color(kColorTextSuccess) : get_sys_color(kColorTextNormal);
                draw_icon16(but->icon, bx, by, get_sys_color(kColorDarkEdge));
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
            send_message(sub, kWindowMessagePaint, wparam, lparam);
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
  // Keep at most one queued instance per (target, msg) pair.
  for (uint8_t w = queue.write, r = queue.read; r != w; r++) {
    if (queue.messages[r].target == win &&
        queue.messages[r].msg == msg)
    {
      return;
    }
  }
  // Add new message
  queue.messages[queue.write++] = (msg_t) {
    .target = win,
    .msg = msg,
    .wparam = wparam,
    .lparam = lparam,
  };
  // Wake up axWaitEvent in get_message() so the main loop calls
  // repost_messages() and processes this newly-queued message.
  wake_event_loop();
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
  if (running) {
    ui_begin_frame();   // make GL context current, bind platform framebuffer
  }
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
    ui_end_frame();     // present frame (swap buffers / flushBuffer)
  }
}
