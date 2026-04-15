#ifndef __UI_COLUMNVIEW_H__
#define __UI_COLUMNVIEW_H__

#include <stdint.h>
#include "../user/user.h"

// Layout constants (exported for controls that extend win_columnview)
#define COLUMNVIEW_ENTRY_HEIGHT 13
#define COLUMNVIEW_WIN_PADDING   4

// ColumnView messages
enum {
  CVM_ADDITEM = kWindowMessageUser + 100,
  CVM_DELETEITEM,
  CVM_GETITEMCOUNT,
  CVM_GETSELECTION,
  CVM_SETSELECTION,
  CVM_CLEAR,
  CVM_SETCOLUMNWIDTH,
  CVM_GETCOLUMNWIDTH,
  CVM_GETITEMDATA,
  CVM_SETITEMDATA,
};

// ColumnView notification messages
enum {
  CVN_SELCHANGE = 200,
  CVN_DBLCLK,
  CVN_DELETE,
};

// ColumnView item structure
typedef struct {
  const char *text;
  int icon;
  uint32_t color;
  uint32_t userdata;
} columnview_item_t;

result_t win_columnview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

#endif // __UI_COLUMNVIEW_H__
