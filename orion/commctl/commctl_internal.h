#ifndef __UI_COMMCTL_INTERNAL_H__
#define __UI_COMMCTL_INTERNAL_H__

#include "commctl.h"
#include <orion/user/theme.h>
#include <orion/user/text.h>
#include <orion/user/draw.h>
#include <orion/user/messages.h>
#include <orion/user/rect.h>
#include <string.h>

// ---------------------------------------------------------------------------
// columnview_internal.h
// ---------------------------------------------------------------------------

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
    bool badge;           // cells are status badges toned by the row
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
  int view_mode;          // win_icongrid: RVM_VIEW_LARGE_ICON or RVM_VIEW_THUMBNAIL
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
  const window_t *owner; // the control's window: its classes and #id style it (ui_selector)
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

static inline int rv_entry_height(const reportview_data_t *data) {
  const window_t *win = data ? data->owner : NULL;
  int themed = ui_px(ui_selector(win, "TableView::row"), "min-height"), row = MAX(ENTRY_HEIGHT, themed);
  if (data && data->cell_style != REPORTVIEW_CELL_COLUMNS)
    return themed > 0 ? MAX(TWO_LINE_ENTRY_HEIGHT, row + text_char_height(ui_selector(win, "TableView::detail")) + 2) : TWO_LINE_ENTRY_HEIGHT;
  return row;
}

// ---------------------------------------------------------------------------
// layout_shared.h
// ---------------------------------------------------------------------------

static inline int layout_apply_alignment(int avail, int desired, uint8_t align) {
  if (avail < 0) avail = 0;
  if (desired < 0) desired = 0;
  if (align == LAYOUT_ALIGN_STRETCH) return avail;
  if (desired > avail) desired = avail;
  return desired;
}

static inline bool layout_is_horizontal(const window_t *win) {
  return win && (win->flags & WINDOW_STACK_HORIZONTAL) != 0;
}

static inline bool layout_child_is_flex(const window_t *child) {
  return child && (child->flags & WINDOW_FLEXSPACE) != 0;
}

static inline bool layout_child_is_collapsed(const window_t *child) {
  // A hidden ancestor must not change its subtree's intrinsic layout.
  return child && (!(child->flags & WINDOW_STATE_VISIBLE) ||
    ((child->flags & LABEL_COLLAPSE_EMPTY) && !child->title[0]));
}

static inline int layout_child_count(const window_t *win) {
  int count = 0;
  for (window_t *child = win ? win->children : NULL; child; child = child->next)
    if (!layout_child_is_collapsed(child)) count++;
  return count;
}

static inline window_t *layout_child_at(window_t *win, int index) {
  int i = 0;
  for (window_t *child = win ? win->children : NULL; child; child = child->next) {
    if (layout_child_is_collapsed(child)) continue;
    if (i == index) return child;
    i++;
  }
  return NULL;
}

static inline int layout_spacing_for(window_t *win) {
  return win ? (int)win->layout.layout_spacing : 0;
}

static inline irect16_t layout_padding_for(window_t *win) {
  return win ? win->layout.layout_padding : (irect16_t){0, 0, 0, 0};
}

static inline irect16_t layout_margin_for(window_t *win) {
  return win ? win->layout.layout_margin : (irect16_t){0, 0, 0, 0};
}

static inline irect16_t layout_inset_rect(irect16_t r, irect16_t inset) {
  return rect_inset_edges(r, inset);
}

static inline irect16_t layout_content_rect(window_t *win, irect16_t r) {
  return layout_inset_rect(r, layout_padding_for(win));
}

static inline layout_measure_t layout_measure_child(window_t *child, int avail_w, int avail_h) {
  irect16_t margin = child ? child->layout.layout_margin : (irect16_t){0, 0, 0, 0};
  avail_w -= margin.x + margin.w;
  avail_h -= margin.y + margin.h;
  if (avail_w < 0) avail_w = 0;
  if (avail_h < 0) avail_h = 0;
  layout_measure_t m = {
    .avail_w = avail_w,
    .avail_h = avail_h,
    .desired_w = child && child->layout.layout_fixed_w > 0 ? child->layout.layout_fixed_w : 1,
    .desired_h = child && child->layout.layout_fixed_h > 0 ? child->layout.layout_fixed_h : 1,
  };
  if (child) {
    send_message(child, evMeasure, 0, &m);
    m.desired_w += margin.x + margin.w;
    m.desired_h += margin.y + margin.h;
    if (m.desired_w < 1) m.desired_w = 1;
    if (m.desired_h < 1) m.desired_h = 1;
  }
  return m;
}

