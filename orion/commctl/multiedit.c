// Multi-line text edit control (analogous to WinAPI ES_MULTILINE edit).
//
// State is stored in win->userdata (me_state_t, heap-allocated at create time).
// Text lives in buf[ME_BUF_SIZE]; win->title is not used for content.
//
// Messages handled in addition to the standard window messages:
//   edGetText  wparam=buf_size, lparam=char* → copies text, returns byte count
//   edSetText  wparam=0, lparam=const char* → replaces text, resets cursor/scroll

#include <string.h>
#include <stdlib.h>

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>

#define ME_BUF_SIZE 2048
#define ME_PADDING  3
#define ME_MIN_WIDTH  80
// Maximum number of characters that can be stored (leave room for the NUL).
#define ME_MAX_LEN  (ME_BUF_SIZE - 2)

typedef struct {
  char buf[ME_BUF_SIZE];
  int  len;        // strlen(buf)
  int  cursor;     // byte offset of caret in buf
  int  scroll_y;   // vertical scroll in pixels
} me_state_t;

// ---------------------------------------------------------------------------
// Internal layout helpers
// ---------------------------------------------------------------------------

// Advance one character's worth of wrapping state.  Uses the same algorithm
// as draw_text_wrapped() / calc_text_height() in user/text.c so that cursor
// positions exactly match rendered glyph positions.  Mutates *cx/*cy.
static void me_advance(const char *buf, int i, int max_w, int *cx, int *cy) {
  unsigned char c = (unsigned char)buf[i];
  if (c == '\n') {
    *cx = 0;
    *cy += SMALL_LINE_HEIGHT;
  } else if (c == ' ') {
    *cx += SPACE_WIDTH;
  } else {
    int cw = char_width(c);
    if (cw > 0 && *cx + cw > max_w) {
      *cx = 0;
      *cy += SMALL_LINE_HEIGHT;
    }
    *cx += cw;
  }
}

// Compute visual (cx, cy) of byte offset `cursor` in buf.
// Coordinates are relative to the text-area origin (0, 0).
// out_x may be NULL when only the row position is needed.
static void me_cursor_xy(const char *buf, int cursor, int max_w,
                          int *out_x, int *out_y) {
  int cx = 0, cy = 0;
  for (int i = 0; i < cursor && buf[i]; i++)
    me_advance(buf, i, max_w, &cx, &cy);
  if (out_x) *out_x = cx;
  *out_y = cy;
}

// Find byte offset whose visual position is closest to pixel (tx, ty).
static int me_find_at_xy(const char *buf, int len, int tx, int ty, int max_w) {
  int cx = 0, cy = 0;
  int best = 0, best_d = 0x7fffffff;

  // Check position 0.
  if (cy == ty) {
    int d = abs(cx - tx);
    if (d < best_d) { best_d = d; best = 0; }
  }

  for (int i = 0; i < len; i++) {
    me_advance(buf, i, max_w, &cx, &cy);
    if (cy == ty) {
      int d = abs(cx - tx);
      if (d < best_d) { best_d = d; best = i + 1; }
    }
    if (cy > ty) break;
  }
  return best;
}

// Scroll so that the caret is within the visible viewport.
static void me_ensure_visible(me_state_t *s, int max_w, int vis_h) {
  int cy;
  me_cursor_xy(s->buf, s->cursor, max_w, NULL, &cy);
  if (cy < s->scroll_y)
    s->scroll_y = cy;
  if (cy + SMALL_LINE_HEIGHT > s->scroll_y + vis_h)
    s->scroll_y = cy + SMALL_LINE_HEIGHT - vis_h;
  if (s->scroll_y < 0) s->scroll_y = 0;
}

