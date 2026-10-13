#include <stdlib.h>
#include <string.h>

#include "columnview_internal.h"
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/theme.h>
#include <orion/user/svg_icon_loader.h>

static void icon_sync_scroll(window_t *win, reportview_data_t *data) {
  window_set_scroll_content(win, 0, (int)data->count * ENTRY_HEIGHT,
                            (irect16_t){WIN_PADDING, WIN_PADDING, WIN_PADDING, WIN_PADDING}, (irect16_t){0});
}

static irect16_t icon_item_rect(window_t *win, int index) {
  irect16_t content = window_scroll_content_rect(win);
  return R(content.x, content.y + index * ENTRY_HEIGHT, content.w, ENTRY_HEIGHT);
}

static int icon_hit_index(window_t *win, reportview_data_t *data, uint32_t wparam) {
  int my = (int)(int16_t)HIWORD(wparam);
  irect16_t content = window_scroll_content_rect(win);
  ipoint16_t point = {(int16_t)LOWORD(wparam), my};
  if (!rect_contains_point(content, point)) return RV_INVALID_SELECTION;
  int row = (my - content.y) / ENTRY_HEIGHT;
  return rv_valid_index(data, row) ? row : RV_INVALID_SELECTION;
}

static void icon_paint(window_t *win, reportview_data_t *data) {
  irect16_t cr = get_client_rect(win);
  uint32_t bg_col = get_sys_color(brColumnViewBg);
  fill_rect(bg_col, R(0, 0, cr.w, cr.h));

  bitmap_strip_t *strip = data->icon_strip;
  for (uint32_t i = 0; i < data->count; i++) {
    irect16_t item = window_scroll_rect_to_client(win, icon_item_rect(win, i));
    int y = item.y;
    if (y + ENTRY_HEIGHT <= 0)
      continue;
    if (y >= cr.h)
      break;

    int item_w = item.w;
    int item_h = ENTRY_HEIGHT - 1;
    int x = item.x;
    int gap = data->icon_text_gap;
    irect16_t icon_rect = {x, y, ICON_OFFSET, item_h};
    irect16_t text_rect = {x + ICON_OFFSET + gap, y,
                           MAX(0, item.w - ICON_OFFSET - gap),
                           item_h};

    uint32_t icon_col = (int)i == data->selected ? get_sys_color(brControlBg)
                                                 : data->items[i].color;
    if ((int)i == data->selected)
      fill_rect(get_sys_color(brTextNormal), R(x, y, item_w, item_h));
    else
      fill_rect(bg_col, R(x, y, item_w, item_h));
    const char *icon_name = data->items[i].icon_name;
    if (icon_name) {
      sysicon_resolved_t res;
      if (sysicon_resolve(icon_name, &res))
        draw_sprite_region((int)res.tex, icon_rect,
                           UV_RECT(res.u0, res.v0, res.u1, res.v1), icon_col, 0);
    } else {
      rv_draw_item_icon(strip, data->items[i].icon, &icon_rect,
                        data->preserve_icon_colors ? 0xFFFFFFFF : icon_col);
    }
    draw_text_clipped(FONT_SMALL, data->items[i].text, &text_rect, icon_col, 0);
  }
}

