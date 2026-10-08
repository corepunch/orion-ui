#include <stdlib.h>
#include <string.h>

#include "toolbar.h"
#include "dock.h"
#include "messages.h"
#include "draw.h"
#include "image.h"
#include "svg_icon_loader.h"
#include "theme.h"

#define TB_WINDOW_CLOSE    (-102)
#define TB_WINDOW_COLLAPSE (-103)

static bool toolbar_set_checked(toolbar_state_t *tb, int i, bool checked);

bool toolbar_merged_title(const window_t *win) {
  return win && !(win->flags & WINDOW_NOTITLE) && caption_merged_into_toolbar(win->flags);
}

int toolbar_content_offset(const window_t *win) {
  return caption_extent(win->flags);
}

static int toolbar_item_at(const toolbar_state_t *tb, int tx, int ty, bool include_disabled) {
  if (!tb || !tb->item_rects) return -1;
  for (int i = 0; i < tb->item_count; i++) {
    irect16_t r = tb->item_rects[i];
    if (!include_disabled && (tb->items[i].state & TBSTATE_DISABLED)) continue;
    if (rect_contains_point(r, (ipoint16_t){tx, ty}))
      return i;
  }
  return -1;
}

int toolbar_item_hit(const toolbar_state_t *tb, int tx, int ty) {
  return toolbar_item_at(tb, tx, ty, false);
}

bool toolbar_hit_action(const toolbar_state_t *tb, int x, int y) {
  for (int i = 0; tb && tb->item_rects && i < tb->item_count; i++) {
    int type = tb->items[i].type;
    if (type != TOOLBAR_ITEM_LABEL && type != TOOLBAR_ITEM_SPACER && type != TOOLBAR_ITEM_SEPARATOR &&
        rect_contains_point(tb->item_rects[i], (ipoint16_t){x, y})) return true;
  }
  return false;
}

static int toolbar_state_item_height(const toolbar_state_t *tb) {
  int bsz = (tb && tb->btn_size > 0) ? tb->btn_size : get_theme()->toolbar_button_size;
  return bsz + ((tb && (tb->style & TOOLBAR_STYLE_SHOW_LABELS)) ? text_char_height(FONT_SMALLEST) + 2 : 0);
}

static void compute_toolbar_item_rects(window_t *parent, toolbar_state_t *tb) {
  if (!tb) return;

  free(tb->item_rects);
  tb->item_rects = tb->item_count > 0 ? malloc((size_t)tb->item_count * sizeof(irect16_t)) : NULL;

  if (tb->item_count > 0 && !tb->item_rects) {
    fprintf(stderr, "[tb] rect allocation failed win=%u count=%d\n", parent->id, tb->item_count);
    fflush(stderr);
    return;
  }
  int bsz = (tb->btn_size > 0) ? tb->btn_size : get_theme()->toolbar_button_size;
  int item_h = toolbar_state_item_height(tb);
  int padding = toolbar_effective_padding(parent);
  int spacing = (tb->style & TOOLBAR_STYLE_COMPACT) ? get_theme()->toolbar_compact_spacing : TOOLBAR_SPACING;
  bool vertical = tb->orientation == TOOLBAR_VERTICAL;
  int vertical_spacing = TOOLBAR_SPACING;
  int grip_h = (vertical && (tb->style & TOOLBAR_STYLE_GRIP)) ? get_theme()->toolbar_grip_size : 0;
  int grip_w = (!vertical && (tb->style & TOOLBAR_STYLE_GRIP)) ? get_theme()->toolbar_grip_size : 0;
  int cursor = padding + grip_h;
  int x = padding + grip_w;
  int base_y = padding + grip_h;
  int field_y = base_y + 2;
  int field_h = bsz > 4 ? (bsz - 4) : bsz;
  int column_w = 0;

  for (int i = 0; i < tb->item_count; i++) {
    toolbar_item_t *item = &tb->items[i];
    int w = 0;
    int y = base_y;
    int h = item_h;

    switch (item->type) {
      case TOOLBAR_ITEM_CUSTOM:
      case TOOLBAR_ITEM_BUTTON:
        w = item->w > 0 ? item->w : bsz;
        if (!item->w && (tb->style & TOOLBAR_STYLE_SHOW_LABELS) && item->text)
          w = MAX(w, text_strwidth(FONT_SMALLEST, item->text) + 8);
        if ((item->style & CONTROL_SIZE_MASK) == CONTROL_SIZE_LARGE) { // grows into the band padding, centred on the row
          if (!item->w) w += CONTROL_LARGE_GROWTH;
          h += CONTROL_LARGE_GROWTH;
          if (!vertical) y -= CONTROL_LARGE_GROWTH / 2;
        }
        break;
      case TOOLBAR_ITEM_DROPDOWN:
        w = item->w > 0 ? item->w : bsz;
        if (!item->w && (tb->style & TOOLBAR_STYLE_SHOW_LABELS) && item->text)
          w = MAX(w, text_strwidth(FONT_SMALLEST, item->text) + 8);
        w += get_theme()->toolbar_dropdown_arrow_w;
        break;
      case TOOLBAR_ITEM_LABEL:
        w = item->w > 0 ? item->w
                        : (text_strwidth(FONT_SMALLEST, item->text ? item->text : "") + TOOLBAR_LABEL_PADDING);
        break;
      case TOOLBAR_ITEM_COMBOBOX:
        w = item->w > 0 ? item->w : (bsz * TOOLBAR_COMBOBOX_DEFAULT_WIDTH_MULT);
        y = field_y;
        h = field_h;
        break;
      case TOOLBAR_ITEM_SLIDER:
        w = vertical ? bsz : bsz * 3 + TOOLBAR_SPACING * 2;
        h = vertical ? item_h * 3 + TOOLBAR_SPACING * 2 : item_h;
        break;
      case TOOLBAR_ITEM_TEXTEDIT:
        w = item->w > 0 ? item->w : (bsz * 8);
        y = field_y;
        h = field_h;
        break;
      case TOOLBAR_ITEM_SEGMENTED: // w = 0 is replaced by the control's own measure in tbSetItems
        w = item->w > 0 ? item->w : (bsz * 6);
        y = field_y;
        h = field_h;
        break;
      case TOOLBAR_ITEM_SEPARATOR:
        w = item->w > 0 ? item->w : 6;
        break;
      case TOOLBAR_ITEM_SPACER:
        w = item->w > 0 ? item->w : TOOLBAR_SPACING_GAP_WIDTH;
        break;
      default:
        w = 0;
        break;
    }

    if (vertical) {
      bool small = tb->columns <= 1 && (item->style & TOOLBAR_ITEM_FLAG_SMALL) != 0 &&
                   (item->type == TOOLBAR_ITEM_BUTTON || item->type == TOOLBAR_ITEM_CUSTOM);
      if (small) {
        // Half-size cell: two minis + one gap fill a normal button cell, so a
        // 2x2 block takes one normal row (within 1px when bsz is odd).
        int mini = (bsz - vertical_spacing) / 2;
        if (mini < 1) mini = 1;
        int pair = 1;
        if (i + 1 < tb->item_count) {
          toolbar_item_t *next = &tb->items[i + 1];
          if ((next->style & TOOLBAR_ITEM_FLAG_SMALL) != 0 &&
              (next->type == TOOLBAR_ITEM_BUTTON || next->type == TOOLBAR_ITEM_CUSTOM))
            pair = 2;
        }
        if ((tb->style & TOOLBAR_STYLE_WRAPABLE) && cursor > base_y &&
            cursor + mini + base_y > parent->frame.h) {
          x += column_w + vertical_spacing;
          cursor = base_y;
          column_w = 0;
        }
        column_w = MAX(column_w, pair == 2 ? mini * 2 + vertical_spacing : mini);
        for (int col = 0; col < pair; col++)
          if (tb->item_rects)
            tb->item_rects[i + col] = (irect16_t){x + col * (mini + vertical_spacing),
                                                  y + cursor - base_y, mini, mini};
        cursor += mini + vertical_spacing;
        i += pair - 1;
        continue;
      }
      if (item->type == TOOLBAR_ITEM_SEPARATOR || item->type == TOOLBAR_ITEM_SPACER) {
        h = w;
        w = bsz;
      }
      if (tb->columns <= 1 && (tb->style & TOOLBAR_STYLE_WRAPABLE) && cursor > base_y &&
          cursor + h + base_y > parent->frame.h) {
        x += column_w + vertical_spacing;
        cursor = base_y;
        column_w = 0;
      }
      column_w = MAX(column_w, w);
      y += cursor - base_y;
      cursor += h + vertical_spacing;
    }
    if (tb->item_rects)
      tb->item_rects[i] = (irect16_t){x, y, w, h};
    if (!vertical) x += w + spacing;
  }

  if (!vertical && tb->item_rects) {
    int flex_count = 0, offset = 0;
    for (int i = 0; i < tb->item_count; i++)
      if (tb->items[i].type == TOOLBAR_ITEM_SPACER && (tb->items[i].style & TOOLBAR_ITEM_FLAG_FLEXSPACE)) flex_count++;
    int extra = MAX(0, parent->frame.w - padding - (x - spacing));
    for (int i = 0; i < tb->item_count; i++) {
      tb->item_rects[i].x += offset;
      if (tb->items[i].type != TOOLBAR_ITEM_SPACER || !(tb->items[i].style & TOOLBAR_ITEM_FLAG_FLEXSPACE)) continue;
      int share = extra / flex_count;
      tb->item_rects[i].w += share;
      offset += share;
      extra -= share;
      flex_count--;
    }
  }

  if (vertical && tb->columns > 1) {
    int cell_w = bsz;
    for (int i = 0; i < tb->item_count; i++)
      cell_w = MAX(cell_w, tb->item_rects[i].w);
    int grid_w = tb->columns * cell_w + (tb->columns - 1) * spacing;
    x = padding;
    cursor = base_y;
    for (int i = 0; i < tb->item_count;) {
      int count = 1, row_h = tb->item_rects[i].h;
      bool buttons = tb->items[i].type == TOOLBAR_ITEM_BUTTON ||
                     tb->items[i].type == TOOLBAR_ITEM_CUSTOM;
      while (buttons && count < tb->columns && i + count < tb->item_count &&
             (tb->items[i + count].type == TOOLBAR_ITEM_BUTTON ||
              tb->items[i + count].type == TOOLBAR_ITEM_CUSTOM)) {
        row_h = MAX(row_h, tb->item_rects[i + count].h);
        count++;
      }
      if ((tb->style & TOOLBAR_STYLE_WRAPABLE) && cursor > base_y &&
          cursor + row_h + base_y > parent->frame.h) {
        x += grid_w + spacing;
        cursor = base_y;
      }
      for (int col = 0; col < count; col++)
        tb->item_rects[i + col] = R(x + col * (cell_w + spacing), cursor,
                                    buttons ? cell_w : grid_w, row_h);
      cursor += row_h + spacing;
      i += count;
    }
  }

  if (!vertical && toolbar_merged_title(parent) && tb->item_rects) {
    int end = MAX(0, parent->frame.w - padding);
    for (int i = tb->item_count - 1; i >= 0; i--) {
      if (tb->items[i].ident != TB_WINDOW_CLOSE && tb->items[i].ident != TB_WINDOW_COLLAPSE) continue;
      irect16_t *r = &tb->item_rects[i];
      r->w = MIN(r->w, end); r->x = end - r->w;
      end = MAX(0, r->x - spacing);
    }
    for (int i = 0; i < tb->item_count; i++) {
      if (tb->items[i].ident == TB_WINDOW_CLOSE || tb->items[i].ident == TB_WINDOW_COLLAPSE) continue;
      irect16_t *r = &tb->item_rects[i];
      r->x = MIN(r->x, end); r->w = MIN(r->w, end - r->x);
    }
  }

  for (window_t *tc = tb->children; tc; tc = tc->next) {
    for (int i = 0; i < tb->item_count; i++) {
      if ((uint32_t)tb->items[i].ident == tc->id && tb->item_rects) {
        tc->frame = tb->item_rects[i];
        if (tb->items[i].type == TOOLBAR_ITEM_SLIDER) {
          if (vertical) tc->flags |= SLIDER_VERTICAL;
          else tc->flags &= ~SLIDER_VERTICAL;
        }
        break;
      }
    }
  }

}

