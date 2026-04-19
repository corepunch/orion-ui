// Window management implementation
// Extracted from mapview/window.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

#include "user.h"
#include "messages.h"
#include "draw.h"

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
};

// Forward declarations
extern void post_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
extern int send_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
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

// Internal: allocate and register a window without sending kWindowMessageCreate.
// Callers are responsible for sending kWindowMessageCreate (and invalidating if needed).
static window_t *alloc_window(char const *title, flags_t flags, rect_t const *frame,
                               window_t *parent, winproc_t proc, hinstance_t hinstance) {
  window_t *win = malloc(sizeof(window_t));
  if (!win) return NULL;
  memset(win, 0, sizeof(window_t));
  win->frame = *frame;
  win->proc = proc;
  win->flags = flags;
  // Inherit hinstance from parent for child windows; use supplied value for roots.
  win->hinstance = parent ? parent->hinstance : hinstance;
  if (parent) {
    win->id = ++parent->child_id;
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
window_t* create_window(char const *title,
                        flags_t flags,
                        rect_t const *frame,
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
  };
  int x = frame ? frame->x : 0;
  int y = frame ? frame->y : 0;
  return create_window_from_form(&def, x, y, parent, proc, hinstance, lparam);
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
  if (!a->visible || !b->visible)
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
  post_message(win, kWindowMessageResize, 0, NULL);
  post_message(win, kWindowMessageRefreshStencil, 0, NULL);

  invalidate_overlaps(win);
  invalidate_window(win);

  win->frame.x = x;
  win->frame.y = y;

  invalidate_overlaps(win);
}

// Resize window
void resize_window(window_t *win, int new_w, int new_h) {
  // Update dimensions first so every subsequent call (including the
  // synchronous kWindowMessageResize delivery below) sees the new size.
  win->frame.w = new_w > 0 ? new_w : win->frame.w;
  win->frame.h = new_h > 0 ? new_h : win->frame.h;

  // Notify the window synchronously so child-window resize chains
  // (e.g. doc → canvas) propagate their frames before any queued
  // paint message runs.  Using send_message here prevents a one-frame
  // lag where a child's vertical scrollbar still uses the previous
  // dimensions while the parent's border has already moved.
  send_message(win, kWindowMessageResize, 0, NULL);

  post_message(win, kWindowMessageRefreshStencil, 0, NULL);

  invalidate_overlaps(win);
  invalidate_window(win);
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

// Remove window hooks
extern void remove_from_global_hooks(window_t *win);

// Remove window from message queue
extern void remove_from_global_queue(window_t *win);

// Clear all toolbar child windows
void clear_toolbar_children(window_t *win) {
  while (win->toolbar_children) {
    window_t *tc   = win->toolbar_children;
    window_t *next = tc->next;
    // Detach from parent list before destroy so that any re-entrant traversal
    // (e.g. is_valid_window_ptr, kWindowMessageDestroy) sees only still-live nodes.
    win->toolbar_children = next;
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
  post_message((window_t*)1, kWindowMessageRefreshStencil, 0, NULL);
  invalidate_overlaps(win);
  send_message(win, kWindowMessageDestroy, 0, NULL);
  if (g_ui_runtime.focused == win) set_focus(NULL);
  if (g_ui_runtime.captured == win) set_capture(NULL);
  if (g_ui_runtime.tracked == win) track_mouse(NULL);
  if (g_ui_runtime.dragging == win) g_ui_runtime.dragging = NULL;
  if (g_ui_runtime.resizing == win) g_ui_runtime.resizing = NULL;
  if (g_ui_runtime.toolbar_down_win == win) g_ui_runtime.toolbar_down_win = NULL;
  if (win->toolbar_strip_tex) {
    R_DeleteTexture(win->toolbar_strip_tex);
    win->toolbar_strip_tex = 0;
  }
  remove_from_global_list(win);
  remove_from_global_hooks(win);
  remove_from_global_queue(win);
  clear_toolbar_children(win);
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
  for (window_t *win = g_ui_runtime.windows; win; win = win->next) {
    if (!win->visible) continue;
    if (CONTAINS(x, y, win->frame.x, win->frame.y, win->frame.w, win->frame.h)) {
      last = win;
      int t = titlebar_height(win);
      if (!win->disabled) {
        send_message(win, kWindowMessageHitTest, MAKEDWORD(x - win->frame.x, y - win->frame.y - t), &last);
      }
    }
  }
  return last;
}

// Get root window
window_t *get_root_window(window_t *window) {
  return window->parent ? get_root_window(window->parent) : window;
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
  if (g_ui_runtime.tracked) {
    send_message(g_ui_runtime.tracked, kWindowMessageMouseLeave, 0, win);
    invalidate_window(g_ui_runtime.tracked);
  }
  g_ui_runtime.tracked = win;
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
    g_ui_runtime.focused->editing = false;
    post_message(g_ui_runtime.focused, kWindowMessageKillFocus, 0, win);
    invalidate_window(g_ui_runtime.focused);
  }
  if (win) {
    post_message(win, kWindowMessageSetFocus, 0, g_ui_runtime.focused);
    invalidate_window(win);
  }
  g_ui_runtime.focused = win;
}

// Invalidate window (request repaint).
// Always routes to the root window so that kWindowMessageNonClientPaint
// redraws the panel background (via draw_panel), erasing stale pixels from
// the previous state before kWindowMessagePaint redraws the content.
// For root windows get_root_window() returns win itself, so behaviour is
// identical to the previous implementation.  For child windows the root is
// invalidated, which clears the background and repaints all children —
// necessary to erase, e.g., a stale selection highlight in a child control.
//
// A kWindowMessageRefreshStencil is posted before the paint messages so that
// if the paint messages end up deferred to a later repost_messages() call
// (because they were added during the current processing cycle, beyond the
// captured write index), the stencil is always rebuilt at the current window
// positions before the non-client paint runs.  Without this, a move between
// two repost_messages() calls would leave NonClientPaint using a stale stencil
// from the previous frame, causing the focused border to fail the stencil test
// and not be drawn for that frame.
void invalidate_window(window_t *win) {
  window_t *root = get_root_window(win);
  post_message(root, kWindowMessageRefreshStencil, 0, NULL);
  post_message(root, kWindowMessageNonClientPaint, 0, NULL);
  post_message(root, kWindowMessagePaint, 0, NULL);
}

// Get titlebar Y position (top of the title text row within the window frame)
int window_title_bar_y(window_t const *win) {
  return win->frame.y + 2;
}

// Returns true when the absolute screen Y coordinate 'sy' falls within the
// draggable title-bar row of 'win'.  For windows with WINDOW_TOOLBAR the
// toolbar rows sit below the title bar and must NOT initiate a drag.
// Windows without a toolbar are entirely draggable above client area.
// Windows with WINDOW_NOTITLE have no title row; their toolbar area is the
// only non-client space and may be dragged from freely (e.g. tool palettes).
bool window_in_drag_area(window_t const *win, int sy) {
  if (win->parent) return false;
  int t = titlebar_height(win);
  if (sy < win->frame.y || sy >= win->frame.y + t) return false;
  if (!(win->flags & WINDOW_TOOLBAR) || (win->flags & WINDOW_NOTITLE)) return true;
  // Has both title bar and toolbar: only the title bar row (top TITLEBAR_HEIGHT px) is draggable.
  return sy < win->frame.y + TITLEBAR_HEIGHT;
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
  for (window_t *tc = win->toolbar_children; tc; tc = tc->next) {
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
rect_t get_client_rect(window_t const *win) {
  int t = titlebar_height(win);
  int s = statusbar_height(win);
  bool has_h = (win->flags & WINDOW_HSCROLL) && win->hscroll.visible;
  bool has_v = (win->flags & WINDOW_VSCROLL) && win->vscroll.visible;
  bool h_merged = has_h && (win->flags & WINDOW_STATUSBAR);
  int hstrip = (has_h && !h_merged) ? SCROLLBAR_WIDTH : 0;
  int vstrip = has_v ? SCROLLBAR_WIDTH : 0;
  int cw = win->frame.w - vstrip;
  int ch = win->frame.h - t - s - hstrip;
  if (cw < 0) cw = 0;
  if (ch < 0) ch = 0;
  return (rect_t){0, 0, cw, ch};
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
void adjust_window_rect(rect_t *r, flags_t flags) {
  if (!r) return;
  // Compute non-client heights for the given flags.
  int t = 0;
  if (!(flags & WINDOW_NOTITLE)) t += TITLEBAR_HEIGHT;
  if (flags & WINDOW_TOOLBAR)    t += TB_SPACING + 2 * TOOLBAR_PADDING;  // minimum one toolbar row
  int s = (flags & WINDOW_STATUSBAR) ? STATUSBAR_HEIGHT : 0;
  // Horizontal scrollbar: adds SCROLLBAR_WIDTH to the bottom unless it is
  // merged with the status bar (WINDOW_STATUSBAR also set).
  bool hscroll_standalone = (flags & WINDOW_HSCROLL) && !(flags & WINDOW_STATUSBAR);
  int hstrip = hscroll_standalone ? SCROLLBAR_WIDTH : 0;
  // Vertical scrollbar: adds SCROLLBAR_WIDTH to the right.
  int vstrip = (flags & WINDOW_VSCROLL) ? SCROLLBAR_WIDTH : 0;
  r->y -= t;
  r->w += vstrip;
  r->h += t + s + hstrip;
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
  window_t *win = create_window(def->text, def->flags, &rect, parent, def->proc, 0, NULL);
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

// Include commctl prototypes to get the window procedure declarations used by
// form_ctrl_to_proc().  This avoids duplicating extern declarations that would
// silently drift if a commctl signature changed.
#include "../commctl/commctl.h"

// Map a FORM_CTRL_* type code to the corresponding commctl window procedure.
static winproc_t form_ctrl_to_proc(form_ctrl_type_t type) {
  switch (type) {
    case FORM_CTRL_BUTTON:   return win_button;
    case FORM_CTRL_CHECKBOX: return win_checkbox;
    case FORM_CTRL_LABEL:    return win_label;
    case FORM_CTRL_TEXTEDIT: return win_textedit;
    case FORM_CTRL_MULTIEDIT: return win_multiedit;
    case FORM_CTRL_LIST:     return win_list;
    case FORM_CTRL_COMBOBOX: return win_combobox;
    default:                 return NULL;
  }
}

// Create a window from a form_def_t, instantiating all child controls from
// def->children before firing kWindowMessageCreate on the parent.
// This allows the window proc to find its children already in place during
// kWindowMessageCreate, analogous to WinAPI CreateDialogIndirect behaviour.
window_t *create_window_from_form(form_def_t const *def, int x, int y,
                                  window_t *parent, winproc_t proc,
                                  hinstance_t hinstance, void *lparam) {
  if (!def || !proc) return NULL;

  // Resolve CW_USEDEFAULT for root windows: cascade down from (20, 20).
  // Loop until we find a position not already occupied by another root window,
  // so that windows always cascade rather than stacking on top of each other.
  if (!parent && (x == CW_USEDEFAULT || y == CW_USEDEFAULT)) {
    int cascade_step = 20;
    int nx = 20, ny = 20;
    bool occupied = true;
    while (occupied) {
      occupied = false;
      for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
        if (!w->parent && w->frame.x == nx && w->frame.y == ny) {
          occupied = true;
          nx += cascade_step;
          ny += cascade_step;
          break;
        }
      }
    }
    if (x == CW_USEDEFAULT) x = nx;
    if (y == CW_USEDEFAULT) y = ny;
  }

  rect_t r = {x, y, def->width, def->height};

  // Allocate the parent window without sending kWindowMessageCreate yet.
  window_t *win = alloc_window(def->name ? def->name : "", def->flags, &r, parent, proc, hinstance);
  if (!win) return NULL;

  // Instantiate child controls before the parent proc receives kWindowMessageCreate.
  // Children inherit hinstance from the parent (pass 0 = inherit).
  if (def->children && def->child_count > 0) {
    for (int i = 0; i < def->child_count; i++) {
      const form_ctrl_def_t *cd = &def->children[i];
      winproc_t cp = form_ctrl_to_proc(cd->type);
      if (!cp) continue;
      window_t *child = create_window(cd->text ? cd->text : "", cd->flags,
                                      &cd->frame, win, cp, 0, NULL);
      if (child) child->id = cd->id;
    }
  }

  // Now notify the parent that creation (with children already present) is complete.
  send_message(win, kWindowMessageCreate, 0, lparam);
  // For root windows (no parent), check whether the proc destroyed the window
  // during kWindowMessageCreate (e.g. end_dialog called from within the proc).
  // Child windows are in parent->children, not the global list, so skip the
  // check for them — child self-destruction during create is not a supported pattern.
  if (!parent && !is_window(win)) return NULL;
  if (parent) invalidate_window(win);
  return win;
}

// Show or hide window
void show_window(window_t *win, bool visible) {
  post_message(win, kWindowMessageRefreshStencil, 0, NULL);
  if (!visible) {
    invalidate_overlaps(win);
    if (g_ui_runtime.focused == win) set_focus(NULL);
    if (g_ui_runtime.captured == win) set_capture(NULL);
    if (g_ui_runtime.tracked == win) track_mouse(NULL);
  } else {
    move_to_top(win);
    set_focus(win);
  }
  win->visible = visible;
  post_message(win, kWindowMessageShowWindow, visible, NULL);
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
  win->disabled = !enable;
  invalidate_window(win);
}

// ---- Built-in scrollbar API (WinAPI SetScrollInfo / GetScrollInfo style) ----

// Clamp pos to the valid range [min_val .. max_val-page]
static int sb_clamp_range(win_sb_t const *sb, int pos) {
  int max_pos = sb->max_val - sb->page;
  if (max_pos < sb->min_val) max_pos = sb->min_val;
  if (pos < sb->min_val) return sb->min_val;
  if (pos > max_pos)     return max_pos;
  return pos;
}

// Update one built-in scrollbar from a scroll_info_t.
// Auto-shows the bar when content exceeds the viewport; hides it otherwise.
static void set_scroll_info_one(win_sb_t *sb, scroll_info_t const *info) {
  if (info->fMask & SIF_RANGE) {
    sb->min_val = info->nMin;
    sb->max_val = info->nMax;
  }
  if (info->fMask & SIF_PAGE) {
    sb->page = info->nPage;
  }
  if (info->fMask & SIF_POS) {
    sb->pos = sb_clamp_range(sb, info->nPos);
  }
  // Clamp existing pos whenever range or page changes (even without SIF_POS).
  if (info->fMask & (SIF_RANGE | SIF_PAGE)) {
    sb->pos = sb_clamp_range(sb, sb->pos);
  }
  // Automatic show/hide: hide when the whole content fits in the viewport.
  // Only apply auto logic when not overridden by an explicit show_scroll_bar() call.
  if (sb->visible_mode == SB_VIS_HIDE) {
    sb->visible = false; // forced hidden
  } else if (sb->visible_mode == SB_VIS_SHOW) {
    sb->visible = true;  // forced shown
  } else {
    bool should_show = (sb->page < sb->max_val - sb->min_val);
    sb->visible = should_show;
  }
  if (sb->visible && !sb->enabled) {
    // First time visible: default to enabled.
    sb->enabled = true;
  }
}

void set_scroll_info(window_t *win, int bar, scroll_info_t const *info, bool redraw) {
  if (!win || !info) return;
  if (bar == SB_VERT) {
    set_scroll_info_one(&win->vscroll, info);
  } else if (bar == SB_HORZ) {
    set_scroll_info_one(&win->hscroll, info);
  } else { // SB_BOTH
    set_scroll_info_one(&win->hscroll, info);
    set_scroll_info_one(&win->vscroll, info);
  }
  if (redraw) invalidate_window(win);
}

void get_scroll_info(window_t *win, int bar, scroll_info_t *info) {
  if (!win || !info) return;
  if (bar == SB_BOTH) bar = SB_HORZ; // SB_BOTH reads horizontal by convention
  win_sb_t *sb = (bar == SB_VERT) ? &win->vscroll : &win->hscroll;
  if (info->fMask & SIF_RANGE) {
    info->nMin = sb->min_val;
    info->nMax = sb->max_val;
  }
  if (info->fMask & SIF_PAGE) info->nPage = sb->page;
  if (info->fMask & SIF_POS)  info->nPos  = sb->pos;
}

int get_scroll_pos(window_t *win, int bar) {
  if (!win) return 0;
  if (bar == SB_VERT) return win->vscroll.pos;
  return win->hscroll.pos; // SB_HORZ or SB_BOTH → horizontal
}

// Explicitly enable or disable a built-in scrollbar's mouse interactivity.
// Disabled bars remain visible but ignore mouse clicks.
void enable_scroll_bar(window_t *win, int bar, bool enable) {
  if (!win) return;
  if (bar == SB_HORZ || bar == SB_BOTH) win->hscroll.enabled = enable;
  if (bar == SB_VERT || bar == SB_BOTH) win->vscroll.enabled = enable;
  invalidate_window(win);
}

// Show or hide a built-in scrollbar explicitly.
// Calling this locks the bar's visibility so that subsequent set_scroll_info()
// calls do not auto-show or auto-hide it.  To restore auto-visibility mode,
// call reset_scroll_bar_auto(win, bar).
void show_scroll_bar(window_t *win, int bar, bool show) {
  if (!win) return;
  if (bar == SB_HORZ || bar == SB_BOTH) {
    win->hscroll.visible = show;
    win->hscroll.visible_mode = show ? SB_VIS_SHOW : SB_VIS_HIDE;
  }
  if (bar == SB_VERT || bar == SB_BOTH) {
    win->vscroll.visible = show;
    win->vscroll.visible_mode = show ? SB_VIS_SHOW : SB_VIS_HIDE;
  }
  invalidate_window(win);
}

// Restore auto visibility mode for a built-in scrollbar.
// After this call, set_scroll_info() will again auto-show/hide the bar based
// on the content range vs page size, undoing any prior show_scroll_bar() call.
void reset_scroll_bar_auto(window_t *win, int bar) {
  if (!win) return;
  if (bar == SB_HORZ || bar == SB_BOTH) win->hscroll.visible_mode = SB_VIS_AUTO;
  if (bar == SB_VERT || bar == SB_BOTH) win->vscroll.visible_mode = SB_VIS_AUTO;
}
