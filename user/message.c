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
#include "image.h"
#include "icons.h"

// Forward declarations for toolbar child creation.
// These procs live in commctl but are referenced from user/ via extern linkage.
extern result_t win_toolbar_button(window_t *, uint32_t, uint32_t, void *);
extern result_t win_label(window_t *, uint32_t, uint32_t, void *);
extern result_t win_combobox(window_t *, uint32_t, uint32_t, void *);
extern result_t win_textedit(window_t *, uint32_t, uint32_t, void *);
extern result_t win_button(window_t *, uint32_t, uint32_t, void *);
extern bitmap_strip_t *ui_get_sysicon_strip(void);

#define CONTAINS(x, y, x1, y1, w1, h1) \
((x1) <= (x) && (y1) <= (y) && (x1) + (w1) > (x) && (y1) + (h1) > (y))

#define SPRITE_REGION(scol, srow, strip) \
  (float)((scol) * (strip)->icon_w) / (float)(strip)->sheet_w, \
  (float)((srow) * (strip)->icon_h) / (float)(strip)->sheet_h, \
  (float)((scol) * (strip)->icon_w + (strip)->icon_w) / (float)(strip)->sheet_w, \
  (float)((srow) * (strip)->icon_h + (strip)->icon_h) / (float)(strip)->sheet_h


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



// Separator pseudo-proc: draws a 1-pixel vertical divider line.
static result_t win_toolbar_sep(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam) {
  (void)wparam; (void)lparam;
  if (msg == evCreate || msg == evDestroy) return 1;
  if (msg == evPaint) {
    fill_rect(get_sys_color(brDarkEdge), R(win->frame.x + win->frame.w / 2, win->frame.y + 2, 1, win->frame.h - 4));
    return 1;
  }
  return 0;
}

// Destroy all toolbar children of win.
// Must be called before the toolbar_children pointer is repurposed or the
// window is freed, since each child's evDestroy frees its state.
void clear_toolbar_children(window_t *win);  // defined in window.c

// Returns the effective toolbar button size for win.
static int toolbar_effective_bsz(window_t const *win) {
  return (win->toolbar_btn_size > 0) ? win->toolbar_btn_size : TB_SPACING;
}

// Create one toolbar child window at the given toolbar-band-relative position,
// then remove it from parent->children (where create_window puts it) so the
// caller can add it to parent->toolbar_children.
// Frames are relative to the toolbar band top-left (not screen-absolute).
// For BUTTON items: sets btnSetImage if a sysicon or custom strip.
static window_t *create_toolbar_child(window_t *parent, winproc_t proc,
                                       uint32_t id, flags_t extra_flags,
                                       const char *title,
                                       int rel_x, int rel_y, int w, int h,
                                       int icon) {
  rect_t r = {rel_x, rel_y, w, h};
  // Toolbar children use toolbar-band-relative frames and WINDOW_NOTITLE | WINDOW_NOFILL
  // so the framework neither draws a title bar nor fills their background.
  window_t *tc = create_window(title ? title : "",
                                WINDOW_NOTITLE | WINDOW_NOFILL | extra_flags,
                                &r, parent, proc, parent->hinstance, NULL);
  if (!tc) return NULL;
  tc->id = id;
  // create_window() appended tc to parent->children; remove it from there so
  // that the default evPaint handler (which walks children) does
  // not double-paint toolbar items, and so that clear_window_children does not
  // double-free them (clear_toolbar_children handles that instead).
  // Search for tc and splice it out without assuming it is at the tail;
  // re-entrant create paths may have appended additional siblings after tc.
  {
    window_t *prev = NULL;
    window_t *c = parent->children;
    while (c && c != tc) {
      prev = c;
      c = c->next;
    }
    if (c == tc) {
      if (prev) {
        prev->next = tc->next;
      } else {
        parent->children = tc->next;
      }
    }
  }
  tc->next = NULL;
  // Enforce the requested frame dimensions.  Some procs (win_button, win_label)
  // expand frame.w/h during evCreate to fit their text content.
  // Clamping here keeps sequential toolbar layout stable regardless of text length
  // and ensures that an explicitly-provided width (item->w) is always honoured.
  tc->frame.x = rel_x;
  tc->frame.y = rel_y;
  tc->frame.w = w;
  tc->frame.h = h;
  // Wire up icon image for button children.
  if (proc == win_toolbar_button && icon >= 0) {
    if (icon >= SYSICON_BASE) {
      bitmap_strip_t *sys = ui_get_sysicon_strip();
      if (sys) {
        send_message(tc, btnSetImage,
                     (uint32_t)(icon - SYSICON_BASE), sys);
      }
    } else if (parent->toolbar_strip.tex != 0) {
      send_message(tc, btnSetImage,
                   (uint32_t)icon, &parent->toolbar_strip);
    }
  }
  return tc;
}