static void draw_toolbar_icon_in_rect(window_t *win, toolbar_state_t *tb, const char *icon_name, irect16_t r, int offset, bool disabled) {
  sysicon_resolved_t res = {0};
  bool from_strip = icon_name && strncmp(icon_name, "strip:", 6) == 0;
  if (from_strip) {
    char *end;
    long index = strtol(icon_name + 6, &end, 10);
    const bitmap_strip_t *strip = &tb->strip;
    int rows = strip->icon_h > 0 ? strip->sheet_h / strip->icon_h : 0;
    if (!strip->tex || strip->cols <= 0 || strip->icon_w <= 0 || rows <= 0 ||
        strip->sheet_w <= 0 || strip->cols > strip->sheet_w / strip->icon_w ||
        index < 0 || index >= (long)strip->cols * rows || end == icon_name + 6 || *end) {
      fprintf(stderr, "[tb] invalid strip win=%u icon=%s texture=%u cols=%d rows=%d\n", win->id, icon_name, strip->tex, strip->cols, rows);
      fflush(stderr);
      return;
    }
    irect16_t region = R((index % strip->cols) * strip->icon_w, (index / strip->cols) * strip->icon_h, strip->icon_w, strip->icon_h);
    res = (sysicon_resolved_t){ .tex = strip->tex, .w = region.w, .h = region.h,
      .u0 = (float)region.x / strip->sheet_w, .v0 = (float)region.y / strip->sheet_h,
      .u1 = (float)(region.x + region.w) / strip->sheet_w, .v1 = (float)(region.y + region.h) / strip->sheet_h };
  } else if (!sysicon_resolve(icon_name ? icon_name : "missing", &res)) return;
  bool compact = tb && (tb->style & TOOLBAR_STYLE_COMPACT);
  int w = res.w, h = res.h;
  if (w > 0 && h > 0) {
    int size = MAX(1, compact ? MIN(get_theme()->toolbar_compact_icon, MIN(r.w, r.h)) : MIN(r.w, r.h) - 4);
    size = MIN(size, MAX(w, h));
    int extent = MAX(w, h);
    w = MAX(1, w * size / extent);
    h = MAX(1, h * size / extent);
  }
  irect16_t icon = rect_offset(rect_center(r, w, h), offset, offset);
  draw_sprite_region((int)res.tex, icon,
                     UV_RECT(res.u0, res.v0, res.u1, res.v1),
                     from_strip ? (disabled ? 0x80ffffffu : 0xffffffffu) :
                     get_sys_color(disabled ? brTextDisabled : (compact ? brTextNormal : brToolbarForeground)), 0);
}

