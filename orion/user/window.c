// Window management implementation
// Extracted from mapview/window.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>

#include "user.h"
#include "messages.h"
#include "draw.h"
#include "theme.h"
#include <orion/kernel/renderer.h>
#include <orion/commctl/commctl.h>

// NeXTSTEP-style database singleton
static database_t *g_app_database = NULL;

void ui_set_database(database_t *db) {
  g_app_database = db;
}

database_t *ui_get_database(void) {
  return g_app_database;
}

static bool streq(const char *a, const char *b) {
  if (!a || !b) return false;
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
      return false;
    a++;
    b++;
  }
  return *a == '\0' && *b == '\0';
}

// Check if window is a layout container (arranges its children).
// Uses flags only - no proc checks, respecting user.dll/commctl.dll boundary.
static bool is_layout_container(const window_t *win) {
  if (!win) return false;
  return (win->flags & WINDOW_LAYOUT_CONTAINER) ||
         (win->flags & WINDOW_AUTO_LAYOUT);
}

static bool layout_child_flex_affects_parent(const window_t *parent, const window_t *child) {
  if (!parent || !child || !(child->flags & WINDOW_FLEXSPACE)) return false;

  bool parent_is_container = is_layout_container(parent);
  bool child_is_container = is_layout_container(child);

  // Horizontal action rows often contain local flex spacers.  If that row is
  // nested in a vertical container, keep the spacer local to the row instead of
  // promoting the whole row to a vertically flexible child.
  if (!child_is_container && parent_is_container &&
      (parent->flags & WINDOW_STACK_HORIZONTAL) &&
      parent->parent && is_layout_container(parent->parent) &&
      !(parent->parent->flags & WINDOW_STACK_HORIZONTAL)) {
    return false;
  }

  // Horizontal stack/flow rows use flex spacers locally.  They should not make
  // an orthogonal parent stack claim extra vertical room.
  if (child_is_container && (child->flags & WINDOW_LAYOUT_CONTAINER)) {
    bool child_horizontal  = (child->flags & WINDOW_STACK_HORIZONTAL) != 0;
    bool parent_horizontal = (parent->flags & WINDOW_STACK_HORIZONTAL) != 0;
    if (child_horizontal != parent_horizontal) return false;
  }

  return true;
}

// Global window state
ui_runtime_state_t g_ui_runtime = {
  .running = false,
  .windows = NULL,
  .focused = NULL,
  .tracked = NULL,
  .captured = NULL,
  .dragging = NULL,
  .resizing = NULL,
  .toolbar_down_win = NULL,
  .modal_overlay_parent = NULL,
  .default_window_x = 20,
  .default_window_y = 20,
  .last_mouse_sx = 0,
  .last_mouse_sy = 0,
  .tracked_toolbar = NULL,
};

// Forward declarations
extern void post_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
extern intptr_t send_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
extern int titlebar_height(window_t const *win);
extern int statusbar_height(window_t const *win);

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

static uint32_t next_child_id(window_t const *parent) {
  if (!parent) return 0;
  uint32_t max_id = 0;
  for (window_t *c = parent->children; c; c = c->next) {
    if (c->id > max_id)
      max_id = c->id;
  }
  toolbar_state_t *tb = window_toolbar_state((window_t *)parent);
  for (window_t *c = tb ? tb->children : NULL; c; c = c->next) {
    if (c->id > max_id)
      max_id = c->id;
  }
  if (max_id == UINT32_MAX)
    return UINT32_MAX;
  return max_id + 1;
}

