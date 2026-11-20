#include "diff_view.h"
#include <orion/user/rect.h>

// A unified diff drawn the way GitHub Desktop draws one: a gutter with the old
// and new line numbers, added and removed rows tinted across their width, the
// changed part of a paired line tinted deeper, long lines wrapped at spaces,
// and a check on each line the commit includes. Every colour and metric comes
// from the DiffView rule in orion/themes/base.css.

#define DIFF_MAX_CELLS 4096
#define DIFF_INNER_MAX 1024   // longer lines get no intra-line highlight, as in GitHub Desktop

// ── Parsing ──────────────────────────────────────────────────────────────────

static int parse_range(const char **p, int *count) {
  char *end;
  int start = (int)strtol(*p, &end, 10);
  *count = 1;
  if (*end == ',') *count = (int)strtol(end + 1, &end, 10);
  *p = end;
  return start;
}

static bool parse_hunk_header(const char *s, int *old_start, int *old_count, int *new_start, int *new_count) {
  if (strncmp(s, "@@ -", 4)) return false;
  s += 4;
  *old_start = parse_range(&s, old_count);
  if (strncmp(s, " +", 2)) return false;
  s += 2;
  *new_start = parse_range(&s, new_count);
  return !strncmp(s, " @@", 3);
}

static int utf8_back(const char *s, int i) { while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--; return i; }

// The changed range of each line against the other: what is left after the
// common prefix and suffix (GitHub Desktop's relativeChanges).
static void pair_lines(gc_diff_line_t *del, gc_diff_line_t *add) {
  int a = (int)strlen(del->text), b = (int)strlen(add->text);
  if (a >= DIFF_INNER_MAX || b >= DIFF_INNER_MAX) return;
  int pre = 0;
  while (pre < a && pre < b && del->text[pre] == add->text[pre]) pre++;
  pre = MIN(utf8_back(del->text, pre), utf8_back(add->text, pre));
  int suf = 0;
  while (suf < a - pre && suf < b - pre && del->text[a - 1 - suf] == add->text[b - 1 - suf]) suf++;
  while (suf > 0 && ((unsigned char)del->text[a - suf] & 0xC0) == 0x80) suf--;
  del->inner_start = add->inner_start = pre;
  del->inner_len = a - pre - suf;
  add->inner_len = b - pre - suf;
}

// Within a run of removed then added lines, pair them up when the counts match.
static void pair_run(gc_diff_state_t *st, int from, int to) {
  int dels = 0, adds = 0;
  for (int i = from; i < to; i++) {
    if (st->lines[i].kind == GC_DIFF_DEL) dels++;
    if (st->lines[i].kind == GC_DIFF_ADD) adds++;
  }
  if (!dels || dels != adds) return;
  for (int d = from, a = from, k = 0; k < dels; k++, d++, a++) {
    while (st->lines[d].kind != GC_DIFF_DEL) d++;
    while (st->lines[a].kind != GC_DIFF_ADD) a++;
    pair_lines(&st->lines[d], &st->lines[a]);
  }
}