static void draw_toolbar_item_at_origin(window_t *win, toolbar_state_t *tb, int i) {
  toolbar_item_t *item = &tb->items[i];
  irect16_t r = tb->item_rects[i];
  if (r.w <= 0 || r.h <= 0) return;
  bool disabled = (item->state & TBSTATE_DISABLED) != 0;
  bool is_pressed = !disabled && (tb->pressed_item == i);
  bool compact = (tb->style & TOOLBAR_STYLE_COMPACT) != 0;
  bool interactive = !compact || (tb->style & TOOLBAR_STYLE_PLASTIC);
  bool is_active  = !disabled && (item->state & TBSTATE_CHECKED) != 0; // compact rows keep checks, not hover
  bool is_hot     = !disabled && interactive && (tb->hot_item == i);
  theme_t *th = get_theme();

  switch (item->type) {
    case TOOLBAR_ITEM_CUSTOM: {
      toolbar_draw_item_t draw = {R(0, 0, r.w, r.h),
        (is_active ? CTRL_SELECTED : 0) | (is_pressed ? CTRL_PRESSED : 0) |
        (is_hot ? CTRL_HOVER : 0) | (disabled ? CTRL_DISABLED : 0), i};
      send_message(win, tbDrawItem, (uint32_t)item->ident, &draw);
      break;
    }
    case TOOLBAR_ITEM_BUTTON: {
      irect16_t local = {0, 0, r.w, r.h};
      // A checked button swaps to its checked image, which is a state image like any other.
      const char *icon = is_active && item->checked_icon ? item->checked_icon : item->icon;
      const char *icon_name = icon ? icon : "missing";
      // Derive each flag independently; let the theme decide rendering.
      ctrl_state_t state = disabled ? CTRL_DISABLED : CTRL_NORMAL;
      if (is_active)  state |= CTRL_SELECTED;
      if (is_pressed) state |= CTRL_PRESSED;
      if (is_hot)     state |= CTRL_HOVER;
      theme_part_t part = (tb->style & TOOLBAR_STYLE_SHOW_LABELS)
                                     ? THEME_PART_TOOLBAR_LABELED_BUTTON
                                     : THEME_PART_TOOLBAR_BUTTON;
      bool plastic = (tb->style & TOOLBAR_STYLE_PLASTIC) && !(item->style & TOOLBAR_ITEM_FLAG_ARTWORK) &&
                     item->ident != TB_WINDOW_CLOSE && item->ident != TB_WINDOW_COLLAPSE;
      if (plastic) {
        irect16_t face = local;
        if (tb->style & TOOLBAR_STYLE_SHOW_LABELS) face.h -= text_char_height(FONT_SMALLEST) + 2;
        theme_draw_ex(THEME_PART_TOOLBAR_BUTTON, face, state | CTRL_PLASTIC,
                      &(theme_draw_opts_t){.color = item->color, .icon = icon, .control_size = item->style & CONTROL_SIZE_MASK});
      } else if (tb->style & TOOLBAR_STYLE_COMPACT) {
        if (is_pressed || is_active)
          theme_draw(THEME_PART_TOOLBAR_BUTTON, rect_center(local, local.h, local.h), is_pressed ? CTRL_PRESSED : CTRL_SELECTED);
      } else {
        theme_draw(part, local, state);
      }
      int poff = is_pressed ? th->press_icon_offset : 0;
      irect16_t icon_rect = local;
      if (tb->style & TOOLBAR_STYLE_SHOW_LABELS)
        icon_rect.h = (tb->btn_size > 0) ? tb->btn_size : get_theme()->toolbar_button_size;
      if (item->ident == TB_WINDOW_CLOSE || item->ident == TB_WINDOW_COLLAPSE)
        draw_theme_icon_in_rect(item->ident == TB_WINDOW_CLOSE ? THEME_ICON_CLOSE : THEME_ICON_RESTORE,
                                icon_rect, get_sys_color(brTextNormal));
      else if (!plastic) draw_toolbar_icon_in_rect(win, tb, icon_name, icon_rect, poff, disabled);
      if ((tb->style & TOOLBAR_STYLE_SHOW_LABELS) && item->text) {
        int tx = (local.w - text_strwidth(FONT_SMALLEST, item->text)) / 2 + poff;
        int ty = local.h - text_char_height(FONT_SMALLEST) - 2 + poff;
        draw_text(FONT_SMALLEST, item->text, tx, ty, get_sys_color(disabled ? brTextDisabled : brToolbarForeground));
      }
      break;
    }
    case TOOLBAR_ITEM_DROPDOWN: {
      int aw = get_theme()->toolbar_dropdown_arrow_w;
      irect16_t btn_part = {0, 0, r.w - aw, r.h};
      irect16_t arr_part = {r.w - aw, 0, aw, r.h};
      bool arrow_pressed = is_pressed && tb->pressed_in_arrow;
      bool btn_pressed   = is_pressed && !tb->pressed_in_arrow;

      ctrl_state_t btn_state = disabled ? CTRL_DISABLED : CTRL_NORMAL;
      if (is_active)   btn_state |= CTRL_SELECTED;
      if (btn_pressed) btn_state |= CTRL_PRESSED;
      if (is_hot)      btn_state |= CTRL_HOVER;
      theme_draw(THEME_PART_TOOLBAR_SPLIT_BUTTON, btn_part, btn_state);

      int btn_poff = btn_pressed ? th->press_icon_offset : 0;
      const char *icon_name = item->icon ? item->icon : "missing";
      irect16_t icon_rect = btn_part;
      if (tb->style & TOOLBAR_STYLE_SHOW_LABELS)
        icon_rect.h = (tb->btn_size > 0) ? tb->btn_size : get_theme()->toolbar_button_size;
      draw_toolbar_icon_in_rect(win, tb, icon_name, icon_rect, btn_poff, disabled);
      if ((tb->style & TOOLBAR_STYLE_SHOW_LABELS) && item->text) {
        int tx = (btn_part.w - text_strwidth(FONT_SMALLEST, item->text)) / 2 + btn_poff;
        int ty = btn_part.h - text_char_height(FONT_SMALLEST) - 2 + btn_poff;
        draw_text(FONT_SMALLEST, item->text, tx, ty, get_sys_color(disabled ? brTextDisabled : brToolbarForeground));
      }

      ctrl_state_t arr_state = disabled ? CTRL_DISABLED : CTRL_NORMAL;
      if (arrow_pressed) arr_state |= CTRL_PRESSED;
      if (is_hot)        arr_state |= CTRL_HOVER;
      theme_draw(THEME_PART_TOOLBAR_SPLIT_ARROW, arr_part, arr_state);

      break;
    }
    case TOOLBAR_ITEM_SEPARATOR:
      theme_draw(THEME_PART_TOOLBAR_SEPARATOR, R(0, 0, r.w, r.h), CTRL_NORMAL);
      break;
    case TOOLBAR_ITEM_LABEL: {
      int ty = (r.h - text_char_height(FONT_SMALLEST)) / 2;
      draw_text_ellipsized(FONT_SMALLEST, item->text ? item->text : "", 2, ty, MAX(0, r.w - 4), get_sys_color(disabled ? brTextDisabled : brToolbarForeground));
      break;
    }
    case TOOLBAR_ITEM_SLIDER:
    case TOOLBAR_ITEM_SEGMENTED:
    case TOOLBAR_ITEM_SPACER:
    case TOOLBAR_ITEM_COMBOBOX:
    case TOOLBAR_ITEM_TEXTEDIT:
      break;
  }
}

