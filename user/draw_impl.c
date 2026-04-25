// Drawing primitives implementation
// Extracted from mapview/window.c

#include "../platform/platform.h"
#include "gl_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "user.h"
#include "messages.h"
#include "draw.h"
#include "icons.h"

// External references
extern window_t *get_root_window(window_t *window);

static bool g_scissor_valid = false;
static rect_t g_scissor_rect = {0};

static void set_scissor_cached(rect_t const *r) {
  if (!r) return;
  glEnable(GL_SCISSOR_TEST);
  if (g_scissor_valid &&
      g_scissor_rect.x == r->x && g_scissor_rect.y == r->y &&
      g_scissor_rect.w == r->w && g_scissor_rect.h == r->h) {
    return;
  }
  g_scissor_rect = *r;
  g_scissor_valid = true;
  glScissor(r->x, r->y, r->w, r->h);
}

// Returns true if win is the root window that currently "owns" keyboard focus
// (either win itself is focused, or one of its descendants is focused).
bool window_has_focus(const window_t *win) {
  return g_ui_runtime.focused && get_root_window(g_ui_runtime.focused) == (window_t *)win;
}

// Forward declarations
extern void draw_text_small(const char* text, int x, int y, uint32_t col);
extern void draw_icon8(int icon, int x, int y, uint32_t col);
extern void draw_icon16(int icon, int x, int y, uint32_t col);
extern int send_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
extern void set_projection(int x, int y, int w, int h);

static int builtin_sb_thumb_len(win_sb_t const *sb, int track);
static int builtin_sb_thumb_off(win_sb_t const *sb, int track, int tl);

void set_fullscreen(void) {
  int w = ui_get_system_metrics(kSystemMetricScreenWidth);
  int h = ui_get_system_metrics(kSystemMetricScreenHeight);
  set_viewport(&(rect_t){0, 0, w, h});
  set_projection(0, 0, w, h);
}

rect_t get_opengl_rect(rect_t const *r) {
  uint32_t ws = axGetSize(NULL);
  float scale_x = (float)LOWORD(ws) * axGetScaling() / (float)MAX(1,ui_get_system_metrics(kSystemMetricScreenWidth));
  float scale_y = (float)HIWORD(ws) * axGetScaling() / (float)MAX(1,ui_get_system_metrics(kSystemMetricScreenHeight));
  
  return (rect_t){
    (int)(r->x * scale_x),
    (int)((ui_get_system_metrics(kSystemMetricScreenHeight) - r->y - r->h) * scale_y), // flip Y
    (int)(r->w * scale_x),
    (int)(r->h * scale_y)
  };
}

// Get titlebar height
int titlebar_height(window_t const *win) {
  int t = 0;
  if (!(win->flags & WINDOW_NOTITLE)) t += TITLEBAR_HEIGHT;
  if (win->flags & WINDOW_TOOLBAR) {
    // Toolbar children are always laid out in a single row (no wrapping).
    // The band height = bevel (top) + padding + bsz + padding + bevel (bottom).
    int bsz = (win->toolbar_btn_size > 0) ? win->toolbar_btn_size : TB_SPACING;
    t += bsz + 2 * (TOOLBAR_PADDING + TOOLBAR_BEVEL_WIDTH);
  }
  return t;
}

// Get statusbar height
int statusbar_height(window_t const *win) {
  int s = 0;
  if (win->flags&WINDOW_STATUSBAR) {
    s += STATUSBAR_HEIGHT;
  }
  return s;
}

void draw_wire_rect(rect_t const *r, int expand, uint32_t col) {
  fill_rect(col, R(r->x-expand, r->y-expand, r->w+2*expand, 1));
  fill_rect(col, R(r->x-expand, r->y-expand, 1, r->h+2*expand));
  fill_rect(col, R(r->x + r->w - 1 + expand, r->y-expand, 1, r->h+2*expand));
  fill_rect(col, R(r->x-expand, r->y + r->h - 1 + expand, r->w+2*expand, 1));
}