static void parse_diff(window_t *win, gc_diff_state_t *st) {
  free(st->lines);
  st->lines = NULL;
  st->line_count = st->hunk_count = st->row_count = st->wrap_cols = 0;
  st->current_hunk = -1;

  int count = 1;
  for (char *p = st->diff_buf; *p; p++) count += *p == '\n';
  st->lines = calloc((size_t)count, sizeof(*st->lines));
  if (!st->lines) {
    fprintf(stderr, "[gitclient] diff allocation failed win=%u lines=%d\n", win->id, count);
    fflush(stderr);
    return;
  }

  int old_no = 0, new_no = 0, old_left = 0, new_left = 0, hunk = -1, run = -1, max_no = 0;
  char *p = st->diff_buf;
  while (*p) {
    char *nl = strchr(p, '\n');
    if (nl) *nl = '\0';
    gc_diff_line_t *l = &st->lines[st->line_count++];
    l->text = p; l->hunk = hunk;
    int os, oc, ns, nc;
    if (p[0] == '\\') {
      l->kind = GC_DIFF_NOTE; l->text = p + (p[1] == ' ' ? 2 : 1);
    } else if ((old_left > 0 || new_left > 0) && (p[0] == ' ' || p[0] == '-' || p[0] == '+' || p[0] == '\0')) {
      l->text = p[0] ? p + 1 : p;
      if (p[0] == '-')      { l->kind = GC_DIFF_DEL;     l->old_no = old_no++; old_left--; }
      else if (p[0] == '+') { l->kind = GC_DIFF_ADD;     l->new_no = new_no++; new_left--; }
      else                  { l->kind = GC_DIFF_CONTEXT; l->old_no = old_no++; l->new_no = new_no++; old_left--; new_left--; }
      max_no = MAX(max_no, MAX(old_no, new_no));
    } else if (parse_hunk_header(p, &os, &oc, &ns, &nc)) {
      l->kind = GC_DIFF_HUNK;
      if (st->hunk_count < GC_DIFF_MAX_HUNKS) {
        hunk = st->hunk_count;
        st->hunk_offsets[st->hunk_count++] = st->line_count - 1;
      }
      l->hunk = hunk;
      old_no = os; new_no = ns; old_left = oc; new_left = nc;
    } else {
      l->kind = GC_DIFF_META; l->hunk = -1;
      old_left = new_left = 0;
    }
    bool changed = l->kind == GC_DIFF_ADD || l->kind == GC_DIFF_DEL || l->kind == GC_DIFF_NOTE;
    if (changed && run < 0) run = st->line_count - 1;
    if (!changed && run >= 0) { pair_run(st, run, st->line_count - 1); run = -1; }
    if (!nl) break;
    p = nl + 1;
  }
  if (run >= 0) pair_run(st, run, st->line_count);
  if (st->hunk_count) st->current_hunk = 0;
  st->number_digits = 1;
  for (int n = max_no; n >= 10; n /= 10) st->number_digits++;
}

// ── Geometry ─────────────────────────────────────────────────────────────────

typedef struct {
  int cw, lh, ch;                // cell width, row height, glyph height
  int pad, check, number, marker, text_x, right;
} diff_metrics_t;

static diff_metrics_t diff_metrics(const gc_diff_state_t *st) {
  diff_metrics_t m;
  m.cw  = MAX(1, text_cell_width("DiffView"));
  m.ch  = text_char_height("DiffView");
  m.lh  = MAX(m.ch, text_line_height("DiffView"));
  m.pad = ui_px("DiffView", "--gutter-padding");
  m.check  = ui_px("DiffView", "--check-size") + m.pad;
  m.number = st->number_digits * m.cw + m.pad;
  m.marker = 2 * m.cw;
  m.text_x = m.check + 2 * m.number + m.pad + m.marker;
  m.right  = ui_px("DiffView", "padding-right");
  return m;
}

static bool line_hidden(const gc_diff_state_t *st, const gc_diff_line_t *l) {
  if (l->kind != GC_DIFF_META) return false;
  if (!strncmp(l->text, "index ", 6) || !strncmp(l->text, "--- ", 4) || !strncmp(l->text, "+++ ", 4)) return true;
  return st->hunk_path[0] && !strncmp(l->text, "diff --", 7);   // one file: the header already names it
}

static uint32_t utf8_next(const char **s) {
  const unsigned char *p = (const unsigned char *)*s;
  uint32_t c = *p++;
  int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
  if (extra) c &= 0x3F >> extra;
  while (extra-- > 0 && (*p & 0xC0) == 0x80) c = (c << 6) | (*p++ & 0x3F);
  *s = (const char *)p;
  return c;
}

// A line as cells, tabs expanded, with the changed range given its own background.
static int line_cells(const gc_diff_line_t *l, text_cell_t *out, int max, uint32_t ink, uint32_t inner) {
  int tab = MAX(1, (int)ui_number("DiffView", "tab-size")), n = 0;
  const char *s = l->text;
  while (*s && n < max) {
    int at = (int)(s - l->text);
    uint32_t bg = l->inner_len && at >= l->inner_start && at < l->inner_start + l->inner_len ? inner : 0;
    uint32_t c = utf8_next(&s);
    if (c == '\r' && !*s) break;   // CRLF files keep their \r in the text, for patches
    if (c == '\t') {
      do out[n++] = (text_cell_t){' ', ink, bg}; while (n % tab && n < max);
    } else {
      out[n++] = (text_cell_t){c, ink, bg};
    }
  }
  return n;
}