static result_t win_toolbar(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  toolbar_state_t *tb = (toolbar_state_t *)win->userdata;

  switch (msg) {
    case evCreate:
      if (!win->userdata) {
        allocate_window_data(win, sizeof(toolbar_state_t));
        tb = (toolbar_state_t *)win->userdata;
        tb->hot_item = -1;
        tb->pressed_item = -1;
      }
      return true;

    case evDestroy:
      if (tb) {
        SAFE_DELETE(tb->items, free);
        SAFE_DELETE(tb->item_tooltips, free);
        SAFE_DELETE(tb->item_icons, free);
        SAFE_DELETE(tb->item_checked_icons, free);
        SAFE_DELETE(tb->item_rects, free);
        free(tb);
        win->userdata = NULL;
      }
      return true;

    case evLeftButtonDown: {
      if (!tb || !tb->item_rects) return false;
      int tx = (int16_t)LOWORD(wparam);
      int ty = (int16_t)HIWORD(wparam);
      int idx = toolbar_item_hit(tb, tx, ty);
      if (idx < 0) return false;

      toolbar_item_t *item = &tb->items[idx];
      if (item->type != TOOLBAR_ITEM_BUTTON && item->type != TOOLBAR_ITEM_DROPDOWN &&
          item->type != TOOLBAR_ITEM_CUSTOM)
        return false;

      tb->pressed_item = idx;
      set_capture(win);
      tb->pressed_in_arrow = (item->type == TOOLBAR_ITEM_DROPDOWN) &&
                             (tx >= tb->item_rects[idx].x + tb->item_rects[idx].w - get_theme()->toolbar_dropdown_arrow_w);
      invalidate_window(win->parent);
      return true;
    }

    case evLeftButtonUp: {
      if (!tb || tb->pressed_item < 0) return false;
      int tx = (int16_t)LOWORD(wparam);
      int ty = (int16_t)HIWORD(wparam);
      int saved_idx = tb->pressed_item;
      if (g_ui_runtime.captured == win) set_capture(NULL);
      bool saved_in_arrow = tb->pressed_in_arrow;

      tb->pressed_item = -1;
      tb->pressed_in_arrow = false;
      invalidate_window(win->parent);

      if (saved_idx >= tb->item_count) return true;
      int hit = toolbar_item_hit(tb, tx, ty);
      if (hit != saved_idx) {
        if (hit >= 0 && (tb->items[saved_idx].style & TOOLBAR_ITEM_FLAG_REORDERABLE) &&
            (tb->items[hit].style & TOOLBAR_ITEM_FLAG_REORDERABLE)) {
          toolbar_drop_item_t drop = {tb->items[saved_idx].ident, tb->items[hit].ident};
          send_message(win->parent ? win->parent : win, evCommand, MAKEDWORD((uint16_t)drop.to_ident, (uint16_t)tbItemDrop), &drop);
        }
        return true;
      }

      toolbar_item_t *item = &tb->items[saved_idx];
      // Notify the toolbar window that owns this host, not the top-level chrome.
      // Docked palettes are children of app chrome; get_root_window() would send
      // every click to the top band and skip the left tool strip.
      window_t *owner = win->parent ? win->parent : get_root_window(win);
      if (item->ident == TB_WINDOW_CLOSE) {
        if (owner->dock) show_window(owner, false);
        else if (!send_message(owner, evClose, 0, NULL)) destroy_window(owner);
        return true;
      }
      if (item->ident == TB_WINDOW_COLLAPSE) {
        if (owner->dock) dock_collapse(owner, !owner->dock->collapsed);
        else if (window_is_maximized(owner)) restore_window(owner);
        else maximize_window(owner);
        return true;
      }
      if (item->type == TOOLBAR_ITEM_DROPDOWN && saved_in_arrow) {
        send_message(owner, evCommand,
                     MAKEDWORD((uint16_t)item->ident, (uint16_t)tbDropdown), win);
      } else {
        if (item->style & TBSTYLE_CHECK) toolbar_set_checked(tb, saved_idx, !(item->state & TBSTATE_CHECKED));
        else if (item->style & TBSTYLE_CHECKGROUP) toolbar_set_checked(tb, saved_idx, true);
        invalidate_window(win->parent);
        send_message(owner, evCommand, MAKEDWORD((uint16_t)item->ident, (uint16_t)btnClicked), win);
      }
      return true;
    }

    case evMouseMove: {
      if (!tb) return false;
      int tx = (int16_t)LOWORD(wparam);
      int ty = (int16_t)HIWORD(wparam);
      int old_hot = tb->hot_item;
      tb->hot_item = toolbar_item_hit(tb, tx, ty);
      if (tb->hot_item != old_hot)
        invalidate_window(win->parent);
      return true;
    }

    case evPointerCancel:
      if (tb) { tb->pressed_item = -1; tb->pressed_in_arrow = false; }
      if (g_ui_runtime.captured == win) set_capture(NULL);
      invalidate_window(win->parent);
      return true;
    case evMouseLeave:
      if (tb && tb->hot_item >= 0) {
        tb->hot_item = -1;
        invalidate_window(win->parent);
      }
      return false;

    case evGetTooltipRect: {
      if (!tb || !lparam) return false;
      int idx = toolbar_item_at(tb, (int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam), true);
      if (idx < 0) return false;
      irect16_t anchor = tb->item_rects[idx];
      for (window_t *tc = tb->children; tc; tc = tc->next) {
        if (tc->id != (uint32_t)tb->items[idx].ident) continue;
        irect16_t part;
        if (send_message(tc, evGetTooltipRect, MAKEDWORD(LOWORD(wparam) - tc->frame.x, HIWORD(wparam) - tc->frame.y), &part))
          anchor = rect_offset(part, tc->frame.x, tc->frame.y);
        break;
      }
      *(irect16_t *)lparam = anchor;
      return true;
    }
    case evGetTooltipText: {
      if (!tb || !lparam) return false;
      int idx = toolbar_item_at(tb, (int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam), true);
      for (window_t *tc = tb->children; idx >= 0 && tc; tc = tc->next) // an embedded control may name its own parts
        if (tc->id == (uint32_t)tb->items[idx].ident &&
            send_message(tc, evGetTooltipText, MAKEDWORD(LOWORD(wparam) - tc->frame.x, HIWORD(wparam) - tc->frame.y), lparam))
          return true;
      if (idx < 0 || !tb->items[idx].tooltip || !tb->items[idx].tooltip[0])
        return false;
      char *buf = (char *)lparam;
      strncpy(buf, tb->items[idx].tooltip, 255);
      buf[255] = '\0';
      return true;
    }

    case evThemeChanged:
      // Recompute item rects after a theme change: per-theme metrics such as
      // font height (labelled buttons) and padding may differ between themes.
      // Do NOT call invalidate_window here — set_theme() drives repaints for
      // visible windows; hidden toolbars must stay silent until made visible.
      if (tb && tb->items)
        compute_toolbar_item_rects(win->parent, tb);
      return false;

    default:
      (void)wparam;
      (void)lparam;
      return false;
  }
}

toolbar_state_t *toolbar_ensure_state(window_t *win) {
  if (!win || !(win->flags & WINDOW_TOOLBAR)) return NULL;

  if (!win->toolbar) {
    irect16_t r = {0, 0, 0, 0};
    win->toolbar = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL |
                                         WINDOW_NOTABSTOP | WINDOW_HIDDEN,
                                 &r, win, win_toolbar, win->hinstance, NULL);
    if (!win->toolbar) return NULL;

    window_t *prev = NULL;
    window_t *c = win->children;
    while (c && c != win->toolbar) {
      prev = c;
      c = c->next;
    }
    if (c == win->toolbar) {
      if (prev)
        prev->next = win->toolbar->next;
      else
        win->children = win->toolbar->next;
      win->toolbar->next = NULL;
    }
  }

  return window_toolbar_state(win);
}

