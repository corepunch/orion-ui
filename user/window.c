// Window management implementation
// Extracted from mapview/window.c

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "user.h"
#include "messages.h"
#include "draw.h"

// Global window state
window_t *windows = NULL;
window_t *_focused = NULL;
window_t *_tracked = NULL;
window_t *_captured = NULL;

extern window_t *_dragging;
extern window_t *_resizing;

// Forward declarations
extern void post_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
extern int send_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

// Window list management
void push_window(window_t *win, window_t **windows) {
  if (!*windows) {
    *windows = win;
  } else {
    window_t *p = *windows;
    while (p->next) p = p->next;
    p->next = win;
  }
}

// Create a new window
window_t* create_window(char const *title,
                        flags_t flags,
                        rect_t const *frame,
                        window_t *parent,
                        winproc_t proc,
                        void *lparam)
{
  window_t *win = malloc(sizeof(window_t));
  memset(win, 0, sizeof(window_t));
  win->frame = *frame;
  win->proc = proc;
  win->flags = flags;
  if (parent) {
    win->id = ++parent->child_id;
  } else {
    bool used[256]={0};
    for (window_t *w = windows; w; w = w->next) {
      used[w->id] = true;
    }
    for (int i = 1; i < 256; i++) {
      if (!used[i]) {
        win->id = i;
      }
    }
    if (win->id == 0) {
      printf("Too many windows open\n");
    }
  }
  win->parent = parent;
  strncpy(win->title, title, sizeof(win->title));
  _focused = win;
  push_window(win, parent ? &parent->children : &windows);
  send_message(win, kWindowMessageCreate, 0, lparam);
  if (parent) {
    invalidate_window(win);
  }
  return win;
}

void *allocate_window_data(window_t *win, size_t size) {
  void *data = malloc(size);
  memset(data, 0, size);
  if (win->userdata) {
    free(win->userdata);
  }
  win->userdata = data;
  return data;
}

// Check if two windows overlap
bool do_windows_overlap(const window_t *a, const window_t *b) {
  if (!a->visible || !b->visible)
    return false;
  return a && b &&
  a->frame.x < b->frame.x + b->frame.w && a->frame.x + a->frame.w > b->frame.x &&
  a->frame.y < b->frame.y + b->frame.h && a->frame.y + a->frame.h > b->frame.y;
}

// Invalidate overlapping windows
static void invalidate_overlaps(window_t *win) {
  for (window_t *t = windows; t; t = t->next) {
    if (t != win && do_windows_overlap(t, win)) {
      invalidate_window(t);
    }
  }
}

// Move window to new position
void move_window(window_t *win, int x, int y) {
  post_message(win, kWindowMessageResize, 0, NULL);
  post_message(win, kWindowMessageRefreshStencil, 0, NULL);

  invalidate_overlaps(win);
  invalidate_window(win);

  win->frame.x = x;
  win->frame.y = y;
}

// Resize window
void resize_window(window_t *win, int new_w, int new_h) {
  post_message(win, kWindowMessageResize, 0, NULL);
  post_message(win, kWindowMessageRefreshStencil, 0, NULL);

  invalidate_overlaps(win);
  invalidate_window(win);

  win->frame.w = new_w > 0 ? new_w : win->frame.w;
  win->frame.h = new_h > 0 ? new_h : win->frame.h;
}

// Remove window from global window list
static void remove_from_global_list(window_t *win) {
  if (win == windows) {
    windows = win->next;
  } else if (windows) {
    for (window_t *w=windows->next,*p=windows;w;p=w,w=w->next) {
      if (w == win) {
        p->next = w->next;
        break;
      }
    }
  }
}

// Remove window hooks
extern void remove_from_global_hooks(window_t *win);

// Remove window from message queue
extern void remove_from_global_queue(window_t *win);

// Clear all child windows
void clear_window_children(window_t *win) {
  for (window_t *item = win->children, *next = item ? item->next : NULL;
       item; item = next, next = next?next->next:NULL) {
    destroy_window(item);
  }
  win->children = NULL;
}

// Destroy a window
void destroy_window(window_t *win) {
  post_message((window_t*)1, kWindowMessageRefreshStencil, 0, NULL);
  invalidate_overlaps(win);
  send_message(win, kWindowMessageDestroy, 0, NULL);
  if (_focused == win) set_focus(NULL);
  if (_captured == win) set_capture(NULL);
  if (_tracked == win) track_mouse(NULL);
  if (_dragging == win) _dragging = NULL;
  if (_resizing == win) _resizing = NULL;
  if (win->toolbar_buttons) free(win->toolbar_buttons);
  remove_from_global_list(win);
  remove_from_global_hooks(win);
  remove_from_global_queue(win);
  clear_window_children(win);
  free(win);
}

// Find window at coordinates
#define CONTAINS(x, y, x1, y1, w1, h1) \
((x1) <= (x) && (y1) <= (y) && (x1) + (w1) > (x) && (y1) + (h1) > (y))

