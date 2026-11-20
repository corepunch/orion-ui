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
  st->line_count = st->hunk_count = st->row_count = st->wrap_width = 0;
  st->current_hunk = -1;

  int count = 1;
  for (char *p = st->diff_buf; *p; p++) count += *p == '\n';
  st->lines = calloc((size_t)count, sizeof(*st->lines));
  st->line_cap = st->lines ? count : 0;
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
      l->old_no = oc ? os : os + 1; l->new_no = nc ? ns : ns + 1;   // git names the line before an empty side
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

// ── Context expansion ────────────────────────────────────────────────────────
// Lines between hunks come from the new side's text, as GitHub Desktop splices
// them in: a hunk grows up or down by GC_DIFF_EXPAND_STEP lines, and two hunks
// whose gap closes become one.

typedef struct { int first, end, old_first, old_end; } hunk_span_t;   // [first, end) on each side

static bool is_hunk_body(const gc_diff_line_t *l) { return l->kind != GC_DIFF_HUNK && l->kind != GC_DIFF_META; }

static int hunk_last_line(const gc_diff_state_t *st, int h) {
  int i = st->hunk_offsets[h];
  while (i + 1 < st->line_count && is_hunk_body(&st->lines[i + 1])) i++;
  return i;
}

static hunk_span_t hunk_span(const gc_diff_state_t *st, int h) {
  const gc_diff_line_t *head = &st->lines[st->hunk_offsets[h]];
  hunk_span_t s = {head->new_no, head->new_no, head->old_no, head->old_no};
  for (int i = st->hunk_offsets[h] + 1; i < st->line_count && is_hunk_body(&st->lines[i]); i++) {
    s.end     += st->lines[i].kind == GC_DIFF_CONTEXT || st->lines[i].kind == GC_DIFF_ADD;
    s.old_end += st->lines[i].kind == GC_DIFF_CONTEXT || st->lines[i].kind == GC_DIFF_DEL;
  }
  return s;
}

// The hidden new-side lines before hunk h; h == hunk_count is the gap after the last hunk.
static hunk_span_t gap_before(const gc_diff_state_t *st, int h) {
  hunk_span_t g = {1, st->source_count + 1, 1, 1};
  if (h > 0) { hunk_span_t p = hunk_span(st, h - 1); g.first = p.end; g.old_first = p.old_end; }
  if (h < st->hunk_count) { hunk_span_t n = hunk_span(st, h); g.end = n.first; g.old_end = n.old_first; }
  if (!st->source_count) g.end = g.first;
  return g;
}

static bool insert_context(gc_diff_state_t *st, int at, int hunk, int first, int count, int old_offset) {
  if (st->line_count + count > st->line_cap) {
    int cap = MAX(st->line_count + count, st->line_cap * 2);
    gc_diff_line_t *grown = realloc(st->lines, (size_t)cap * sizeof(*grown));
    if (!grown) { fprintf(stderr, "[gitclient] diff expansion allocation failed lines=%d\n", cap); fflush(stderr); return false; }
    st->lines = grown; st->line_cap = cap;
  }
  memmove(&st->lines[at + count], &st->lines[at], (size_t)(st->line_count - at) * sizeof(*st->lines));
  for (int k = 0; k < count; k++)
    st->lines[at + k] = (gc_diff_line_t){ .text = st->source_lines[first + k - 1], .kind = GC_DIFF_CONTEXT,
                                          .old_no = first + k + old_offset, .new_no = first + k, .hunk = hunk };
  st->line_count += count;
  for (int h = 0; h < st->hunk_count; h++) if (st->hunk_offsets[h] >= at) st->hunk_offsets[h] += count;
  st->wrap_width = 0;
  return true;
}

// Hunk h joins the one before it: its header goes, its lines move up a hunk.
static void merge_hunk(gc_diff_state_t *st, int h) {
  int at = st->hunk_offsets[h];
  memmove(&st->lines[at], &st->lines[at + 1], (size_t)(st->line_count - at - 1) * sizeof(*st->lines));
  st->line_count--;
  for (int i = at; i < st->line_count; i++) if (st->lines[i].hunk >= h) st->lines[i].hunk--;
  for (int k = h; k + 1 < st->hunk_count; k++) st->hunk_offsets[k] = st->hunk_offsets[k + 1] - 1;
  st->hunk_count--;
  if (st->current_hunk >= h) st->current_hunk = MAX(0, st->current_hunk - 1);
  st->wrap_width = 0;
}