uint8_t toolbar_dock_hint(const window_t *win) {
  toolbar_state_t *tb = window_toolbar_state((window_t *)win);
  return tb ? tb->dock_hint : TOOLBAR_DOCK_TOP;
}

void toolbar_set_dock_hint(window_t *win, uint8_t hint) {
  toolbar_state_t *tb = toolbar_ensure_state(win);
  if (tb) tb->dock_hint = hint;
}

toolbar_state_t *toolbar_get_state(window_t *win) {
  return window_toolbar_state(win);
}

int toolbar_effective_bsz(window_t const *win) {
  toolbar_state_t *tb = window_toolbar_state((window_t *)win);
  return (tb && tb->btn_size > 0) ? tb->btn_size : get_theme()->toolbar_button_size;
}

int toolbar_effective_padding(window_t const *win) {
  toolbar_state_t *tb = window_toolbar_state((window_t *)win);
  return tb && (tb->style & TOOLBAR_STYLE_COMPACT) ? get_theme()->toolbar_compact_padding
                                                : get_theme()->toolbar_padding;
}

int toolbar_effective_item_height(window_t const *win) {
  toolbar_state_t *tb = window_toolbar_state((window_t *)win);
  int height = toolbar_state_item_height(tb);
  if (tb && tb->orientation == TOOLBAR_VERTICAL && (tb->style & TOOLBAR_STYLE_GRIP))
    height += get_theme()->toolbar_grip_size;
  if (tb && tb->orientation == TOOLBAR_VERTICAL && tb->item_rects) {
    for (int i = 0; i < tb->item_count; i++)
      height = MAX(height, tb->item_rects[i].y + tb->item_rects[i].h -
                   toolbar_effective_padding(win));
  }
  return height;
}

void toolbar_draw_non_client(window_t *win) {
  if (!win || !(win->flags & WINDOW_TOOLBAR)) return;

  toolbar_state_t *tb = toolbar_ensure_state(win);
  window_t *root = get_root_window(win);
  int bsz = toolbar_effective_item_height(win);
  int title_h = toolbar_content_offset(win);
  int total_h = (tb && (tb->style & TOOLBAR_STYLE_WRAPABLE)) ? win->frame.h
                : bsz + 2 * toolbar_effective_padding(win);
  int root_x = window_screen_x(win) - root->frame.x;
  int root_y = window_screen_y(win) - root->frame.y;
  int lift_x, lift_y;
  window_lift_offset(win, &lift_x, &lift_y);
  root_x += lift_x; root_y += lift_y;
  irect16_t tb_rect = {root_x, root_y + title_h, win->frame.w, total_h};

  set_viewport_for_fbo(root);
  set_scissor_fbo(root, R(0, 0, root->frame.w, root->frame.h));
  set_projection(0, 0, root->frame.w, root->frame.h);
  theme_draw(tb && (tb->style & TOOLBAR_STYLE_COMPACT) ? THEME_PART_MENU_BAR : THEME_PART_TOOLBAR,
             tb_rect, CTRL_NORMAL);

  if (tb && (tb->style & TOOLBAR_STYLE_GRIP)) {
    irect16_t grip = tb->orientation == TOOLBAR_VERTICAL
      ? rect_split_top(tb_rect, get_theme()->toolbar_grip_size)
      : rect_split_left(tb_rect, get_theme()->toolbar_grip_size);
    theme_draw(THEME_PART_TOOLBAR_GRIP, grip, CTRL_NORMAL);
  }
  set_viewport(tb_rect);
  if (tb && tb->items && tb->item_rects) {
    for (int i = 0; i < tb->item_count; i++) {
      irect16_t r = tb->item_rects[i];
      set_projection(-r.x, -r.y, win->frame.w - r.x, total_h - r.y);
      draw_toolbar_item_at_origin(win, tb, i);
    }
  }

  for (window_t *tc = tb ? tb->children : NULL; tc; tc = tc->next) {
    set_projection(-tc->frame.x, -tc->frame.y,
                   win->frame.w - tc->frame.x, total_h - tc->frame.y);
    tc->proc(tc, evPaint, 0, NULL);
  }

  set_fullscreen();
}

// TBSTYLE_CHECKGROUP buttons form a radio run: consecutive items that carry the style.
// TBSTATE_* bits of the item with this ident, or -1 when absent (TB_GETSTATE).
int toolbar_item_state(window_t *win, int ident) {
  toolbar_state_t *tb = toolbar_get_state(win);
  for (int i = 0; tb && tb->items && i < tb->item_count; i++)
    if (tb->items[i].ident == ident) return (int)tb->items[i].state;
  fprintf(stderr, "[tb] get state rejected win=%u ident=%d: item unavailable\n", win ? win->id : 0, ident);
  fflush(stderr);
  return -1;
}

bool toolbar_is_button_checked(window_t *win, int ident) {
  int state = toolbar_item_state(win, ident);
  return state >= 0 && (state & TBSTATE_CHECKED);
}

static bool toolbar_set_checked(toolbar_state_t *tb, int i, bool checked) {
  toolbar_item_t *item = &tb->items[i];
  bool changed = ((item->state & TBSTATE_CHECKED) != 0) != checked;
  if (checked && (item->style & TBSTYLE_CHECKGROUP)) {
    int lo = i, hi = i;
    while (lo > 0 && (tb->items[lo - 1].style & TBSTYLE_CHECKGROUP)) lo--;
    while (hi + 1 < tb->item_count && (tb->items[hi + 1].style & TBSTYLE_CHECKGROUP)) hi++;
    for (int j = lo; j <= hi; j++)
      if (j != i && (tb->items[j].state & TBSTATE_CHECKED)) { tb->items[j].state &= ~TBSTATE_CHECKED; changed = true; }
  }
  item->state = checked ? item->state | TBSTATE_CHECKED : item->state & ~TBSTATE_CHECKED;
  return changed;
}

static bool toolbar_set_item_state(window_t *win, toolbar_state_t *tb, int i, uint32_t state) {
  bool changed = false;
  if ((tb->items[i].state ^ state) & TBSTATE_DISABLED) {
    tb->items[i].state ^= TBSTATE_DISABLED;
    changed = true;
    if (state & TBSTATE_DISABLED) {
      if (tb->pressed_item == i) {
        tb->pressed_item = -1;
        if (g_ui_runtime.captured == win->toolbar) set_capture(NULL);
      }
      if (tb->hot_item == i) tb->hot_item = -1;
    }
  }
  return toolbar_set_checked(tb, i, (state & TBSTATE_CHECKED) != 0) || changed;
}

