// Multi-line text edit control (analogous to WinAPI ES_MULTILINE edit).
//
// State is stored in win->userdata (me_state_t, heap-allocated at create time).
// Text lives in buf[ME_BUF_SIZE]; win->title is not used for content.
//
// Messages handled in addition to the standard window messages:
//   kMultiEditMessageGetText  wparam=buf_size, lparam=char* → copies text, returns byte count
//   kMultiEditMessageSetText  wparam=0, lparam=const char* → replaces text, resets cursor/scroll

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"

#define ME_BUF_SIZE    2048
#define ME_PADDING     3
// Must match SMALL_LINE_HEIGHT in user/text.c.
#define ME_LINE_HEIGHT 12
// Must match SPACE_WIDTH in user/text.c.
#define ME_SPACE_W     3

extern window_t *_focused;
extern window_t *get_root_window(window_t *window);
extern int titlebar_height(window_t const *win);

typedef struct {
  char buf[ME_BUF_SIZE];
  int  len;        // strlen(buf)
  int  cursor;     // byte offset of caret in buf
  int  scroll_y;   // vertical scroll in pixels
} me_state_t;

// ---------------------------------------------------------------------------
// Internal layout helpers
// ---------------------------------------------------------------------------

// Advance one character's worth of wrapping state.  Mutates *cx/*cy.
static void me_advance(const char *buf, int i, int max_w, int *cx, int *cy) {
  unsigned char c = (unsigned char)buf[i];
  if (c == '\n') {
    *cx = 0;
    *cy += ME_LINE_HEIGHT;
  } else if (c == ' ') {
    *cx += ME_SPACE_W;
  } else {
    char tmp[2] = { (char)c, '\0' };
    int cw = strnwidth(tmp, 1);
    if (cw > 0 && *cx + cw > max_w) {
      *cx = 0;
      *cy += ME_LINE_HEIGHT;
    }
    *cx += cw;
  }
}

