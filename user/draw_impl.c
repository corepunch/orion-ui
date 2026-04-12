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

// External references
extern window_t *windows;
extern window_t *_focused;

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
  if (!(win->flags&WINDOW_NOTITLE)) {
    t += TITLEBAR_HEIGHT;
  }
  if (win->flags&WINDOW_TOOLBAR) {
    int buttons_per_row = (win->num_toolbar_buttons > 0 && win->frame.w > 0)
        ? MAX(1, win->frame.w / TB_SPACING)
        : 1;
    int num_rows = (win->num_toolbar_buttons > 0)
        ? (int)((win->num_toolbar_buttons + (uint32_t)buttons_per_row - 1) / (uint32_t)buttons_per_row)
        : 1;
    t += num_rows * TOOLBAR_HEIGHT;
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

// Draw focused border
void draw_focused(rect_t const *r) {
  fill_rect(COLOR_FOCUSED, r->x-1, r->y-1, r->w+2, 1);
  fill_rect(COLOR_FOCUSED, r->x-1, r->y-1, 1, r->h+2);
  fill_rect(COLOR_FOCUSED, r->x+r->w, r->y, 1, r->h+1);
  fill_rect(COLOR_FOCUSED, r->x, r->y+r->h, r->w+1, 1);
}

// Draw bevel border
void draw_bevel(rect_t const *r) {
  fill_rect(COLOR_LIGHT_EDGE, r->x-1, r->y-1, r->w+2, 1);
  fill_rect(COLOR_LIGHT_EDGE, r->x-1, r->y-1, 1, r->h+2);
  fill_rect(COLOR_DARK_EDGE, r->x+r->w, r->y, 1, r->h+1);
  fill_rect(COLOR_DARK_EDGE, r->x, r->y+r->h, r->w+1, 1);
  fill_rect(COLOR_FLARE, r->x-1, r->y-1, 1, 1);
}

// Draw button
void draw_button(rect_t const *r, int dx, int dy, bool pressed) {
  fill_rect(pressed?COLOR_DARK_EDGE:COLOR_LIGHT_EDGE, r->x-dx, r->y-dy, r->w+dx+dy, r->h+dx+dy);
  fill_rect(pressed?COLOR_LIGHT_EDGE:COLOR_DARK_EDGE, r->x, r->y, r->w+dx, r->h+dy);
  fill_rect(pressed?COLOR_PANEL_DARK_BG:COLOR_PANEL_BG, r->x, r->y, r->w, r->h);
  if (pressed) {
    fill_rect(COLOR_FLARE, r->x+r->w, r->y+r->h, dx, dy);
  } else {
    fill_rect(COLOR_FLARE, r->x-dx, r->y-dy, dx, dy);
  }
}

// Draw window panel
void draw_panel(window_t const *win) {
  int t = titlebar_height(win);
  int s = statusbar_height(win);
  int x = win->frame.x, y = win->frame.y-t;
  int w = win->frame.w, h = win->frame.h+t+s;
  bool active = _focused == win;
  if (active) {
    draw_focused(MAKERECT(x, y, w, h));
  } else {
    draw_bevel(MAKERECT(x, y, w, h));
  }
  if (!(win->flags & WINDOW_NORESIZE)) {
    int r = RESIZE_HANDLE;
    fill_rect(COLOR_LIGHT_EDGE, x+w, y+h-r+1, 1, r);
    fill_rect(COLOR_LIGHT_EDGE, x+w-r+1, y+h, r, 1);
  }
  if (!(win->flags&WINDOW_NOFILL)) {
    fill_rect(COLOR_PANEL_BG, x, y, w, h);
  }
}

// Draw window controls (close, minimize, etc.)
void draw_window_controls(window_t *win) {
  rect_t r = win->frame;
  int t = titlebar_height(win);
  fill_rect(COLOR_PANEL_DARK_BG, r.x, r.y-t, r.w, t);
  set_fullscreen();
  
  for (int i = 0; i < 1; i++) {
    int x = win->frame.x + win->frame.w - (i+1)*CONTROL_BUTTON_WIDTH - CONTROL_BUTTON_PADDING;
    int y = window_title_bar_y(win);
    draw_icon8(icon8_minus + i, x, y, COLOR_TEXT_NORMAL);
  }
}

// Draw status bar
// When WINDOW_HSCROLL is also set and the horizontal bar is visible, the row
// is shared: status text occupies the left 20 % and the scrollbar the rest.
void draw_statusbar(window_t *win, const char *text) {
  if (!(win->flags&WINDOW_STATUSBAR)) return;

  rect_t r = win->frame;
  int s = statusbar_height(win);
  int y = r.y + r.h;

  bool has_h = (win->flags & WINDOW_HSCROLL) && win->hscroll.visible;
  int split_x = has_h ? SB_STATUS_SPLIT_X(r.w) : r.w;

  fill_rect(COLOR_STATUSBAR_BG, r.x, y, split_x, s);
  set_fullscreen();

  if (text) {
    draw_text_small(text, r.x + 2, y + 2, COLOR_TEXT_NORMAL);
  }

  if (has_h) {
    win_sb_t *sb = &win->hscroll;
    // Horizontal scrollbar fills the right portion of the status bar row.
    // Always reserve the rightmost SCROLLBAR_WIDTH cell for the resize corner.
    int bw = (r.w - split_x) - SCROLLBAR_WIDTH;
    if (bw > 0) {
      fill_rect(COLOR_PANEL_DARK_BG, r.x + split_x, y, bw, s);
      // Arrow buttons (each SCROLLBAR_WIDTH wide)
      if (bw >= 2 * SCROLLBAR_WIDTH) {
        int icon_off = (SCROLLBAR_WIDTH - ICON8_SIZE) / 2;
        fill_rect(COLOR_PANEL_BG, r.x + split_x, y, SCROLLBAR_WIDTH, s);
        draw_icon8(icon8_scroll_left,  r.x + split_x + icon_off, y + icon_off, COLOR_TEXT_NORMAL);
        fill_rect(COLOR_PANEL_BG, r.x + split_x + bw - SCROLLBAR_WIDTH, y, SCROLLBAR_WIDTH, s);
        draw_icon8(icon8_scroll_right, r.x + split_x + bw - SCROLLBAR_WIDTH + icon_off, y + icon_off, COLOR_TEXT_NORMAL);
        // Thumb in effective track between buttons
        int eff_track = bw - 2 * SCROLLBAR_WIDTH;
        if (eff_track > 0) {
          int tl = builtin_sb_thumb_len(sb, eff_track);
          int to = builtin_sb_thumb_off(sb, eff_track, tl);
          uint32_t thumb_col = sb->enabled ? COLOR_LIGHT_EDGE : COLOR_DARK_EDGE;
          fill_rect(thumb_col, r.x + split_x + SCROLLBAR_WIDTH + to, y, tl, s);
        }
      } else {
        // Not enough room for buttons — draw plain thumb
        int tl = builtin_sb_thumb_len(sb, bw);
        int to = builtin_sb_thumb_off(sb, bw, tl);
        uint32_t thumb_col = sb->enabled ? COLOR_LIGHT_EDGE : COLOR_DARK_EDGE;
        fill_rect(thumb_col, r.x + split_x + to, y, tl, s);
      }
    }
    // Resize corner — always present at the far right of the status-bar row.
    {
      int cx = r.x + r.w - SCROLLBAR_WIDTH;
      int icon_off = (SCROLLBAR_WIDTH - ICON8_SIZE) / 2;
      fill_rect(COLOR_PANEL_DARK_BG, cx, y, SCROLLBAR_WIDTH, s);
      draw_icon8(icon8_resize_br, cx + icon_off, y + icon_off, COLOR_TEXT_NORMAL);
    }
  }
}

// Set OpenGL viewport for window
void set_viewport(rect_t const *frame) {
  extern bool running;
  if (!running) return;
  rect_t ogl_rect = get_opengl_rect(frame);
  
  glEnable(GL_SCISSOR_TEST);
  glViewport(ogl_rect.x, ogl_rect.y, ogl_rect.w, ogl_rect.h);
  glScissor(ogl_rect.x, ogl_rect.y, ogl_rect.w, ogl_rect.h);
}

void set_clip_rect(window_t const *win, rect_t const *r) {
  extern bool running;
  if (!running) return;
  rect_t ogl_rect = get_opengl_rect(win?&(rect_t){
    win->frame.x + r->x, win->frame.y + r->y, r->w, r->h
  }:r);
  glEnable(GL_SCISSOR_TEST);
  glScissor(ogl_rect.x, ogl_rect.y, ogl_rect.w, ogl_rect.h);
}

// Paint window to stencil buffer
void paint_window_stencil(window_t const *w) {
  int p = 1;
  int t = titlebar_height(w);
  int s = statusbar_height(w);
  glStencilFunc(GL_ALWAYS, w->id, 0xFF);            // Always pass
  glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE); // Replace stencil with window ID
  draw_rect(1, w->frame.x-p, w->frame.y-t-p, w->frame.w+p*2, w->frame.h+t+s+p*2);
}