// Draw focused border
void draw_focused(rect_t const *r) {
  draw_wire_rect(r, 1, get_sys_color(brFocusRing));
}

// Draw a softer single-colour outline for a window that contains focused
// controls but is not itself the focused widget.
static void draw_active_frame(rect_t const *r) {
  draw_wire_rect(r, 1, get_sys_color(brBorderActive));
}

// Draw bevel border
void draw_bevel(rect_t const *r) {
  fill_rect(get_sys_color(brLightEdge), R(r->x-1, r->y-1, r->w+2, 1));
  fill_rect(get_sys_color(brLightEdge), R(r->x-1, r->y-1, 1, r->h+2));
  fill_rect(get_sys_color(brDarkEdge), R(r->x+r->w, r->y, 1, r->h+1));
  fill_rect(get_sys_color(brDarkEdge), R(r->x, r->y+r->h, r->w+1, 1));
  fill_rect(get_sys_color(brFlare), R(r->x-1, r->y-1, 1, 1));
}

// Draw button
void draw_button(rect_t const *r, int dx, int dy, bool pressed) {
  if (pressed) {
    fill_rect(get_sys_color(brDarkEdge), R(r->x, r->y, r->w, r->h));
    fill_rect(get_sys_color(brLightEdge), R(r->x+1, r->y+1, r->w-1, r->h-1));
    fill_rect(get_sys_color(brDarkEdge), R(r->x+1, r->y+1, r->w-2, r->h-2));
    fill_rect(get_sys_color(brWindowDarkBg), R(r->x+2, r->y+2, r->w-3, r->h-3));
    fill_rect(get_sys_color(brFlare), R(r->x+r->w-1, r->y+r->h-1, 1, 1));
  } else {
    fill_rect(get_sys_color(brDarkEdge), R(r->x, r->y, r->w, r->h));
    fill_rect(get_sys_color(brLightEdge), R(r->x, r->y, r->w-1, r->h-1));
    fill_rect(get_sys_color(brDarkEdge), R(r->x+1, r->y+1, r->w-2, r->h-2));
    fill_rect(get_sys_color(brWindowBg), R(r->x+1, r->y+1, r->w-3, r->h-3));
    fill_rect(get_sys_color(brFlare), R(r->x, r->y, 1, 1));
  }
}

// Draw window panel
void draw_panel(window_t const *win) {
  int x = win->frame.x, y = win->frame.y;
  int w = win->frame.w, h = win->frame.h;
  if (window_has_focus(win)) {
    draw_focused(MAKERECT(x, y, w, h));
  } else if (window_has_focus(win)) {
    draw_active_frame(MAKERECT(x, y, w, h));
  } else {
    draw_bevel(MAKERECT(x, y, w, h));
  }
  if (!(win->flags & WINDOW_NORESIZE)) {
    int r = RESIZE_HANDLE;
    fill_rect(get_sys_color(brLightEdge), R(x+w, y+h-r+1, 1, r));
    fill_rect(get_sys_color(brLightEdge), R(x+w-r+1, y+h, r, 1));
  }
  if (!(win->flags&WINDOW_NOFILL)) {
    fill_rect(get_sys_color(brWindowBg), R(x, y, w, h));
  }
}

// Draw window controls (close, minimize, etc.)
void draw_window_controls(window_t *win) {
  rect_t r = win->frame;
  fill_rect(get_sys_color(window_has_focus(win) ? brActiveTitlebar : brInactiveTitlebar),
            R(r.x, r.y, r.w, titlebar_height(win)));
  set_fullscreen();
  
  for (int i = 0; i < 1; i++) {
    int x = win->frame.x + win->frame.w - (i+1)*CONTROL_BUTTON_WIDTH - CONTROL_BUTTON_PADDING;
    int y = window_title_bar_y(win);
    draw_icon8(icon8_minus + i, x, y, get_sys_color(brTextNormal));
  }
}

