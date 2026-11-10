// Multi-line text edit control (analogous to WinAPI ES_MULTILINE edit).
//
// State is stored in win->userdata (me_state_t, heap-allocated at create time).
// Text lives in a growable buffer; win->title is not used for content.
//
// Messages handled in addition to the standard window messages:
//   edGetText  wparam=buf_size, lparam=char* → copies text, returns byte count
//              lparam=NULL → returns required byte count excluding NUL
//   edSetText  wparam=0, lparam=const char* → replaces text, resets cursor/scroll

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/theme.h>
#include "commctl.h"

#define ME_INITIAL_SIZE 2048

typedef struct {
  char *buf;
  size_t capacity;
  char placeholder[128];
  int  len;        // strlen(buf)
  int  cursor;     // byte offset of caret in buf
  int  scroll_y;   // vertical scroll in pixels
} me_state_t;

static bool me_reserve(window_t *win, me_state_t *s, size_t needed) {
  if (needed > INT_MAX) {
    fprintf(stderr, "[multiedit] window %u: text length %zu exceeds supported range\n", win->id, needed);
    return false;
  }
  if (needed <= s->capacity) return true;
  size_t capacity = MAX(needed, s->capacity ? s->capacity * 2 : ME_INITIAL_SIZE);
  capacity = MIN(capacity, INT_MAX);
  char *buf = realloc(s->buf, capacity);
  if (!buf) {
    fprintf(stderr, "[multiedit] window %u: cannot allocate %zu text bytes\n", win->id, capacity);
    return false;
  }
  s->buf = buf; s->capacity = capacity;
  return true;
}

// ---------------------------------------------------------------------------
// Internal layout helpers
// ---------------------------------------------------------------------------

// Advance one character's worth of wrapping state.  Uses the same algorithm
// as draw_text_wrapped_font() / calc_text_height_font() in user/text.c so that cursor
// positions exactly match rendered glyph positions.  Mutates *cx/*cy.
static void me_advance(ui_font_t font, const char *buf, int i, int max_w, int *cx, int *cy) {
  unsigned char c = (unsigned char)buf[i];
  if (c == '\n') {
    *cx = 0;
    *cy += text_char_height(font);
  } else if (c == ' ') {
    *cx += text_strwidth(font, " ");
  } else {
    int cw = text_strnwidth(font, (const char[]){(char)c, 0}, 1);
    if (cw > 0 && *cx + cw > max_w) {
      *cx = 0;
      *cy += text_char_height(font);
    }
    *cx += cw;
  }
}

// Compute visual (cx, cy) of byte offset `cursor` in buf.
// Coordinates are relative to the text-area origin (0, 0).
// out_x may be NULL when only the row position is needed.
static void me_cursor_xy(ui_font_t font, const char *buf, int cursor, int max_w,
                          int *out_x, int *out_y) {
  int cx = 0, cy = 0;
  for (int i = 0; i < cursor && buf[i]; i++)
    me_advance(font, buf, i, max_w, &cx, &cy);
  if (out_x) *out_x = cx;
  *out_y = cy;
}

// Find byte offset whose visual position is closest to pixel (tx, ty).
static int me_find_at_xy(ui_font_t font, const char *buf, int len, int tx, int ty, int max_w) {
  int cx = 0, cy = 0;
  int best = 0, best_d = 0x7fffffff;

  // Check position 0.
  if (cy == ty) {
    int d = abs(cx - tx);
    if (d < best_d) { best_d = d; best = 0; }
  }

  for (int i = 0; i < len; i++) {
    me_advance(font, buf, i, max_w, &cx, &cy);
    if (cy == ty) {
      int d = abs(cx - tx);
      if (d < best_d) { best_d = d; best = i + 1; }
    }
    if (cy > ty) break;
  }
  return best;
}