// Wrap every line to cols columns, breaking after the last space that fits.
// Returns the row count; rows are stored only when store is set.
static int layout_rows(gc_diff_state_t *st, int cols, bool store) {
  static text_cell_t cells[DIFF_MAX_CELLS];
  int rows = 0;
  for (int i = 0; i < st->line_count; i++) {
    if (line_hidden(st, &st->lines[i])) continue;
    int n = line_cells(&st->lines[i], cells, DIFF_MAX_CELLS, 0, 0), start = 0;
    do {
      int len = MIN(cols, n - start);
      if (start + len < n) {
        int brk = len;
        while (brk > 0 && cells[start + brk - 1].codepoint != ' ') brk--;
        if (brk > 0) len = brk;
      }
      if (store) {
        if (rows == st->row_cap) {
          int cap = MAX(256, st->row_cap * 2);
          gc_diff_row_t *grown = realloc(st->rows, (size_t)cap * sizeof(*grown));
          if (!grown) { fprintf(stderr, "[gitclient] diff row allocation failed rows=%d\n", cap); fflush(stderr); return rows; }
          st->rows = grown; st->row_cap = cap;
        }
        st->rows[rows] = (gc_diff_row_t){i, start, len};
      }
      rows++;
      start += len;
    } while (start < n);
  }
  return rows;
}

static int wrap_columns(const gc_diff_state_t *st, int width) {
  diff_metrics_t m = diff_metrics(st);
  return MAX(8, (width - m.text_x - m.right) / m.cw);
}

static void ensure_rows(window_t *win, gc_diff_state_t *st) {
  int cols = wrap_columns(st, window_scroll_content_rect(win).w);
  if (cols == st->wrap_cols && st->rows) return;
  st->wrap_cols = cols;
  st->row_count = layout_rows(st, cols, true);
}

static int row_of_line(const gc_diff_state_t *st, int line) {
  for (int r = 0; r < st->row_count; r++)
    if (st->rows[r].line >= line) return r;
  return MAX(0, st->row_count - 1);
}

static irect16_t row_rect(window_t *win, int row) {
  irect16_t content = window_scroll_content_rect(win);
  int lh = diff_metrics(win->userdata).lh;
  return R(content.x, content.y + row * lh, content.w, lh);
}

// ── Inclusion ────────────────────────────────────────────────────────────────

static void notify_toggled(window_t *win) {
  window_t *root = get_root_window(win);
  if (root) send_message(root, evCommand, MAKEDWORD(0, (uint16_t)GC_DIFF_LINES_TOGGLED), win);
}

// A hunk toggles as GitHub Desktop's does: all in unless all were in already.
static bool toggle_hunk(gc_diff_state_t *st, int hunk) {
  bool any = false, all = true;
  for (int i = 0; i < st->line_count; i++)
    if (st->lines[i].hunk == hunk && st->lines[i].selectable) { any = true; all &= st->lines[i].included; }
  for (int i = 0; i < st->line_count; i++)
    if (st->lines[i].hunk == hunk && st->lines[i].selectable) st->lines[i].included = !all;
  return any;
}

static void reveal_hunk(window_t *win, gc_diff_state_t *st) {
  if (st->current_hunk < 0) return;
  ensure_rows(win, st);
  window_ensure_rect_visible(win, row_rect(win, row_of_line(st, st->hunk_offsets[st->current_hunk])), SB_VERT);
  invalidate_window(win);
}

static void scroll_by(window_t *win, int dy) {
  scroll_info_t si = { .fMask = SIF_POS, .nPos = (int)win->vscroll.pos + dy };
  set_scroll_info(win, SB_VERT, &si, true);
  invalidate_window(win);
}

// ── Painting ─────────────────────────────────────────────────────────────────

static void draw_number(int n, int right, int y, uint32_t col) {
  if (n <= 0) return;
  char buf[16];
  int len = snprintf(buf, sizeof(buf), "%d", n);
  text_cell_t cells[16];
  for (int i = 0; i < len; i++) cells[i] = (text_cell_t){(uint32_t)buf[i], col, 0};
  draw_text_cells("DiffView", cells, len, right - len * text_cell_width("DiffView"), y);
}

