// VIEW: Task list — 3-column report view (Title stretched, Priority/Status fixed).

#include "taskmanager.h"

// ============================================================
// Internal state
// ============================================================

#define TL_MAX_ROWS         256   // upper bound matching MAX_TASKS in controller

typedef struct {
  tasklist_row_t rows[TL_MAX_ROWS];
  int            count;
  int            selected;
} tl_state_t;

// ============================================================
// Layout helpers
// ============================================================

// Width available for the title column (accounts for visible vscrollbar).
static int tl_title_w(window_t *win) {
  int sb    = win->vscroll.visible ? SCROLLBAR_WIDTH : 0;
  int avail = win->frame.w - sb - TASKLIST_PRIORITY_W - TASKLIST_STATUS_W;
  return avail > 20 ? avail : 20;
}

// Total content height (header + rows).
static int tl_content_h(tl_state_t *st) {
  return TASKLIST_HEADER_H + st->count * TASKLIST_ROW_H;
}

// ============================================================
// Text truncation helper
// ============================================================

static void draw_text_clipped(const char *text, int x, int y,
                               int max_w, uint32_t col) {
  if (strwidth(text) <= max_w) {
    draw_text_small(text, x, y, col);
    return;
  }
  int dots_w = strwidth("...");
  int avail  = max_w - dots_w;
  if (avail <= 0) return;
  char buf[sizeof(((tasklist_row_t *)0)->title) + 4];  // title + "..."
  int n = 0, w = 0;
  while (text[n] && n < (int)(sizeof(buf) - 4)) {
    int cw = char_width((unsigned char)text[n]);
    if (w + cw > avail) break;
    buf[n] = text[n];
    w += cw;
    n++;
  }
  buf[n] = '\0';
  strcat(buf, "...");
  draw_text_small(buf, x, y, col);
}

// ============================================================
// Scrollbar sync
// ============================================================

static void tl_sync_scroll(window_t *win, tl_state_t *st) {
  if (!win || !st || win->frame.h <= 0) return;
  int total_h = tl_content_h(st);
  int view_h  = win->frame.h;
  int max_s   = total_h - view_h;
  if (max_s < 0) max_s = 0;
  if ((int)win->scroll[1] > max_s) win->scroll[1] = (uint32_t)max_s;

  scroll_info_t si;
  si.fMask = SIF_ALL;
  si.nMin  = 0;
  si.nMax  = total_h;
  si.nPage = (uint32_t)view_h;
  si.nPos  = (int)win->scroll[1];
  set_scroll_info(win, SB_VERT, &si, false);
}

// ============================================================
// Hit testing
// ============================================================

// Returns the row index clicked, or -1 if in the header or out of range.
// LOCAL_Y (kernel/event.c) packs coordinates as: visual_y + scroll[1], so
// my is already in content space.  To check whether the click falls inside
// the fixed non-scrolling header we convert back to visual space:
//   view_y = my - scroll[1]   (visual position from window top)
// Row index is derived from my directly because the row layout is also in
// content space:  row = (my - HEADER_H) / ROW_H.
static int tl_hit_row(window_t *win, uint32_t wparam, tl_state_t *st) {
  int my     = (int)(int16_t)HIWORD(wparam);
  int view_y = my - (int)win->scroll[1];
  if (view_y < TASKLIST_HEADER_H) return -1;
  int row = (my - TASKLIST_HEADER_H) / TASKLIST_ROW_H;
  return (row >= 0 && row < st->count) ? row : -1;
}

// Adjust win->scroll[1] so the currently selected row is fully visible below
// the non-scrolling header.  Scrolls up if the row is above the visible area,
// scrolls down if it extends past the bottom.  Calls tl_sync_scroll to clamp
// and update the scrollbar thumb after any change.
static void tl_ensure_visible(window_t *win, tl_state_t *st) {
  if (st->selected < 0) return;
  int row_top    = TASKLIST_HEADER_H + st->selected * TASKLIST_ROW_H;
  int row_bottom = row_top + TASKLIST_ROW_H;
  int scroll_y   = (int)win->scroll[1];
  int view_h     = win->frame.h;
  if (row_top - scroll_y < TASKLIST_HEADER_H) {
    win->scroll[1] = (uint32_t)(row_top - TASKLIST_HEADER_H);
    tl_sync_scroll(win, st);
  } else if (row_bottom - scroll_y > view_h) {
    win->scroll[1] = (uint32_t)(row_bottom - view_h);
    tl_sync_scroll(win, st);
  }
}