// Lay out toolbar_item_t[] items and create child windows.
static void layout_toolbar_items(window_t *parent,
                                  const toolbar_item_t *items,
                                  uint32_t n) {
  int bsz     = toolbar_effective_bsz(parent);
  int base_x  = TOOLBAR_BEVEL_WIDTH + TOOLBAR_PADDING;
  int base_y  = TOOLBAR_BEVEL_WIDTH + TOOLBAR_PADDING;
  int field_y = base_y + 2;
  int field_h = bsz > 4 ? (bsz - 4) : bsz;
  int cur_x   = 0;
  bool placed_visual_item = false;
  window_t **tail = &parent->toolbar_children;
  while (*tail) tail = &(*tail)->next;
  for (uint32_t i = 0; i < n; i++) {
    const toolbar_item_t *item = &items[i];
    if (item->type != TOOLBAR_ITEM_SPACER && placed_visual_item)
      cur_x += TOOLBAR_SPACING;
    switch (item->type) {
      case TOOLBAR_ITEM_SPACER:
        cur_x += item->w > 0 ? item->w : TOOLBAR_SPACING_GAP_WIDTH;
        break;
      case TOOLBAR_ITEM_SEPARATOR: {
        int sw = item->w > 0 ? item->w : 6;
        window_t *tc = create_toolbar_child(parent, win_toolbar_sep,
                                             (uint32_t)item->ident, 0, NULL,
                                             base_x + cur_x, base_y, sw, bsz, -1);
        if (!tc) { cur_x += sw; break; }
        *tail = tc; tail = &tc->next;
        cur_x += sw;
        placed_visual_item = true;
        break;
      }
      case TOOLBAR_ITEM_BUTTON: {
        int w = item->w > 0 ? item->w : bsz;
        int icon = item->icon >= 0 ? item->icon : sysicon_missing;
        flags_t extra = item->flags & ~(TOOLBAR_BUTTON_FLAG_ACTIVE | TOOLBAR_BUTTON_FLAG_PRESSED);
        window_t *tc = create_toolbar_child(parent, win_toolbar_button,
                                             (uint32_t)item->ident,
                                             extra,
                                             NULL,
                                             base_x + cur_x, base_y,
                                             w, bsz, icon);
        if (!tc) { cur_x += w; break; }
        if (item->flags & TOOLBAR_BUTTON_FLAG_ACTIVE) tc->value = true;
        *tail = tc; tail = &tc->next;
        cur_x += w;
        placed_visual_item = true;
        break;
      }
      case TOOLBAR_ITEM_LABEL: {
        int w = item->w > 0 ? item->w : (strwidth(item->text ? item->text : "") + TOOLBAR_LABEL_PADDING);
        window_t *tc = create_toolbar_child(parent, win_label,
                                             (uint32_t)item->ident, 0,
                                             item->text,
                                             base_x + cur_x, base_y,
                                             w, bsz, -1);
        if (!tc) { cur_x += w; break; }
        *tail = tc; tail = &tc->next;
        cur_x += w;
        placed_visual_item = true;
        break;
      }
      case TOOLBAR_ITEM_COMBOBOX: {
        int w = item->w > 0 ? item->w : (bsz * TOOLBAR_COMBOBOX_DEFAULT_WIDTH_MULT);
        window_t *tc = create_toolbar_child(parent, win_combobox,
                                             (uint32_t)item->ident, 0,
                                             item->text,
                                             base_x + cur_x, field_y,
                                             w, field_h, -1);
        if (!tc) { cur_x += w; break; }
        *tail = tc; tail = &tc->next;
        cur_x += w;
        placed_visual_item = true;
        break;
      }
      case TOOLBAR_ITEM_TEXTEDIT: {
        int w = item->w > 0 ? item->w : (bsz * 8);
        window_t *tc = create_toolbar_child(parent, win_textedit,
                                             (uint32_t)item->ident, 0,
                                             item->text,
                                             base_x + cur_x, field_y,
                                             w, field_h, -1);
        if (!tc) { cur_x += w; break; }
        *tail = tc; tail = &tc->next;
        cur_x += w;
        placed_visual_item = true;
        break;
      }
    }
  }
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
extern void draw_bevel(rect_t const *r);
extern void draw_button(rect_t const *r, int dx, int dy, bool pressed);
extern void paint_window_stencil(window_t const *w);
extern void repaint_stencil(void);
extern void set_fullscreen(void);
extern window_t *get_root_window(window_t *window);
extern int titlebar_height(window_t const *win);
extern int statusbar_height(window_t const *win);

// Returns win's frame rect in absolute screen coordinates.
// For root windows, frame.x/y are already screen-absolute.
// For child windows, frame.x/y are root-client-space coords; they are mapped
// to screen by adding the root's screen origin and the root's non-client height.
// root_titlebar_h should be titlebar_height(root) — callers that already have
// it pass it in to avoid recomputing.
static rect_t win_frame_in_screen(window_t *win, window_t *root, int root_titlebar_h) {
  if (win == root) return win->frame;
  return (rect_t){root->frame.x + win->frame.x,
                  root->frame.y + root_titlebar_h + win->frame.y,
                  win->frame.w, win->frame.h};
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

// ---- Built-in scrollbar mouse handling --------------------------------------
//
// All mouse events are delivered in the receiving window's own client
// coordinate system (see kernel/event.c), which includes win->scroll[] offset.
// Scrollbar geometry is expressed in *unscrolled* client coords, so we
// subtract win->scroll[] to get the correct hit position.
static void sb_local_coords(window_t *win, uint32_t wparam, int *cx, int *cy) {
  *cx = (int16_t)LOWORD(wparam) - win->scroll[0];
  *cy = (int16_t)HIWORD(wparam) - win->scroll[1];
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

// Clamp new_pos, and if it differs from sb->pos apply it, fire scroll_msg, and invalidate.
// Returns true if the position actually changed.
static bool sb_try_scroll(window_t *win, win_sb_t *sb, uint32_t scroll_msg, int new_pos) {
  new_pos = sb_clamp_msg(sb, new_pos);
  if (new_pos == sb->pos) return false;
  sb->pos = new_pos;
  send_message(win, scroll_msg, (uint32_t)new_pos, NULL);
  invalidate_window(win);
  return true;
}

// Update the scroll position while a thumb drag is in progress.
// pos   — current mouse position along the scroll axis, relative to the scrollbar strip origin
//         (i.e. cx - h_x_min for hscroll; cy for vscroll).
// track — total track length in pixels for this axis.
static void sb_handle_drag_move(window_t *win, win_sb_t *sb, uint32_t scroll_msg,
                                 int pos, int track) {
  int eff_track = track - 2 * SCROLLBAR_WIDTH;
  int pos_eff   = (eff_track > 0) ? pos - SCROLLBAR_WIDTH : pos;
  int tl        = builtin_sb_thumb_len_msg(sb, eff_track > 0 ? eff_track : track);
  int tp        = (eff_track > 0 ? eff_track : track) - tl;
  int tr        = sb->max_val - sb->min_val - sb->page;
  if (tp > 0 && tr > 0) {
    sb_try_scroll(win, sb, scroll_msg,
                  sb->drag_start_pos + (pos_eff - sb->drag_start_mouse) * tr / tp);
  }
}

// Dispatch a mouse-down click at position pos within a scrollbar track of total length track.
// Fires SB_ARROW_STEP scrolls for arrow-button hits, starts a thumb drag, or scrolls by page.
static void sb_handle_track_click(window_t *win, win_sb_t *sb, uint32_t scroll_msg,
                                   int pos, int track) {
  if (track >= 2 * SCROLLBAR_WIDTH) {
    if (pos < SCROLLBAR_WIDTH) {
      sb_try_scroll(win, sb, scroll_msg, sb->pos - SB_ARROW_STEP);
      return;
    }
    if (pos >= track - SCROLLBAR_WIDTH) {
      sb_try_scroll(win, sb, scroll_msg, sb->pos + SB_ARROW_STEP);
      return;
    }
    int eff_track = track - 2 * SCROLLBAR_WIDTH;
    int pos_eff   = pos - SCROLLBAR_WIDTH;
    if (eff_track > 0) {
      int tl = builtin_sb_thumb_len_msg(sb, eff_track);
      int to = builtin_sb_thumb_off_msg(sb, eff_track, tl);
      if (pos_eff >= to && pos_eff < to + tl) {
        sb->dragging         = true;
        sb->drag_start_mouse = pos_eff;
        sb->drag_start_pos   = sb->pos;
        set_capture(win);
      } else {
        sb_try_scroll(win, sb, scroll_msg,
                      sb->pos + (pos_eff < to ? -sb->page : sb->page));
      }
    }
  } else {
    // Narrow track — no room for arrow buttons; plain thumb behaviour
    int tl = builtin_sb_thumb_len_msg(sb, track);
    int to = builtin_sb_thumb_off_msg(sb, track, tl);
    if (pos >= to && pos < to + tl) {
      sb->dragging         = true;
      sb->drag_start_mouse = pos;
      sb->drag_start_pos   = sb->pos;
      set_capture(win);
    } else {
      sb_try_scroll(win, sb, scroll_msg,
                    sb->pos + (pos < to ? -sb->page : sb->page));
    }
  }
}

// Handle mouse events for a window's built-in scrollbars.
// Returns true if the event was consumed by a scrollbar.
static bool handle_builtin_scrollbars(window_t *win, uint32_t msg, uint32_t wparam) {
  bool has_h = (win->flags & WINDOW_HSCROLL) && win->hscroll.visible;
  bool has_v = (win->flags & WINDOW_VSCROLL) && win->vscroll.visible;

  // Non-client heights: mouse coords are delivered in CLIENT space
  // (y=0 = client top, which excludes the title bar and toolbar rows).
  // For child windows these are typically 0 (WINDOW_NOTITLE).
  int t = titlebar_height(win);
  int s = statusbar_height(win);
  // Content height = client area + scrollbar strips (but NOT titlebar/statusbar).
  int content_h = win->frame.h - t - s;

  // When WINDOW_STATUSBAR and WINDOW_HSCROLL are both set, the horizontal bar
  // is merged into the status-bar row.  In that case its geometry is:
  //   y range : [content_h, content_h + STATUSBAR_HEIGHT)
  //   x range : [frame.w*20/100, frame.w)        (right 80 %)
  //   track   : (frame.w - h_x_min) - vscroll_width
  bool h_merged = has_h && (win->flags & WINDOW_STATUSBAR);
  int h_x_min   = h_merged ? SB_STATUS_SPLIT_X(win->frame.w) : 0;
  int h_y_min   = h_merged ? content_h : content_h - SCROLLBAR_WIDTH;
  int h_y_max   = h_merged ? content_h + STATUSBAR_HEIGHT : content_h;
  // In merged mode the resize corner is always at the far right (SCROLLBAR_WIDTH wide),
  // so always exclude it from the hscroll track.  In non-merged mode only exclude when vscroll is present.
  int h_track   = (win->frame.w - h_x_min) - (h_merged ? SCROLLBAR_WIDTH : (has_v ? SCROLLBAR_WIDTH : 0));

  // When merged, the vscroll is not shortened by the hscroll row (which lives
  // in the status bar, outside the content area).
  int v_track = content_h - (has_h && !h_merged ? SCROLLBAR_WIDTH : 0);

  if (msg == evMouseMove || msg == evLeftButtonUp) {
    if (win->hscroll.dragging) {
      int cx, cy; sb_local_coords(win, wparam, &cx, &cy); (void)cy;
      if (msg == evMouseMove)
        sb_handle_drag_move(win, &win->hscroll, evHScroll,
                            cx - h_x_min, h_track);
      else { win->hscroll.dragging = false; set_capture(NULL); }
      return true;
    }
    if (win->vscroll.dragging) {
      int cx, cy; sb_local_coords(win, wparam, &cx, &cy); (void)cx;
      if (msg == evMouseMove)
        sb_handle_drag_move(win, &win->vscroll, evVScroll, cy, v_track);
      else { win->vscroll.dragging = false; set_capture(NULL); }
      return true;
    }
    return false;
  }

  if (msg != evLeftButtonDown &&
      msg != evLeftButtonDoubleClick) return false;
  if (!has_h && !has_v) return false;

  int cx, cy;
  sb_local_coords(win, wparam, &cx, &cy);

  // Horizontal scrollbar hit — always consume geometry even when disabled
  if (has_h && cy >= h_y_min && cy < h_y_max &&
      cx >= h_x_min && cx < win->frame.w) {
    if (!win->hscroll.enabled) return true; // consume click but do nothing
    int lx = cx - h_x_min;  // position within the hscroll strip
    if (lx >= h_track) return true; // corner square
    sb_handle_track_click(win, &win->hscroll, evHScroll, lx, h_track);
    return true;
  }

  // Vertical scrollbar hit — always consume geometry even when disabled
  if (has_v && cx >= win->frame.w - SCROLLBAR_WIDTH && cx < win->frame.w &&
      cy >= 0 && cy < content_h) {
    if (!win->vscroll.enabled) return true; // consume click but do nothing
    if (cy >= v_track) return true; // corner square
    sb_handle_track_click(win, &win->vscroll, evVScroll, cy, v_track);
    return true;
  }

  return false;
}

// Send message to window (synchronous)
int send_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  if (!win) return false;
  rect_t const *frame = &win->frame;
  window_t *root = get_root_window(win);
  int value = 0;
  // Call registered hooks
  for (winhook_t *hook = g_hooks; hook; hook = hook->next) {
    if (msg == hook->msg) {
      hook->func(win, msg, wparam, lparam, hook->userdata);
    }
  }
  // Handle special messages
  switch (msg) {
    case evNCPaint:
      // Skip OpenGL calls if graphics aren't initialized (e.g., in tests)
      if (g_ui_runtime.running) {
        ui_set_stencil_for_window(win->id);
        set_fullscreen();
        if (!(win->flags&WINDOW_TRANSPARENT)) {
          draw_panel(win);
        }
        if (!(win->flags&WINDOW_NOTITLE)) {
          draw_window_controls(win);
          draw_text_small(win->title, frame->x+2, window_title_bar_y(win),
                          get_sys_color(window_has_focus(win) ? brActiveTitlebarText : brInactiveTitlebarText));
        }
        if (win->flags&WINDOW_TOOLBAR) {
          int bsz      = toolbar_effective_bsz(win);
          int title_h  = (win->flags & WINDOW_NOTITLE) ? 0 : TITLEBAR_HEIGHT;
          int total_h  = bsz + 2 * (TOOLBAR_PADDING + TOOLBAR_BEVEL_WIDTH);
          rect_t rect  = {win->frame.x + TOOLBAR_BEVEL_WIDTH,
                          win->frame.y + title_h + TOOLBAR_BEVEL_WIDTH,
                          win->frame.w - 2 * TOOLBAR_BEVEL_WIDTH,
                          total_h - 2 * TOOLBAR_BEVEL_WIDTH};
          draw_bevel(&rect);
          fill_rect(get_sys_color(brWindowBg), R(rect.x, rect.y, rect.w, rect.h));
          // Paint each toolbar child. tc->frame.x/y are toolbar-band-relative,
          // so set up a viewport with (0,0) = toolbar band top-left so each
          // child can draw at its stored coordinates without knowing the parent's
          // screen position.  Restore fullscreen projection afterwards so the
          // status bar and any subsequent code use screen-absolute coordinates.
          rect_t tb_rect = {win->frame.x, win->frame.y + title_h, win->frame.w, total_h};
          set_viewport(&tb_rect);
          set_projection(0, 0, win->frame.w, total_h);
          for (window_t *tc = win->toolbar_children; tc; tc = tc->next) {
            tc->proc(tc, evPaint, 0, NULL);
          }
          set_fullscreen();
        }
        if (win->flags&WINDOW_STATUSBAR) {
          draw_statusbar(win, win->statusbar_text);
        }
      }
      break;
    case evPaint:
      // Skip OpenGL calls if graphics aren't initialized (e.g., in tests)
      if (g_ui_runtime.running) {
        int t = titlebar_height(root);
        ui_set_stencil_for_root_window(get_root_window(win)->id);
        set_viewport(&root->frame);
        // Shift projection so that y=0 maps to the client area top-left
        // (i.e. below the title bar / toolbar).  This makes the window proc
        // coordinate system purely client-relative while allowing scrollbar
        // drawing code (draw_builtin_scrollbars) to address the full frame.
        set_projection(root->scroll[0],
                       -t + root->scroll[1],
                       root->frame.w + root->scroll[0],
                       root->frame.h - t + root->scroll[1]);
        // For scrollable windows, tighten the scissor to the client area so
        // that scrolled content cannot bleed into non-client areas (title bar,
        // toolbar, status bar).  Only applied when a window actually has
        // built-in scrollbars — no scissor state is wasted on non-scrollable
        // windows, and the stencil buffer is not touched at all for this.
        if (win->flags & (WINDOW_HSCROLL | WINDOW_VSCROLL)) {
          int t_win = titlebar_height(win);   /* win's own non-client height */
          rect_t cr = get_client_rect(win);
          rect_t wf = win_frame_in_screen(win, root, t);
          set_clip_rect(NULL, &(rect_t){wf.x, wf.y + t_win, cr.w, cr.h});
        }
      }
      break;
    case tbSetItems:
      // Replace existing toolbar children with new mixed-type item children.
      clear_toolbar_children(win);
      if (wparam > 0 && lparam) {
        layout_toolbar_items(win, (const toolbar_item_t *)lparam, wparam);
      }
      invalidate_window(win);
      break;
    case tbSetStrip:
      if (lparam) {
        memcpy(&win->toolbar_strip, lparam, sizeof(bitmap_strip_t));
      } else {
        memset(&win->toolbar_strip, 0, sizeof(bitmap_strip_t));
      }
      invalidate_window(win);
      break;
    case tbSetActiveButton: {
      // Mark the toolbar child whose id == wparam as active (value=true);
      // clear all others.
      uint32_t ident = wparam;
      for (window_t *tc = win->toolbar_children; tc; tc = tc->next) {
        bool active = (tc->id == ident);
        if (tc->value != active) {
          tc->value = active;
        }
      }
      invalidate_window(win);
      break;
    }
    case tbSetButtonSize: {
      int old_btn_size = win->toolbar_btn_size;
      // Accept 0 (reset to default TB_SPACING) or a positive value >= 8.
      // Values in [1,7] are rejected: bsz is used as a divisor in toolbar
      // column-count calculations (win->frame.w / bsz) and very small sizes
      // would also produce broken layout (sub-pixel buttons, huge row counts).
      int new_btn_size = (int)wparam;
      if (new_btn_size != 0 && new_btn_size < 8) new_btn_size = 8;
      if (old_btn_size != new_btn_size) {
        win->toolbar_btn_size = new_btn_size;
        post_message(win, evRefreshStencil, 0, NULL);
        invalidate_window(get_root_window(win));
      }
      break;
    }
    case tbLoadStrip: {
      // wparam = icon tile size (square, pixels); lparam = const char* path
      // Loads a PNG (with native RGBA transparency) and stores it as a GL
      // texture in win->toolbar_strip.  The window owns the texture; freed on
      // destroy.  Requires graphics to be initialized.
      const char *path = (const char *)lparam;
      int tile_sz = (int)wparam;
      if (!path || tile_sz <= 0 || !g_ui_runtime.running) break;
      int w = 0, h = 0;
      uint8_t *src = load_image(path, &w, &h);
      if (!src) break;
      if (w < tile_sz || h < tile_sz ||
          (w % tile_sz) != 0 || (h % tile_sz) != 0) {
        image_free(src);
        break;
      }
      // Use the PNG's native RGBA data: real artist colors and PNG alpha channel.
      // Free any previously framework-owned texture via the renderer.
      R_DeleteTexture(win->toolbar_strip_tex);
      uint32_t tex = R_CreateTextureRGBA(w, h, src, R_FILTER_NEAREST, R_WRAP_CLAMP);
      image_free(src);
      win->toolbar_strip_tex    = tex;
      win->toolbar_strip.tex    = tex;
      win->toolbar_strip.icon_w = tile_sz;
      win->toolbar_strip.icon_h = tile_sz;
      win->toolbar_strip.cols   = w / tile_sz;
      win->toolbar_strip.sheet_w = w;
      win->toolbar_strip.sheet_h = h;
      invalidate_window(win);
      break;
    }
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
    if (handle_builtin_scrollbars(win, msg, wparam)) return true;
  }
  // Call window procedure
  if (!(value = win->proc(win, msg, wparam, lparam))) {
    switch (msg) {
      case evPaint:
        for (window_t *sub = win->children; sub; sub = sub->next) {
          send_message(sub, evPaint, wparam, lparam);
        }
        break;
      case evWheel:
        // Only drive built-in scrollbars when they are actually visible.
        // Windows without visible scrollbars should not respond to wheel events.
        if ((win->flags & (WINDOW_HSCROLL | WINDOW_VSCROLL)) &&
            (win->hscroll.visible || win->vscroll.visible)) {
          if ((win->flags & WINDOW_HSCROLL) && win->hscroll.visible &&
              win->hscroll.enabled) {
            int delta = (int16_t)LOWORD(wparam);
            sb_try_scroll(win, &win->hscroll, evHScroll,
                          win->hscroll.pos + delta);
          }
          if ((win->flags & WINDOW_VSCROLL) && win->vscroll.visible &&
              win->vscroll.enabled) {
            int delta = -(int16_t)HIWORD(wparam);
            sb_try_scroll(win, &win->vscroll, evVScroll,
                          win->vscroll.pos + delta);
          }
        }
        break;
      case evPaintStencil:
        paint_window_stencil(win);
        break;
      case evHitTest:
        for (window_t *item = win->children; item; item = item->next) {
          rect_t r = item->frame;
          uint16_t x = LOWORD(wparam), y = HIWORD(wparam);
          if (!item->notabstop && CONTAINS(x, y, r.x, r.y, r.w, r.h)) {
            *(window_t **)lparam = item;
          }
        }
        break;
      case evNCLeftButtonUp:
        // For WINDOW_NOTITLE toolbar windows the toolbar band is treated as a
        // drag area (window_in_drag_area returns true), so LeftButtonDown is
        // never routed to toolbar children.  Instead the toolbar fires on
        // release: find the child under the cursor, pre-set pressed, and send
        // LeftButtonUp so win_toolbar_button fires its command normally.
        if ((win->flags & WINDOW_TOOLBAR) && (win->flags & WINDOW_NOTITLE)) {
          int sx = (int)(int16_t)LOWORD(wparam);
          int sy = (int)(int16_t)HIWORD(wparam);
          // Convert screen-absolute coords to toolbar-band-relative.
          int title_h = (win->flags & WINDOW_NOTITLE) ? 0 : TITLEBAR_HEIGHT;
          int tb_x = sx - win->frame.x;
          int tb_y = sy - (win->frame.y + title_h);
          for (window_t *tc = win->toolbar_children; tc; tc = tc->next) {
            if (CONTAINS(tb_x, tb_y, tc->frame.x, tc->frame.y, tc->frame.w, tc->frame.h)) {
              tc->pressed = true;
              send_message(tc, evLeftButtonUp,
                           MAKEDWORD(tb_x - tc->frame.x, tb_y - tc->frame.y), NULL);
              break;
            }
          }
          invalidate_window(win);
        }
        break;
      case evCommand:
        // When a toolbar child button fires btnClicked,
        // translate it to tbButtonClick for backward compatibility
        // with existing callers (taskmanager, formeditor, filepicker, …).
        if (HIWORD(wparam) == btnClicked && lparam) {
          window_t *sender = (window_t *)lparam;
          for (window_t *tc = win->toolbar_children; tc; tc = tc->next) {
            if (tc == sender) {
              send_message(win, tbButtonClick,
                           (uint32_t)sender->id, sender);
              break;
            }
          }
        }
        break;
    }
  }
  // Draw disabled overlay
  if (win->disabled && msg == evPaint && win != g_ui_runtime.modal_overlay_parent) {
    uint32_t col = (get_sys_color(brWindowBg) & 0x00FFFFFF) | 0x80000000;
    int root_t = titlebar_height(root);
    rect_t wf = win_frame_in_screen(win, root, root_t);
    set_viewport(&(rect_t){ 0, 0, ui_get_system_metrics(kSystemMetricScreenWidth), ui_get_system_metrics(kSystemMetricScreenHeight)});
    set_projection(0, 0, ui_get_system_metrics(kSystemMetricScreenWidth), ui_get_system_metrics(kSystemMetricScreenHeight));
    fill_rect(col, R(wf.x, wf.y, wf.w, wf.h));
  }
  if (msg == evPaint && win == g_ui_runtime.modal_overlay_parent) {
    int root_t = titlebar_height(root);
    rect_t wf = win_frame_in_screen(win, root, root_t);
    set_viewport(&(rect_t){ 0, 0, ui_get_system_metrics(kSystemMetricScreenWidth), ui_get_system_metrics(kSystemMetricScreenHeight)});
    set_projection(0, 0, ui_get_system_metrics(kSystemMetricScreenWidth), ui_get_system_metrics(kSystemMetricScreenHeight));
    fill_rect(get_sys_color(brModalOverlay), R(wf.x, wf.y, wf.w, wf.h));
  }
  // Draw built-in scrollbars on top of window content.
  // Restore the window/root paint state first: the disabled overlay above
  // switches to a fullscreen viewport/projection, but the built-in bars are
  // drawn in the root-relative coordinate space established by paint setup.
  // Also restore the scissor to the window's full frame: the bars live in
  // the non-client area outside the client rect that was scissored above.
  if (msg == evPaint && g_ui_runtime.running &&
      (win->flags & (WINDOW_HSCROLL | WINDOW_VSCROLL))) {
    int root_t = titlebar_height(root);
    rect_t wf = win_frame_in_screen(win, root, root_t);
    rect_t rootf = root->frame;
    set_viewport(&rootf);
    set_projection(root->scroll[0],
                   -root_t + root->scroll[1],
                   root->frame.w + root->scroll[0],
                   root->frame.h - root_t + root->scroll[1]);
    set_clip_rect(NULL, &wf);
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
      if (msg == evHttpProgress) {
        free_posted_lparam(msg, queue.messages[r].lparam);
        queue.messages[r].wparam = wparam;
        queue.messages[r].lparam = lparam;
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
  for (window_t *w = list; w; w = w->next) {
    if (w == target) return true;
    if (is_valid_window_ptr(target, w->children)) return true;
    if (is_valid_window_ptr(target, w->toolbar_children)) return true;
  }
  return false;
}

void repost_messages(void) {
  if (g_ui_runtime.running) {
    ui_begin_frame();   // make GL context current, bind platform framebuffer
  }
  for (uint8_t write = queue.write; queue.read != write;) {
    msg_t *m = &queue.messages[queue.read++];
    if (m->target == NULL) {
      free_posted_lparam(m->msg, m->lparam);
      continue;
    }
    if (m->msg == evRefreshStencil) {
      free_posted_lparam(m->msg, m->lparam);
      if (g_ui_runtime.running) {
        repaint_stencil();
      }
      continue;
    }
    if (!is_valid_window_ptr(m->target, g_ui_runtime.windows)) {
      free_posted_lparam(m->msg, m->lparam);
      continue;
    }
    send_message(m->target, m->msg, m->wparam, m->lparam);
    free_posted_lparam(m->msg, m->lparam);
  }
  if (g_ui_runtime.running) {
    ui_end_frame();     // present frame (swap buffers / flushBuffer)
  }
}
