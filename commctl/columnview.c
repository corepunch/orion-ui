#include <SDL2/SDL.h>
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

// ColumnView control window procedure
result_t win_columnview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  columnview_data_t *data = (columnview_data_t *)win->userdata2;
  
  switch (msg) {
    case kWindowMessageCreate: {
      data = malloc(sizeof(columnview_data_t));
      if (!data) return false;
      win->userdata2 = data;
      win->flags |= WINDOW_VSCROLL;
      data->count = 0;
      data->selected = -1;
      data->column_width = DEFAULT_COLUMN_WIDTH;
      data->last_click_time = 0;
      data->last_click_index = -1;
      return true;
    }
    
    case kWindowMessagePaint: {
      const int ncol = get_column_count(win->frame.w, data->column_width);
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
          fill_rect(COLOR_TEXT_NORMAL, x - 2, y - 2, data->column_width - 6, ENTRY_HEIGHT - 2);
          draw_icon8(data->items[i].icon, x, y - ICON_DODGE, COLOR_PANEL_BG);
          draw_text_small(data->names[i], x + ICON_OFFSET, y, COLOR_PANEL_BG);
        } else {
          fill_rect(COLOR_PANEL_BG, x - 2, y - 2, data->column_width - 6, ENTRY_HEIGHT - 2);
          draw_icon8(data->items[i].icon, x, y - ICON_DODGE, data->items[i].color);
          draw_text_small(data->names[i], x + ICON_OFFSET, y, data->items[i].color);
        }
      }

      return false;
    }
    
    case kWindowMessageLeftButtonDown: {
      int mx = LOWORD(wparam);
      int my = HIWORD(wparam);
      const int ncol = get_column_count(win->frame.w, data->column_width);
      int col = mx / data->column_width;
      int row = (my - WIN_PADDING) / ENTRY_HEIGHT;
      uint32_t index = row * ncol + col;
      
      if (index < data->count) {
        uint32_t now = SDL_GetTicks();
        
        // Check for double-click
        if (data->last_click_index == index && (now - data->last_click_time) < 500) {
          // Send double-click notification
          send_message(get_root_window(win), kWindowMessageCommand, MAKEDWORD(index, CVN_DBLCLK), &data->items[index]);
          data->last_click_time = 0;
          data->last_click_index = -1;
        } else {
          // Single click - update selection
          uint32_t old_selection = data->selected;
          data->selected = index;
          data->last_click_time = now;
          data->last_click_index = index;
          
          // Send selection change notification if changed
          if (old_selection != data->selected) {
            send_message(get_root_window(win), kWindowMessageCommand, MAKEDWORD(index, CVN_SELCHANGE), &data->items[index]);
          }
          
          invalidate_window(win);
        }
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
      invalidate_window(win);
      return true;
    
    case CVM_SETCOLUMNWIDTH: {
      if (wparam > 0) {
        data->column_width = wparam;
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
    
    case kWindowMessageDestroy:
      if (data) {
        free(data);
      }
      return true;
    
    default:
      return false;
  }
}