// Internal: allocate and register a window without sending evCreate.
// Callers are responsible for sending evCreate (and invalidating if needed).
static window_t *alloc_window(char const *title, flags_t flags, irect16_t const *frame,
                               window_t *parent, winproc_t proc, hinstance_t hinstance) {
  window_t *win = malloc(sizeof(window_t));
  if (!win) return NULL;
  memset(win, 0, sizeof(window_t));
  win->frame = *frame;
  win->layout.layout_fixed_w = frame ? frame->w : 0;
  win->layout.layout_fixed_h = frame ? frame->h : 0;
  win->proc = proc;
  // Child controls participate in client-area layout, so they should not
  // reserve a title bar unless a caller explicitly creates a root window.
  if (parent)
    flags |= WINDOW_NOTITLE;
  
  // Phase 3: Merge class defaults with instance flags.
  // Find the class descriptor by proc and OR in default_flags.
  // This replaces the old hardcoded `if (proc == win_space)` check.
  const fe_component_desc_t *class_desc = find_window_class_desc_by_proc(proc);
  if (class_desc)
    flags |= class_desc->default_flags;
  
  win->flags = flags;
  window_set_state(win, WINDOW_STATE_VISIBLE, (flags & WINDOW_HIDDEN) == 0);
  window_set_state(win, WINDOW_STATE_DISABLED, false);
  window_set_state(win, WINDOW_STATE_EDITING, false);
  window_set_state(win, WINDOW_STATE_PRESSED, false);
  window_set_state(win, WINDOW_STATE_HOVERED, false);
  // Inherit hinstance from parent for child windows; use supplied value for roots.
  win->hinstance = parent ? parent->hinstance : hinstance;
  if (parent) {
    win->id = next_child_id(parent);
  } else {
    bool used[256]={0};
    for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
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
  // Default built-in scrollbar visibility to auto so set_scroll_info() can
  // auto-show / auto-hide them.  Without this, memset(0) would leave
  // visible_mode == SB_VIS_HIDE and the bars would never appear.
  if (flags & WINDOW_HSCROLL) win->hscroll.visible_mode = SB_VIS_AUTO;
  if (flags & WINDOW_VSCROLL) win->vscroll.visible_mode = SB_VIS_AUTO;
  g_ui_runtime.focused = win;
  push_window(win, parent ? &parent->children : &g_ui_runtime.windows);
  return win;
}

// Create a new window.
// Delegates to create_window_from_form() so that both creation paths share a
// single implementation.  create_window_from_form() is declared in user.h and
// defined later in this file; the declaration makes the call valid here.
window_t* create_window_proc(char const *title,
                             flags_t flags,
                             irect16_t const *frame,
                             window_t *parent,
                             winproc_t proc,
                             hinstance_t hinstance,
                             void *lparam)
{
  form_def_t def = {
    .name        = title,
    .width       = frame ? frame->w : 0,
    .height      = frame ? frame->h : 0,
    .flags       = flags,
    .children    = NULL,
    .child_count = 0,
    .layout_spacing = 4,
  };
  int x = frame ? frame->x : 0;
  int y = frame ? frame->y : 0;
  return create_window_from_form(&def, x, y, parent, proc, hinstance, lparam);
}

window_t* create_window_class(char const *title,
                              flags_t flags,
                              irect16_t const *frame,
                              window_t *parent,
                              const char *class_name,
                              hinstance_t hinstance,
                              void *lparam)
{
  winproc_t proc = find_window_class_proc(class_name);
  if (!proc) return NULL;
  return create_window_proc(title, flags, frame, parent, proc, hinstance, lparam);
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

// Check if two windows overlap, including their non-client areas (title bar, status bar)
bool do_windows_overlap(const window_t *a, const window_t *b) {
  if (!window_has_state(a, WINDOW_STATE_VISIBLE) ||
      !window_has_state(b, WINDOW_STATE_VISIBLE))
    return false;
  int border = 1;
  int a_x1 = a->frame.x - border,              a_y1 = a->frame.y - border;
  int a_x2 = a->frame.x + a->frame.w + border, a_y2 = a->frame.y + a->frame.h + border;
  int b_x1 = b->frame.x - border,              b_y1 = b->frame.y - border;
  int b_x2 = b->frame.x + b->frame.w + border, b_y2 = b->frame.y + b->frame.h + border;
  return a_x1 < b_x2 && a_x2 > b_x1 && a_y1 < b_y2 && a_y2 > b_y1;
}

// Invalidate overlapping windows
static void invalidate_overlaps(window_t *win) {
  for (window_t *t = g_ui_runtime.windows; t; t = t->next) {
    if (t != win && do_windows_overlap(t, win)) {
      invalidate_window(t);
    }
  }
}

// Move window to new position
void move_window(window_t *win, int x, int y) {
  post_message(win, evResize, 0, NULL);

  invalidate_overlaps(win);
  invalidate_window(win);

  win->frame.x = x;
  win->frame.y = y;

  invalidate_overlaps(win);
}

// Resize window
void resize_window(window_t *win, int new_w, int new_h) {
  // Update dimensions first so every subsequent call (including the
  // synchronous evResize delivery below) sees the new size.
  win->frame.w = new_w > 0 ? new_w : win->frame.w;
  win->frame.h = new_h > 0 ? new_h : win->frame.h;

  // Notify the window synchronously so child-window resize chains
  // (e.g. doc → canvas) propagate their frames before any queued
  // paint message runs.  Using send_message here prevents a one-frame
  // lag where a child's vertical scrollbar still uses the previous
  // dimensions while the parent's border has already moved.
  send_message(win, evResize, 0, NULL);
  window_layout_sync(win);

  invalidate_overlaps(win);
  invalidate_window(win);
}

void set_default_window_position(int x, int y) {
  g_ui_runtime.default_window_x = x;
  g_ui_runtime.default_window_y = y;
}

// Remove window from global window list
static void remove_from_global_list(window_t *win) {
  if (win == g_ui_runtime.windows) {
    g_ui_runtime.windows = win->next;
  } else if (g_ui_runtime.windows) {
    for (window_t *w=g_ui_runtime.windows->next,*p=g_ui_runtime.windows;w;p=w,w=w->next) {
      if (w == win) {
        p->next = w->next;
        break;
      }
    }
  }
}

static void remove_from_parent_child_list(window_t *win) {
  if (!win || !win->parent) return;

  toolbar_state_t *parent_tb = window_toolbar_state(win->parent);

  window_t **lists[] = {
    &win->parent->children,
    parent_tb ? &parent_tb->children : NULL,
  };

  for (size_t i = 0; i < sizeof(lists) / sizeof(lists[0]); i++) {
    window_t **link = lists[i];
    while (*link) {
      if (*link == win) {
        *link = win->next;
        win->next = NULL;
        return;
      }
      link = &(*link)->next;
    }
  }
}

// Remove window hooks
extern void remove_from_global_hooks(window_t *win);

// Remove window from message queue
extern void remove_from_global_queue(window_t *win);

// Clear all toolbar child windows
void clear_toolbar_children(window_t *win) {
  toolbar_state_t *tb = window_toolbar_state(win);
  while (tb && tb->children) {
    window_t *tc   = tb->children;
    window_t *next = tc->next;
    // Detach from parent list before destroy so that any re-entrant traversal
    // (e.g. is_valid_window_ptr, evDestroy) sees only still-live nodes.
    tb->children = next;
    tc->next = NULL;
    destroy_window(tc);
  }
}

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
  window_t *root = get_root_window(win);
  invalidate_overlaps(win);
  if (win->role == WINDOW_ROLE_HOST && win->active_page)
    set_host_page(win, NULL);
  if (win->role == WINDOW_ROLE_PAGE && win->page_host)
    set_host_page(win->page_host, NULL);
  send_message(win, evDestroy, 0, NULL);
  if (g_ui_runtime.focused == win) set_focus(NULL);
  if (g_ui_runtime.captured == win) set_capture(NULL);
  if (g_ui_runtime.tracked == win) track_mouse(NULL);
  if (g_ui_runtime.tracked_toolbar == win) g_ui_runtime.tracked_toolbar = NULL;
  if (g_ui_runtime.dragging == win) g_ui_runtime.dragging = NULL;
  if (g_ui_runtime.resizing == win) g_ui_runtime.resizing = NULL;
  if (g_ui_runtime.toolbar_down_win == win) g_ui_runtime.toolbar_down_win = NULL;
  if (win->parent && win->parent->toolbar == win)
    win->parent->toolbar = NULL;
  if (win->parent)
    remove_from_parent_child_list(win);
  else
    remove_from_global_list(win);
  remove_from_global_hooks(win);
  remove_from_global_queue(win);
  clear_toolbar_children(win);
  clear_window_children(win);
  // Release the per-window render target before freeing the struct.
  R_DestroyWindowTarget(&win->surface_fbo, &win->surface_tex,
                        &win->surface_w, &win->surface_h);
  free(win);

  if (root && root != win && is_window(root) && window_has_state(root, WINDOW_STATE_VISIBLE)) {
    invalidate_window(root);
  }
  for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
    if (window_has_state(w, WINDOW_STATE_VISIBLE))
      invalidate_window(w);
  }
}

