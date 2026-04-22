// Unified diff viewer — custom window proc using the VGA monospace font.
//
// Diff lines are colour-coded:
//   '+'  → dark green background,  bright green text
//   '-'  → dark red background,    bright red text
//   '@'  → dark blue background,   cyan text
//   ' '  → default background,     normal text
//
// The window has WINDOW_VSCROLL for scrolling through the output.

#include "gitclient.h"
#include "vga_font.h"

// ============================================================
// Colours (0xAARRGGBB)
// ============================================================

#define CLR_ADD_BG   0xFF1A3A1A
#define CLR_ADD_FG   0xFF66FF66
#define CLR_DEL_BG   0xFF3A1A1A
#define CLR_DEL_FG   0xFFFF6666
#define CLR_HUNK_BG  0xFF1A1A3A
#define CLR_HUNK_FG  0xFF66CCFF
#define CLR_CTX_BG   0xFF1E1E1E
#define CLR_CTX_FG   0xFFCCCCCC
#define CLR_LNUM_BG  0xFF2A2A2A
#define CLR_LNUM_FG  0xFF888888
#define CLR_HEADER_BG 0xFF2E2E2E
#define CLR_HEADER_FG 0xFFAAAAAA

#define LINE_NUM_W    (5 * VGA_CHAR_W)  /* "12345" */

// ============================================================
// Per-window state
// ============================================================

typedef struct {
  char  **lines;      // pointers into diff_buf (not owned, gc->diff_buf is)
  int     line_count;
  int     scroll_y;   // first visible line index
} diff_state_t;

static int visible_lines(window_t *win) {
  return MAX(1, win->frame.h / VGA_CHAR_H);
}

// ============================================================
// Refresh: parse diff lines
// ============================================================

void gc_diff_refresh(void) {
  gc_state_t *gc = g_gc;
  if (!gc || !gc->diff_win) return;

  window_t *win = gc->diff_win;
  diff_state_t *st = (diff_state_t *)win->userdata;
  if (!st) return;

  // Free previous line table.
  free(st->lines);
  st->lines      = NULL;
  st->line_count = 0;
  st->scroll_y   = 0;

  if (!gc->repo) {
    invalidate_window(win);
    return;
  }

  // Decide what diff to show.
  int fsel = gc->selected_file;
  bool staged = false;
  const char *path = NULL;
  if (fsel >= 0 && fsel < gc->file_count) {
    staged = gc->files[fsel].staged;
    path   = gc->files[fsel].path;
  }

  git_get_diff(gc->repo, path, staged, gc->diff_buf, GC_MAX_DIFF_SIZE);

  if (!gc->diff_buf[0]) {
    invalidate_window(win);
    return;
  }

  // Count lines so we can allocate the pointer table.
  int count = 0;
  for (char *p = gc->diff_buf; *p; p++)
    if (*p == '\n') count++;
  if (gc->diff_buf[0]) count++;  // last line without trailing newline

  st->lines = (char **)malloc((size_t)count * sizeof(char *));
  if (!st->lines) { invalidate_window(win); return; }

  // Fill pointer table, replacing '\n' with '\0'.
  char *p = gc->diff_buf;
  while (*p) {
    st->lines[st->line_count++] = p;
    char *nl = strchr(p, '\n');
    if (!nl) break;
    *nl = '\0';
    p = nl + 1;
  }

  // Update the scrollbar.
  scroll_info_t si = {
    .fMask = SIF_RANGE | SIF_PAGE | SIF_POS,
    .nMin  = 0,
    .nMax  = MAX(0, st->line_count - 1),
    .nPage = (uint32_t)visible_lines(win),
    .nPos  = 0,
  };
  set_scroll_info(win, SB_VERT, &si, false);

  invalidate_window(win);
}

// ============================================================
// Window procedure
// ============================================================