// Draw status bar
// When WINDOW_HSCROLL is also set and the horizontal bar is visible, the row
// is shared: status text occupies the left 20 % and the scrollbar the rest.
void draw_statusbar(window_t *win, const char *text) {
  if (!(win->flags&WINDOW_STATUSBAR)) return;

  rect_t r = win->frame;
  int s = statusbar_height(win);
  int y = r.y + r.h - s;  // statusbar row is at bottom of the total window frame

  bool has_h = (win->flags & WINDOW_HSCROLL) && win->hscroll.visible;
  int split_x = has_h ? SB_STATUS_SPLIT_X(r.w) : r.w;

  fill_rect(get_sys_color(brStatusbarBg), R(r.x, y, split_x, s));
  set_fullscreen();

  if (text) {
    draw_text_small_clipped(text, &(rect_t){r.x, y, split_x, s},
                            get_sys_color(brTextNormal), TEXT_PADDING_LEFT);
  }

  if (has_h) {
    win_sb_t *sb = &win->hscroll;
    // Horizontal scrollbar fills the right portion of the status bar row.
    // Always reserve the rightmost SCROLLBAR_WIDTH cell for the resize corner.
    int bw = (r.w - split_x) - SCROLLBAR_WIDTH;
    if (bw > 0) {
      fill_rect(get_sys_color(brWindowDarkBg), R(r.x + split_x, y, bw, s));
      // Arrow buttons (each SCROLLBAR_WIDTH wide)
      if (bw >= 2 * SCROLLBAR_WIDTH) {
        int icon_off = (SCROLLBAR_WIDTH - ICON8_SIZE) / 2;
        fill_rect(get_sys_color(brWindowBg), R(r.x + split_x, y, SCROLLBAR_WIDTH, s));
        draw_icon8(icon8_scroll_left,  r.x + split_x + icon_off, y + icon_off, get_sys_color(brTextNormal));
        fill_rect(get_sys_color(brWindowBg), R(r.x + split_x + bw - SCROLLBAR_WIDTH, y, SCROLLBAR_WIDTH, s));
        draw_icon8(icon8_scroll_right, r.x + split_x + bw - SCROLLBAR_WIDTH + icon_off, y + icon_off, get_sys_color(brTextNormal));
        // Thumb in effective track between buttons
        int eff_track = bw - 2 * SCROLLBAR_WIDTH;
        if (eff_track > 0) {
          int tl = builtin_sb_thumb_len(sb, eff_track);
          int to = builtin_sb_thumb_off(sb, eff_track, tl);
          uint32_t thumb_col = sb->enabled ? get_sys_color(brLightEdge) : get_sys_color(brDarkEdge);
          fill_rect(thumb_col, R(r.x + split_x + SCROLLBAR_WIDTH + to, y, tl, s));
        }
      } else {
        // Not enough room for buttons — draw plain thumb
        int tl = builtin_sb_thumb_len(sb, bw);
        int to = builtin_sb_thumb_off(sb, bw, tl);
        uint32_t thumb_col = sb->enabled ? get_sys_color(brLightEdge) : get_sys_color(brDarkEdge);
        fill_rect(thumb_col, R(r.x + split_x + to, y, tl, s));
      }
    }
    // Resize corner — always present at the far right of the status-bar row.
    {
      int cx = r.x + r.w - SCROLLBAR_WIDTH;
      int icon_off = (SCROLLBAR_WIDTH - ICON8_SIZE) / 2;
      fill_rect(get_sys_color(brWindowDarkBg), R(cx, y, SCROLLBAR_WIDTH, s));
      draw_icon8(icon8_resize_br, cx + icon_off, y + icon_off, get_sys_color(brTextNormal));
    }
  }
}

// Set OpenGL viewport for window
void set_viewport(rect_t const *frame) {
  if (!g_ui_runtime.running) return;
  rect_t ogl_rect = get_opengl_rect(frame);
  
  glViewport(ogl_rect.x, ogl_rect.y, ogl_rect.w, ogl_rect.h);
  set_scissor_cached(&ogl_rect);
}

