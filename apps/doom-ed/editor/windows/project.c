#include <mapview/map.h>
#include <editor/editor.h>

#define MAX_MAPNAMES 256

typedef char map_name_t[64];

typedef struct {
  uint16_t num_items;
  map_name_t items[MAX_MAPNAMES];
} items_t;

void collect_proc(const char *name, void *parm) {
  window_t *win = parm;
  items_t *arr = win->userdata;
  if (arr->num_items == MAX_MAPNAMES) return;
	strncpy(arr->items[arr->num_items], name, sizeof(map_name_t));
  arr->num_items++;
}

result_t win_project(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  items_t const *items = win->userdata;
  switch (msg) {
    case evCreate:
      win->userdata = calloc(1, sizeof(items_t));
      if (!win->userdata) {
        return false;
      }
			find_all_maps(collect_proc, win);
      return true;
    case evDestroy:
      free(win->userdata);
      win->userdata = NULL;
      return true;
    case evPaint:
      for (int i = 0, y = 0; i < items->num_items; i++, y += BUTTON_HEIGHT) {
        if (win->cursor_pos == i) {
          fill_rect(get_sys_color(brTextNormal), R(0, y, win->frame.w, BUTTON_HEIGHT));
          draw_text_small(items->items[i], 4, y+3, get_sys_color(brControlBg));
        } else {
          draw_text_small(items->items[i], 4, y+3, get_sys_color(brTextNormal));
        }
      }
      return true;
    case evLeftButtonUp:
      win->cursor_pos = HIWORD(wparam)/BUTTON_HEIGHT;
      if (win->cursor_pos < items->num_items) {
        open_map(items->items[win->cursor_pos]);
      }
      invalidate_window(win);
      return true;
    case evKeyDown:
      if (wparam == AX_KEY_UPARROW && win->cursor_pos > 0) {
        win->cursor_pos--;
        invalidate_window(win);
        return true;
      }
      if (wparam == AX_KEY_DOWNARROW && win->cursor_pos < items->num_items-1) {
        win->cursor_pos++;
        invalidate_window(win);
        return true;
      }
      return false;
  }
  return false;
}
