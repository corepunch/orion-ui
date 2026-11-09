#ifndef GC_DIFF_VIEW_H
#define GC_DIFF_VIEW_H

#include <orion/ui.h>

#define GC_DIFF_VIEW_CLASS_NAME "DiffView"

#define GC_DIFF_BUF_SIZE (256 * 1024)
#define GC_DIFF_MAX_HUNKS 256
#define GC_DIFF_STAGE_HUNK 1101
#define GC_DIFF_TOGGLE_UNIFIED 1102

typedef struct {
  char  **lines;
  int     line_count;
  int     scroll_y;
  char    diff_buf[GC_DIFF_BUF_SIZE];

  bool    unified_mode;
  int     hunk_offsets[GC_DIFF_MAX_HUNKS];
  int     hunk_count;
  int     current_hunk;
  int     current_hunk_staged;
  char    hunk_path[512];
} gc_diff_state_t;

result_t gc_diff_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

#endif