extern int titlebar_height(window_t const *win);
extern int statusbar_height(window_t const *win);

window_t *find_window(int x, int y) {
  window_t *last = NULL;
  for (window_t *win = windows; win; win = win->next) {
    if (!win->visible) continue;
    int t = titlebar_height(win);
    int s = statusbar_height(win);
    if (CONTAINS(x, y, win->frame.x, win->frame.y-t, win->frame.w, win->frame.h+t+s)) {
      last = win;
      if (!win->disabled) {
        send_message(win, kWindowMessageHitTest, MAKEDWORD(x - win->frame.x, y - win->frame.y), &last);
      }
    }
  }
  return last;
}

// Get root window
window_t *get_root_window(window_t *window) {
  return window->parent ? get_root_window(window->parent) : window;
}

// Track mouse over window
void track_mouse(window_t *win) {
  if (_tracked == win)
    return;
  if (_tracked) {
    send_message(_tracked, kWindowMessageMouseLeave, 0, win);
    invalidate_window(_tracked);
  }
  _tracked = win;
}

// Set window capture
void set_capture(window_t *win) {
  _captured = win;
}

// Set focused window
void set_focus(window_t* win) {
  if (win == _focused)
    return;
  if (_focused) {
    _focused->editing = false;
    post_message(_focused, kWindowMessageKillFocus, 0, win);
    invalidate_window(_focused);
  }
  if (win) {
    post_message(win, kWindowMessageSetFocus, 0, _focused);
    invalidate_window(win);
  }
  _focused = win;
}

// Invalidate window (request repaint)
void invalidate_window(window_t *win) {
  if (!win->parent) {
    post_message(win, kWindowMessageNonClientPaint, 0, NULL);
  }
  post_message(win, kWindowMessagePaint, 0, NULL);
}

// Get titlebar Y position
int window_title_bar_y(window_t const *win) {
  return win->frame.y + 2 - titlebar_height(win);
}

// Get child window by ID
window_t *get_window_item(window_t const *win, uint32_t id) {
  for (window_t *item = win->children; item; item = item->next) {
    if (item->id == id) {
      return item;
    }
    window_t *child = get_window_item(item, id);
    if (child) return child;
  }
  return NULL;
}

// Set window item text
void set_window_item_text(window_t *win, uint32_t id, const char *fmt, ...) {
  window_t *item = get_window_item(win, id);
  if (!item) return;
  va_list args;
  va_start(args, fmt);
  vsnprintf(item->title, sizeof(item->title), fmt, args);
  va_end(args);
  invalidate_window(item);
}

// Create window from definition
//extern result_t win_label(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
//extern result_t win_button(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
//extern result_t win_checkbox(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
//extern result_t win_textedit(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
//extern result_t win_combobox(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
extern result_t win_space(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

window_t *create_window2(windef_t const *def, rect_t const *r, window_t *parent) {
  rect_t rect = {r->x, r->y, def->w, def->h};
  window_t *win = create_window(def->text, def->flags, &rect, parent, def->proc, NULL);
  win->id = def->id;
  return win;
}

// Load child windows from definition array
void load_window_children(window_t *win, windef_t const *def) {
  int x = WINDOW_PADDING;
  int y = WINDOW_PADDING;
  for (; def->proc; def++) {
    int w = def->w == -1 ? win->frame.w - WINDOW_PADDING*2 : def->w;
    int h = def->h == 0 ? CONTROL_HEIGHT : def->h;
    if (x + w > win->frame.w - WINDOW_PADDING || def->proc == win_space) {
      x = WINDOW_PADDING;
      for (window_t *child = win->children; child; child = child->next) {
        y = MAX(y, child->frame.y + child->frame.h);
      }
      y += LINE_PADDING;
    }
    if (def->proc == win_space)
      continue;
    window_t *item = create_window2(def, MAKERECT(x, y, w, h), win);
    if (item) {
      x += item->frame.w + LINE_PADDING;
    }
  }
}

// Show or hide window
void show_window(window_t *win, bool visible) {
  post_message(win, kWindowMessageRefreshStencil, 0, NULL);
  if (!visible) {
    invalidate_overlaps(win);
    if (_focused == win) set_focus(NULL);
    if (_captured == win) set_capture(NULL);
    if (_tracked == win) track_mouse(NULL);
  } else {
    move_to_top(win);
    set_focus(win);
  }
  win->visible = visible;
  post_message(win, kWindowMessageShowWindow, visible, NULL);
}

// Check if pointer is a valid window
bool is_window(window_t *win) {
  for (window_t *w = windows; w; w = w->next) {
    if (w == win) return true;
  }
  return false;
}

// Enable or disable window
void enable_window(window_t *win, bool enable) {
  if (!enable && _focused == win) {
    set_focus(NULL);
  }
  win->disabled = !enable;
  invalidate_window(win);
}