// Find window at coordinates
#define CONTAINS(x, y, x1, y1, w1, h1) \
((x1) <= (x) && (y1) <= (y) && (x1) + (w1) > (x) && (y1) + (h1) > (y))

extern int titlebar_height(window_t const *win);
extern int statusbar_height(window_t const *win);

window_t *find_window(int x, int y) {
  window_t *last = NULL;
  for (window_t *win = g_ui_runtime.windows; win; win = win->next) {
    if (!window_has_state(win, WINDOW_STATE_VISIBLE)) continue;
    if (CONTAINS(x, y, win->frame.x, win->frame.y, win->frame.w, win->frame.h)) {
      last = win;
      int t = titlebar_height(win);
      if (!window_has_state(win, WINDOW_STATE_DISABLED)) {
        send_message(win, evHitTest, MAKEDWORD(x - win->frame.x, y - win->frame.y - t), &last);
      }
    }
  }
  return last;
}

// Get root window
window_t *get_root_window(window_t *window) {
  return window->parent ? get_root_window(window->parent) : window;
}

int window_screen_x(window_t const *win) {
  if (!win) return 0;
  if (!win->parent) return win->frame.x;
  return window_screen_x(win->parent) + win->frame.x;
}

int window_screen_y(window_t const *win) {
  if (!win) return 0;
  if (!win->parent) return win->frame.y;
  return window_screen_y(win->parent) + titlebar_height(win->parent) + win->frame.y;
}