// Docked panes are placed by their dock host (dock_layout), never by the host's auto-layout.
static inline void layout_arrange_child(window_t *child, irect16_t rect) {
  if (child && child->dock) return;
  rect = layout_inset_rect(rect, layout_margin_for(child));
  layout_arrange_t a = {
    .rect = rect,
    .h_align = child ? child->layout.h_align : LAYOUT_ALIGN_STRETCH,
    .v_align = child ? child->layout.v_align : LAYOUT_ALIGN_STRETCH,
  };
  send_message(child, evArrange, 0, &a);
}

static inline int layout_horizontal_gap_total(window_t *first, int gap) {
  int count = 0;
  for (window_t *child = first; child; child = child->next)
    if (!layout_child_is_collapsed(child)) count++;
  return count > 1 ? gap * (count - 1) : 0;
}

static inline void layout_paint_children(window_t *win) {
  if (!win) return;
  for (window_t *child = win->children; child; child = child->next) {
    if (window_has_state(child, WINDOW_STATE_VISIBLE) && !window_is_lifted(child))
      send_message(child, evPaint, 0, NULL);
  }
}

// An empty container with a declared width or height is a sized box, like an
// empty div with width and height: that size is definite, and stretch does
// not apply to it. A container with children measures its content.
static inline bool layout_definite_w(const window_t *win) {
  return win && !win->children && (win->flags & WINDOW_LAYOUT_CONTAINER) && win->layout.layout_fixed_w > 0;
}
static inline bool layout_definite_h(const window_t *win) {
  return win && !win->children && (win->flags & WINDOW_LAYOUT_CONTAINER) && win->layout.layout_fixed_h > 0;
}
static inline void layout_measure_empty(window_t *win, layout_measure_t *m) {
  if (!m) return;
  if (layout_definite_w(win)) m->desired_w = win->layout.layout_fixed_w;
  if (layout_definite_h(win)) m->desired_h = win->layout.layout_fixed_h;
}

// Spacing, padding and direction from the style sheet (layout_style.c). A StackView's gap
// runs along its main axis; a GridView's and FlowView's is one value, `row-gap`
// (the first of `gap: 6px 10px`).
typedef enum { LAYOUT_GAP_MAIN, LAYOUT_GAP_ROW } layout_gap_axis_t;
bool layout_style_resolve(window_t *win, const char *element, layout_gap_axis_t gap_axis);
void layout_style_refresh(window_t *win, const char *element, layout_gap_axis_t gap_axis);

// Its box's shadow and outline paint beyond its frame.
static inline void layout_update_ink(window_t *win, theme_part_t part) {
  win->ink_overflow = theme_ink_overflow_in(win, part, CTRL_NORMAL);
}

// A container is a div: its own box (background, border, radius, shadow) from
// its element, classes and #id, under its children.
static inline void layout_paint(window_t *win, theme_part_t part) {
  theme_draw_in(win, part, get_client_rect(win), CTRL_NORMAL);
  layout_paint_children(win);
}

// ---------------------------------------------------------------------------
// popup_item.h
// ---------------------------------------------------------------------------

// Popup rows grow at half the rate of regular controls above desktop size.
#define POPUP_ITEM_HEIGHT (25 + (CONTROL_HEIGHT_REGULAR - 25 + 1) / 2)

static inline void popup_item_paint(irect16_t row, const char *text, ctrl_state_t state, ui_font_t font) {
  theme_draw(THEME_PART_MENU_ITEM, rect_inset_xy(row, 1, 0), state);
  irect16_t label = rect_inset_xy(row, ui_px("Menu::item", "padding-left"), 0);
  char buf[256];
  const char *tab = text ? strchr(text, '\t') : NULL;
  if (tab) {
    size_t n = MIN((size_t)(tab - text), sizeof(buf) - 1);
    memcpy(buf, text, n);
    buf[n] = 0;
    text = buf;
  }
  if (text) draw_text_ellipsized(font, text, label.x, label.y + (label.h - text_char_height(font)) / 2,
                                MAX(0, label.w), theme_foreground(THEME_PART_MENU_ITEM, state));
}

#endif