void set_clip_rect(window_t const *win, rect_t const *r) {
  if (!g_ui_runtime.running) return;
  rect_t ogl_rect = get_opengl_rect(win?&(rect_t){
    win->frame.x + r->x, win->frame.y + r->y, r->w, r->h
  }:r);
  set_scissor_cached(&ogl_rect);
}

// Paint window to stencil buffer
void paint_window_stencil(window_t const *w) {
  int p = 1;
  glStencilFunc(GL_ALWAYS, w->id, 0xFF);            // Always pass
  glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE); // Replace stencil with window ID
  draw_rect(1, R(w->frame.x-p, w->frame.y-p, w->frame.w+p*2, w->frame.h+p*2));
}

// Repaint window stencil buffer
void repaint_stencil(void) {
  set_fullscreen();
  
  glEnable(GL_STENCIL_TEST);
  glClearStencil(0);
  glClear(GL_STENCIL_BUFFER_BIT);
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
    if (!w->visible)
      continue;
    send_message(w, evPaintStencil, 0, NULL);
  }
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}

// Set stencil test to render for specific window
void ui_set_stencil_for_window(uint32_t window_id) {
  glStencilFunc(GL_EQUAL, window_id, 0xFF);
}

// Set stencil test to render for root window
void ui_set_stencil_for_root_window(uint32_t window_id) {
  glStencilFunc(GL_EQUAL, window_id, 0xFF);
}

// Fill a rectangle with a solid color
void fill_rect(uint32_t color, rect_t const *r) {
  extern uint32_t ui_white_texture;
  
  // Skip drawing if graphics aren't initialized (e.g., in tests)
  if (!g_ui_runtime.running || !r) return;
  
  // Update the white texture with the desired color
  glBindTexture(GL_TEXTURE_2D, ui_white_texture);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &color);
  
  // Draw a rectangle using the texture
  draw_rect_ex(ui_white_texture, r, false, 1);
}

// Draw a dashed selection-outline rectangle using 4 tiled draw calls instead of
// one fill_rect per dash segment.  The 4×4 checker texture is sampled with tiled
// UVs so that the first row/column produces a B,B,W,W repeating dash regardless
// of the selection size, keeping the GL call count constant (O(1)).
void draw_sel_rect(rect_t const *r) {
  extern uint32_t ui_checker_texture;

  if (!g_ui_runtime.running || !r || r->w < 1 || r->h < 1) return;
  int x = r->x;
  int y = r->y;
  int w = r->w;
  int h = r->h;

  // Top edge: tile along U, sample only the first texture row (v 0..0.25)
  draw_sprite_region(ui_checker_texture, R(x, y, w, 1),
                     0.0f, 0.0f, (float)w / 4.0f, 0.25f, 0xFFFFFFFF);
  // Bottom edge
  draw_sprite_region(ui_checker_texture, R(x, y + h - 1, w, 1),
                     0.0f, 0.0f, (float)w / 4.0f, 0.25f, 0xFFFFFFFF);
  if (h > 2) {
    // Left edge (skip corners already drawn above): tile along V, sample col 0 (u 0..0.25)
    draw_sprite_region(ui_checker_texture, R(x, y + 1, 1, h - 2),
                       0.0f, 0.0f, 0.25f, (float)(h - 2) / 4.0f, 0xFFFFFFFF);
    // Right edge
    draw_sprite_region(ui_checker_texture, R(x + w - 1, y + 1, 1, h - 2),
                       0.0f, 0.0f, 0.25f, (float)(h - 2) / 4.0f, 0xFFFFFFFF);
  }
}

void draw_icon8(int icon, int x, int y, uint32_t col) {
  char str[2] = { icon+128+6*16, 0 };
  draw_text_small(str, x, y, col);
}