// Compute visual (cx, cy) of byte offset `cursor` in buf.
// Coordinates are relative to the text-area origin (0, 0).
static void me_cursor_xy(const char *buf, int cursor, int max_w,
                          int *out_x, int *out_y) {
  int cx = 0, cy = 0;
  for (int i = 0; i < cursor && buf[i]; i++)
    me_advance(buf, i, max_w, &cx, &cy);
  *out_x = cx;
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
  int cx, cy;
  me_cursor_xy(s->buf, s->cursor, max_w, &cx, &cy);
  if (cy < s->scroll_y)
    s->scroll_y = cy;
  if (cy + ME_LINE_HEIGHT > s->scroll_y + vis_h)
    s->scroll_y = cy + ME_LINE_HEIGHT - vis_h;
  if (s->scroll_y < 0) s->scroll_y = 0;
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

result_t win_multiedit(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  me_state_t *s = (me_state_t *)win->userdata;

  switch (msg) {
    // ── Create ─────────────────────────────────────────────────────────────
    case kWindowMessageCreate: {
      s = (me_state_t *)allocate_window_data(win, sizeof(me_state_t));
      if (!s) return true;
      strncpy(s->buf, win->title, ME_BUF_SIZE - 1);
      s->buf[ME_BUF_SIZE - 1] = '\0';
      s->len      = (int)strlen(s->buf);
      s->cursor   = s->len;
      s->scroll_y = 0;
      return true;
    }

    // ── Destroy ────────────────────────────────────────────────────────────
    case kWindowMessageDestroy:
      free(s);
      win->userdata = NULL;
      return false;

    // ── Focus / blur ───────────────────────────────────────────────────────
    case kWindowMessageSetFocus:
    case kWindowMessageKillFocus:
      invalidate_window(win);
      return false;

    // ── Paint ──────────────────────────────────────────────────────────────
    case kWindowMessagePaint: {
      if (!s) return true;
      bool focused = (_focused == win);

      // Focus ring (matches win_textedit style).
      fill_rect(focused ? get_sys_color(kColorFocusRing)
                        : get_sys_color(kColorWindowBg),
                win->frame.x - 2, win->frame.y - 2,
                win->frame.w + 4, win->frame.h + 4);

      // Inset bevel border.
      draw_button(&win->frame, 1, 1, true);

      int tw = win->frame.w - ME_PADDING * 2;
      int th = win->frame.h - ME_PADDING * 2;
      int tx = win->frame.x + ME_PADDING;
      int ty = win->frame.y + ME_PADDING;

      // Clip to text area (scissor uses absolute screen coordinates).
      window_t *root = get_root_window(win);
      int root_t = titlebar_height(root);
      set_clip_rect(NULL, &(rect_t){
        root->frame.x + win->frame.x + ME_PADDING,
        root->frame.y + root_t + win->frame.y + ME_PADDING,
        tw, th
      });

      // Draw wrapped text, offset upward by scroll_y.
      rect_t vp = { tx, ty - s->scroll_y, tw, th + s->scroll_y };
      draw_text_wrapped(s->buf, &vp, get_sys_color(kColorTextNormal));

      // Draw caret when focused.
      if (focused) {
        int cx, cy;
        me_cursor_xy(s->buf, s->cursor, tw, &cx, &cy);
        int cur_y = ty + cy - s->scroll_y;
        if (cur_y >= ty - ME_LINE_HEIGHT && cur_y < ty + th) {
          fill_rect(get_sys_color(kColorTextNormal),
                    tx + cx, cur_y, 2, CHAR_HEIGHT);
        }
      }

      // Reset scissor to full control frame so subsequent rendering is unclipped.
      set_clip_rect(NULL, &(rect_t){
        root->frame.x + win->frame.x,
        root->frame.y + root_t + win->frame.y,
        win->frame.w, win->frame.h
      });

      return true;
    }

    // ── Mouse click to position ────────────────────────────────────────────
    case kWindowMessageLeftButtonUp: {
      if (!s) return true;
      int tw = win->frame.w - ME_PADDING * 2;
      int th = win->frame.h - ME_PADDING * 2;
      // wparam carries client-local x (LOWORD) and y (HIWORD).
      int lx = (int)(int16_t)LOWORD(wparam) - ME_PADDING;
      int ly = (int)(int16_t)HIWORD(wparam) - ME_PADDING + s->scroll_y;
      int target_y = (ly / ME_LINE_HEIGHT) * ME_LINE_HEIGHT;
      if (target_y < 0) target_y = 0;
      s->cursor = me_find_at_xy(s->buf, s->len, lx, target_y, tw);
      me_ensure_visible(s, tw, th);
      invalidate_window(win);
      return true;
    }

    // ── Mouse-wheel scroll ─────────────────────────────────────────────────
    case kWindowMessageWheel: {
      if (!s) return true;
      int th = win->frame.h - ME_PADDING * 2;
      int tw = win->frame.w - ME_PADDING * 2;
      int delta = -(int16_t)HIWORD(wparam);
      s->scroll_y += delta;
      if (s->scroll_y < 0) s->scroll_y = 0;
      int total_h = calc_text_height(s->buf, tw);
      int max_scroll = total_h - th;
      if (max_scroll < 0) max_scroll = 0;
      if (s->scroll_y > max_scroll) s->scroll_y = max_scroll;
      invalidate_window(win);
      return true;
    }

    // ── Text input ─────────────────────────────────────────────────────────
    case kWindowMessageTextInput: {
      if (!s || !lparam) return true;
      char c = *(const char *)lparam;
      // Accept only printable ASCII; newlines are handled via AX_KEY_ENTER.
      if ((unsigned char)c < 32 || (unsigned char)c > 126) return true;
      if (s->len + 1 >= ME_BUF_SIZE - 1) return true;
      memmove(s->buf + s->cursor + 1,
              s->buf + s->cursor,
              (size_t)(s->len - s->cursor + 1));
      s->buf[s->cursor] = c;
      s->cursor++;
      s->len++;
      int tw = win->frame.w - ME_PADDING * 2;
      int th = win->frame.h - ME_PADDING * 2;
      me_ensure_visible(s, tw, th);
      invalidate_window(win);
      return true;
    }

    // ── Key navigation and editing ─────────────────────────────────────────
    case kWindowMessageKeyDown: {
      if (!s) return true;
      int tw = win->frame.w - ME_PADDING * 2;
      int th = win->frame.h - ME_PADDING * 2;
      switch (wparam) {

        case AX_KEY_ENTER:
          if (s->len + 1 < ME_BUF_SIZE - 1) {
            memmove(s->buf + s->cursor + 1,
                    s->buf + s->cursor,
                    (size_t)(s->len - s->cursor + 1));
            s->buf[s->cursor] = '\n';
            s->cursor++;
            s->len++;
            me_ensure_visible(s, tw, th);
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
          int ny = cy - ME_LINE_HEIGHT;
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
          int ny = cy + ME_LINE_HEIGHT;
          int new_pos = me_find_at_xy(s->buf, s->len, cx, ny, tw);
          if (new_pos != s->cursor) {
            s->cursor = new_pos;
            me_ensure_visible(s, tw, th);
            invalidate_window(win);
          }
          return true;
        }

        case AX_KEY_HOME:
          s->cursor = 0;
          me_ensure_visible(s, tw, th);
          invalidate_window(win);
          return true;

        case AX_KEY_END:
          s->cursor = s->len;
          me_ensure_visible(s, tw, th);
          invalidate_window(win);
          return true;

        case AX_KEY_TAB:
          // Notify parent and yield focus so Tab advances to next control.
          send_message(get_root_window(win), kWindowMessageCommand,
                       MAKEDWORD(win->id, kEditNotificationUpdate), win);
          return false;

        case AX_KEY_ESCAPE:
          return true;

        default:
          return false;
      }
    }

    // ── kMultiEditMessageSetText ───────────────────────────────────────────
    case kMultiEditMessageSetText: {
      if (!s || !lparam) return true;
      const char *src = (const char *)lparam;
      strncpy(s->buf, src, ME_BUF_SIZE - 1);
      s->buf[ME_BUF_SIZE - 1] = '\0';
      s->len      = (int)strlen(s->buf);
      s->cursor   = s->len;
      s->scroll_y = 0;
      invalidate_window(win);
      return true;
    }

    // ── kMultiEditMessageGetText ───────────────────────────────────────────
    case kMultiEditMessageGetText: {
      if (!s || !lparam) return 0;
      int maxlen = (int)wparam;
      char *dst  = (char *)lparam;
      if (maxlen <= 0) return 0;
      strncpy(dst, s->buf, (size_t)(maxlen - 1));
      dst[maxlen - 1] = '\0';
      return (result_t)strlen(dst);
    }

    default:
      return false;
  }
}
