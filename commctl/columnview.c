#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "columnview.h"
#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"

#define MAX_COLUMNVIEW_ITEM_NAME 256
#define MAX_COLUMNVIEW_ITEMS 256
#define ENTRY_HEIGHT 13
#define DEFAULT_COLUMN_WIDTH 160
#define ICON_OFFSET 12
#define ICON_DODGE 1
#define WIN_PADDING 4

// ColumnView data structure
typedef struct {
  columnview_item_t items[MAX_COLUMNVIEW_ITEMS];
  char names[MAX_COLUMNVIEW_ITEMS][MAX_COLUMNVIEW_ITEM_NAME];
  uint32_t count;
  uint32_t selected;
  uint32_t column_width;
  uint32_t last_click_time;
  uint32_t last_click_index;
} columnview_data_t;

// Calculate number of columns that fit in window
static inline int get_column_count(int window_width, int column_width) {
  if (window_width <= 0 || column_width <= 0) {
    return 1;
  }
  int ncol = window_width / column_width;
  return (ncol > 0) ? ncol : 1;
}

// Effective content width: subtract the vscroll strip when the bar is visible.
static inline int cv_content_width(window_t *win) {
  return win->frame.w - (win->vscroll.visible ? SCROLLBAR_WIDTH : 0);
}

// Convert packed wparam coordinates to a columnview item index.
// Returns -1 when the position falls outside the item grid.
static int cv_hit_index(window_t *win, columnview_data_t *data, uint32_t wparam) {
  int mx = (int)(int16_t)LOWORD(wparam);
  int my = (int)(int16_t)HIWORD(wparam);
  int eff_w = cv_content_width(win);
  const int ncol = get_column_count(eff_w, data->column_width);
  int col = mx / data->column_width;
  int row = (my - WIN_PADDING + (int)win->scroll[1]) / ENTRY_HEIGHT;
  int index = row * ncol + col;
  return (index >= 0 && index < (int)data->count) ? index : -1;
}

// Ensure the item at the given index is visible, scrolling if necessary.
static void cv_scroll_to_item(window_t *win, columnview_data_t *data, int index) {
  if (!win || !data || index < 0 || index >= (int)data->count) return;
  int eff_w = cv_content_width(win);
  int ncol  = get_column_count(eff_w, (int)data->column_width);
  int row   = index / ncol;
  int item_y_top    = row * ENTRY_HEIGHT + WIN_PADDING;
  int item_y_bottom = item_y_top + ENTRY_HEIGHT;
  int scroll_y      = (int)win->scroll[1];
  int visible_h     = win->frame.h;
  if (item_y_top - scroll_y < 0)
    win->scroll[1] = (uint32_t)(item_y_top > 0 ? item_y_top : 0);
  else if (item_y_bottom - scroll_y > visible_h)
    win->scroll[1] = (uint32_t)(item_y_bottom - visible_h);
}

// Update the built-in vertical scrollbar to reflect current content and scroll position.
// All values are in pixels so the thumb can be dragged to any sub-row offset,
// giving smooth per-pixel scrolling identical to the image-editor canvas.
static void cv_sync_scroll(window_t *win, columnview_data_t *data) {
  if (!win || !data || win->frame.h <= 0) return;
  int eff_w     = cv_content_width(win);
  int ncol      = get_column_count(eff_w, (int)data->column_width);
  int total_rows = (data->count == 0) ? 0
                 : (int)((data->count + (unsigned)ncol - 1) / (unsigned)ncol);
  int total_h   = total_rows * ENTRY_HEIGHT;
  int max_scroll_px = total_h - win->frame.h;
  if (max_scroll_px < 0) max_scroll_px = 0;
  if ((int)win->scroll[1] > max_scroll_px) win->scroll[1] = (uint32_t)max_scroll_px;

  scroll_info_t si;
  si.fMask = SIF_ALL;
  si.nMin  = 0;
  si.nMax  = total_h;
  si.nPage = (uint32_t)win->frame.h;
  si.nPos  = (int)win->scroll[1];
  set_scroll_info(win, SB_VERT, &si, false);
}