void draw_icon16(int icon, int x, int y, uint32_t col) {
  if (icon >= SYSICON_BASE) {
    bitmap_strip_t *s = ui_get_sysicon_strip();
    if (s && s->tex != 0 && s->cols > 0) {
      int idx  = icon - SYSICON_BASE;
      int scol = idx % s->cols;
      int srow = idx / s->cols;
      float u0 = (float)(scol * s->icon_w) / (float)s->sheet_w;
      float v0 = (float)(srow * s->icon_h) / (float)s->sheet_h;
      float u1 = u0 + (float)s->icon_w / (float)s->sheet_w;
      float v1 = v0 + (float)s->icon_h / (float)s->sheet_h;
      draw_sprite_region((int)s->tex, R(x, y, s->icon_w, s->icon_h),
                         u0, v0, u1, v1, col);
    }
    return;
  }
  icon*=2;
  draw_text_small((char[]) { icon+128, icon+129, 0 }, x, y, col);
  draw_text_small((char[]) { icon+144, icon+145, 0 }, x, y+8, col);
}

// ---- Built-in scrollbar rendering -------------------------------------------
//
// Called from send_message() after evPaint when a window has
// WINDOW_HSCROLL and/or WINDOW_VSCROLL set.  The bars are drawn in the same
// projection as the window's paint pass (root-relative coordinates), on top
// of the window's content.

static int builtin_sb_thumb_len(win_sb_t const *sb, int track) {
  int range = sb->max_val - sb->min_val;
  if (range <= 0 || sb->page >= range) return track;
  int tl = track * sb->page / range;
  return tl < 8 ? 8 : tl;
}

static int builtin_sb_thumb_off(win_sb_t const *sb, int track, int tl) {
  int travel = sb->max_val - sb->min_val - sb->page;
  if (travel <= 0) return 0;
  int tt = track - tl;
  if (tt <= 0) return 0;
  return (sb->pos - sb->min_val) * tt / travel;
}