// Apply the fields of `info` named by its mask to item i (TB_SETBUTTONINFO). Strings are copied into
// the toolbar's owned per-item buffers.
static bool toolbar_apply_info(window_t *win, toolbar_state_t *tb, int i, const toolbar_button_info_t *info) {
  toolbar_item_t *it = &tb->items[i];
  bool changed = false, relayout = false;
  if ((info->mask & TBIF_IMAGE) && tb->item_icons) {
    char name[sizeof(tb->item_icons[i])];
    snprintf(name, sizeof(name), "%s", info->icon ? info->icon : "");
    if (strcmp(name, tb->item_icons[i]) != 0) {
      memcpy(tb->item_icons[i], name, strlen(name) + 1);
      it->icon = name[0] ? tb->item_icons[i] : NULL;
      if (it->type == TOOLBAR_ITEM_TEXTEDIT)
        for (window_t *tc = tb->children; tc; tc = tc->next)
          if ((int)tc->id == it->ident) send_message(tc, edSetLeadingIcon, 0, (void *)it->icon);
      changed = true;
    }
  }
  if ((info->mask & TBIF_TOOLTIP) && tb->item_tooltips) {
    char text[sizeof(tb->item_tooltips[i])];
    snprintf(text, sizeof(text), "%s", info->tooltip ? info->tooltip : "");
    if (strcmp(text, tb->item_tooltips[i]) != 0) {
      memcpy(tb->item_tooltips[i], text, strlen(text) + 1);
      changed = true;
    }
    it->tooltip = text[0] ? tb->item_tooltips[i] : NULL;
  }
  if ((info->mask & TBIF_CHECKEDIMAGE) && tb->item_checked_icons) {
    char name[sizeof(tb->item_checked_icons[i])];
    snprintf(name, sizeof(name), "%s", info->checked_icon ? info->checked_icon : "");
    if (strcmp(name, tb->item_checked_icons[i]) != 0) {
      memcpy(tb->item_checked_icons[i], name, strlen(name) + 1);
      changed = true;
    }
    it->checked_icon = name[0] ? tb->item_checked_icons[i] : NULL;
  }
  if ((info->mask & TBIF_COLOR) && it->color != info->color) { it->color = info->color; changed = true; }
  if ((info->mask & TBIF_STYLE) && it->style != info->style) { it->style = info->style; changed = relayout = true; }
  if ((info->mask & TBIF_SIZE) && it->w != info->w) { it->w = info->w; changed = relayout = true; }
  if ((info->mask & TBIF_STATE) && toolbar_set_item_state(win, tb, i, info->state)) changed = true;
  if (relayout) compute_toolbar_item_rects(win, tb);
  if (changed) invalidate_window(win);
  return true;
}

