// Tooltip system — singleton tooltip popup for toolbar and toolbox buttons.
//
// Usage:
//   tooltip_update(source_win, text, screen_x, screen_y)
//     Call on every mouse-move with the window/control currently under the
//     cursor and the tooltip text it should show (NULL or "" = no tooltip).
//     Starts a TOOLTIP_DELAY_MS one-shot timer; shows the popup when it fires.
//   tooltip_cancel()
//     Immediately hide any visible tooltip and disarm the pending timer.
//     Also called internally when the source changes.
//
// The tooltip window is created lazily on first use and lives for the
// lifetime of the process (cleanup_all_windows destroys it on shutdown).

#include <string.h>
#include <stdbool.h>

#include "user.h"
#include "messages.h"
#include "draw.h"
#include "text.h"
#include "../platform/platform.h"

// Delay in milliseconds before the tooltip appears (matches WinAPI default).
#define TOOLTIP_DELAY_MS  600
// Inner padding between tooltip border and text (horizontal and vertical).
#define TOOLTIP_PAD       4
// Offset of the tooltip below the cursor hot-spot.
#define TOOLTIP_Y_OFFSET  18

// ── Global tooltip state ─────────────────────────────────────────────────────

static window_t *g_tooltip_win         = NULL; // singleton popup (lazily created)
static char      g_tooltip_pending[256] = {0};  // text waiting to be shown
static int       g_tooltip_sx          = 0;    // cursor screen-x when delay started
static int       g_tooltip_sy          = 0;    // cursor screen-y when delay started
static uint32_t  g_tooltip_timer_id    = 0;    // axSetTimer handle (0 = none)

// ── Tooltip window procedure ─────────────────────────────────────────────────

static result_t tooltip_win_proc(window_t *win, uint32_t msg,
                                 uint32_t wparam, void *lparam) {
  (void)wparam; (void)lparam;
  switch (msg) {
    case evCreate:
      return true;

    case evPaint: {
      int w = win->frame.w;
      int h = win->frame.h;
      // Yellow tooltip background (matches Windows classic tooltip colour).
      fill_rect(0xFFFFFFE1, R(0, 0, w, h));
      // 1-pixel black border.
      fill_rect(0xFF000000, R(0,     0,     w, 1));
      fill_rect(0xFF000000, R(0,     h - 1, w, 1));
      fill_rect(0xFF000000, R(0,     0,     1, h));
      fill_rect(0xFF000000, R(w - 1, 0,     1, h));
      draw_text_small(win->title, TOOLTIP_PAD, TOOLTIP_PAD, 0xFF000000);
      return true;
    }

    case evTimer: {
      // One-shot timer fired — show the tooltip at the stored position.
      g_tooltip_timer_id = 0;
      if (!win->visible && g_tooltip_pending[0]) {
        int tw = strwidth(g_tooltip_pending);
        int th = get_char_height();
        int w  = tw + TOOLTIP_PAD * 2;
        int h  = th + TOOLTIP_PAD * 2;
        int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
        int sh = ui_get_system_metrics(kSystemMetricScreenHeight);
        int x  = g_tooltip_sx;
        int y  = g_tooltip_sy + TOOLTIP_Y_OFFSET;
        // Flip above cursor if the tooltip would go off screen at the bottom.
        if (y + h > sh) y = g_tooltip_sy - h - 4;
        // Clamp horizontally.
        if (x + w > sw) x = sw - w;
        if (x < 0) x = 0;
        if (y < 0) y = 0;

        win->frame.x = x;
        win->frame.y = y;
        win->frame.w = w;
        win->frame.h = h;
        strncpy(win->title, g_tooltip_pending, sizeof(win->title) - 1);
        win->title[sizeof(win->title) - 1] = '\0';
        show_window(win, true);
        invalidate_window(win);
      }
      return true;
    }
  }
  return false;
}

// ── Internal helpers ─────────────────────────────────────────────────────────

static void ensure_tooltip_win(void) {
  if (g_tooltip_win) return;
  g_tooltip_win = create_window("",
      WINDOW_NOTITLE | WINDOW_NORESIZE | WINDOW_ALWAYSONTOP |
      WINDOW_NOTRAYBUTTON | WINDOW_NOFILL,
      MAKERECT(0, 0, 10, 10),
      NULL, tooltip_win_proc, 0, NULL);
  if (g_tooltip_win)
    show_window(g_tooltip_win, false);
}

// ── Public API ────────────────────────────────────────────────────────────────

// Hide any visible tooltip and cancel the pending show-timer.
void tooltip_cancel(void) {
  if (g_tooltip_timer_id) {
    axCancelTimer(g_tooltip_timer_id);
    g_tooltip_timer_id = 0;
  }
  if (g_tooltip_win && g_tooltip_win->visible)
    show_window(g_tooltip_win, false);
  g_tooltip_pending[0] = '\0';
}

// Update the tooltip for the currently hovered control.
//
// src_win  — the window acting as the tooltip source (used to detect when the
//            hovered target changes; may be NULL to cancel).
// text     — tooltip text to show; NULL or "" cancels.
// sx, sy   — current cursor position in screen coordinates.
//
// Rules:
//   • If text is NULL/"": cancel any pending/visible tooltip and return.
//   • If the same text is already pending or visible: no-op (prevents
//     restarting the timer every mouse-move while hovering a button).
//   • Otherwise: cancel previous, arm a new TOOLTIP_DELAY_MS one-shot timer.
void tooltip_update(window_t *src_win, const char *text, int sx, int sy) {
  (void)src_win;

  if (!text || !text[0]) {
    tooltip_cancel();
    return;
  }

  // Same text is already pending or visible — update cursor pos but keep timer.
  if (g_tooltip_timer_id && strcmp(g_tooltip_pending, text) == 0) {
    g_tooltip_sx = sx;
    g_tooltip_sy = sy;
    return;
  }
  if (g_tooltip_win && g_tooltip_win->visible &&
      strcmp(g_tooltip_win->title, text) == 0) {
    return;
  }

  // New tooltip: restart the delay timer.
  tooltip_cancel();

  strncpy(g_tooltip_pending, text, sizeof(g_tooltip_pending) - 1);
  g_tooltip_pending[sizeof(g_tooltip_pending) - 1] = '\0';
  g_tooltip_sx = sx;
  g_tooltip_sy = sy;

  ensure_tooltip_win();
  if (g_tooltip_win) {
    g_tooltip_timer_id = axSetTimer(g_tooltip_win, TOOLTIP_DELAY_MS,
                                    NULL, FALSE);
  }
}