static void paint(window_t *win, gc_diff_state_t *st) {
  irect16_t client = get_client_rect(win);
  fill_rect(ui_color("DiffView", "background"), client);
  if (!st) return;
  diff_metrics_t m = diff_metrics(st);
  uint32_t ink = ui_color("DiffView", "color");

  if (!st->line_count) {
    const char *text = st->placeholder[0] ? st->placeholder : ui_tr("No local changes");
    int w = text_strwidth("DiffView", text);
    draw_text("DiffView", text, client.x + MAX(0, (client.w - w) / 2), client.y + (client.h - m.ch) / 2,
              ui_color("DiffView", "--meta-color"));
    return;
  }

  ensure_rows(win, st);
  uint32_t gutter_bg = ui_color("DiffView", "--gutter-background"), gutter_ink = ui_color("DiffView", "--gutter-color");
  uint32_t meta = ui_color("DiffView", "--meta-color"), check = ui_color("DiffView", "--included-color");
  int check_size = ui_px("DiffView", "--check-size");
  static text_cell_t cells[DIFF_MAX_CELLS];
  int cells_line = -1, n = 0;
  int first = MAX(0, (int)win->vscroll.pos / m.lh);
  for (int r = first; r < st->row_count; r++) {
    irect16_t row = window_scroll_rect_to_client(win, row_rect(win, r));
    if (row.y >= client.y + client.h) break;
    const gc_diff_row_t *vr = &st->rows[r];
    gc_diff_line_t *l = &st->lines[vr->line];
    const char *bg_key = NULL, *gutter_key = NULL, *inner_key = NULL, *marker = NULL;
    switch (l->kind) {
      case GC_DIFF_ADD:  bg_key = "--added-background";   gutter_key = "--added-gutter";   inner_key = "--added-inner";   marker = "+"; break;
      case GC_DIFF_DEL:  bg_key = "--removed-background"; gutter_key = "--removed-gutter"; inner_key = "--removed-inner"; marker = "-"; break;
      case GC_DIFF_HUNK: bg_key = l->hunk == st->current_hunk ? "--hunk-selection" : "--hunk-background"; gutter_key = bg_key; break;
      default: break;
    }
    if (l->selectable && l->included) gutter_key = "--included-gutter";
    irect16_t gutter = rect_split_left(row, m.text_x - m.marker - m.pad / 2);
    irect16_t body = rect_trim_left(row, gutter.w);
    fill_rect(gutter_key ? ui_color("DiffView", gutter_key) : gutter_bg, gutter);
    if (bg_key) fill_rect(ui_color("DiffView", bg_key), body);
    int ty = row.y + (m.lh - m.ch) / 2;
    if (vr->start == 0) {
      uint32_t numbers = l->selectable && l->included ? check : gutter_ink;
      if (l->selectable && l->included)
        draw_sysicon("gc-check", row.x + m.pad / 2, row.y + (m.lh - check_size) / 2, check_size, check);
      draw_number(l->old_no, row.x + m.check + m.number, ty, numbers);
      draw_number(l->new_no, row.x + m.check + 2 * m.number, ty, numbers);
      if (marker) draw_text("DiffView", marker, row.x + m.text_x - m.marker, ty, ink);
    }
    if (vr->line != cells_line) {
      uint32_t text_ink = l->kind == GC_DIFF_HUNK || l->kind == GC_DIFF_META || l->kind == GC_DIFF_NOTE ? meta : ink;
      n = line_cells(l, cells, DIFF_MAX_CELLS, text_ink, inner_key ? ui_color("DiffView", inner_key) : 0);
      cells_line = vr->line;
    }
    if (vr->start < n)
      draw_text_cells("DiffView", cells + vr->start, MIN(vr->len, n - vr->start), row.x + m.text_x, ty);
  }
}

// ── Window procedure ─────────────────────────────────────────────────────────