bool toolbar_handle_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evResize: {
      toolbar_state_t *tb = toolbar_get_state(win);
      if (tb && tb->items) compute_toolbar_item_rects(win, tb);
      return true;
    }
    case tbSetItemColor: {
      toolbar_state_t *tb = toolbar_get_state(win);
      for (int i = 0; tb && tb->items && i < tb->item_count; i++) {
        if ((uint32_t)tb->items[i].ident != wparam) continue;
        if (tb->items[i].type != TOOLBAR_ITEM_BUTTON) break;
        toolbar_button_info_t info = {.mask = TBIF_COLOR, .ident = (int)wparam, .color = lparam ? *(const uint32_t *)lparam : 0};
        return toolbar_apply_info(win, tb, i, &info);
      }
      fprintf(stderr, "[tb] set color rejected win=%u ident=%u: button unavailable\n", win->id, wparam);
      fflush(stderr);
      return false;
    }
    case tbSetItemIcon: {
      toolbar_state_t *tb = toolbar_get_state(win);
      for (int i = 0; tb && tb->items && i < tb->item_count; i++) {
        if ((uint32_t)tb->items[i].ident != wparam) continue;
        toolbar_button_info_t info = {.mask = TBIF_IMAGE, .ident = (int)wparam, .icon = lparam};
        return toolbar_apply_info(win, tb, i, &info);
      }
      fprintf(stderr, "[tb] set icon rejected win=%u ident=%u: item unavailable\n", win->id, wparam);
      fflush(stderr);
      return false;
    }
    case tbSetButtonInfo:
    case tbGetButtonInfo: {
      toolbar_state_t *tb = toolbar_get_state(win);
      toolbar_button_info_t *info = lparam;
      if (!info) {
        fprintf(stderr, "[tb] button info rejected win=%u: null info\n", win->id);
        fflush(stderr);
        return false;
      }
      for (int i = 0; tb && tb->items && i < tb->item_count; i++) {
        if (tb->items[i].ident != info->ident) continue;
        if (msg == tbSetButtonInfo) return toolbar_apply_info(win, tb, i, info);
        toolbar_item_t *it = &tb->items[i];
        if (info->mask & TBIF_IMAGE)        info->icon = it->icon;
        if (info->mask & TBIF_STYLE)        info->style = it->style;
        if (info->mask & TBIF_STATE)        info->state = it->state;
        if (info->mask & TBIF_TOOLTIP)      info->tooltip = it->tooltip;
        if (info->mask & TBIF_CHECKEDIMAGE) info->checked_icon = it->checked_icon;
        if (info->mask & TBIF_COLOR)        info->color = it->color;
        if (info->mask & TBIF_SIZE)         info->w = it->w;
        return true;
      }
      fprintf(stderr, "[tb] button info rejected win=%u ident=%d: item unavailable\n", win->id, info->ident);
      fflush(stderr);
      return false;
    }
    case tbGetIdealSize: {
      toolbar_state_t *tb = toolbar_get_state(win);
      isize16_t *size = lparam;
      if (!tb || !size || !tb->item_rects) {
        fprintf(stderr, "[tb] ideal size rejected win=%u state=%p out=%p\n", win->id, (void *)tb, lparam);
        fflush(stderr);
        return false;
      }
      int pad = toolbar_effective_padding(win), w = 2 * pad, h = 2 * pad;
      for (int i = 0; i < tb->item_count; i++) {
        w = MAX(w, tb->item_rects[i].x + tb->item_rects[i].w + pad);
        h = MAX(h, tb->item_rects[i].y + tb->item_rects[i].h + pad);
      }
      *size = (isize16_t){w, h};
      return true;
    }
    case tbGetItemRect: {
      toolbar_state_t *tb = toolbar_get_state(win);
      for (int i = 0; tb && tb->items && tb->item_rects && lparam && i < tb->item_count; i++) {
        if ((uint32_t)tb->items[i].ident != wparam) continue;
        *(irect16_t *)lparam = tb->item_rects[i];
        return true;
      }
      fprintf(stderr, "[tb] item rect rejected win=%u ident=%u: item unavailable\n", win->id, wparam);
      fflush(stderr);
      return false;
    }
    case tbFitItem: {
      toolbar_state_t *tb = toolbar_get_state(win);
      for (int i = 0; tb && tb->items && i < tb->item_count; i++) {
        if ((uint32_t)tb->items[i].ident != wparam) continue;
        for (window_t *tc = tb->children; tc; tc = tc->next) {
          if (tc->id != wparam) continue;
          layout_measure_t measure = {0};
          send_message(tc, evMeasure, 0, &measure);
          if (measure.desired_w > 0) tb->items[i].w = measure.desired_w;
          compute_toolbar_item_rects(win, tb);
          invalidate_window(win);
          return true;
        }
      }
      fprintf(stderr, "[tb] fit rejected win=%u ident=%u: embedded control unavailable\n", win->id, wparam);
      fflush(stderr);
      return false;
    }
    case tbSetItems: {
      toolbar_item_t *merged = NULL;
      if (toolbar_merged_title(win)) {
        int count = (int)wparam;
        if (count < 0 || count > 4096 || (count && !lparam)) {
          fprintf(stderr, "[tb] invalid title toolbar items win=%u count=%d\n", win->id, count);
          fflush(stderr);
          return false;
        }
        bool collapse = (win->parent || !(win->flags & WINDOW_NORESIZE)) && !(win->flags & WINDOW_NOCOLLAPSE);
        bool flex = false;
        toolbar_item_t *input = lparam;
        for (int i = 0; i < count; i++)
          if (input[i].type == TOOLBAR_ITEM_SPACER && (input[i].style & TOOLBAR_ITEM_FLAG_FLEXSPACE)) flex = true;
        int n = count + !flex + !(win->flags & WINDOW_NOCLOSE) + collapse;
        merged = calloc(n, sizeof(*merged));
        if (!merged) {
          fprintf(stderr, "[tb] title toolbar allocation failed win=%u count=%d\n", win->id, n);
          fflush(stderr);
          return false;
        }
        if (count) memcpy(merged, lparam, count * sizeof(*merged)); // the caption shows no title, only the window controls
        int i = count;
        if (!flex) merged[i++] = (toolbar_item_t){.type = TOOLBAR_ITEM_SPACER, .style = TOOLBAR_ITEM_FLAG_FLEXSPACE};
        if (collapse) merged[i++] = (toolbar_item_t){.type = TOOLBAR_ITEM_BUTTON, .ident = TB_WINDOW_COLLAPSE, .tooltip = "Collapse / restore"};
        if (!(win->flags & WINDOW_NOCLOSE)) merged[i++] = (toolbar_item_t){.type = TOOLBAR_ITEM_BUTTON, .ident = TB_WINDOW_CLOSE, .tooltip = "Hide window"};
        wparam = n;
        lparam = merged;
      }
      toolbar_state_t *tb = toolbar_ensure_state(win);
      if (!tb) { free(merged); return true; }

      int pressed_ident = 0;
      bool preserve_pressed = tb->pressed_item >= 0 &&
                              tb->pressed_item < tb->item_count && tb->items;
      if (preserve_pressed)
        pressed_ident = tb->items[tb->pressed_item].ident;

      clear_toolbar_children(win);
      SAFE_DELETE(tb->items, free);
      SAFE_DELETE(tb->item_tooltips, free);
      SAFE_DELETE(tb->item_icons, free);
      SAFE_DELETE(tb->item_checked_icons, free);
      tb->item_count = 0;
      SAFE_DELETE(tb->item_rects, free);
      tb->hot_item = -1;
      tb->pressed_item = -1;

      if ((int)wparam > 0 && lparam) {
        int n = (int)wparam;
        tb->items = malloc((size_t)n * sizeof(toolbar_item_t));
        if (tb->items) {
          memcpy(tb->items, lparam, (size_t)n * sizeof(toolbar_item_t));
          tb->item_count = n;
        }

        tb->item_tooltips = calloc((size_t)n, sizeof(*tb->item_tooltips));
        if (tb->item_tooltips && tb->items) {
          for (int i = 0; i < n; i++) {
            if (tb->items[i].tooltip && tb->items[i].tooltip[0]) {
              strncpy(tb->item_tooltips[i], tb->items[i].tooltip,
                      sizeof(tb->item_tooltips[i]) - 1);
              tb->item_tooltips[i][sizeof(tb->item_tooltips[i]) - 1] = '\0';
              tb->items[i].tooltip = tb->item_tooltips[i];
            } else {
              tb->item_tooltips[i][0] = '\0';
              tb->items[i].tooltip = NULL;
            }
          }
        }

        tb->item_icons = calloc((size_t)n, sizeof(*tb->item_icons));
        if (tb->item_icons && tb->items) {
          for (int i = 0; i < n; i++) {
            if (tb->items[i].icon && tb->items[i].icon[0]) {
              strncpy(tb->item_icons[i], tb->items[i].icon,
                      sizeof(tb->item_icons[i]) - 1);
              tb->item_icons[i][sizeof(tb->item_icons[i]) - 1] = '\0';
              tb->items[i].icon = tb->item_icons[i];
            }
          }
        }

        tb->item_checked_icons = calloc((size_t)n, sizeof(*tb->item_checked_icons));
        for (int i = 0; tb->items && i < n; i++) {
          const char *alt = tb->items[i].checked_icon;
          tb->items[i].checked_icon = NULL;
          if (!alt || !alt[0]) continue;
          if (!tb->item_checked_icons) {
            fprintf(stderr, "[tb] checked icon allocation failed win=%u ident=%d\n", win->id, tb->items[i].ident);
            fflush(stderr);
            break;
          }
          snprintf(tb->item_checked_icons[i], sizeof(tb->item_checked_icons[i]), "%s", alt);
          tb->items[i].checked_icon = tb->item_checked_icons[i];
        }

        compute_toolbar_item_rects(win, tb);

        if (preserve_pressed) {
          for (int i = 0; i < tb->item_count; i++) {
            if (tb->items[i].ident == pressed_ident) {
              tb->pressed_item = i;
              break;
            }
          }
        }

        window_t **tail = &tb->children;
        bool refit = false;
        for (int i = 0; tb->items && i < n && tb->item_rects; i++) {
          toolbar_item_t *item = &tb->items[i];
          if (item->type != TOOLBAR_ITEM_COMBOBOX && item->type != TOOLBAR_ITEM_TEXTEDIT &&
              item->type != TOOLBAR_ITEM_SLIDER && item->type != TOOLBAR_ITEM_SEGMENTED)
            continue;

          const char *cls = item->type == TOOLBAR_ITEM_COMBOBOX ? "ComboBox"
                            : item->type == TOOLBAR_ITEM_SLIDER ? "Slider"
                            : item->type == TOOLBAR_ITEM_SEGMENTED ? "SegmentedControl" : "TextBox";
          irect16_t r = tb->item_rects[i];
          irect16_t rf = {r.x, r.y, r.w, r.h};
          window_t *tc = create_window(item->text ? item->text : "",
                                       WINDOW_NOTITLE | WINDOW_NOFILL |
                                       ((item->type == TOOLBAR_ITEM_SLIDER && tb->orientation == TOOLBAR_VERTICAL) ? SLIDER_VERTICAL : 0),
                                       &rf, win, cls, win->hinstance, NULL);
          if (!tc) {
            fprintf(stderr, "[tb] embedded control allocation failed win=%u ident=%d class=%s\n", win->id, item->ident, cls);
            fflush(stderr);
            continue;
          }

          tc->id = (uint32_t)item->ident;
          tc->frame = r;
          if (item->type == TOOLBAR_ITEM_TEXTEDIT && item->icon)
            send_message(tc, edSetLeadingIcon, 0, (void *)item->icon);
          if (item->type == TOOLBAR_ITEM_SEGMENTED && item->w <= 0) {
            layout_measure_t measure = {0};
            send_message(tc, evMeasure, 0, &measure);
            if (measure.desired_w > 0) { item->w = measure.desired_w; refit = true; }
          }

          window_t *prev = NULL;
          window_t *c = win->children;
          while (c && c != tc) {
            prev = c;
            c = c->next;
          }
          if (c == tc) {
            if (prev)
              prev->next = tc->next;
            else
              win->children = tc->next;
          }

          tc->next = NULL;
          *tail = tc;
          tail = &tc->next;
        }
        if (refit) compute_toolbar_item_rects(win, tb);
      }

      free(merged);
      post_message(win, evRefreshStencil, 0, NULL);
      invalidate_window(win);
      return true;
    }

    case tbSetStrip: {
      toolbar_state_t *tb = toolbar_ensure_state(win);
      if (!tb) return true;
      if (lparam)
        memcpy(&tb->strip, lparam, sizeof(bitmap_strip_t));
      else
        memset(&tb->strip, 0, sizeof(bitmap_strip_t));
      invalidate_window(win);
      return true;
    }

    case tbCheckButton:
    case tbSetState: {
      toolbar_state_t *tb = toolbar_get_state(win);
      bool check = msg == tbCheckButton;
      for (int i = 0; tb && tb->items && i < tb->item_count; i++) {
        if ((uint32_t)tb->items[i].ident != wparam) continue;
        if (check && tb->items[i].type != TOOLBAR_ITEM_BUTTON && tb->items[i].type != TOOLBAR_ITEM_CUSTOM) break;
        uint32_t want = check ? (lparam ? TBSTATE_CHECKED : 0) : (uint32_t)(uintptr_t)lparam;
        bool changed = check ? toolbar_set_checked(tb, i, want != 0)
                             : toolbar_set_item_state(win, tb, i, want);
        if (changed) invalidate_window(win);
        return true;
      }
      fprintf(stderr, "[tb] %s rejected win=%u ident=%u: %s\n", check ? "check" : "set state", win->id, wparam,
              check ? "button unavailable" : "item unavailable");
      fflush(stderr);
      return false;
    }
    case tbEnableItem: {
      toolbar_state_t *tb = toolbar_get_state(win);
      for (int i = 0; tb && i < tb->item_count; i++) {
        if ((uint32_t)tb->items[i].ident != wparam) continue;
        uint32_t state = tb->items[i].state;
        state = lparam ? state & ~TBSTATE_DISABLED : state | TBSTATE_DISABLED;
        if (toolbar_set_item_state(win, tb, i, state)) invalidate_window(win);
        return true;
      }
      fprintf(stderr, "[tb] enable rejected win=%u ident=%u: item unavailable\n", win->id, wparam);
      fflush(stderr);
      return false;
    }

    case tbSetButtonSize: {
      toolbar_state_t *tb = toolbar_ensure_state(win);
      if (!tb) return true;
      int old_btn_size = tb->btn_size;
      int new_btn_size = (int)wparam;
      if (new_btn_size != 0 && new_btn_size < 8) new_btn_size = 8;
      if (old_btn_size != new_btn_size) {
        tb->btn_size = new_btn_size;
        compute_toolbar_item_rects(win, tb);
        post_message(win, evRefreshStencil, 0, NULL);
        invalidate_window(get_root_window(win));
      }
      return true;
    }

    case tbSetColumns: {
      if (wparam < 1 || wparam > 4) {
        fprintf(stderr, "[tb] invalid columns win=%u value=%u\n", win->id, wparam);
        fflush(stderr);
        return false;
      }
      toolbar_state_t *tb = toolbar_ensure_state(win);
      if (!tb) return false;
      if (tb->columns != (int)wparam) {
        tb->columns = (int)wparam;
        compute_toolbar_item_rects(win, tb);
        post_message(win, evRefreshStencil, 0, NULL);
        invalidate_window(get_root_window(win));
      }
      return true;
    }

    case tbSetOrientation: {
      if (wparam != TOOLBAR_HORIZONTAL && wparam != TOOLBAR_VERTICAL) {
        fprintf(stderr, "[tb] invalid orientation win=%u value=%u\n", win->id, wparam);
        fflush(stderr);
        return true;
      }
      toolbar_state_t *tb = toolbar_ensure_state(win);
      if (!tb) return true;
      if (tb->orientation != (toolbar_orientation_t)wparam) {
        tb->orientation = (toolbar_orientation_t)wparam;
        compute_toolbar_item_rects(win, tb);
        post_message(win, evRefreshStencil, 0, NULL);
        invalidate_window(get_root_window(win));
      }
      return true;
    }

    case tbSetStyle:
    case tbModifyStyle: {
      toolbar_state_t *tb = toolbar_ensure_state(win);
      if (!tb) return true;
      uint32_t style = msg == tbSetStyle ? wparam : (tb->style & ~wparam) | ((uint32_t)(uintptr_t)lparam & wparam);
      if (tb->style != style) {
        tb->style = style;
        compute_toolbar_item_rects(win, tb);
        post_message(win, evRefreshStencil, 0, NULL);
        invalidate_window(get_root_window(win));
      }
      return true;
    }

    default:
      return false;
  }
}