irect16_t center_window_rect(irect16_t frame_rect, window_t const *owner) {
  int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
  int sh = ui_get_system_metrics(kSystemMetricScreenHeight);
  int top_padding = 40; // Minimum padding from the top of the screen to avoid overlapping with system UI elements
  window_t *root = owner ? get_root_window((window_t *)owner) : NULL;

  if (root) {
    frame_rect.x = root->frame.x + (root->frame.w - frame_rect.w) / 2;
    frame_rect.y = root->frame.y + (root->frame.h - frame_rect.h) / 2;
  } else if (sw > 0 && sh > 0) {
    frame_rect.x = (sw - frame_rect.w) / 2;
    frame_rect.y = (sh - frame_rect.h) / 2;
  } else {
    frame_rect.x = 0;
    frame_rect.y = 0;
  }

  if (sw > 0) {
    int max_x = MAX(0, sw - frame_rect.w);
    frame_rect.x = MAX(0, MIN(frame_rect.x, max_x));
  }

  if (sh > 0) {
    int min_y = (sh - frame_rect.h >= top_padding) ? top_padding : 0;
    int max_y = MAX(0, sh - frame_rect.h);
    if (min_y > max_y) min_y = max_y;
    frame_rect.y = MAX(min_y, MIN(frame_rect.y, max_y));
  }

  return frame_rect;
}

// Find the first descendant (depth-first) with BUTTON_DEFAULT set.
// Analogous to DM_GETDEFID in WinAPI dialog management.
window_t *find_default_button(window_t *win) {
  for (window_t *child = win ? win->children : NULL; child; child = child->next) {
    if (child->flags & BUTTON_DEFAULT) return child;
    window_t *found = find_default_button(child);
    if (found) return found;
  }
  return NULL;
}

// Track mouse over window
void track_mouse(window_t *win) {
  if (g_ui_runtime.tracked == win)
    return;
  window_t *prev = g_ui_runtime.tracked;
  g_ui_runtime.tracked = win;
  if (prev) {
    send_message(prev, evMouseLeave, 0, win);
    if (is_window(prev))
      invalidate_window(prev);
  }
}

// Set window capture
void set_capture(window_t *win) {
  g_ui_runtime.captured = win;
}

// Set focused window
void set_focus(window_t* win) {
  if (win == g_ui_runtime.focused)
    return;
  if (g_ui_runtime.focused) {
    window_set_state(g_ui_runtime.focused, WINDOW_STATE_EDITING, false);
    post_message(g_ui_runtime.focused, evKillFocus, 0, win);
    invalidate_window(g_ui_runtime.focused);
  }
  if (win) {
    post_message(win, evSetFocus, 0, g_ui_runtime.focused);
    invalidate_window(win);
  }
  g_ui_runtime.focused = win;
}

// Invalidate window (request repaint).
// Always routes to the root window so that evNCPaint redraws the panel
// background, erasing stale pixels from the previous state before evPaint
// redraws the content.
void invalidate_window(window_t *win) {
  window_t *root = get_root_window(win);
  post_message(root, evNCPaint, 0, NULL);
  post_message(root, evPaint, 0, NULL);
}

// Returns true when the absolute screen Y coordinate 'sy' falls within the
// draggable title-bar row of 'win'.  For windows with WINDOW_TOOLBAR the
// toolbar rows sit below the title bar and must NOT initiate a drag.
// Windows without a toolbar are entirely draggable above client area.
// Windows with WINDOW_NOTITLE have no title row; their toolbar area is the
// only non-client space and may be dragged from freely (e.g. tool palettes).
bool window_in_drag_area(window_t const *win, int sy) {
  if (win->parent || (win->flags & WINDOW_NODRAG)) return false;
  int t = titlebar_height(win);
  if (sy < win->frame.y || sy >= win->frame.y + t) return false;
  if (!(win->flags & WINDOW_TOOLBAR) || (win->flags & WINDOW_NOTITLE)) return true;
  // Has both title bar and toolbar: only the title bar row (top TITLEBAR_HEIGHT px) is draggable.
  return sy < win->frame.y + TITLEBAR_HEIGHT;
}