void draw_builtin_scrollbars(window_t *win) {
  bool has_h = (win->flags & WINDOW_HSCROLL) && win->hscroll.visible;
  bool has_v = (win->flags & WINDOW_VSCROLL) && win->vscroll.visible;
  if (!has_h && !has_v) return;

  // Non-client heights that shift scroll bar positions within the projection.
  // For child windows (typically WINDOW_NOTITLE) these are usually 0, so
  // child-window scroll bar behaviour is unchanged.
  int t = titlebar_height(win);
  int s = statusbar_height(win);

  // Coordinate base: for child windows use win->frame.x/y (offset within the
  // root-relative projection); for top-level windows the projection origin
  // maps to the client top-left, so use 0.
  int base_x = win->parent ? win->frame.x : 0;
  int base_y = win->parent ? win->frame.y : 0;

  // When the horizontal bar is merged with the status bar, draw_statusbar()
  // already rendered it during evNCPaint.  Skip it here so
  // it isn't drawn twice, and don't subtract its height from the vscroll track.
  bool h_merged = has_h && (win->flags & WINDOW_STATUSBAR);

  // Offset for centering 8×8 icons inside SCROLLBAR_WIDTH×SCROLLBAR_WIDTH buttons
  int icon_off = (SCROLLBAR_WIDTH - ICON8_SIZE) / 2;

  // Content height (client area minus scrollbar strips).
  // For top-level windows t/s shift where the strips appear within the frame;
  // for child windows t=s=0 so the formula degenerates to the old behaviour.
  int content_h = win->frame.h - t - s;

  if (has_h && !h_merged) {
    win_sb_t *sb = &win->hscroll;
    int x  = base_x;
    int y  = base_y + content_h - SCROLLBAR_WIDTH;
    int bw = win->frame.w - (has_v ? SCROLLBAR_WIDTH : 0);
    // Track background
    fill_rect(get_sys_color(brWindowDarkBg), R(x, y, bw, SCROLLBAR_WIDTH));
    // Arrow buttons
    if (bw >= 2 * SCROLLBAR_WIDTH) {
      fill_rect(get_sys_color(brWindowBg), R(x, y, SCROLLBAR_WIDTH, SCROLLBAR_WIDTH));
      draw_icon8(icon8_scroll_left,  x + icon_off, y + icon_off, get_sys_color(brTextNormal));
      fill_rect(get_sys_color(brWindowBg), R(x + bw - SCROLLBAR_WIDTH, y, SCROLLBAR_WIDTH, SCROLLBAR_WIDTH));
      draw_icon8(icon8_scroll_right, x + bw - SCROLLBAR_WIDTH + icon_off, y + icon_off, get_sys_color(brTextNormal));
      // Thumb in effective track between buttons
      int eff_track = bw - 2 * SCROLLBAR_WIDTH;
      if (eff_track > 0) {
        int tl = builtin_sb_thumb_len(sb, eff_track);
        int to = builtin_sb_thumb_off(sb, eff_track, tl);
        uint32_t thumb_col = sb->enabled ? get_sys_color(brLightEdge) : get_sys_color(brDarkEdge);
        fill_rect(thumb_col, R(x + SCROLLBAR_WIDTH + to, y, tl, SCROLLBAR_WIDTH));
      }
    } else {
      // Not enough room for buttons — plain thumb
      int tl = builtin_sb_thumb_len(sb, bw);
      int to = builtin_sb_thumb_off(sb, bw, tl);
      uint32_t thumb_col = sb->enabled ? get_sys_color(brLightEdge) : get_sys_color(brDarkEdge);
      fill_rect(thumb_col, R(x + to, y, tl, SCROLLBAR_WIDTH));
    }
  }

  if (has_v) {
    win_sb_t *sb = &win->vscroll;
    int x  = base_x + win->frame.w - SCROLLBAR_WIDTH;
    int y  = base_y;
    // In merged mode the horizontal bar lives in the status-bar row below the
    // content area, so the vertical bar spans the full content height.
    int bh = content_h - (has_h && !h_merged ? SCROLLBAR_WIDTH : 0);
    // Track background
    fill_rect(get_sys_color(brWindowDarkBg), R(x, y, SCROLLBAR_WIDTH, bh));
    // Arrow buttons
    if (bh >= 2 * SCROLLBAR_WIDTH) {
      fill_rect(get_sys_color(brWindowBg), R(x, y, SCROLLBAR_WIDTH, SCROLLBAR_WIDTH));
      draw_icon8(icon8_scroll_up,   x + icon_off, y + icon_off, get_sys_color(brTextNormal));
      fill_rect(get_sys_color(brWindowBg), R(x, y + bh - SCROLLBAR_WIDTH, SCROLLBAR_WIDTH, SCROLLBAR_WIDTH));
      draw_icon8(icon8_scroll_down, x + icon_off, y + bh - SCROLLBAR_WIDTH + icon_off, get_sys_color(brTextNormal));
      // Thumb in effective track between buttons
      int eff_track = bh - 2 * SCROLLBAR_WIDTH;
      if (eff_track > 0) {
        int tl = builtin_sb_thumb_len(sb, eff_track);
        int to = builtin_sb_thumb_off(sb, eff_track, tl);
        uint32_t thumb_col = sb->enabled ? get_sys_color(brLightEdge) : get_sys_color(brDarkEdge);
        fill_rect(thumb_col, R(x, y + SCROLLBAR_WIDTH + to, SCROLLBAR_WIDTH, tl));
      }
    } else {
      // Not enough room for buttons — plain thumb
      int tl = builtin_sb_thumb_len(sb, bh);
      int to = builtin_sb_thumb_off(sb, bh, tl);
      uint32_t thumb_col = sb->enabled ? get_sys_color(brLightEdge) : get_sys_color(brDarkEdge);
      fill_rect(thumb_col, R(x, y + to, SCROLLBAR_WIDTH, tl));
    }
  }

  // Bottom-right corner: resize icon when both scrollbars are visible.
  if (has_h && !h_merged && has_v) {
    int cx = base_x + win->frame.w - SCROLLBAR_WIDTH;
    int cy = base_y + content_h - SCROLLBAR_WIDTH;
    fill_rect(get_sys_color(brWindowDarkBg), R(cx, cy, SCROLLBAR_WIDTH, SCROLLBAR_WIDTH));
    draw_icon8(icon8_resize_br, cx + icon_off, cy + icon_off, get_sys_color(brTextNormal));
  }
}