result_t gc_diff_proc(window_t *win, uint32_t msg,
                      uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      diff_state_t *st = (diff_state_t *)calloc(1, sizeof(diff_state_t));
      win->userdata = st;
      return true;
    }

    case evDestroy: {
      diff_state_t *st = (diff_state_t *)win->userdata;
      if (st) {
        free(st->lines);
        free(st);
        win->userdata = NULL;
      }
      return false;
    }

    case evResize: {
      diff_state_t *st = (diff_state_t *)win->userdata;
      if (!st) return false;
      scroll_info_t si = {
        .fMask = SIF_PAGE,
        .nPage = (uint32_t)visible_lines(win),
      };
      set_scroll_info(win, SB_VERT, &si, true);
      invalidate_window(win);
      return false;
    }

    case evVScroll: {
      diff_state_t *st = (diff_state_t *)win->userdata;
      if (!st) return false;
      st->scroll_y = (int)wparam;
      scroll_info_t si = { .fMask = SIF_POS, .nPos = st->scroll_y };
      set_scroll_info(win, SB_VERT, &si, false);
      invalidate_window(win);
      return true;
    }

    case evWheel: {
      diff_state_t *st = (diff_state_t *)win->userdata;
      if (!st || !st->line_count) return false;
      int delta = (int)(int16_t)HIWORD(wparam);  // positive = scroll up
      int lines = delta < 0 ? 3 : -3;
      int new_pos = CLAMP(st->scroll_y + lines, 0, st->line_count - 1);
      if (new_pos != st->scroll_y) {
        st->scroll_y = new_pos;
        scroll_info_t si = { .fMask = SIF_POS, .nPos = st->scroll_y };
        set_scroll_info(win, SB_VERT, &si, false);
        invalidate_window(win);
      }
      return true;
    }

    case evPaint: {
      diff_state_t *st = (diff_state_t *)win->userdata;
      rect_t cr = get_client_rect(win);

      // Dark overall background.
      fill_rect(CLR_CTX_BG, &cr);

      if (!st || !st->lines) {
        if (!g_gc || !g_gc->repo) {
          draw_text_small("No repository open.", cr.x + 4, cr.y + 4,
                          get_sys_color(brTextDisabled));
        } else {
          draw_text_small("Select a file to view its diff.",
                          cr.x + 4, cr.y + 4,
                          get_sys_color(brTextDisabled));
        }
        return true;
      }

      int vis   = visible_lines(win);
      int start = st->scroll_y;
      int end   = MIN(start + vis, st->line_count);

      // Determine available width for the text content.
      int text_x = cr.x + LINE_NUM_W;
      int text_w = cr.w - LINE_NUM_W;
      if (text_w < 8) text_w = 8;

      int max_cols = text_w / VGA_CHAR_W;

      for (int li = start; li < end; li++) {
        const char *line = st->lines[li];
        int y = cr.y + (li - start) * VGA_CHAR_H;

        // Colour based on first character.
        uint32_t fg, bg;
        char first = line[0];
        if (first == '+') {
          fg = CLR_ADD_FG;  bg = CLR_ADD_BG;
        } else if (first == '-') {
          fg = CLR_DEL_FG;  bg = CLR_DEL_BG;
        } else if (first == '@') {
          fg = CLR_HUNK_FG; bg = CLR_HUNK_BG;
        } else if (first == 'd' || first == 'i' || first == 'n' ||
                   first == 'B') {
          // diff/index/new/Binary headers
          fg = CLR_HEADER_FG; bg = CLR_HEADER_BG;
        } else {
          fg = CLR_CTX_FG;  bg = CLR_CTX_BG;
        }

        // Line-number gutter.
        char lnum[16];
        snprintf(lnum, sizeof(lnum), "%4d ", li + 1);
        vga_draw_text(lnum, cr.x, y, CLR_LNUM_FG, CLR_LNUM_BG);

        // Content (clipped to visible columns).
        vga_draw_textn(line, max_cols, text_x, y, fg, bg);
      }

      return true;
    }

    default:
      return false;
  }
}