// Text-area dimensions in client pixels (scrollbar strip excluded when visible).
// Uses win->frame.w directly (not get_client_rect) because multiedit draws into
// the full frame without a title bar; only the vertical scrollbar strip is carved out.
static void me_text_dims(window_t *win, int *tw, int *th) {
  bool has_v = (win->flags & WINDOW_VSCROLL) && win->vscroll.visible;
  *tw = win->frame.w - (has_v ? SCROLLBAR_WIDTH : 0) - ME_PADDING * 2;
  *th = win->frame.h - ME_PADDING * 2;
  if (*tw < 1) *tw = 1;
  if (*th < 1) *th = 1;
}

// Synchronise the built-in vertical scrollbar with the current text height.
// Only does anything when WINDOW_VSCROLL is set.  The bar remains permanently
// visible (forced by show_scroll_bar in evCreate) but is disabled/greyed out
// when the entire text fits in the viewport without scrolling.
static void me_sync_scrollbar(window_t *win, me_state_t *s) {
  if (!(win->flags & WINDOW_VSCROLL)) return;
  int tw, th;
  me_text_dims(win, &tw, &th);
  int total_h = calc_text_height(s->buf, tw);
  bool needs_scroll = (total_h > th);
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
      // Multiedit participates in auto-layout as a flexible child.
      win->flags |= WINDOW_FLEXSPACE;
      strncpy(s->buf, win->title, ME_BUF_SIZE - 1);
      s->buf[ME_BUF_SIZE - 1] = '\0';
      s->len      = (int)strlen(s->buf);
      s->cursor   = s->len;
      s->scroll_y = 0;
      // When WINDOW_VSCROLL is set, lock the bar permanently visible so it is
      // always shown (enabled/disabled to indicate whether scrolling is needed).
      if (win->flags & WINDOW_VSCROLL)
        show_scroll_bar(win, SB_VERT, true);
      me_sync_scrollbar(win, s);
      return true;
    }

    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (m) {
        int avail_w = m->avail_w > 0 ? m->avail_w : ME_MIN_WIDTH;
        if (avail_w < ME_MIN_WIDTH) avail_w = ME_MIN_WIDTH;
        m->desired_w = MAX(ME_MIN_WIDTH,
                           text_strwidth(FONT_SMALL, win->title) + TEXTEDIT_PADDING_HORZ * 2);
        m->desired_h = MAX(text_char_height(FONT_SMALL) * 4,
                           calc_text_height_font(FONT_SMALL, s ? s->buf : win->title, avail_w) + ME_PADDING * 2);
      }
      return true;
    }

    // ── Destroy ────────────────────────────────────────────────────────────
    case evDestroy:
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

      // Focus ring (matches win_textedit style).
      fill_rect(focused ? get_sys_color(brAccent)
                        : get_sys_color(brControlBg),
                R(-1, -1, win->frame.w + 2, win->frame.h + 2));

      // Inset bevel border.
      draw_button((irect16_t){0, 0, win->frame.w, win->frame.h}, 1, 1, true);

      int tw, th;
      me_text_dims(win, &tw, &th);
      int tx = ME_PADDING;
      int ty = ME_PADDING;

      set_clip_rect(win, R(tx, ty, tw, th));

      // Draw wrapped text, offset upward by scroll_y.
      irect16_t vp = { tx, ty - s->scroll_y, tw, th + s->scroll_y };
      draw_text_wrapped(s->buf, &vp, get_sys_color(brTextNormal));

      // Draw caret when focused.
      if (focused) {
        int cx, cy;
        me_cursor_xy(s->buf, s->cursor, tw, &cx, &cy);
        int cur_y = ty + cy - s->scroll_y;
        if (cur_y >= ty - SMALL_LINE_HEIGHT && cur_y < ty + th) {
          fill_rect(get_sys_color(brTextNormal),
                    R(tx + cx, cur_y, 2, CHAR_HEIGHT));
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
      int lx = (int)(int16_t)LOWORD(wparam) - ME_PADDING;
      int ly = (int)(int16_t)HIWORD(wparam) - ME_PADDING + s->scroll_y;
      int target_y = (ly / SMALL_LINE_HEIGHT) * SMALL_LINE_HEIGHT;
      if (target_y < 0) target_y = 0;
      s->cursor = me_find_at_xy(s->buf, s->len, lx, target_y, tw);
      me_ensure_visible(s, tw, th);
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
      int total_h = calc_text_height(s->buf, tw);
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
      if (s->len >= ME_MAX_LEN) return true;
      memmove(s->buf + s->cursor + 1,
              s->buf + s->cursor,
              (size_t)(s->len - s->cursor + 1));
      s->buf[s->cursor] = c;
      s->cursor++;
      s->len++;
      int tw, th;
      me_text_dims(win, &tw, &th);
      me_ensure_visible(s, tw, th);
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
          if (s->len < ME_MAX_LEN) {
            memmove(s->buf + s->cursor + 1,
                    s->buf + s->cursor,
                    (size_t)(s->len - s->cursor + 1));
            s->buf[s->cursor] = '\n';
            s->cursor++;
            s->len++;
            me_ensure_visible(s, tw, th);
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
            me_ensure_visible(s, tw, th);
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
            me_ensure_visible(s, tw, th);
            me_sync_scrollbar(win, s);
            invalidate_window(win);
          }
          return true;

        case AX_KEY_LEFTARROW:
          if (s->cursor > 0) {
            s->cursor--;
            me_ensure_visible(s, tw, th);
            invalidate_window(win);
          }
          return true;

        case AX_KEY_RIGHTARROW:
          if (s->cursor < s->len) {
            s->cursor++;
            me_ensure_visible(s, tw, th);
            invalidate_window(win);
          }
          return true;

        case AX_KEY_UPARROW: {
          int cx, cy;
          me_cursor_xy(s->buf, s->cursor, tw, &cx, &cy);
          int ny = cy - SMALL_LINE_HEIGHT;
          if (ny >= 0) {
            s->cursor = me_find_at_xy(s->buf, s->len, cx, ny, tw);
            me_ensure_visible(s, tw, th);
            invalidate_window(win);
          }
          return true;
        }

        case AX_KEY_DOWNARROW: {
          int cx, cy;
          me_cursor_xy(s->buf, s->cursor, tw, &cx, &cy);
          int ny = cy + SMALL_LINE_HEIGHT;
          int new_pos = me_find_at_xy(s->buf, s->len, cx, ny, tw);
          if (new_pos != s->cursor) {
            s->cursor = new_pos;
            me_ensure_visible(s, tw, th);
            invalidate_window(win);
          }
          return true;
        }

        case AX_KEY_HOME: {
          // Move to start of the current logical line (scan back to previous \n or buf start).
          int p = s->cursor;
          while (p > 0 && s->buf[p - 1] != '\n') p--;
          s->cursor = p;
          me_ensure_visible(s, tw, th);
          invalidate_window(win);
          return true;
        }

        case AX_KEY_END: {
          // Move to end of the current logical line (forward to next \n or buf end).
          int p = s->cursor;
          while (p < s->len && s->buf[p] != '\n') p++;
          s->cursor = p;
          me_ensure_visible(s, tw, th);
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

    // ── edSetText ───────────────────────────────────────────
    case edSetText: {
      if (!s || !lparam) return true;
      const char *src = (const char *)lparam;
      strncpy(s->buf, src, ME_BUF_SIZE - 1);
      s->buf[ME_BUF_SIZE - 1] = '\0';
      s->len      = (int)strlen(s->buf);
      s->cursor   = s->len;
      s->scroll_y = 0;
      me_sync_scrollbar(win, s);
      invalidate_window(win);
      return true;
    }

    // ── edGetText ───────────────────────────────────────────
    case edGetText: {
      if (!s || !lparam) return 0;
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
      int total_h   = calc_text_height(s->buf, tw);
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
