#ifndef __UI_COLUMNVIEW_INTERNAL_H__
#define __UI_COLUMNVIEW_INTERNAL_H__

#include "columnview.h"
#include <orion/user/theme.h>
#include <orion/user/text.h>

#define MAX_COLUMNVIEW_ITEM_NAME 256
#define MAX_COLUMNVIEW_ITEMS MAX_LIST_ITEMS
#define MAX_REPORTVIEW_TITLE 64
#define DEFAULT_COLUMN_WIDTH 160
#define DEFAULT_ICON_SIZE     32
#define DEFAULT_ICON_TEXT_GAP  0
#define ICON_OFFSET 16
#define WIN_PADDING 4
#define RV_INVALID_SELECTION (-1)

// Large icon view dimensions:
#define RV_LARGE_ICON_PAD       LARGE_ICON_PAD
#define RV_LARGE_ICON_TOP_PAD   LARGE_ICON_TOP_PAD
#define RV_LARGE_ICON_LABEL_GAP LARGE_ICON_LABEL_GAP
#define RV_LARGE_ICON_BOT_PAD   LARGE_ICON_BOT_PAD
#define ENTRY_HEIGHT  COLUMNVIEW_ENTRY_HEIGHT
#define TWO_LINE_ENTRY_HEIGHT REPORTVIEW_TWO_LINE_ENTRY_HEIGHT
#define HEADER_HEIGHT COLUMNVIEW_HEADER_HEIGHT

typedef struct {
  reportview_item_t items[MAX_COLUMNVIEW_ITEMS];
  char names[MAX_COLUMNVIEW_ITEMS][MAX_COLUMNVIEW_ITEM_NAME];
  char subnames[MAX_COLUMNVIEW_ITEMS][REPORTVIEW_MAX_SUBITEMS][MAX_COLUMNVIEW_ITEM_NAME];
  char sections[MAX_COLUMNVIEW_ITEMS][MAX_REPORTVIEW_TITLE];  // group captions above rows
  int  row_tops[MAX_COLUMNVIEW_ITEMS + 1];                    // report rows' tops once captions take room
  bool has_sections;

  struct {
    char title[MAX_REPORTVIEW_TITLE];
    uint32_t width_spec;  // Original width specification: 0 = flex, >0 = fixed pixels
    uint32_t width;       // Current effective width (calculated for flex, same as spec for fixed)
    uint32_t min_width;   // Per-column minimum width, 0 = default (20px)
    uint32_t align;       // 0 = left, TEXT_ALIGN_RIGHT, TEXT_ALIGN_CENTER
    int color;            // sys colour role, 0 = the row's colour
  } columns[MAX_REPORTVIEW_COLUMNS];

  char footer[MAX_REPORTVIEW_COLUMNS][MAX_REPORTVIEW_TITLE]; // totals row pinned under the rows
  bool footer_visible;

  uint32_t count;
  int selected;
  int column_width;

  uint32_t column_count;
  int icon_size;
  int icon_text_gap;
  int fixed_large_icon_cols;
  bool redraw_enabled;
  bool redraw_dirty;
  bool column_titles_visible;
  bool preserve_icon_colors;
  reportview_cell_style_t cell_style;
  uint32_t extended_style;
  bitmap_strip_t *icon_strip;
  // Column resize state (mouse-driven, like splitter)
  int  resize_col;       // column being resized (-1 = none)
  int  resize_start_x;   // mouse client-x when drag started
  int  resize_start_w;   // column width when drag started
  int  resize_hot_col;   // column whose edge has hover (-1 = none)
  int  press_row;        // RVS_EX_ONECLICK: the row the button went down on (-1 = none)
} reportview_data_t;

#define REPORTVIEW_RESIZE_HOT_ZONE 4

// When defined, column dividers span the full client height and can be grabbed
// from anywhere (like splitter bars), not just the header.  Comment out or
// set to 0 for header-only resize handles.
#ifndef REPORTVIEW_RESIZE_FULL_HEIGHT
#define REPORTVIEW_RESIZE_FULL_HEIGHT 1
#endif

void rv_invalidate(window_t *win, reportview_data_t *data);
bool rv_valid_index(const reportview_data_t *data, int index);
void rv_notify(window_t *win, reportview_data_t *data, int index, uint16_t code);
bool rv_store_item(reportview_data_t *data, uint32_t i, const reportview_item_t *item);
void rv_rebind_item_refs(reportview_data_t *data, uint32_t start);
void rv_reset_view_state(window_t *win, reportview_data_t *data);
int rv_content_width(window_t *win);
int rv_report_header_height(const reportview_data_t *data);
int rv_report_footer_height(const reportview_data_t *data);
int rv_get_report_column_width(reportview_data_t *data, int col, int avail_w);
int rv_report_total_width(reportview_data_t *data, int avail_w);
int rv_large_icon_cell_h(const reportview_data_t *data);
void rv_draw_item_icon(bitmap_strip_t *strip, int icon_id,
                       irect16_t const *icon_rect, uint32_t col);

// Rows are at least as tall as the theme asks (theme_t.report.row_height); a
// two-line row adds a secondary line under it.
static inline int rv_entry_height(const reportview_data_t *data) {
  int themed = get_theme()->report.row_height, row = MAX(ENTRY_HEIGHT, themed);
  if (data && data->cell_style != REPORTVIEW_CELL_COLUMNS)
    return themed > 0 ? MAX(TWO_LINE_ENTRY_HEIGHT, row + text_char_height(FONT_SMALLEST) + 2) : TWO_LINE_ENTRY_HEIGHT;
  return row;
}

#endif