// Get child window by ID
window_t *get_window_item(window_t const *win, uint32_t id) {
  toolbar_state_t *tb = window_toolbar_state((window_t *)win);
  for (window_t *item = win->children; item; item = item->next) {
    if (item->id == id) {
      return item;
    }
    window_t *child = get_window_item(item, id);
    if (child) return child;
  }
  for (window_t *tc = tb ? tb->children : NULL; tc; tc = tc->next) {
    if (tc->id == id) return tc;
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

// Returns the client area of win in client coordinates {0, 0, client_w, client_h}.
// Analogous to WinAPI GetClientRect.
irect16_t get_client_rect(window_t const *win) {
  int t = titlebar_height(win);
  int s = statusbar_height(win);
  bool has_h = (win->flags & WINDOW_HSCROLL) && win->hscroll.visible;
  bool has_v = (win->flags & WINDOW_VSCROLL) && win->vscroll.visible;
  bool h_merged = has_h && (win->flags & WINDOW_STATUSBAR);
  bool overlay = get_theme()->scrollbar_overlay;
  // Overlay scrollbars draw over content — no reserved gutter strips.
  int hstrip = (has_h && !h_merged && !overlay) ? SCROLLBAR_WIDTH : 0;
  int vstrip = (has_v && !overlay) ? SCROLLBAR_WIDTH : 0;
  int cw = win->frame.w - vstrip;
  int ch = win->frame.h - t - s - hstrip;
  if (cw < 0) cw = 0;
  if (ch < 0) ch = 0;
  return (irect16_t){0, 0, cw, ch};
}

// Adjusts *r (initially a desired client rect) to include the non-client area.
// Analogous to WinAPI AdjustWindowRectEx (without menu support).
// After the call, r->x/y are the window-top-left offsets relative to the
// desired client origin (r->x is 0, r->y is -titlebar_height), and
// r->w/r->h are the total window dimensions.
// Accounts for: title bar, toolbar (minimum one row), status bar, and
// scrollbar strips indicated by WINDOW_HSCROLL / WINDOW_VSCROLL.
// Note: WINDOW_HSCROLL merged with WINDOW_STATUSBAR does not add extra height
// (the bar is drawn inside the status-bar row in that case).
void adjust_window_rect(irect16_t *r, flags_t flags) {
  if (!r) return;
  // Compute non-client heights for the given flags.
  int t = 0;
  if (!(flags & WINDOW_NOTITLE)) t += TITLEBAR_HEIGHT;
  if (flags & WINDOW_TOOLBAR)    t += TB_SPACING + 2 * TOOLBAR_PADDING;  // minimum one toolbar row
  int s = (flags & WINDOW_STATUSBAR) ? STATUSBAR_HEIGHT : 0;
  // Horizontal scrollbar: adds SCROLLBAR_WIDTH to the bottom unless it is
  // merged with the status bar (WINDOW_STATUSBAR also set), or the active
  // theme uses overlay scrollbars (no reserved gutter).
  bool hscroll_standalone = (flags & WINDOW_HSCROLL) && !(flags & WINDOW_STATUSBAR);
  bool overlay = get_theme()->scrollbar_overlay;
  int hstrip = (hscroll_standalone && !overlay) ? SCROLLBAR_WIDTH : 0;
  // Vertical scrollbar: adds SCROLLBAR_WIDTH to the right (Classic only).
  int vstrip = ((flags & WINDOW_VSCROLL) && !overlay) ? SCROLLBAR_WIDTH : 0;
  r->y -= t;
  r->w += vstrip;
  r->h += t + s + hstrip;
}

window_t *create_window2(windef_t const *def, irect16_t const *r, window_t *parent) {
  irect16_t rect = {r->x, r->y, def->w, def->h};
  window_t *win = create_window(def->text, def->flags, &rect, parent, def->class_name, 0, NULL);
  win->id = def->id;
  return win;
}

// Load child windows from definition array
void load_window_children(window_t *win, windef_t const *def) {
  int x = WINDOW_PADDING;
  int y = WINDOW_PADDING;
  for (; def->class_name; def++) {
    int w = def->w == -1 ? win->frame.w - WINDOW_PADDING*2 : def->w;
    int h = def->h == 0 ? CONTROL_HEIGHT : def->h;
    if (x + w > win->frame.w - WINDOW_PADDING || streq(def->class_name, "space")) {
      x = WINDOW_PADDING;
      for (window_t *child = win->children; child; child = child->next) {
        y = MAX(y, child->frame.y + child->frame.h);
      }
      y += LINE_PADDING;
    }
    if (streq(def->class_name, "space"))
      continue;
    window_t *item = create_window2(def, MAKERECT(x, y, w, h), win);
    if (item) {
      x += item->frame.w + LINE_PADDING;
    }
  }
}

// Create a window from a form_def_t, instantiating all child controls from
// def->children before firing evCreate on the parent.
// This allows the window proc to find its children already in place during
// evCreate, analogous to WinAPI CreateDialogIndirect behaviour.
static void create_form_children(window_t *parent, const form_ctrl_def_t *children,
                                 int child_count);

static void propagate_database_message(window_t *win, database_t *db) {
  if (!win || !db) return;

  send_message(win, evSetDatabase, 0, db);

  for (window_t *child = win->children; child; child = child->next)
    propagate_database_message(child, db);

  toolbar_state_t *tb = window_toolbar_state(win);
  for (window_t *child = tb ? tb->children : NULL; child; child = child->next)
    propagate_database_message(child, db);
}

static bool form_children_use_parent_links(const form_ctrl_def_t *children, int child_count) {
  if (!children || child_count <= 0) return false;
  for (int i = 0; i < child_count; i++) {
    if (children[i].parent != 0)
      return true;
  }
  return false;
}

static bool form_children_have_parent(const form_ctrl_def_t *children, int child_count,
                                      uint32_t parent_id) {
  if (!children || child_count <= 0 || parent_id == 0) return false;
  for (int i = 0; i < child_count; i++) {
    if (children[i].parent == parent_id)
      return true;
  }
  return false;
}

static void warn_missing_form_class(const form_ctrl_def_t *cd,
                                    const char *scope,
                                    uint32_t parent_id) {
  if (!cd || !scope) return;
  fprintf(stderr,
          "create_window_from_form: class '%s' not found (scope=%s, id=%u, name=%s, parent=%u)\n",
          cd->class_name ? cd->class_name : "<null>",
          scope,
          (unsigned)cd->id,
          cd->name ? cd->name : "<null>",
          (unsigned)parent_id);
}

static void create_form_children_flat(window_t *parent, const form_ctrl_def_t *children,
                                      int child_count, uint32_t parent_id) {
  if (!parent || !children || child_count <= 0) return;

  for (int i = 0; i < child_count; i++) {
    const form_ctrl_def_t *cd = &children[i];
    if (cd->parent != parent_id) {
      continue;
    }

    winproc_t cp = find_window_class_proc(cd->class_name);
    if (!cp) {
      warn_missing_form_class(cd, "flat", parent_id);
      continue;
    }

    // Apply class defaults for dimensions and flags
    const fe_component_desc_t *class_desc = find_window_class_desc(cd->class_name);
    int16_t child_w = cd->size.w;
    int16_t child_h = cd->size.h;
    flags_t child_flags = cd->flags;
    uint8_t child_h_align = cd->h_align;
    uint8_t child_v_align = cd->v_align;
    
    if (class_desc) {
      // Apply default dimensions if not explicitly specified
      if (child_w == 0 && class_desc->default_layout_size.w > 0)
        child_w = class_desc->default_layout_size.w;
      if (child_h == 0 && class_desc->default_layout_size.h > 0)
        child_h = class_desc->default_layout_size.h;
      
      // Merge class default flags with instance flags
      child_flags |= class_desc->default_flags;

      // Use class default alignment if not explicitly set
      if (child_h_align == 0)
        child_h_align = class_desc->default_h_align;
      if (child_v_align == 0)
        child_v_align = class_desc->default_v_align;
    }

    irect16_t child_frame = {0, 0, child_w, child_h};
    window_t *child = create_window(cd->text ? cd->text : "", child_flags,
                                    &child_frame, parent, cp, 0, (void *)cd);
    if (!child) continue;
    
    child->id = cd->id;
    child->context_menu = cd->context_menu;
    child->context_menu_count = cd->context_menu_count;
    child->layout.h_align = child_h_align;
    child->layout.v_align = child_v_align;
    child->layout.layout_margin = cd->margin;
    child->layout.layout_padding = cd->padding;
    child->layout.layout_spacing = cd->layout_spacing;

    if (form_children_have_parent(children, child_count, child->id))
      create_form_children_flat(child, children, child_count, child->id);

    if (child->flags & WINDOW_AUTO_LAYOUT)
      window_layout_sync(child);
  }

  // Propagate WINDOW_FLEXSPACE from children to parent
  // This allows flexible controls (like reportview/multiedit) to automatically
  // make their container windows flexible without explicit flags in XML
  if (parent && parent->children) {
    bool any_child_flexspace = false;
    for (window_t *child = parent->children; child; child = child->next) {
      if (layout_child_flex_affects_parent(parent, child)) {
        any_child_flexspace = true;
        break;
      }
    }
    if (any_child_flexspace && !(parent->flags & WINDOW_FLEXSPACE)) {
      parent->flags |= WINDOW_FLEXSPACE;
    }
  }
}

window_t *create_window_from_form(form_def_t const *def, int x, int y,
                                  window_t *parent, winproc_t proc,
                                  hinstance_t hinstance, void *lparam) {
  if (!def || !proc) return NULL;
  

  
  if (!(def->flags & WINDOW_AUTO_LAYOUT) && def->child_count > 0) {
    fprintf(stderr, "create_window_from_form: forms with children require auto_layout=true\n");
    return NULL;
  }

  // Resolve CW_USEDEFAULT for root windows: cascade down from the configured
  // default origin.
  // Loop until we find a position not already occupied by another root window,
  // so that windows always cascade rather than stacking on top of each other.
  if (!parent && (x == CW_USEDEFAULT || y == CW_USEDEFAULT)) {
    int nx = g_ui_runtime.default_window_x;
    int ny = g_ui_runtime.default_window_y;
    bool occupied = true;
    while (occupied) {
      occupied = false;
      for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
        if (!w->parent && w->frame.x == nx && w->frame.y == ny) {
          occupied = true;
          nx += DEFAULT_WINDOW_CASCADE_X;
          ny += DEFAULT_WINDOW_CASCADE_Y;
          break;
        }
      }
    }
    if (x == CW_USEDEFAULT) x = nx;
    if (y == CW_USEDEFAULT) y = ny;
  }

  irect16_t r = {x, y, def->width, def->height};

  // Allocate the parent window without sending evCreate yet.
  window_t *win = alloc_window(def->name ? def->name : "", def->flags, &r, parent, proc, hinstance);
  if (!win) return NULL;

  win->role = def->role;
  win->page_toolbar_items = (const toolbar_item_t *)def->toolbar_items;
  win->page_toolbar_count = def->toolbar_count;

  
  if (def->flags & WINDOW_AUTO_LAYOUT)
    win->flags |= WINDOW_AUTO_LAYOUT;
  win->flags &= ~WINDOW_STACK_HORIZONTAL;
  win->layout.layout_spacing    = def->layout_spacing;
  win->layout.layout_padding    = def->padding;
  win->layout.layout_margin     = def->margin;
  // Removed: forced spacing override - respect explicit spacing=0 from forms

  // Instantiate child controls before the parent proc receives evCreate.
  // Children inherit hinstance from the parent (pass 0 = inherit).

  create_form_children(win, def->children, def->child_count);

  // Auto-populate toolbar if defined
  if (def->toolbar_items && def->toolbar_count > 0 && (win->flags & WINDOW_TOOLBAR)) {
    send_message(win, tbSetItems, (uint32_t)def->toolbar_count, (void *)def->toolbar_items);
  }

  // Propagate the global database context through the full child tree.
  // Controls that care consume evSetDatabase; all other recipients ignore it.
  database_t *effective_db = ui_get_database();
  if (effective_db) {
    for (window_t *child = win->children; child; child = child->next)
      propagate_database_message(child, effective_db);
  }

  if (win->flags & WINDOW_AUTO_LAYOUT)
    window_layout_sync(win);

  // Now notify the parent that creation (with children already present) is complete.
  send_message(win, evCreate, 0, lparam);
  if (win->flags & WINDOW_AUTO_LAYOUT)
    window_layout_sync(win);
  // For root windows (no parent), check whether the proc destroyed the window
  // during evCreate (e.g. end_dialog called from within the proc).
  // Child windows are in parent->children, not the global list, so skip the
  // check for them — child self-destruction during create is not a supported pattern.
  if (!parent && !is_window(win)) return NULL;
  if (parent) invalidate_window(win);
  return win;
}

bool set_host_page(window_t *host, window_t *page) {
  if (!host || host->role != WINDOW_ROLE_HOST) {
    fprintf(stderr, "[window] set_host_page rejected host=%p role=%d\n",
            (void *)host, host ? (int)host->role : -1);
    fflush(stderr);
    return false;
  }
  if (page && page->role != WINDOW_ROLE_PAGE) {
    fprintf(stderr, "[window] set_host_page rejected page=%p role=%d\n",
            (void *)page, (int)page->role);
    fflush(stderr);
    return false;
  }
  if (host->active_page == page) return true;

  if (page && page->page_host && page->page_host != host)
    set_host_page(page->page_host, NULL);

  if (host->active_page) {
    send_message(host->active_page, evDeactivate, 0, host);
    host->active_page->page_host = NULL;
  }
  host->active_page = page;
  send_message(host, tbSetItems, page ? (uint32_t)page->page_toolbar_count : 0,
               page ? (void *)page->page_toolbar_items : NULL);
  if (page) {
    page->page_host = host;
    send_message(page, evActivate, 0, host);
  }
  invalidate_window(host);
  return true;
}

static void create_form_children(window_t *parent, const form_ctrl_def_t *children,
                                 int child_count) {
  if (!parent || !children || child_count <= 0) return;

  if (form_children_use_parent_links(children, child_count)) {
    create_form_children_flat(parent, children, child_count, 0);
    return;
  }
  
  for (int i = 0; i < child_count; i++) {
    const form_ctrl_def_t *cd = &children[i];
    winproc_t cp = find_window_class_proc(cd->class_name);
    if (!cp) {
      warn_missing_form_class(cd, "tree", cd->parent);
      continue;
    }

    // Phase 3: Apply class defaults for width/height when form doesn't specify (0).
    // Get class descriptor to check for default dimensions.
    const fe_component_desc_t *class_desc = find_window_class_desc(cd->class_name);
    int16_t effective_w = cd->size.w;
    int16_t effective_h = cd->size.h;
    flags_t child_flags = cd->flags;
    uint8_t child_h_align = cd->h_align;
    uint8_t child_v_align = cd->v_align;
    if (class_desc) {
      if (effective_w == 0 && class_desc->default_layout_size.w > 0)
        effective_w = class_desc->default_layout_size.w;
      if (effective_h == 0 && class_desc->default_layout_size.h > 0)
        effective_h = class_desc->default_layout_size.h;
      child_flags |= class_desc->default_flags;
      if (child_h_align == 0)
        child_h_align = class_desc->default_h_align;
      if (child_v_align == 0)
        child_v_align = class_desc->default_v_align;
    }

    irect16_t child_frame = {0, 0, effective_w, effective_h};
    window_t *child = create_window(cd->text ? cd->text : "", child_flags,
                                    &child_frame, parent, cp, 0, (void *)cd);
    if (!child) continue;
    child->id = cd->id;
    child->context_menu = cd->context_menu;
    child->context_menu_count = cd->context_menu_count;
    child->layout.h_align = child_h_align;
    child->layout.v_align = child_v_align;
    child->layout.layout_margin = cd->margin;
    child->layout.layout_padding = cd->padding;
    child->layout.layout_spacing = cd->layout_spacing;

    if (cd->children && cd->child_count > 0)
      create_form_children(child, cd->children, cd->child_count);

    if (child->flags & WINDOW_AUTO_LAYOUT)
      window_layout_sync(child);
  }

  // Propagate WINDOW_FLEXSPACE from children to parent
  if (parent && parent->children) {
    bool any_child_flexspace = false;
    for (window_t *child = parent->children; child; child = child->next) {
      if (layout_child_flex_affects_parent(parent, child)) {
        any_child_flexspace = true;
        break;
      }
    }
    if (any_child_flexspace && !(parent->flags & WINDOW_FLEXSPACE)) {
      parent->flags |= WINDOW_FLEXSPACE;
    }
  }
}

// Show or hide window
void show_window(window_t *win, bool visible) {
  if (!visible) {
    invalidate_overlaps(win);
    if (g_ui_runtime.focused == win) set_focus(NULL);
    if (g_ui_runtime.captured == win) set_capture(NULL);
    if (g_ui_runtime.tracked == win) track_mouse(NULL);
  } else {
    move_to_top(win);
    if (!(win->flags & WINDOW_NOACTIVATE))
      set_focus(win);
  }
  window_set_state(win, WINDOW_STATE_VISIBLE, visible);
  post_message(win, evShowWindow, visible, NULL);
}

// Check if pointer is a valid window
bool is_window(window_t *win) {
  for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
    if (w == win) return true;
  }
  return false;
}

// Enable or disable window
void enable_window(window_t *win, bool enable) {
  if (!enable && g_ui_runtime.focused == win) {
    set_focus(NULL);
  }
  window_set_state(win, WINDOW_STATE_DISABLED, !enable);
  invalidate_window(win);
}