result_t gc_diff_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  gc_diff_state_t *st = (gc_diff_state_t *)win->userdata;
  switch (msg) {
    case evCreate:
      st = calloc(1, sizeof(gc_diff_state_t));
      if (!st) { fprintf(stderr, "[gitclient] diff view allocation failed win=%u\n", win->id); fflush(stderr); return false; }
      win->userdata = st;
      st->current_hunk = -1;
      window_set_scroll_content(win, 0, 0, (irect16_t){0}, (irect16_t){0});
      return true;

    case evDestroy:
      if (st) { free(st->lines); free(st->rows); free(st); win->userdata = NULL; }
      return false;

    case edSetText: {   // lparam: plain unified diff text; the view keeps a copy and parses it
      if (!st) return false;
      const char *text = lparam ? (const char *)lparam : "";
      if (text != st->diff_buf) snprintf(st->diff_buf, sizeof(st->diff_buf), "%s", text);
      parse_diff(win, st);
      scroll_info_t si = { .fMask = SIF_POS, .nPos = 0 };
      set_scroll_info(win, SB_VERT, &si, false);
      window_sync_scroll_content(win);
      invalidate_window(win);
      return true;
    }

    case edSetPlaceholder:
      if (!st) return false;
      snprintf(st->placeholder, sizeof(st->placeholder), "%s", lparam ? (const char *)lparam : "");
      invalidate_window(win);
      return true;

    case evMeasureScrollContent: {
      layout_measure_t *m = lparam;
      if (!st || !m) return false;
      m->desired_w = 0;
      m->desired_h = st->line_count ? layout_rows(st, wrap_columns(st, m->avail_w), false) * diff_metrics(st).lh : 0;
      return true;
    }

    case evArrange: {
      layout_arrange_t const *a = (layout_arrange_t const *)lparam;
      if (a) win->frame = R(a->rect.x, a->rect.y, MAX(1, a->rect.w), MAX(1, a->rect.h));
      window_sync_scroll_content(win);
      invalidate_window(win);
      return true;
    }

    case evResize:
    case evVScroll:
      invalidate_window(win);
      return false;

    case evLeftButtonDown: {
      set_focus(win);
      if (!st || !st->line_count) return true;
      ensure_rows(win, st);
      irect16_t content = window_scroll_content_rect(win);
      diff_metrics_t m = diff_metrics(st);
      int x = (int16_t)LOWORD(wparam) - content.x, y = (int16_t)HIWORD(wparam) - content.y;
      int r = y / m.lh;
      if (y < 0 || r >= st->row_count) return true;
      gc_diff_line_t *l = &st->lines[st->rows[r].line];
      if (l->hunk >= 0) st->current_hunk = l->hunk;
      bool toggled = false;
      if (x < m.text_x - m.marker) {
        if (l->kind == GC_DIFF_HUNK) toggled = toggle_hunk(st, l->hunk);
        else if (l->selectable)      { l->included = !l->included; toggled = true; }
      }
      invalidate_window(win);
      if (toggled) notify_toggled(win);
      return true;
    }

    case evKeyDown: {
      if (!st) return false;
      int lh = diff_metrics(st).lh, page = MAX(lh, window_scroll_viewport(win).h - lh);
      switch (wparam) {
        case AX_KEY_J:    scroll_by(win, lh);    return true;
        case AX_KEY_K:    scroll_by(win, -lh);   return true;
        case AX_KEY_PGDN: scroll_by(win, page);  return true;
        case AX_KEY_PGUP: scroll_by(win, -page); return true;
        case AX_KEY_UPARROW:
        case AX_KEY_DOWNARROW:
          if (!st->hunk_count) { scroll_by(win, (wparam == AX_KEY_UPARROW ? -3 : 3) * lh); return true; }
          st->current_hunk = CLAMP(st->current_hunk + (wparam == AX_KEY_UPARROW ? -1 : 1), 0, st->hunk_count - 1);
          reveal_hunk(win, st);
          return true;
        case AX_KEY_ENTER:
        case AX_KEY_SPACE:
          if (st->current_hunk >= 0 && toggle_hunk(st, st->current_hunk)) { invalidate_window(win); notify_toggled(win); }
          return true;
        default:
          return false;
      }
    }

    case evPaint:
      paint(win, st);
      return true;

    default:
      return false;
  }
}
