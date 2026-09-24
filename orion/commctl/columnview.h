#ifndef __UI_COLUMNVIEW_H__
#define __UI_COLUMNVIEW_H__

#include <stdint.h>
#include <orion/user/user.h>

// Layout constants shared by the columnview-style controls.
// FONT_SIZE       — chrome font, from kernel/kernel.h.
// FONT_SIZE_SMALL — content font, from kernel/kernel.h.
#define COLUMNVIEW_ENTRY_HEIGHT  (FONT_SIZE_SMALL + 5)  // data rows use FONT_SMALL
#define REPORTVIEW_TWO_LINE_ENTRY_HEIGHT (FONT_SIZE_SMALL * 2 + 5)
#define COLUMNVIEW_HEADER_HEIGHT (FONT_SIZE + 6)        // header uses FONT_SYSTEM
#define COLUMNVIEW_WIN_PADDING   4
#define REPORTVIEW_MAX_SUBITEMS  8

// ReportView messages (WinAPI-style report/list view naming).
enum {
  RVM_ADDITEM = evUser + 100,
  RVM_DELETEITEM,
  RVM_GETITEMCOUNT,
  RVM_GETSELECTION,
  RVM_SETSELECTION,
  RVM_CLEAR,
  RVM_SETCOLUMNWIDTH,
  RVM_GETCOLUMNWIDTH,
  RVM_GETITEMDATA,
  RVM_SETITEMDATA,
  RVM_SETVIEWMODE,
  RVM_ADDCOLUMN,
  RVM_CLEARCOLUMNS,
  RVM_GETCOLUMNCOUNT,
  RVM_SETREPORTCOLUMNWIDTH, // wparam = col_index; lparam = (void*)(uintptr_t)new_width (0 = auto)
  RVM_GETREPORTCOLUMNWIDTH, // wparam = col_index; returns current report column width
  RVM_SETREDRAW,            // wparam = 0 suspend redraw; non-zero resume and repaint if dirty
  RVM_SETICONSTRIP,         // lparam = bitmap_strip_t* (NULL to clear); strip owned by caller
  RVM_SETICONSIZE,          // wparam = icon pixel size for RVM_VIEW_LARGE_ICON mode
  RVM_SETLARGEICONCOLS,     // wparam = fixed column count for RVM_VIEW_LARGE_ICON (0 = auto)
  RVM_SETCOLUMNTITLESVISIBLE, // wparam = 0 hide report headers; non-zero show
  RVM_GETCOLUMNTITLESVISIBLE,
  RVM_SETPRESERVEICONCOLORS, // wparam = 0 tint icons with row color; non-zero draw strip colors
  RVM_SETICONTEXTGAP,      // wparam = pixels between icon slot and item text in icon-list mode
  RVM_HITTEST,             // wparam = MAKEDWORD(client_x, client_y); returns item index or -1
  RVM_SETEXTENDEDSTYLE,    // wparam = style mask (0 = all); lparam = new RVS_EX_* bits
  RVM_GETEXTENDEDSTYLE,
  RVM_SETITEMSTATE,        // wparam = item index (-1 = all); lparam = reportview_item_state_t*
  RVM_GETITEMSTATE,        // wparam = item index; lparam = state mask
  RVM_SETCELLSTYLE,        // wparam = reportview_cell_style_t
  RVM_GETCELLSTYLE,
};

// WinAPI list-view compatible state-image model. State image indices are
// one-based: 1 = unchecked, 2 = checked when RVS_EX_CHECKBOXES is enabled.
#define RVS_EX_CHECKBOXES          0x00000004u
#define RVIS_STATEIMAGEMASK        0x0000f000u
#define RV_INDEXTOSTATEIMAGEMASK(i) ((uint32_t)(i) << 12)
#define RV_STATEIMAGEINDEX(state)   (((uint32_t)(state) & RVIS_STATEIMAGEMASK) >> 12)

enum {
  RVM_VIEW_ICON = 0,        // Small-icon list (icon-left, label-right, one row per item)
  RVM_VIEW_REPORT = 1,      // Multi-column report with sortable header
  RVM_VIEW_LARGE_ICON = 2,  // Large-thumbnail grid (icon-above, label-below) — WinAPI LVS_ICON
};

typedef enum {
  REPORTVIEW_CELL_COLUMNS = 0, // One line per item, rendered as report columns.
  REPORTVIEW_CELL_TWO_LINE = 1, // First column is title; second is subtitle.
} reportview_cell_style_t;
#define TABLEVIEW_CELL_COLUMNS  REPORTVIEW_CELL_COLUMNS
#define TABLEVIEW_CELL_TWO_LINE REPORTVIEW_CELL_TWO_LINE

typedef struct {
  const char *title;
  uint32_t width;
  uint32_t min_width;  // 0 = use default (20px), >0 = per-column minimum
} reportview_column_t;

typedef struct {
  const char *text;
  int         icon;       // strip tile index; icon_name overrides it outside icon grid
  const char *icon_name;  // SVG icon for report/icon lists; icon grid always uses strip index
  uint32_t    color;
  uint32_t    userdata;
  uint32_t    state;
  const char *subitems[REPORTVIEW_MAX_SUBITEMS];
  uint32_t    subitem_count;
} reportview_item_t;

typedef struct {
  uint32_t state;
  uint32_t state_mask;
} reportview_item_state_t;

#define ReportView_SetCheckState(win, item, checked) do {                    \
  reportview_item_state_t _state = {                                         \
    RV_INDEXTOSTATEIMAGEMASK((checked) ? 2 : 1), RVIS_STATEIMAGEMASK         \
  };                                                                          \
  send_message((win), RVM_SETITEMSTATE, (uint32_t)(item), &_state);           \
} while (0)
#define ReportView_GetCheckState(win, item)                                  \
  (RV_STATEIMAGEINDEX(send_message((win), RVM_GETITEMSTATE,                  \
                                    (uint32_t)(item),                         \
                                    (void *)(uintptr_t)RVIS_STATEIMAGEMASK)) == 2)

// ReportView notifications
enum {
  RVN_SELCHANGE = 200,
  RVN_DBLCLK,
  RVN_DELETE,
  RVN_ITEMCHECK,
};

result_t win_reportview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_iconview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_icongrid(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

#endif // __UI_COLUMNVIEW_H__
