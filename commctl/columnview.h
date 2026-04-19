#ifndef __UI_COLUMNVIEW_H__
#define __UI_COLUMNVIEW_H__

#include <stdint.h>
#include "../user/user.h"

// Layout constants (exported for controls that extend win_reportview)
#define COLUMNVIEW_ENTRY_HEIGHT 13
#define COLUMNVIEW_WIN_PADDING   4
#define REPORTVIEW_MAX_SUBITEMS  8

// ReportView messages (WinAPI-style report/list view naming).
enum {
  RVM_ADDITEM = kWindowMessageUser + 100,
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
  RVM_SETREDRAW,            // wparam = 0 suspend redraw; non-zero resume and repaint if dirty
};

enum {
  RVM_VIEW_ICON = 0,
  RVM_VIEW_REPORT = 1,
};

typedef struct {
  const char *title;
  uint32_t width;
} reportview_column_t;

typedef struct {
  const char *text;
  int icon;
  uint32_t color;
  uint32_t userdata;
  const char *subitems[REPORTVIEW_MAX_SUBITEMS];
  uint32_t subitem_count;
} reportview_item_t;

// ReportView notifications
enum {
  RVN_SELCHANGE = 200,
  RVN_DBLCLK,
  RVN_DELETE,
};

result_t win_reportview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

#endif // __UI_COLUMNVIEW_H__
