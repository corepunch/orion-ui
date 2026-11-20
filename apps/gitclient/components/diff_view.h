#ifndef GC_DIFF_VIEW_H
#define GC_DIFF_VIEW_H

#include <orion/ui.h>

#define GC_DIFF_VIEW_CLASS_NAME "DiffView"

#define GC_DIFF_BUF_SIZE (256 * 1024)
#define GC_DIFF_MAX_HUNKS 256
#define GC_DIFF_LINES_TOGGLED 1101   // evCommand code: the user changed which lines are included

// A unified diff as the view reads it: edSetText parses plain `git diff` output
// (no colour) into lines, the way GitHub Desktop's diff parser does.
typedef enum {
  GC_DIFF_META,      // file header: diff --git, index, ---, +++, mode, rename, Binary files
  GC_DIFF_HUNK,      // @@ -a,b +c,d @@ section
  GC_DIFF_CONTEXT,
  GC_DIFF_ADD,
  GC_DIFF_DEL,
  GC_DIFF_NOTE,      // \ No newline at end of file
} gc_diff_kind_t;

typedef struct {
  char    *text;          // without the +/-/space prefix; hunk and meta lines whole
  uint8_t  kind;          // gc_diff_kind_t
  bool     selectable;    // the owner set it: the line can be toggled in or out of the commit
  bool     included;      // the line is in the index (shown with a check in the gutter)
  int      old_no, new_no; // 0 where the side has no line
  int      hunk;          // index into hunk_offsets, -1 before the first hunk of a file
  int      inner_start, inner_len;   // bytes of text that changed against its paired line
} gc_diff_line_t;

typedef struct {   // one painted row: a slice of a line, in columns
  int line, start, len;
} gc_diff_row_t;

typedef struct gc_diff_state_s {
  gc_diff_line_t *lines;
  int     line_count;
  char    diff_buf[GC_DIFF_BUF_SIZE];

  gc_diff_row_t *rows;   // lines wrapped to wrap_cols text columns
  int     row_count, row_cap, wrap_cols;
  int     number_digits;

  int     hunk_offsets[GC_DIFF_MAX_HUNKS];   // line index of each @@ line
  int     hunk_count;
  int     current_hunk;
  char    hunk_path[512];    // the file the diff shows, when it shows one
  char    placeholder[160];  // shown while there is no diff; edSetPlaceholder
} gc_diff_state_t;

result_t gc_diff_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

#endif