// Show new-side lines [first, end) wherever they border a hunk.
static void reveal_lines(gc_diff_state_t *st, int first, int end) {
  for (int h = 0; h <= st->hunk_count && first < end; h++) {
    hunk_span_t g = gap_before(st, h);
    int a = MAX(first, g.first), b = MIN(end, g.end);
    if (a >= b) continue;
    if (h > 0 && a == g.first) {                 // the hunk above grows down
      if (!insert_context(st, hunk_last_line(st, h - 1) + 1, h - 1, a, b - a, g.old_first - g.first)) return;
    } else if (h < st->hunk_count && b == g.end) {   // this hunk grows up
      if (!insert_context(st, st->hunk_offsets[h] + 1, h, a, b - a, g.old_end - g.end)) return;
      gc_diff_line_t *head = &st->lines[st->hunk_offsets[h]];
      head->new_no = a; head->old_no = a + g.old_end - g.end;
    } else {
      continue;
    }
    if (h > 0 && h < st->hunk_count && hunk_span(st, h - 1).end == hunk_span(st, h).first) { merge_hunk(st, h); h--; }
  }
}

static void expand(gc_diff_state_t *st, int first, int end) {
  if (first >= end) return;
  reveal_lines(st, first, end);
  if (st->revealed_count < GC_DIFF_MAX_REVEALED)
    { st->revealed[st->revealed_count].first = first; st->revealed[st->revealed_count++].end = end; }
}

static void free_source(gc_diff_state_t *st) {
  free(st->source); free(st->source_lines);
  st->source = NULL; st->source_lines = NULL; st->source_count = 0;
}

static void set_source(window_t *win, gc_diff_state_t *st, const char *text) {
  if (st->source) {   // lines already point into it; a new source needs a new edSetText
    fprintf(stderr, "[gitclient] diff source set twice win=%u path=%s\n", win->id, st->hunk_path);
    fflush(stderr);
    return;
  }
  if (strcmp(st->source_path, st->hunk_path)) {
    snprintf(st->source_path, sizeof(st->source_path), "%s", st->hunk_path);
    st->revealed_count = 0;
  }
  if (!text || !text[0] || !st->hunk_path[0] || !st->hunk_count) return;
  size_t len = strlen(text);
  int count = 1;
  for (const char *p = text; *p; p++) count += *p == '\n';
  st->source = malloc(len + 1);
  st->source_lines = malloc((size_t)count * sizeof(char *));
  if (!st->source || !st->source_lines) {
    fprintf(stderr, "[gitclient] diff source allocation failed win=%u bytes=%zu\n", win->id, len);
    fflush(stderr);
    free_source(st);
    return;
  }
  memcpy(st->source, text, len + 1);
  for (char *p = st->source; *p; ) {
    st->source_lines[st->source_count++] = p;
    char *nl = strchr(p, '\n');
    if (!nl) break;
    *nl = '\0';
    p = nl + 1;
  }
  int digits = 1;
  for (int n = st->source_count; n >= 10; n /= 10) digits++;
  st->number_digits = MAX(st->number_digits, digits);
  for (int r = 0; r < st->revealed_count; r++) reveal_lines(st, st->revealed[r].first, st->revealed[r].end);
  st->wrap_width = 0;
}

static int gap_size(const gc_diff_state_t *st, int h) { hunk_span_t g = gap_before(st, h); return MAX(0, g.end - g.first); }

// ── Geometry ─────────────────────────────────────────────────────────────────