// ============================================================
// tasklist_proc
// ============================================================

result_t tasklist_proc(window_t *win, uint32_t msg,
                       uint32_t wparam, void *lparam) {
  tl_state_t *st = (tl_state_t *)win->userdata2;

  switch (msg) {
    case kWindowMessageCreate: {
      st = calloc(1, sizeof(tl_state_t));
      if (!st) return false;
      win->userdata2             = st;
      win->flags                |= WINDOW_VSCROLL;
      win->vscroll.visible_mode  = SB_VIS_AUTO;
      st->selected               = -1;
      tl_sync_scroll(win, st);
      return true;
    }

    case kWindowMessagePaint: {
      int scroll_y = (int)win->scroll[1];
      int title_w  = tl_title_w(win);
      int prio_x   = title_w;
      int stat_x   = title_w + TASKLIST_PRIORITY_W;
      int row_w    = win->frame.w - (win->vscroll.visible ? SCROLLBAR_WIDTH : 0);

      uint32_t hdr_bg  = get_sys_color(kColorWindowBg);
      uint32_t hdr_fg  = get_sys_color(kColorTextDisabled);
      uint32_t sep_col = get_sys_color(kColorDarkEdge);

      int hdr_y       = win->parent ? win->frame.y : 0;
      int clip_top    = win->parent ? win->frame.y + TASKLIST_HEADER_H : TASKLIST_HEADER_H;
      int clip_bottom = win->parent ? win->frame.y + win->frame.h      : win->frame.h;

      // Rows first — header is drawn after to stay always on top.
      for (int i = 0; i < st->count; i++) {
        int content_y = TASKLIST_HEADER_H + i * TASKLIST_ROW_H;
        int y         = content_y - scroll_y;
        int abs_y     = win->parent ? win->frame.y + y : y;

        if (abs_y + TASKLIST_ROW_H <= clip_top) continue;
        if (abs_y >= clip_bottom) break;

        bool sel    = (i == st->selected);
        uint32_t bg = sel ? get_sys_color(kColorTextNormal) : get_sys_color(kColorWindowBg);
        uint32_t fg = sel ? get_sys_color(kColorWindowBg)   : st->rows[i].color;

        fill_rect(bg, 0, y, row_w, TASKLIST_ROW_H - 1);
        draw_text_clipped(st->rows[i].title,    TASKLIST_PADDING, y + 2,
                          title_w - TASKLIST_PADDING * 2, fg);
        draw_text_clipped(st->rows[i].priority, prio_x + 3,       y + 2,
                          TASKLIST_PRIORITY_W - 6, fg);
        draw_text_clipped(st->rows[i].status,   stat_x + 3,       y + 2,
                          TASKLIST_STATUS_W   - 6, fg);
      }

      // Header drawn after rows so it always paints on top regardless of scroll.
      fill_rect(hdr_bg, 0, hdr_y, row_w, TASKLIST_HEADER_H);
      draw_text_small("Title",    TASKLIST_PADDING, hdr_y + 3, hdr_fg);
      draw_text_small("Priority", prio_x + 3,       hdr_y + 3, hdr_fg);
      draw_text_small("Status",   stat_x + 3,       hdr_y + 3, hdr_fg);
      fill_rect(sep_col, 0, hdr_y + TASKLIST_HEADER_H - 1, row_w, 1);

      // Vertical column dividers — drawn last so they span header and rows.
      fill_rect(sep_col, prio_x, win->parent ? win->frame.y : 0, 1, win->frame.h);
      fill_rect(sep_col, stat_x, win->parent ? win->frame.y : 0, 1, win->frame.h);

      return false;
    }

    case kWindowMessageResize:
      tl_sync_scroll(win, st);
      invalidate_window(win);
      return false;

    case kWindowMessageVScroll: {
      int total_h = tl_content_h(st);
      int max_s   = total_h - win->frame.h;
      if (max_s < 0) max_s = 0;
      int new_s = (int)wparam;
      if (new_s > max_s) new_s = max_s;
      win->scroll[1] = (uint32_t)new_s;
      tl_sync_scroll(win, st);
      invalidate_window(win);
      return true;
    }

    case kWindowMessageLeftButtonDown: {
      int row = tl_hit_row(win, wparam, st);
      if (row >= 0) {
        int old      = st->selected;
        st->selected = row;
        if (old != row) {
          send_message(get_root_window(win), kWindowMessageCommand,
                       MAKEDWORD(row, CVN_SELCHANGE), &st->rows[row]);
          invalidate_window(win);
        }
      }
      return true;
    }

    case kWindowMessageLeftButtonDoubleClick: {
      int row = tl_hit_row(win, wparam, st);
      if (row >= 0) {
        send_message(get_root_window(win), kWindowMessageCommand,
                     MAKEDWORD(row, CVN_DBLCLK), &st->rows[row]);
      }
      return true;
    }

    case kWindowMessageKeyDown: {
      if (!st || st->count == 0) return false;
      int cur  = st->selected;
      int next = cur;
      switch (wparam) {
        case AX_KEY_UPARROW:
          next = (cur <= 0) ? 0 : cur - 1;
          break;
        case AX_KEY_DOWNARROW:
          next = (cur < 0) ? 0 : (cur + 1 < st->count ? cur + 1 : cur);
          break;
        case AX_KEY_ENTER:
          if (cur < 0) return false;
          send_message(get_root_window(win), kWindowMessageCommand,
                       MAKEDWORD(cur, CVN_DBLCLK), &st->rows[cur]);
          return true;
        case AX_KEY_DEL:
          if (cur < 0) return false;
          send_message(get_root_window(win), kWindowMessageCommand,
                       MAKEDWORD(cur, CVN_DELETE), &st->rows[cur]);
          return true;
        default:
          return false;
      }
      if (next != cur && next >= 0) {
        st->selected = next;
        tl_ensure_visible(win, st);
        send_message(get_root_window(win), kWindowMessageCommand,
                     MAKEDWORD(next, CVN_SELCHANGE), &st->rows[next]);
        invalidate_window(win);
      }
      return true;
    }

    case TLVM_ADDROW: {
      tasklist_row_t *r = (tasklist_row_t *)lparam;
      if (!r || st->count >= TL_MAX_ROWS) return (result_t)-1;
      st->rows[st->count] = *r;
      st->count++;
      tl_sync_scroll(win, st);
      invalidate_window(win);
      return (result_t)(st->count - 1);
    }

    case CVM_CLEAR:
      st->count      = 0;
      st->selected   = -1;
      win->scroll[1] = 0;
      tl_sync_scroll(win, st);
      invalidate_window(win);
      return true;

    case CVM_SETSELECTION:
      if ((int)wparam >= 0 && (int)wparam < st->count) {
        st->selected = (int)wparam;
        invalidate_window(win);
        return true;
      }
      return false;

    case CVM_GETSELECTION:
      return (result_t)st->selected;

    case kWindowMessageDestroy:
      free(st);
      win->userdata2 = NULL;
      return true;

    default:
      return false;
  }
}

// ============================================================
// tasklist_refresh — repopulate the list from app state
// ============================================================

void tasklist_refresh(window_t *list_win) {
  if (!list_win || !g_app) return;

  send_message(list_win, CVM_CLEAR, 0, NULL);

  for (int i = 0; i < g_app->task_count; i++) {
    task_t *t = g_app->tasks[i];
    if (!t) continue;

    tasklist_row_t row;
    strncpy(row.title,    t->title,                        sizeof(row.title)    - 1);
    strncpy(row.priority, priority_to_string(t->priority), sizeof(row.priority) - 1);
    strncpy(row.status,   status_to_string(t->status),     sizeof(row.status)   - 1);
    row.title[sizeof(row.title) - 1]       = '\0';
    row.priority[sizeof(row.priority) - 1] = '\0';
    row.status[sizeof(row.status) - 1]     = '\0';
    row.task_idx = (uint32_t)i;
    row.color    = get_sys_color(kColorTextNormal);

    send_message(list_win, TLVM_ADDROW, 0, &row);
  }

  if (g_app->selected_idx >= 0 && g_app->selected_idx < g_app->task_count)
    send_message(list_win, CVM_SETSELECTION,
                 (uint32_t)g_app->selected_idx, NULL);
}