// ColumnView control window procedure
result_t win_columnview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  columnview_data_t *data = (columnview_data_t *)win->userdata2;
  
  switch (msg) {
    case kWindowMessageCreate: {
      data = malloc(sizeof(columnview_data_t));
      if (!data) return false;
      win->userdata2 = data;
      win->flags |= WINDOW_VSCROLL;
      // alloc_window() only initializes visible_mode to SB_VIS_AUTO when
      // WINDOW_VSCROLL is present at creation time.  We add the flag here
      // (post-creation), so we must initialize it explicitly.
      win->vscroll.visible_mode = SB_VIS_AUTO;
      data->count = 0;
      data->selected = -1;
      data->column_width = DEFAULT_COLUMN_WIDTH;
      data->last_click_time = 0;
      data->last_click_index = -1;
      cv_sync_scroll(win, data);
      return true;
    }
    
    case kWindowMessagePaint: {
      int eff_w   = cv_content_width(win);
      const int ncol = get_column_count(eff_w, data->column_width);
      int scroll_y = (int)win->scroll[1];
      // For child windows frame.y/frame.h are in root-content-relative (drawing)
      // coordinates, so they directly bound the visible area.  For root windows
      // the drawing coordinate system starts at 0 (the content-area origin), so
      // the visible area is [0, frame.h) regardless of the screen position stored
      // in frame.y.
      int clip_top    = win->parent ? win->frame.y              : 0;
      int clip_bottom = win->parent ? win->frame.y + win->frame.h : win->frame.h;

      for (uint32_t i = 0; i < data->count; i++) {
        int col = i % ncol;
        int x = col * data->column_width + WIN_PADDING;
        int y = (i / ncol) * ENTRY_HEIGHT + WIN_PADDING - scroll_y;

        if (y + ENTRY_HEIGHT <= clip_top) continue;
        // y = (i/ncol)*ENTRY_HEIGHT + ... is monotonically non-decreasing as i
        // increases (same row → same y, next row → strictly higher y), so once
        // an item is below the visible area all subsequent items are too.
        if (y >= clip_bottom) break;

        if (i == data->selected) {
          fill_rect(get_sys_color(kColorTextNormal), x - 2, y - 2, data->column_width - 6, ENTRY_HEIGHT - 2);
          draw_icon8(data->items[i].icon, x, y - ICON_DODGE, get_sys_color(kColorWindowBg));
          draw_text_small(data->names[i], x + ICON_OFFSET, y, get_sys_color(kColorWindowBg));
        } else {
          fill_rect(get_sys_color(kColorWindowBg), x - 2, y - 2, data->column_width - 6, ENTRY_HEIGHT - 2);
          draw_icon8(data->items[i].icon, x, y - ICON_DODGE, data->items[i].color);
          draw_text_small(data->names[i], x + ICON_OFFSET, y, data->items[i].color);
        }
      }

      return false;
    }
    
    case kWindowMessageLeftButtonDown: {
      int index = cv_hit_index(win, data, wparam);
      if (index >= 0) {
        uint32_t now = axGetMilliseconds();

        // Check for double-click
        if (data->last_click_index == (uint32_t)index &&
            (now - data->last_click_time) < 500) {
          // Send double-click notification
          send_message(get_root_window(win), kWindowMessageCommand,
                       MAKEDWORD(index, CVN_DBLCLK), &data->items[index]);
          data->last_click_time  = 0;
          data->last_click_index = -1;
        } else {
          // Single click - update selection
          uint32_t old_selection = data->selected;
          data->selected         = (uint32_t)index;
          data->last_click_time  = now;
          data->last_click_index = (uint32_t)index;

          // Send selection change notification if changed
          if (old_selection != data->selected) {
            send_message(get_root_window(win), kWindowMessageCommand,
                         MAKEDWORD(index, CVN_SELCHANGE), &data->items[index]);
          }

          invalidate_window(win);
        }
      }
      return true;
    }

    case kWindowMessageLeftButtonDoubleClick: {
      int index = cv_hit_index(win, data, wparam);
      if (index >= 0) {
        // Reset timing state so a subsequent single click starts fresh.
        data->last_click_time  = 0;
        data->last_click_index = -1;
        send_message(get_root_window(win), kWindowMessageCommand,
                     MAKEDWORD(index, CVN_DBLCLK), &data->items[index]);
      }
      return true;
    }

    case CVM_ADDITEM: {
      columnview_item_t *item = (columnview_item_t *)lparam;
      if (data->count < MAX_COLUMNVIEW_ITEMS && item) {
        char *name = data->names[data->count];
        strncpy(name, item->text, MAX_COLUMNVIEW_ITEM_NAME - 1);
        name[MAX_COLUMNVIEW_ITEM_NAME - 1] = '\0';
        data->items[data->count] = (columnview_item_t){
          .text = name,
          .icon = item->icon,
          .color = item->color,
          .userdata = item->userdata,
        };
        data->count++;
        cv_sync_scroll(win, data);
        invalidate_window(win);
        return data->count - 1; // Return index of added item
      }
      return -1;
    }
    
    case CVM_DELETEITEM: {
      if (wparam < data->count) {
        // Shift items down
        memmove(data->items + wparam, data->items + wparam + 1, (data->count - wparam - 1) * sizeof(data->items[0]));
        memmove(data->names + wparam, data->names + wparam + 1, (data->count - wparam - 1) * sizeof(data->names[0]));
        data->count--;

        // Update text pointers: after memmove they still point to old (now-wrong)
        // positions in the names array, so fix each shifted item.
        for (uint32_t i = wparam; i < data->count; i++) {
          data->items[i].text = data->names[i];
        }

        // Adjust selection
        if (data->selected == wparam) {
          data->selected = -1;
        } else if (data->selected > wparam) {
          data->selected--;
        }

        cv_sync_scroll(win, data);
        invalidate_window(win);
        return true;
      }
      return false;
    }
    
    case CVM_GETITEMCOUNT:
      return data->count;
    
    case CVM_GETSELECTION:
      return data->selected;
    
    case CVM_SETSELECTION: {
      if (wparam < data->count) {
        data->selected = wparam;
        invalidate_window(win);
        return true;
      }
      return false;
    }
    
    case CVM_CLEAR:
      data->count = 0;
      data->selected = -1;
      data->last_click_time = 0;
      data->last_click_index = -1;
      win->scroll[1] = 0;
      cv_sync_scroll(win, data);
      invalidate_window(win);
      return true;
    
    case CVM_SETCOLUMNWIDTH: {
      if (wparam > 0) {
        data->column_width = wparam;
        cv_sync_scroll(win, data);
        invalidate_window(win);
        return true;
      }
      return false;
    }
    
    case CVM_GETCOLUMNWIDTH:
      return data->column_width;
    
    case CVM_GETITEMDATA: {
      if (wparam < data->count) {
        columnview_item_t *dest = (columnview_item_t *)lparam;
        if (dest) {
          *dest = data->items[wparam];
          return true;
        }
      }
      return false;
    }
    
    case CVM_SETITEMDATA: {
      columnview_item_t *item = (columnview_item_t *)lparam;
      if (wparam >= 0 && wparam < data->count && item) {
        char *name = data->names[wparam];
        strncpy(name, item->text, MAX_COLUMNVIEW_ITEM_NAME - 1);
        name[MAX_COLUMNVIEW_ITEM_NAME - 1] = '\0';
        data->items[wparam] = (columnview_item_t) {
          .text = name,
          .icon = item->icon,
          .color = item->color,
          .userdata = item->userdata,
        };
        invalidate_window(win);
        return true;
      }
      return false;
    }
    
    case kWindowMessageVScroll: {
      // wparam is the new pixel scroll position (pixel-based scrollbar).
      int eff_w = cv_content_width(win);
      int ncol  = get_column_count(eff_w, (int)data->column_width);
      int total_rows = (data->count == 0) ? 0
                     : (int)((data->count + (unsigned)ncol - 1) / (unsigned)ncol);
      int max_scroll_px = total_rows * ENTRY_HEIGHT - win->frame.h;
      if (max_scroll_px < 0) max_scroll_px = 0;
      int new_scroll = (int)wparam;
      if (new_scroll > max_scroll_px) new_scroll = max_scroll_px;
      win->scroll[1] = (uint32_t)new_scroll;
      cv_sync_scroll(win, data);
      invalidate_window(win);
      return true;
    }

    case kWindowMessageResize:
      cv_sync_scroll(win, data);
      return false;

    case kWindowMessageWheel: {
      if (!data) return false;
      // dy is already scaled by SCROLL_SENSITIVITY in the kernel event loop.
      // Use it directly as a pixel offset for smooth per-pixel scrolling,
      // matching the behaviour of the image-editor canvas.
      int dy = -(int16_t)HIWORD(wparam);
      int eff_w  = cv_content_width(win);
      int ncol   = get_column_count(eff_w, (int)data->column_width);
      int total_rows = (data->count == 0) ? 0
                     : (int)((data->count + (unsigned)ncol - 1) / (unsigned)ncol);
      int max_scroll_px = total_rows * ENTRY_HEIGHT - win->frame.h;
      if (max_scroll_px < 0) max_scroll_px = 0;
      int new_scroll = (int)win->scroll[1] + dy;
      if (new_scroll < 0) new_scroll = 0;
      if (new_scroll > max_scroll_px) new_scroll = max_scroll_px;
      win->scroll[1] = (uint32_t)new_scroll;
      cv_sync_scroll(win, data);
      invalidate_window(win);
      return true;
    }

    case kWindowMessageKeyDown: {
      if (!data || data->count == 0) return false;
      int eff_w = cv_content_width(win);
      int ncol  = get_column_count(eff_w, (int)data->column_width);
      int count = (int)data->count;
      int cur   = (data->selected == (uint32_t)-1) ? -1 : (int)data->selected;
      int next  = cur;

      switch (wparam) {
        case AX_KEY_UPARROW:
          next = (cur < 0) ? 0 : (cur - ncol >= 0 ? cur - ncol : cur);
          break;
        case AX_KEY_DOWNARROW:
          // Stay at current item if already on the last row; don't jump to count-1.
          next = (cur < 0) ? 0 : (cur + ncol < count ? cur + ncol : cur);
          break;
        case AX_KEY_LEFTARROW:
          next = (cur < 0) ? 0 : (cur > 0 ? cur - 1 : 0);
          break;
        case AX_KEY_RIGHTARROW:
          next = (cur < 0) ? 0 : (cur + 1 < count ? cur + 1 : cur);
          break;
        case AX_KEY_ENTER:
          if (cur < 0) return false;
          send_message(get_root_window(win), kWindowMessageCommand,
                       MAKEDWORD(cur, CVN_DBLCLK), &data->items[cur]);
          return true;
        case AX_KEY_DEL:
          if (cur < 0) return false;
          send_message(get_root_window(win), kWindowMessageCommand,
                       MAKEDWORD(cur, CVN_DELETE), &data->items[cur]);
          return true;
        default:
          return false;
      }

      if (next != cur && next >= 0) {
        data->selected = (uint32_t)next;
        cv_scroll_to_item(win, data, next);
        cv_sync_scroll(win, data);
        send_message(get_root_window(win), kWindowMessageCommand,
                     MAKEDWORD(next, CVN_SELCHANGE), &data->items[next]);
        invalidate_window(win);
      }
      return true;
    }

    case kWindowMessageDestroy:
      if (data) {
        free(data);
      }
      return true;
    
    default:
      return false;
  }
}