// Scroll so that the caret is within the visible viewport.
static void me_ensure_visible(ui_font_t font, me_state_t *s, int max_w, int vis_h) {
  int cy;
  me_cursor_xy(font, s->buf, s->cursor, max_w, NULL, &cy);
  if (cy < s->scroll_y)
    s->scroll_y = cy;
  if (cy + text_char_height(font) > s->scroll_y + vis_h)
    s->scroll_y = cy + text_char_height(font) - vis_h;
  if (s->scroll_y < 0) s->scroll_y = 0;
}

// Text-area dimensions in client pixels (scrollbar strip excluded when visible).
// Uses win->frame.w directly (not get_client_rect) because multiedit draws into
// the full frame without a title bar; only the vertical scrollbar strip is carved out.
static int me_gutter(void) { return ui_px("Scrollbar", "width"); }
static int me_min_width(const window_t *win) { return ui_px(ui_selector(win, "TextEdit"), "min-width"); }

static void me_text_dims(window_t *win, int *tw, int *th) {
  bool has_v = (win->flags & WINDOW_VSCROLL) && win->vscroll.visible;
  *tw = win->frame.w - (has_v ? me_gutter() : 0) - edit_padding_x(win) * 2;
  *th = win->frame.h - control_text_padding_y(win->flags) * 2;
  if (*tw < 1) *tw = 1;
  if (*th < 1) *th = 1;
}