typedef struct {
  int cw, lh, ch;                // cell width, row height, glyph height
  int pad, check, number, marker, text_x, right;   // offsets within a column: the view, or one half
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
  m.text_x = m.check + (st->split ? 1 : 2) * m.number + m.pad + m.marker;   // split: each half numbers one side
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

// A hunk header as git would write it now that expansion may have grown the hunk.
static const char *line_text(const gc_diff_state_t *st, const gc_diff_line_t *l) {
  if (l->kind != GC_DIFF_HUNK || l->hunk < 0) return l->text;
  static char label[512];
  hunk_span_t s = hunk_span(st, l->hunk);
  const char *rest = strstr(l->text + 2, "@@");
  int oc = s.old_end - s.old_first, nc = s.end - s.first;
  snprintf(label, sizeof(label), "@@ -%d,%d +%d,%d @@%s", oc ? s.old_first : s.old_first - 1, oc,
           nc ? s.first : s.first - 1, nc, rest ? rest + 2 : "");
  return label;
}

// A line as cells, tabs expanded, with the changed range given its own background.
static int line_cells(const gc_diff_state_t *st, const gc_diff_line_t *l, text_cell_t *out, int max, uint32_t ink, uint32_t inner) {
  int tab = MAX(1, (int)ui_number("DiffView", "tab-size")), n = 0;
  const char *s = line_text(st, l);
  while (*s && n < max) {
    int at = (int)(s - l->text);   // the inner range is set only on lines shown as they are
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

static bool push_row(gc_diff_state_t *st, int rows, gc_diff_row_t row) {
  if (rows == st->row_cap) {
    int cap = MAX(256, st->row_cap * 2);
    gc_diff_row_t *grown = realloc(st->rows, (size_t)cap * sizeof(*grown));
    if (!grown) { fprintf(stderr, "[gitclient] diff row allocation failed rows=%d\n", cap); fflush(stderr); return false; }
    st->rows = grown; st->row_cap = cap;
  }
  st->rows[rows] = row;
  return true;
}

// Cut line i into slices of at most cols columns, breaking after the last space that fits.
static int wrap_line(const gc_diff_state_t *st, int i, int cols, gc_diff_cell_t *out, int max) {
  static text_cell_t cells[DIFF_MAX_CELLS];
  int n = line_cells(st, &st->lines[i], cells, DIFF_MAX_CELLS, 0, 0), start = 0, count = 0;
  do {
    int len = MIN(cols, n - start);
    if (start + len < n) {
      int brk = len;
      while (brk > 0 && cells[start + brk - 1].codepoint != ' ') brk--;
      if (brk > 0) len = brk;
    }
    if (count < max) out[count++] = (gc_diff_cell_t){i, start, len};
    start += len;
  } while (start < n);
  return count;
}

static int text_columns(const gc_diff_state_t *st, int width) {
  diff_metrics_t m = diff_metrics(st);
  return MAX(8, (width - m.text_x - m.right) / m.cw);
}

static bool spans_view(const gc_diff_line_t *l) { return l->kind == GC_DIFF_HUNK || l->kind == GC_DIFF_META || l->kind == GC_DIFF_NOTE; }

// Lay the lines out in rows for a view width pixels wide. Side by side, each run
// of removed and added lines pairs up line by line, as GitHub Desktop pairs them,
// and the shorter side leaves empty cells. A last row, line == line_count, holds
// the button that shows the lines after the last hunk. Returns the row count;
// rows are stored only when store is set.
static int layout_rows(gc_diff_state_t *st, int width, bool store) {
  static gc_diff_cell_t a[DIFF_MAX_CELLS], b[DIFF_MAX_CELLS];
  static int dels[DIFF_MAX_CELLS], adds[DIFF_MAX_CELLS];
  int full = text_columns(st, width), half = text_columns(st, width / 2), rows = 0;
  const gc_diff_cell_t none = {-1, 0, 0};
  for (int i = 0; i < st->line_count; i++) {
    const gc_diff_line_t *l = &st->lines[i];
    if (line_hidden(st, l)) continue;
    if (!st->split || spans_view(l)) {
      int k = wrap_line(st, i, full, a, DIFF_MAX_CELLS);
      for (int q = 0; q < k; q++, rows++)
        if (store && !push_row(st, rows, (gc_diff_row_t){a[q].line, a[q].start, a[q].len, none, false})) return rows;
      continue;
    }
    if (l->kind == GC_DIFF_CONTEXT) {
      int k = wrap_line(st, i, half, a, DIFF_MAX_CELLS);
      for (int q = 0; q < k; q++, rows++)
        if (store && !push_row(st, rows, (gc_diff_row_t){a[q].line, a[q].start, a[q].len, a[q], true})) return rows;
      continue;
    }
    int nd = 0, na = 0, j = i;
    for (; j < st->line_count && (st->lines[j].kind == GC_DIFF_DEL || st->lines[j].kind == GC_DIFF_ADD); j++) {
      if (st->lines[j].kind == GC_DIFF_DEL && nd < DIFF_MAX_CELLS) dels[nd++] = j;
      if (st->lines[j].kind == GC_DIFF_ADD && na < DIFF_MAX_CELLS) adds[na++] = j;
    }
    for (int p = 0; p < MAX(nd, na); p++) {
      int kl = p < nd ? wrap_line(st, dels[p], half, a, DIFF_MAX_CELLS) : 0;
      int kr = p < na ? wrap_line(st, adds[p], half, b, DIFF_MAX_CELLS) : 0;
      for (int q = 0; q < MAX(kl, kr); q++, rows++) {
        gc_diff_cell_t left = q < kl ? a[q] : none, right = q < kr ? b[q] : none;
        if (store && !push_row(st, rows, (gc_diff_row_t){left.line, left.start, left.len, right, true})) return rows;
      }
    }
    i = j - 1;
  }
  if (st->hunk_count && gap_size(st, st->hunk_count) > 0) {
    if (store && !push_row(st, rows, (gc_diff_row_t){st->line_count, 0, 0, none, false})) return rows;
    rows++;
  }
  return rows;
}

static void ensure_rows(window_t *win, gc_diff_state_t *st) {
  int width = window_scroll_content_rect(win).w;
  if (width == st->wrap_width && st->rows) return;
  st->wrap_width = width;
  st->row_count = layout_rows(st, width, true);
}

static int row_of_line(const gc_diff_state_t *st, int line) {
  for (int r = 0; r < st->row_count; r++)
    if (st->rows[r].line >= line || (st->rows[r].split && st->rows[r].right.line >= line)) return r;
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

typedef enum { HIT_NONE, HIT_LINE, HIT_HUNK, HIT_UP, HIT_DOWN, HIT_ALL, HIT_BELOW } diff_hit_t;

// What a content-space point is over, its line (line_count for the last row),
// and, side by side, its half (1 old, 2 new; 0 across the view). A hunk header's
// gutter holds its expand buttons: one when the gap above is short or the hunk
// is the first, else lines below the hunk above on its left half and lines above
// this hunk on its right half.
static diff_hit_t hit_test(window_t *win, gc_diff_state_t *st, uint32_t wparam, int *line, int *side) {
  *side = 0;
  if (!st->line_count) return HIT_NONE;
  ensure_rows(win, st);
  irect16_t content = window_scroll_content_rect(win);
  diff_metrics_t m = diff_metrics(st);
  int x = (int16_t)LOWORD(wparam) - content.x, y = (int16_t)HIWORD(wparam) - content.y, r = y / m.lh;
  if (y < 0 || r >= st->row_count) return HIT_NONE;
  const gc_diff_row_t *vr = &st->rows[r];
  *line = vr->line;
  if (vr->split) {
    *side = x >= content.w / 2 ? 2 : 1;
    if (*side == 2) { x -= content.w / 2; *line = vr->right.line; }
    if (*line < 0) return HIT_NONE;
  }
  int gutter_w = m.text_x - m.marker - m.pad / 2;
  bool gutter = x < gutter_w;
  if (*line == st->line_count) return HIT_BELOW;
  gc_diff_line_t *l = &st->lines[*line];
  if (l->kind == GC_DIFF_HUNK && l->hunk >= 0) {
    if (!gutter) return HIT_HUNK;
    int gap = gap_size(st, l->hunk);
    if (!gap) return HIT_NONE;
    if (l->hunk == 0) return HIT_UP;
    if (gap <= GC_DIFF_EXPAND_STEP) return HIT_ALL;
    return x < gutter_w / 2 ? HIT_DOWN : HIT_UP;
  }
  return gutter && l->selectable ? HIT_LINE : HIT_NONE;
}

// The line under a content-space y on the dragged half, held to the rows so a
// drag past either end keeps going.
static int line_at_y(window_t *win, gc_diff_state_t *st, int y) {
  ensure_rows(win, st);
  if (!st->row_count) return -1;
  int r = CLAMP((y - window_scroll_content_rect(win).y) / diff_metrics(st).lh, 0, st->row_count - 1);
  if (st->rows[r].line == st->line_count && r > 0) r--;   // the expand row after the last hunk
  const gc_diff_row_t *vr = &st->rows[r];
  if (!vr->split) return vr->line;
  int line = st->drag_side == 2 ? vr->right.line : vr->line;
  return line >= 0 ? line : st->drag_side == 2 ? vr->line : vr->right.line;
}

// A gutter drag sets every selectable line from the anchor to the pointer the
// way the first one went, and puts the lines it has left back, as GitHub Desktop does.
static void drag_to(gc_diff_state_t *st, int line) {
  int lo = MIN(st->drag_anchor, line), hi = MAX(st->drag_anchor, line);
  for (int i = 0; i < st->line_count; i++) {
    const gc_diff_line_t *l = &st->lines[i];
    bool on_side = !st->drag_side || l->kind == (st->drag_side == 1 ? GC_DIFF_DEL : GC_DIFF_ADD);
    if (l->selectable) st->lines[i].included = i >= lo && i <= hi && on_side ? st->drag_target : st->drag_base[i];
  }
}

static bool drag_end(window_t *win, gc_diff_state_t *st, bool keep) {
  if (!st->drag_base) return false;
  bool changed = false;
  for (int i = 0; i < st->line_count; i++) {
    if (!keep) st->lines[i].included = st->drag_base[i];
    changed |= st->lines[i].included != st->drag_base[i];
  }
  free(st->drag_base);
  st->drag_base = NULL;
  set_capture(NULL);
  invalidate_window(win);
  return changed;
}

static bool hunk_selectable(const gc_diff_state_t *st, int h) {
  for (int i = st->hunk_offsets[h] + 1; i < st->line_count && is_hunk_body(&st->lines[i]); i++)
    if (st->lines[i].selectable) return true;
  return false;
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

static void draw_expander(const char *icon, irect16_t r, uint32_t col) {
  int size = ui_px("DiffView", "--expand-size");
  draw_sysicon(icon, r.x + (r.w - size) / 2, r.y + (r.h - size) / 2, size, col);
}

enum { SIDE_BOTH, SIDE_OLD, SIDE_NEW };

// One column of a row: the whole view, or a half side by side. Its gutter shows
// the line numbers the column has room for, a check on included lines, or a hunk
// header's expand buttons; the body holds the slice of text.
static void paint_cell(gc_diff_state_t *st, const diff_metrics_t *m, irect16_t r, int line, int start, int len, int side) {
  static text_cell_t cells[DIFF_MAX_CELLS];
  uint32_t ink = ui_color("DiffView", "color"), gutter_ink = ui_color("DiffView", "--gutter-color");
  irect16_t gutter = rect_split_left(r, m->text_x - m->marker - m->pad / 2), body = rect_trim_left(r, gutter.w);
  if (line < 0) {   // the side a removal or addition has no counterpart on
    fill_rect(ui_color("DiffView", "--gutter-background"), gutter);
    fill_rect(ui_color("DiffView", "--empty-background"), body);
    return;
  }
  if (line == st->line_count) {   // the button for the lines after the last hunk
    fill_rect(ui_color("DiffView", "--hunk-background"), r);
    draw_expander("gc-arrow-line-down", gutter, gutter_ink);
    return;
  }
  const gc_diff_line_t *l = &st->lines[line];
  const char *bg_key = NULL, *gutter_key = NULL, *inner_key = NULL, *marker = NULL;
  switch (l->kind) {
    case GC_DIFF_ADD:  bg_key = "--added-background";   gutter_key = "--added-gutter";   inner_key = "--added-inner";   marker = "+"; break;
    case GC_DIFF_DEL:  bg_key = "--removed-background"; gutter_key = "--removed-gutter"; inner_key = "--removed-inner"; marker = "-"; break;
    case GC_DIFF_HUNK: bg_key = l->hunk == st->current_hunk ? "--hunk-selection" : "--hunk-background"; gutter_key = bg_key; break;
    default: break;
  }
  bool checked = l->selectable && l->included;
  if (checked) gutter_key = "--included-gutter";
  fill_rect(gutter_key ? ui_color("DiffView", gutter_key) : ui_color("DiffView", "--gutter-background"), gutter);
  if (bg_key) fill_rect(ui_color("DiffView", bg_key), body);
  int ty = r.y + (m->lh - m->ch) / 2;
  if (start == 0 && l->kind == GC_DIFF_HUNK && l->hunk >= 0) {
    int gap = gap_size(st, l->hunk);
    irect16_t down = rect_split_left(gutter, gutter.w / 2), up = rect_trim_left(gutter, down.w);
    if (gap && l->hunk == 0)                    draw_expander("gc-arrow-line-up", gutter, gutter_ink);
    else if (gap && gap <= GC_DIFF_EXPAND_STEP) draw_expander("gc-arrows-out-line-vertical", gutter, gutter_ink);
    else if (gap) { draw_expander("gc-arrow-line-down", down, gutter_ink); draw_expander("gc-arrow-line-up", up, gutter_ink); }
  } else if (start == 0) {
    uint32_t numbers = checked ? ui_color("DiffView", "--included-color") : gutter_ink;
    int check_size = ui_px("DiffView", "--check-size");
    if (checked) draw_sysicon("gc-check", r.x + m->pad / 2, r.y + (m->lh - check_size) / 2, check_size, numbers);
    int first = r.x + m->check + m->number;
    if (side == SIDE_BOTH) { draw_number(l->old_no, first, ty, numbers); draw_number(l->new_no, first + m->number, ty, numbers); }
    else draw_number(side == SIDE_OLD ? l->old_no : l->new_no, first, ty, numbers);
    if (marker) draw_text("DiffView", marker, r.x + m->text_x - m->marker, ty, ink);
  }
  uint32_t text_ink = spans_view(l) ? ui_color("DiffView", "--meta-color") : ink;
  int n = line_cells(st, l, cells, DIFF_MAX_CELLS, text_ink, inner_key ? ui_color("DiffView", inner_key) : 0);
  if (start < n) draw_text_cells("DiffView", cells + start, MIN(len, n - start), r.x + m->text_x, ty);
}

static void paint(window_t *win, gc_diff_state_t *st) {
  irect16_t client = get_client_rect(win);
  fill_rect(ui_color("DiffView", "background"), client);
  if (!st) return;
  diff_metrics_t m = diff_metrics(st);

  if (!st->line_count) {
    const char *text = st->placeholder[0] ? st->placeholder : ui_tr("No local changes");
    int w = text_strwidth("DiffView", text);
    draw_text("DiffView", text, client.x + MAX(0, (client.w - w) / 2), client.y + (client.h - m.ch) / 2,
              ui_color("DiffView", "--meta-color"));
    return;
  }

  ensure_rows(win, st);
  for (int r = MAX(0, (int)win->vscroll.pos / m.lh); r < st->row_count; r++) {
    irect16_t row = window_scroll_rect_to_client(win, row_rect(win, r));
    if (row.y >= client.y + client.h) break;
    const gc_diff_row_t *vr = &st->rows[r];
    if (!vr->split) {
      paint_cell(st, &m, row, vr->line, vr->start, vr->len, SIDE_BOTH);
      continue;
    }
    irect16_t left = rect_split_left(row, row.w / 2);
    paint_cell(st, &m, left, vr->line, vr->start, vr->len, SIDE_OLD);
    paint_cell(st, &m, rect_trim_left(row, left.w), vr->right.line, vr->right.start, vr->right.len, SIDE_NEW);
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
      if (st) { free(st->drag_base); free_source(st); free(st->lines); free(st->rows); free(st); win->userdata = NULL; }
      return false;

    case edSetText: {   // lparam: plain unified diff text; the view keeps a copy and parses it
      if (!st) return false;
      const char *text = lparam ? (const char *)lparam : "";
      if (text != st->diff_buf) snprintf(st->diff_buf, sizeof(st->diff_buf), "%s", text);
      if (st->drag_base) drag_end(win, st, false);   // the lines it remembered are gone
      free_source(st);
      parse_diff(win, st);
      scroll_info_t si = { .fMask = SIF_POS, .nPos = 0 };
      set_scroll_info(win, SB_VERT, &si, false);
      window_sync_scroll_content(win);
      invalidate_window(win);
      return true;
    }

    case dvSetSource:
      if (!st) return false;
      set_source(win, st, (const char *)lparam);
      window_sync_scroll_content(win);
      invalidate_window(win);
      return true;

    case dvSetSplit:
      if (!st) return false;
      if (st->drag_base) drag_end(win, st, false);
      st->split = wparam != 0;
      st->wrap_width = 0;
      window_sync_scroll_content(win);
      invalidate_window(win);
      return true;

    case edSetPlaceholder:
      if (!st) return false;
      snprintf(st->placeholder, sizeof(st->placeholder), "%s", lparam ? (const char *)lparam : "");
      invalidate_window(win);
      return true;

    case evMeasureScrollContent: {
      layout_measure_t *m = lparam;
      if (!st || !m) return false;
      m->desired_w = 0;
      m->desired_h = st->line_count ? layout_rows(st, m->avail_w, false) * diff_metrics(st).lh : 0;
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
      if (!st) return true;
      int line = -1, side = 0;
      diff_hit_t hit = hit_test(win, st, wparam, &line, &side);
      int h = line >= 0 && line < st->line_count ? st->lines[line].hunk : st->hunk_count;
      if (h >= 0 && h < st->hunk_count) st->current_hunk = h;
      hunk_span_t g = h >= 0 ? gap_before(st, h) : (hunk_span_t){0};
      bool toggled = false;
      switch (hit) {
        case HIT_LINE:
          free(st->drag_base);
          st->drag_base = malloc((size_t)st->line_count * sizeof(bool));
          if (!st->drag_base) { fprintf(stderr, "[gitclient] diff drag allocation failed lines=%d\n", st->line_count); fflush(stderr); break; }
          for (int i = 0; i < st->line_count; i++) st->drag_base[i] = st->lines[i].included;
          st->drag_anchor = line;
          st->drag_side = side;
          st->drag_target = !st->lines[line].included;
          drag_to(st, line);
          set_capture(win);
          break;
        case HIT_HUNK:  toggled = toggle_hunk(st, h); break;
        case HIT_UP:    expand(st, MAX(g.first, g.end - GC_DIFF_EXPAND_STEP), g.end); break;
        case HIT_DOWN:
        case HIT_BELOW: expand(st, g.first, MIN(g.end, g.first + GC_DIFF_EXPAND_STEP)); break;
        case HIT_ALL:   expand(st, g.first, g.end); break;
        case HIT_NONE:  break;
      }
      if (hit >= HIT_UP) window_sync_scroll_content(win);
      invalidate_window(win);
      if (toggled) notify_toggled(win);
      return true;
    }

    case evMouseMove:
      if (!st || !st->drag_base) return false;
      drag_to(st, line_at_y(win, st, (int16_t)HIWORD(wparam)));
      invalidate_window(win);
      return true;

    case evLeftButtonUp:
      if (!st || !st->drag_base) return false;
      if (drag_end(win, st, true)) notify_toggled(win);
      return true;

    case evPointerCancel:
      if (!st || !st->drag_base) return false;
      drag_end(win, st, false);
      return true;

    case evGetTooltipText: {
      int line = -1, side = 0;
      const char *tip = NULL;
      switch (st && lparam ? hit_test(win, st, wparam, &line, &side) : HIT_NONE) {
        case HIT_UP:    tip = ui_tr("Show lines above"); break;
        case HIT_DOWN:
        case HIT_BELOW: tip = ui_tr("Show lines below"); break;
        case HIT_ALL:   tip = ui_tr("Show the hidden lines"); break;
        case HIT_HUNK:  if (hunk_selectable(st, st->lines[line].hunk)) tip = ui_tr("Include or leave out this hunk"); break;
        default: break;
      }
      if (!tip) return false;
      snprintf((char *)lparam, 256, "%s", tip);
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