bool toolbar_handle_notitle_nc_left_button_up(window_t *win, uint32_t wparam) {
  if (!win || !(win->flags & WINDOW_TOOLBAR) || !(win->flags & WINDOW_NOTITLE))
    return false;

  int sx = (int)(int16_t)LOWORD(wparam);
  int sy = (int)(int16_t)HIWORD(wparam);
  int tb_x = sx - window_screen_x(win);
  int tb_y = sy - window_screen_y(win);

  if (win->toolbar) {
    toolbar_state_t *tb = (toolbar_state_t *)win->toolbar->userdata;
    if (!tb || tb->pressed_item < 0) {
      send_message(win->toolbar, evLeftButtonDown,
                   MAKEDWORD((uint16_t)tb_x, (uint16_t)tb_y), NULL);
    }
    send_message(win->toolbar, evLeftButtonUp,
                 MAKEDWORD((uint16_t)tb_x, (uint16_t)tb_y), NULL);
  }

  invalidate_window(win);
  return true;
}

bool toolbar_dispatch_embedded_mouse(window_t *parent, uint32_t msg, int tb_x, int tb_y) {
  if (!parent) return false;

  toolbar_state_t *tb = toolbar_get_state(parent);
  for (window_t *tc = tb ? tb->children : NULL; tc; tc = tc->next) {
    if (!(tb_x >= tc->frame.x && tb_x < tc->frame.x + tc->frame.w &&
          tb_y >= tc->frame.y && tb_y < tc->frame.y + tc->frame.h)) {
      continue;
    }

    int lx = tb_x - tc->frame.x;
    int ly = tb_y - tc->frame.y;
    if (msg == evLeftButtonDown)
      set_focus(tc);
    send_message(tc, msg, MAKEDWORD((uint16_t)lx, (uint16_t)ly), NULL);
    return true;
  }

  return false;
}

irect16_t layout_docked_toolbars(window_t *owner, irect16_t area) {
  return dock_layout(owner, area);
}

window_t *create_docked_toolbar(window_t *owner, toolbar_dock_t dock, winproc_t proc) {
  if (!owner || !proc || (dock != TOOLBAR_DOCK_TOP && dock != TOOLBAR_DOCK_LEFT)) {
    fprintf(stderr, "[tb] invalid dock owner=%p dock=%d proc=%p\n", (void *)owner, dock, (void *)proc);
    fflush(stderr);
    return NULL;
  }
  irect16_t area = get_client_rect(owner);
  window_t *bar = create_window("", WINDOW_TOOLBAR | WINDOW_NOTITLE | WINDOW_NORESIZE |
                                WINDOW_NOTRAYBUTTON,
                                &area, owner, proc, owner->hinstance, NULL);
  if (!bar) {
    fprintf(stderr, "[tb] dock allocation failed win=%u dock=%d\n", owner->id, dock);
    fflush(stderr);
    return NULL;
  }
  toolbar_state_t *tb = toolbar_get_state(bar);
  send_message(bar, tbSetStyle, (tb ? tb->style : 0) | TOOLBAR_STYLE_GRIP, NULL);
  dock_window(bar, dock == TOOLBAR_DOCK_LEFT ? DOCK_LEFT : DOCK_TOP, DOCK_ALL_EDGES, DOCK_TOOLBAR, 0, 0);
  invalidate_window(owner);
  return bar;
}