// Synchronise the built-in vertical scrollbar with the current text height.
// Measure without the gutter first so deleting text can hide the scrollbar.
static void me_sync_scrollbar(window_t *win, me_state_t *s) {
  if (!(win->flags & WINDOW_VSCROLL)) return;
  int tw, th;
  tw = MAX(1, win->frame.w - edit_padding_x(win) * 2);
  th = MAX(1, win->frame.h - control_text_padding_y(win->flags) * 2);
  int total_h = calc_text_height_font(ui_selector(win, "TextEdit"), s->buf, tw);
  bool needs_scroll = (total_h > th);
  if (needs_scroll) {
    tw = MAX(1, tw - me_gutter());
    total_h = calc_text_height_font(ui_selector(win, "TextEdit"), s->buf, tw);
  }
  int max_scroll = needs_scroll ? (total_h - th) : 0;
  if (s->scroll_y > max_scroll) s->scroll_y = max_scroll;
  if (s->scroll_y < 0)         s->scroll_y = 0;
  scroll_info_t si = {
    .fMask = SIF_RANGE | SIF_PAGE | SIF_POS,
    .nMin  = 0,
    .nMax  = (total_h > th) ? total_h : th,
    .nPage = (uint32_t)th,
    .nPos  = s->scroll_y,
  };
  set_scroll_info(win, SB_VERT, &si, false);
  enable_scroll_bar(win, SB_VERT, needs_scroll);
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

result_t win_multiedit(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  me_state_t *s = (me_state_t *)win->userdata;

  switch (msg) {
    // ── Create ─────────────────────────────────────────────────────────────
    case evCreate: {
      s = (me_state_t *)allocate_window_data(win, sizeof(me_state_t));
      if (!s) return true;
      // Flexible unless the form gives a height without asking for flexspace;
      // a fixed height keeps the viewport fixed as text grows.
      const form_ctrl_def_t *cd = (const form_ctrl_def_t *)lparam;
      if (cd && cd->size.h > 0 && !(cd->flags & WINDOW_FLEXSPACE)) win->flags &= ~WINDOW_FLEXSPACE;
      else win->flags |= WINDOW_FLEXSPACE;
      if (!me_reserve(win, s, strlen(win->title) + 2)) { free(s); win->userdata = NULL; return true; }
      strcpy(s->buf, win->title);
      s->len      = (int)strlen(s->buf);
      s->cursor   = s->len;
      s->scroll_y = 0;
      me_sync_scrollbar(win, s);
      return true;
    }

    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (m) {
        int avail_w = m->avail_w > 0 ? m->avail_w : me_min_width(win);
        if (avail_w < me_min_width(win)) avail_w = me_min_width(win);
        m->desired_w = MAX(me_min_width(win),
                           text_strwidth(ui_selector(win, "TextEdit"), win->title) + edit_padding_x(win) * 2);
        m->desired_h = MAX(text_char_height(ui_selector(win, "TextEdit")) * 4,
                           calc_text_height_font(ui_selector(win, "TextEdit"), s ? s->buf : win->title, MAX(1, avail_w - edit_padding_x(win) * 2)) + control_text_padding_y(win->flags) * 2);
        if (!(win->flags & WINDOW_FLEXSPACE) && win->layout.layout_fixed_h > 0)
          m->desired_h = win->layout.layout_fixed_h;
      }
      return true;
    }

    // ── Destroy ────────────────────────────────────────────────────────────
    case evDestroy:
      if (s) free(s->buf);
      free(s);
      win->userdata = NULL;
      return false;

    // ── Focus / blur ───────────────────────────────────────────────────────
    case evSetFocus:
    case evKillFocus:
      invalidate_window(win);
      return false;

    // ── Paint ──────────────────────────────────────────────────────────────
    case evPaint: {
      if (!s) return true;
      bool focused = (g_ui_runtime.focused == win);

      ctrl_state_t state = focused ? CTRL_FOCUSED : CTRL_NORMAL;
      state |= CTRL_MULTILINE;
      if (window_is_disabled(win)) state |= CTRL_DISABLED;
      theme_draw_in(win, THEME_PART_FIELD, R(0, 0, win->frame.w, win->frame.h), state);

      int tw, th;
      me_text_dims(win, &tw, &th);
      int tx = edit_padding_x(win);
      int ty = control_text_padding_y(win->flags);

      set_clip_rect(win, R(tx, ty, tw, th));

      // Draw wrapped text, offset upward by scroll_y.
      irect16_t vp = { tx, ty - s->scroll_y, tw, th + s->scroll_y };
      draw_text_wrapped_font(ui_selector(win, "TextEdit"), s->buf, &vp, get_sys_color(brTextNormal));
      if (!s->len && s->placeholder[0])
        draw_text_ellipsized(ui_selector(win, "TextEdit"), s->placeholder, tx, ty, tw, get_sys_color(brTextSecondary));

      // Draw caret when focused.
      if (focused) {
        int cx, cy;
        me_cursor_xy(ui_selector(win, "TextEdit"), s->buf, s->cursor, tw, &cx, &cy);
        int cur_y = ty + cy - s->scroll_y;
        if (cur_y >= ty - text_char_height(ui_selector(win, "TextEdit")) && cur_y < ty + th) {
          theme_draw_in(win, THEME_PART_CARET, R(tx + cx, cur_y, 2, text_char_height(ui_selector(win, "TextEdit"))), CTRL_NORMAL);
        }
      }

      // Reset scissor to full control frame so subsequent rendering is unclipped.
      set_clip_rect(win, R(0, 0, win->frame.w, win->frame.h));

      return true;
    }

    // ── Mouse click to position ────────────────────────────────────────────
    case evLeftButtonUp: {
      if (!s) return true;
      int tw, th;
      me_text_dims(win, &tw, &th);
      // wparam carries client-local x (LOWORD) and y (HIWORD).
      int lx = (int)(int16_t)LOWORD(wparam) - edit_padding_x(win);
      int ly = (int)(int16_t)HIWORD(wparam) - control_text_padding_y(win->flags);
      int target_y = (ly / text_char_height(ui_selector(win, "TextEdit"))) * text_char_height(ui_selector(win, "TextEdit"));
      if (target_y < 0) target_y = 0;
      s->cursor = me_find_at_xy(ui_selector(win, "TextEdit"), s->buf, s->len, lx, target_y, tw);
      me_ensure_visible(ui_selector(win, "TextEdit"), s, tw, th);
      invalidate_window(win);
      return true;
    }

    // ── Mouse-wheel scroll ─────────────────────────────────────────────────
    case evWheel: {
      if (!s) return true;
      int tw, th;
      me_text_dims(win, &tw, &th);
      // lparam = scroll deltas MAKEDWORD(dx, dy)
      int delta = -(int16_t)HIWORD((uintptr_t)lparam);
      s->scroll_y += delta;
      if (s->scroll_y < 0) s->scroll_y = 0;
      int total_h = calc_text_height_font(ui_selector(win, "TextEdit"), s->buf, tw);
      int max_scroll = total_h - th;
      if (max_scroll < 0) max_scroll = 0;
      if (s->scroll_y > max_scroll) s->scroll_y = max_scroll;
      me_sync_scrollbar(win, s);
      invalidate_window(win);
      return true;
    }

    // ── Text input ─────────────────────────────────────────────────────────
    case evTextInput: {
      if (!s || !lparam) return true;
      char c = *(const char *)lparam;
      // Accept only printable ASCII; newlines are handled via AX_KEY_ENTER.
      if ((unsigned char)c < 32 || (unsigned char)c > 126) return true;
      if (!me_reserve(win, s, (size_t)s->len + 3)) return true;
      memmove(s->buf + s->cursor + 1,
              s->buf + s->cursor,
              (size_t)(s->len - s->cursor + 1));
      s->buf[s->cursor] = c;
      s->cursor++;
      s->len++;
      int tw, th;
      me_text_dims(win, &tw, &th);
      me_ensure_visible(ui_selector(win, "TextEdit"), s, tw, th);
      me_sync_scrollbar(win, s);
      invalidate_window(win);
      return true;
    }

    // ── Key navigation and editing ─────────────────────────────────────────
    case evKeyDown: {
      if (!s) return true;
      int tw, th;
      me_text_dims(win, &tw, &th);
      switch (wparam) {

        case AX_KEY_ENTER:
          if (me_reserve(win, s, (size_t)s->len + 3)) {
            memmove(s->buf + s->cursor + 1,
                    s->buf + s->cursor,
                    (size_t)(s->len - s->cursor + 1));
            s->buf[s->cursor] = '\n';
            s->cursor++;
            s->len++;
            me_ensure_visible(ui_selector(win, "TextEdit"), s, tw, th);
            me_sync_scrollbar(win, s);
            invalidate_window(win);
          }
          return true;

        case AX_KEY_BACKSPACE:
          if (s->cursor > 0) {
            memmove(s->buf + s->cursor - 1,
                    s->buf + s->cursor,
                    (size_t)(s->len - s->cursor + 1));
            s->cursor--;
            s->len--;
            me_ensure_visible(ui_selector(win, "TextEdit"), s, tw, th);
            me_sync_scrollbar(win, s);
            invalidate_window(win);
          }
          return true;

        case AX_KEY_DEL:
          if (s->cursor < s->len) {
            memmove(s->buf + s->cursor,
                    s->buf + s->cursor + 1,
                    (size_t)(s->len - s->cursor));
            s->len--;
            me_ensure_visible(ui_selector(win, "TextEdit"), s, tw, th);
            me_sync_scrollbar(win, s);
            invalidate_window(win);
          }
          return true;

        case AX_KEY_LEFTARROW:
          if (s->cursor > 0) {
            s->cursor--;
            me_ensure_visible(ui_selector(win, "TextEdit"), s, tw, th);
            invalidate_window(win);
          }
          return true;

        case AX_KEY_RIGHTARROW:
          if (s->cursor < s->len) {
            s->cursor++;
            me_ensure_visible(ui_selector(win, "TextEdit"), s, tw, th);
            invalidate_window(win);
          }
          return true;

        case AX_KEY_UPARROW: {
          int cx, cy;
          me_cursor_xy(ui_selector(win, "TextEdit"), s->buf, s->cursor, tw, &cx, &cy);
          int ny = cy - text_char_height(ui_selector(win, "TextEdit"));
          if (ny >= 0) {
            s->cursor = me_find_at_xy(ui_selector(win, "TextEdit"), s->buf, s->len, cx, ny, tw);
            me_ensure_visible(ui_selector(win, "TextEdit"), s, tw, th);
            invalidate_window(win);
          }
          return true;
        }

        case AX_KEY_DOWNARROW: {
          int cx, cy;
          me_cursor_xy(ui_selector(win, "TextEdit"), s->buf, s->cursor, tw, &cx, &cy);
          int ny = cy + text_char_height(ui_selector(win, "TextEdit"));
          int new_pos = me_find_at_xy(ui_selector(win, "TextEdit"), s->buf, s->len, cx, ny, tw);
          if (new_pos != s->cursor) {
            s->cursor = new_pos;
            me_ensure_visible(ui_selector(win, "TextEdit"), s, tw, th);
            invalidate_window(win);
          }
          return true;
        }

        case AX_KEY_HOME: {
          // Move to start of the current logical line (scan back to previous \n or buf start).
          int p = s->cursor;
          while (p > 0 && s->buf[p - 1] != '\n') p--;
          s->cursor = p;
          me_ensure_visible(ui_selector(win, "TextEdit"), s, tw, th);
          invalidate_window(win);
          return true;
        }

        case AX_KEY_END: {
          // Move to end of the current logical line (forward to next \n or buf end).
          int p = s->cursor;
          while (p < s->len && s->buf[p] != '\n') p++;
          s->cursor = p;
          me_ensure_visible(ui_selector(win, "TextEdit"), s, tw, th);
          invalidate_window(win);
          return true;
        }

        case AX_KEY_TAB:
          // Notify parent and yield focus so Tab advances to next control.
          send_message(get_root_window(win), evCommand,
                       MAKEDWORD(win->id, edUpdate), win);
          return false;

        case AX_KEY_ESCAPE:
          return true;

        default:
          return false;
      }
    }

    case edSetPlaceholder:
      if (!s) {
        fprintf(stderr, "[multiedit] window %u: placeholder requires control state\n", win->id);
        fflush(stderr);
        return false;
      }
      snprintf(s->placeholder, sizeof(s->placeholder), "%s", lparam ? (const char *)lparam : "");
      invalidate_window(win);
      return true;

    // ── edSetText ───────────────────────────────────────────
    case edSetText: {
      if (!s || !lparam) return true;
      const char *src = (const char *)lparam;
      if (!me_reserve(win, s, strlen(src) + 2)) return false;
      memmove(s->buf, src, strlen(src) + 1);
      s->len      = (int)strlen(s->buf);
      s->cursor   = s->len;
      s->scroll_y = 0;
      me_sync_scrollbar(win, s);
      invalidate_window(win);
      return true;
    }

    // ── edGetText ───────────────────────────────────────────
    case edGetText: {
      if (!s) return 0;
      if (!lparam) return s->len;
      int maxlen = (int)wparam;
      char *dst  = (char *)lparam;
      if (maxlen <= 0) return 0;
      strncpy(dst, s->buf, (size_t)(maxlen - 1));
      dst[maxlen - 1] = '\0';
      return (result_t)strlen(dst);
    }

    // ── evVScroll — scrollbar thumb dragged / arrow clicked ─────────────────
    case evVScroll: {
      if (!s) return true;
      int tw, th;
      me_text_dims(win, &tw, &th);
      int total_h   = calc_text_height_font(ui_selector(win, "TextEdit"), s->buf, tw);
      int max_scroll = total_h - th;
      if (max_scroll < 0) max_scroll = 0;
      s->scroll_y = (int)wparam;
      if (s->scroll_y > max_scroll) s->scroll_y = max_scroll;
      if (s->scroll_y < 0)         s->scroll_y = 0;
      me_sync_scrollbar(win, s);
      invalidate_window(win);
      return true;
    }

    // ── evResize — re-sync scrollbar after layout change ────────────────────
    case evResize:
      if (s) me_sync_scrollbar(win, s);
      return false;

    default:
      return false;
  }
}
