// Color palette floating window

#include "imageeditor.h"

#define SWATCH_W        (COLOR_WIN_W / COLOR_SWATCH_COLS)

void swap_foreground_background_colors(void) {
  if (!g_app) return;
  uint32_t tmp = g_app->fg_color;
  g_app->fg_color = g_app->bg_color;
  g_app->bg_color = tmp;
  if (g_app->tool_win)  invalidate_window(g_app->tool_win);
  if (g_app->color_win) invalidate_window(g_app->color_win);
}

#if IMAGEEDITOR_INDEXED
// ──────────────────────────────────────────────────────────────────────
// Indexed-mode color palette window
// Shows the active document's 256-entry palette instead of the global
// palette array.  The transparent index is marked with a small "T" cell.
// ──────────────────────────────────────────────────────────────────────

// Number of columns in the indexed-mode swatch grid.
#define INDEXED_PAL_COLS 16
#define INDEXED_PAL_ROWS 16
// Cell size: width = palette window width / 16 columns.
#define INDEXED_SWATCH_W (COLOR_WIN_W / INDEXED_PAL_COLS)
// Row height for indexed palette (make it square-ish).
#define INDEXED_SWATCH_H INDEXED_SWATCH_W

result_t win_color_palette_proc(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam) {
  (void)lparam;
  switch (msg) {
    case evPaint: {
      fill_rect(get_sys_color(brWindowDarkBg), R(0, 0, win->frame.w, win->frame.h));
      fill_rect(get_sys_color(brDarkEdge), R(win->frame.w - 1, 0, 1, win->frame.h));
      if (!g_app) return true;
      canvas_doc_t *doc = g_app->active_doc;
      if (!doc) return true;

      for (int i = 0; i < 256; i++) {
        int col = i % INDEXED_PAL_COLS;
        int row = i / INDEXED_PAL_COLS;
        int sx = col * INDEXED_SWATCH_W + 1;
        int sy = row * INDEXED_SWATCH_H + 1;
        int sw = INDEXED_SWATCH_W - 2;
        int sh = INDEXED_SWATCH_H - 2;

        if (i == doc->ipal.transparent) {
          // Transparent slot: draw checkerboard pattern.
          fill_rect(MAKE_COLOR(0xCC, 0xCC, 0xCC, 0xFF), R(sx, sy, sw, sh));
          fill_rect(MAKE_COLOR(0x88, 0x88, 0x88, 0xFF), R(sx, sy, sw/2, sh/2));
          fill_rect(MAKE_COLOR(0x88, 0x88, 0x88, 0xFF), R(sx+sw/2, sy+sh/2, sw-sw/2, sh-sh/2));
        } else {
          fill_rect(doc->ipal.entries[i], R(sx, sy, sw, sh));
        }

        // Highlight the selected foreground index.
        if (i == g_app->fg_palette_idx)
          fill_rect(get_sys_color(brFocusRing), R(sx, sy, sw, 2));
      }
      return true;
    }

    case evLeftButtonDown: {
      if (!g_app) return true;
      canvas_doc_t *doc = g_app->active_doc;
      if (!doc) return true;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      int col = lx / INDEXED_SWATCH_W;
      int row = ly / INDEXED_SWATCH_H;
      int idx = row * INDEXED_PAL_COLS + col;
      if (idx >= 0 && idx < 256) {
        g_app->fg_palette_idx = idx;
        g_app->fg_color = (idx == doc->ipal.transparent)
                        ? MAKE_COLOR(0, 0, 0, 0)
                        : doc->ipal.entries[idx];
        invalidate_window(win);
        if (g_app->tool_win) invalidate_window(g_app->tool_win);
      }
      return true;
    }

    case evRightButtonDown: {
      if (!g_app) return true;
      canvas_doc_t *doc = g_app->active_doc;
      if (!doc) return true;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      int col = lx / INDEXED_SWATCH_W;
      int row = ly / INDEXED_SWATCH_H;
      int idx = row * INDEXED_PAL_COLS + col;
      if (idx >= 0 && idx < 256 && idx != doc->ipal.transparent) {
        uint32_t result;
        if (show_color_picker(win, doc->ipal.entries[idx], &result)) {
          doc->ipal.entries[idx] = result;
          // Update fg_color if this was the selected index.
          if (idx == g_app->fg_palette_idx)
            g_app->fg_color = result;
          doc->canvas_dirty = true;
          invalidate_window(win);
          if (doc->canvas_win) invalidate_window(doc->canvas_win);
          if (g_app->tool_win) invalidate_window(g_app->tool_win);
        }
      }
      return true;
    }

    case evDestroy:
      if (g_app && g_app->color_win == win) g_app->color_win = NULL;
      return false;

    default:
      return false;
  }
}

#else
// ──────────────────────────────────────────────────────────────────────
// 32-bit RGBA mode color palette window (original behavior).
// ──────────────────────────────────────────────────────────────────────

result_t win_color_palette_proc(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam) {
  (void)lparam;
  switch (msg) {
    case evPaint: {
      fill_rect(get_sys_color(brWindowDarkBg), R(0, 0, win->frame.w, win->frame.h));
      fill_rect(get_sys_color(brDarkEdge), R(win->frame.w - 1, 0, 1, win->frame.h));
      if (!g_app) return true;

      for (int i = 0; i < NUM_COLORS; i++) {
        int col = i % COLOR_SWATCH_COLS;
        int row = i / COLOR_SWATCH_COLS;
        int sx = col * SWATCH_W + 1;
        int sy = row * SWATCH_ROW_H + 1;
        fill_rect(g_app->palette[i], R(sx, sy, SWATCH_W - 2, SWATCH_ROW_H - 2));

        if (g_app->fg_color == g_app->palette[i])
          fill_rect(get_sys_color(brFocusRing), R(sx, sy, SWATCH_W - 2, 2));
      }
      return true;
    }

    case evLeftButtonDown: {
      if (!g_app) return true;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      int col = lx / SWATCH_W;
      int row = ly / SWATCH_ROW_H;
      int idx = row * COLOR_SWATCH_COLS + col;
      if (idx >= 0 && idx < NUM_COLORS) {
        g_app->fg_color = g_app->palette[idx];
        invalidate_window(win);
        if (g_app->tool_win) invalidate_window(g_app->tool_win);
      }
      return true;
    }

    case evRightButtonDown: {
      if (!g_app) return true;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      int col = lx / SWATCH_W;
      int row = ly / SWATCH_ROW_H;
      int idx = row * COLOR_SWATCH_COLS + col;
      if (idx >= 0 && idx < NUM_COLORS) {
        uint32_t result;
        if (show_color_picker(win, g_app->palette[idx], &result)) {
          g_app->fg_color = result;
          invalidate_window(win);
          if (g_app->tool_win) invalidate_window(g_app->tool_win);
        }
      }
      return true;
    }

    case evDestroy:
      if (g_app && g_app->color_win == win) g_app->color_win = NULL;
      return false;

    default:
      return false;
  }
}

#endif // IMAGEEDITOR_INDEXED