// Repaint window stencil buffer
void repaint_stencil(void) {
  set_fullscreen();
  
  glEnable(GL_STENCIL_TEST);
  glClearStencil(0);
  glClear(GL_STENCIL_BUFFER_BIT);
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
  for (window_t *w = windows; w; w = w->next) {
    if (!w->visible)
      continue;
    send_message(w, kWindowMessagePaintStencil, 0, NULL);
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
void fill_rect(int color, int x, int y, int w, int h) {
  extern bool running;
  extern GLuint ui_white_texture;
  
  // Skip drawing if graphics aren't initialized (e.g., in tests)
  if (!running) return;
  
  // Update the white texture with the desired color
  glBindTexture(GL_TEXTURE_2D, ui_white_texture);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &color);
  
  // Draw a rectangle using the texture
  draw_rect_ex(ui_white_texture, x, y, w, h, false, 1);
}

// Draw a dashed selection-outline rectangle using 4 tiled draw calls instead of
// one fill_rect per dash segment.  The 4×4 checker texture is sampled with tiled
// UVs so that the first row/column produces a B,B,W,W repeating dash regardless
// of the selection size, keeping the GL call count constant (O(1)).
void draw_sel_rect(int x, int y, int w, int h) {
  extern bool running;
  extern GLuint ui_checker_texture;

  if (!running || w < 1 || h < 1) return;

  // Top edge: tile along U, sample only the first texture row (v 0..0.25)
  draw_sprite_region(ui_checker_texture, x, y, w, 1,
                     0.0f, 0.0f, (float)w / 4.0f, 0.25f, 1.0f);
  // Bottom edge
  draw_sprite_region(ui_checker_texture, x, y + h - 1, w, 1,
                     0.0f, 0.0f, (float)w / 4.0f, 0.25f, 1.0f);
  if (h > 2) {
    // Left edge (skip corners already drawn above): tile along V, sample col 0 (u 0..0.25)
    draw_sprite_region(ui_checker_texture, x, y + 1, 1, h - 2,
                       0.0f, 0.0f, 0.25f, (float)(h - 2) / 4.0f, 1.0f);
    // Right edge
    draw_sprite_region(ui_checker_texture, x + w - 1, y + 1, 1, h - 2,
                       0.0f, 0.0f, 0.25f, (float)(h - 2) / 4.0f, 1.0f);
  }
}

void draw_icon8(int icon, int x, int y, uint32_t col) {
  char str[2] = { icon+128+6*16, 0 };
  draw_text_small(str, x, y, col);
}

void draw_icon16(int icon, int x, int y, uint32_t col) {
  icon*=2;
  draw_text_small((char[]) { icon+128, icon+129, 0 }, x, y, col);
  draw_text_small((char[]) { icon+144, icon+145, 0 }, x, y+8, col);
}

// ---- Built-in scrollbar rendering -------------------------------------------
//
// Called from send_message() after kWindowMessagePaint when a window has
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

  // Coordinate base: for child windows use win->frame.x/y (offset within the
  // root-relative projection); for top-level windows the projection already
  // starts at the window's top-left, so use 0.
  int base_x = win->parent ? win->frame.x : 0;
  int base_y = win->parent ? win->frame.y : 0;

  // When the horizontal bar is merged with the status bar, draw_statusbar()
  // already rendered it during kWindowMessageNonClientPaint.  Skip it here so
  // it isn't drawn twice, and don't subtract its height from the vscroll track.
  bool h_merged = has_h && (win->flags & WINDOW_STATUSBAR);

  // Offset for centering 8×8 icons inside SCROLLBAR_WIDTH×SCROLLBAR_WIDTH buttons
  int icon_off = (SCROLLBAR_WIDTH - ICON8_SIZE) / 2;

  if (has_h && !h_merged) {
    win_sb_t *sb = &win->hscroll;
    int x  = base_x;
    int y  = base_y + win->frame.h - SCROLLBAR_WIDTH;
    int bw = win->frame.w - (has_v ? SCROLLBAR_WIDTH : 0);
    // Track background
    fill_rect(COLOR_PANEL_DARK_BG, x, y, bw, SCROLLBAR_WIDTH);
    // Arrow buttons
    if (bw >= 2 * SCROLLBAR_WIDTH) {
      fill_rect(COLOR_PANEL_BG, x, y, SCROLLBAR_WIDTH, SCROLLBAR_WIDTH);
      draw_icon8(icon8_scroll_left,  x + icon_off, y + icon_off, COLOR_TEXT_NORMAL);
      fill_rect(COLOR_PANEL_BG, x + bw - SCROLLBAR_WIDTH, y, SCROLLBAR_WIDTH, SCROLLBAR_WIDTH);
      draw_icon8(icon8_scroll_right, x + bw - SCROLLBAR_WIDTH + icon_off, y + icon_off, COLOR_TEXT_NORMAL);
      // Thumb in effective track between buttons
      int eff_track = bw - 2 * SCROLLBAR_WIDTH;
      if (eff_track > 0) {
        int tl = builtin_sb_thumb_len(sb, eff_track);
        int to = builtin_sb_thumb_off(sb, eff_track, tl);
        uint32_t thumb_col = sb->enabled ? COLOR_LIGHT_EDGE : COLOR_DARK_EDGE;
        fill_rect(thumb_col, x + SCROLLBAR_WIDTH + to, y, tl, SCROLLBAR_WIDTH);
      }
    } else {
      // Not enough room for buttons — plain thumb
      int tl = builtin_sb_thumb_len(sb, bw);
      int to = builtin_sb_thumb_off(sb, bw, tl);
      uint32_t thumb_col = sb->enabled ? COLOR_LIGHT_EDGE : COLOR_DARK_EDGE;
      fill_rect(thumb_col, x + to, y, tl, SCROLLBAR_WIDTH);
    }
  }

  if (has_v) {
    win_sb_t *sb = &win->vscroll;
    int x  = base_x + win->frame.w - SCROLLBAR_WIDTH;
    int y  = base_y;
    // In merged mode the horizontal bar lives in the status-bar row below the
    // content area, so the vertical bar spans the full content height.
    int bh = win->frame.h - (has_h && !h_merged ? SCROLLBAR_WIDTH : 0);
    // Track background
    fill_rect(COLOR_PANEL_DARK_BG, x, y, SCROLLBAR_WIDTH, bh);
    // Arrow buttons
    if (bh >= 2 * SCROLLBAR_WIDTH) {
      fill_rect(COLOR_PANEL_BG, x, y, SCROLLBAR_WIDTH, SCROLLBAR_WIDTH);
      draw_icon8(icon8_scroll_up,   x + icon_off, y + icon_off, COLOR_TEXT_NORMAL);
      fill_rect(COLOR_PANEL_BG, x, y + bh - SCROLLBAR_WIDTH, SCROLLBAR_WIDTH, SCROLLBAR_WIDTH);
      draw_icon8(icon8_scroll_down, x + icon_off, y + bh - SCROLLBAR_WIDTH + icon_off, COLOR_TEXT_NORMAL);
      // Thumb in effective track between buttons
      int eff_track = bh - 2 * SCROLLBAR_WIDTH;
      if (eff_track > 0) {
        int tl = builtin_sb_thumb_len(sb, eff_track);
        int to = builtin_sb_thumb_off(sb, eff_track, tl);
        uint32_t thumb_col = sb->enabled ? COLOR_LIGHT_EDGE : COLOR_DARK_EDGE;
        fill_rect(thumb_col, x, y + SCROLLBAR_WIDTH + to, SCROLLBAR_WIDTH, tl);
      }
    } else {
      // Not enough room for buttons — plain thumb
      int tl = builtin_sb_thumb_len(sb, bh);
      int to = builtin_sb_thumb_off(sb, bh, tl);
      uint32_t thumb_col = sb->enabled ? COLOR_LIGHT_EDGE : COLOR_DARK_EDGE;
      fill_rect(thumb_col, x, y + to, SCROLLBAR_WIDTH, tl);
    }
  }

  // Bottom-right corner: resize icon when both scrollbars are visible.
  if (has_h && !h_merged && has_v) {
    int cx = base_x + win->frame.w - SCROLLBAR_WIDTH;
    int cy = base_y + win->frame.h - SCROLLBAR_WIDTH;
    fill_rect(COLOR_PANEL_DARK_BG, cx, cy, SCROLLBAR_WIDTH, SCROLLBAR_WIDTH);
    draw_icon8(icon8_resize_br, cx + icon_off, cy + icon_off, COLOR_TEXT_NORMAL);
  }
}