result_t win_iconview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  reportview_data_t *data = (reportview_data_t *)win->userdata2;

  switch (msg) {
    case evCreate: {
      data = calloc(1, sizeof(reportview_data_t));
      if (!data) return false;
      win->userdata2 = data;
      win->flags |= WINDOW_VSCROLL;
      win->flags |= WINDOW_FLEXSPACE;
      win->vscroll.visible_mode = SB_VIS_AUTO;
      data->selected = -1;
      data->last_click_index = RV_INVALID_SELECTION;
      data->column_width = DEFAULT_COLUMN_WIDTH;
      data->icon_size = DEFAULT_ICON_SIZE;
      data->icon_text_gap = DEFAULT_ICON_TEXT_GAP;
      data->redraw_enabled = true;
      data->redraw_dirty = false;
      data->column_titles_visible = true;
      data->preserve_icon_colors = false;
      icon_sync_scroll(win, data);
      return true;
    }
    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (!m) return true;
      m->desired_w = MAX(m->desired_w, 1);
      irect16_t pad = win->scroll_content.padding;
      m->desired_h = MAX(m->desired_h, ENTRY_HEIGHT + pad.y + pad.h);
      return true;
    }
    case evPaint:
      icon_paint(win, data);
      return false;
    case evLeftButtonDown: {
      int index = icon_hit_index(win, data, wparam);
      if (rv_valid_index(data, index)) {
        uint32_t now = axGetMilliseconds();
        if (data->last_click_index == index && (now - data->last_click_time) < RV_DOUBLE_CLICK_MS) {
          rv_notify(win, data, index, RVN_DBLCLK);
          rv_reset_click_state(data);
        } else {
          int old_selection = data->selected;
          data->selected = index;
          data->last_click_time = now;
          data->last_click_index = index;
          if (old_selection != data->selected)
            rv_notify(win, data, index, RVN_SELCHANGE);
          rv_invalidate(win, data);
        }
      }
      return true;
    }
    case evLeftButtonDoubleClick: {
      int index = icon_hit_index(win, data, wparam);
      if (rv_valid_index(data, index)) {
        rv_reset_click_state(data);
        rv_notify(win, data, index, RVN_DBLCLK);
      }
      return true;
    }
    case RVM_ADDITEM: {
      reportview_item_t *item = (reportview_item_t *)lparam;
      if (!item || data->count >= MAX_COLUMNVIEW_ITEMS)
        return -1;
      uint32_t i = data->count;
      if (!rv_store_item(data, i, item))
        return -1;
      data->count++;
      icon_sync_scroll(win, data);
      rv_invalidate(win, data);
      return (result_t)i;
    }
    case RVM_DELETEITEM: {
      if (wparam >= data->count)
        return false;
      memmove(data->items + wparam, data->items + wparam + 1, (data->count - wparam - 1) * sizeof(data->items[0]));
      memmove(data->names + wparam, data->names + wparam + 1, (data->count - wparam - 1) * sizeof(data->names[0]));
      memmove(data->subnames + wparam, data->subnames + wparam + 1, (data->count - wparam - 1) * sizeof(data->subnames[0]));
      data->count--;
      rv_rebind_item_refs(data, (uint32_t)wparam);
      if (data->selected == (int)wparam) data->selected = RV_INVALID_SELECTION;
      else if (data->selected > (int)wparam) data->selected--;
      icon_sync_scroll(win, data);
      rv_invalidate(win, data);
      return true;
    }
    case RVM_GETITEMCOUNT:
      return (result_t)data->count;
    case RVM_GETSELECTION:
      return (result_t)data->selected;
    case RVM_SETSELECTION:
      if ((int)wparam >= 0 && wparam < data->count) {
        data->selected = (int)wparam;
        window_ensure_rect_visible(win, icon_item_rect(win, data->selected), SB_VERT);
        rv_invalidate(win, data);
        return true;
      }
      return false;
    case RVM_CLEAR:
      data->count = 0;
      rv_reset_view_state(win, data);
      icon_sync_scroll(win, data);
      rv_invalidate(win, data);
      return true;
    case RVM_SETCOLUMNWIDTH:
      if (wparam > 0) {
        data->column_width = (int)wparam;
        icon_sync_scroll(win, data);
        rv_invalidate(win, data);
        return true;
      }
      return false;
    case RVM_GETCOLUMNWIDTH:
      return (result_t)data->column_width;
    case RVM_GETITEMDATA:
      if (wparam < data->count && lparam) {
        *(reportview_item_t *)lparam = data->items[wparam];
        return true;
      }
      return false;
    case RVM_HITTEST:
      return (result_t)icon_hit_index(win, data, wparam);
    case RVM_SETITEMDATA: {
      reportview_item_t *item = (reportview_item_t *)lparam;
      if (!item || wparam >= data->count)
        return false;
      if (!rv_store_item(data, (uint32_t)wparam, item))
        return false;
      rv_invalidate(win, data);
      return true;
    }
    case RVM_SETVIEWMODE:
      return wparam == RVM_VIEW_ICON;
    case RVM_SETREDRAW:
      if (wparam) {
        data->redraw_enabled = true;
        if (data->redraw_dirty) {
          data->redraw_dirty = false;
          invalidate_window(win);
        }
      } else {
        data->redraw_enabled = false;
      }
      return true;
    case RVM_SETICONSTRIP:
      data->icon_strip = (bitmap_strip_t *)lparam;
      rv_invalidate(win, data);
      return true;
    case RVM_SETPRESERVEICONCOLORS:
      data->preserve_icon_colors = (wparam != 0);
      rv_invalidate(win, data);
      return true;
    case RVM_SETICONTEXTGAP:
      data->icon_text_gap = (int)wparam;
      if (data->icon_text_gap < 0) data->icon_text_gap = 0;
      rv_invalidate(win, data);
      return true;
    case evVScroll:
      rv_invalidate(win, data);
      return true;
    case evArrange: {
      layout_arrange_t const *a = (layout_arrange_t const *)lparam;
      if (a) {
        irect16_t r = a->rect;
        if (r.w < 1) r.w = 1;
        if (r.h < 1) r.h = 1;
        win->frame = r;
      }
      icon_sync_scroll(win, data);
      return true;
    }
    case evResize:
      icon_sync_scroll(win, data);
      return false;
    case evKeyDown: {
      if (!data || data->count == 0)
        return false;
      int count = (int)data->count;
      int cur = data->selected;
      int next = cur;
      switch (wparam) {
        case AX_KEY_UPARROW:
          next = (cur < 0) ? 0 : (cur > 0 ? cur - 1 : 0);
          break;
        case AX_KEY_DOWNARROW:
          next = (cur < 0) ? 0 : (cur + 1 < count ? cur + 1 : cur);
          break;
        case AX_KEY_LEFTARROW:
          next = (cur < 0) ? 0 : (cur > 0 ? cur - 1 : 0);
          break;
        case AX_KEY_RIGHTARROW:
          next = (cur < 0) ? 0 : (cur + 1 < count ? cur + 1 : cur);
          break;
        case AX_KEY_ENTER:
          if (cur < 0) return false;
          rv_notify(win, data, cur, RVN_DBLCLK);
          return true;
        case AX_KEY_DEL:
          if (cur < 0) return false;
          rv_notify(win, data, cur, RVN_DELETE);
          return true;
        default:
          return false;
      }
      if (next != cur && next >= 0) {
        data->selected = next;
        window_ensure_rect_visible(win, icon_item_rect(win, next), SB_VERT);
        rv_notify(win, data, next, RVN_SELCHANGE);
        rv_invalidate(win, data);
      }
      return true;
    }
    case evDestroy:
      free(data);
      win->userdata2 = NULL;
      return true;
    default:
      return false;
  }
}
