#include <stdlib.h>
#include <string.h>

#include "toolbar.h"
#include "messages.h"
#include "draw.h"
#include "image.h"
#include "svg_icon_loader.h"
#include "theme.h"

int toolbar_item_hit(const toolbar_state_t *tb, int tx, int ty) {
  if (!tb || !tb->item_rects) return -1;
  for (int i = 0; i < tb->item_count; i++) {
    irect16_t r = tb->item_rects[i];
    if (rect_contains_point(r, (ipoint16_t){tx, ty}))
      return i;
  }
  return -1;
}

static int toolbar_state_item_height(const toolbar_state_t *tb) {
  int bsz = (tb && tb->btn_size > 0) ? tb->btn_size : TB_SPACING;
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
  int bsz = (tb->btn_size > 0) ? tb->btn_size : TB_SPACING;
  int item_h = toolbar_state_item_height(tb);
  bool vertical = tb->orientation == TOOLBAR_VERTICAL;
  int grip_h = (vertical && (tb->style & TOOLBAR_STYLE_GRIP)) ? TOOLBAR_GRIP_HEIGHT : 0;
  int grip_w = (!vertical && (tb->style & TOOLBAR_STYLE_GRIP)) ? TOOLBAR_GRIP_WIDTH : 0;
  int cursor = TOOLBAR_BEVEL_WIDTH + TOOLBAR_PADDING + grip_h;
  int x = TOOLBAR_BEVEL_WIDTH + TOOLBAR_PADDING + grip_w;
  int base_y = TOOLBAR_BEVEL_WIDTH + TOOLBAR_PADDING + grip_h;
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
        break;
      case TOOLBAR_ITEM_DROPDOWN:
        w = item->w > 0 ? item->w : bsz;
        if (!item->w && (tb->style & TOOLBAR_STYLE_SHOW_LABELS) && item->text)
          w = MAX(w, text_strwidth(FONT_SMALLEST, item->text) + 8);
        w += DROPDOWN_ARROW_W;
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
      if (item->type == TOOLBAR_ITEM_SEPARATOR || item->type == TOOLBAR_ITEM_SPACER) {
        h = w;
        w = bsz;
      }
      if (parent->toolbar_dock == TOOLBAR_DOCK_LEFT && cursor > base_y &&
          cursor + h + base_y > parent->frame.h) {
        x += column_w + TOOLBAR_SPACING;
        cursor = base_y;
        column_w = 0;
      }
      column_w = MAX(column_w, w);
      y += cursor - base_y;
      cursor += h + TOOLBAR_SPACING;
    }
    if (tb->item_rects)
      tb->item_rects[i] = (irect16_t){x, y, w, h};
    if (!vertical) x += w + TOOLBAR_SPACING;
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

static void draw_toolbar_icon_in_rect(toolbar_state_t *tb, const char *icon_name, irect16_t r, int offset) {
  (void)tb;
  sysicon_resolved_t res;
  if (!sysicon_resolve(icon_name ? icon_name : "missing", &res)) return;
  int ix = r.x + (r.w - res.w) / 2 + offset;
  int iy = r.y + (r.h - res.h) / 2 + offset;
  draw_sprite_region((int)res.tex, R(ix, iy, res.w, res.h),
                     UV_RECT(res.u0, res.v0, res.u1, res.v1), get_sys_color(brToolbarForeground), 0);
}

static void draw_toolbar_item_at_origin(window_t *win, toolbar_state_t *tb, int i) {
  toolbar_item_t *item = &tb->items[i];
  irect16_t r = tb->item_rects[i];
  bool is_pressed = (tb->pressed_item == i);
  bool is_active  = (item->flags & TOOLBAR_BUTTON_FLAG_ACTIVE) != 0;
  bool is_hot     = (tb->hot_item == i);
  theme_t *th = get_theme();

  switch (item->type) {
    case TOOLBAR_ITEM_CUSTOM: {
      toolbar_draw_item_t draw = {R(0, 0, r.w, r.h),
        (is_active ? CTRL_SELECTED : 0) | (is_pressed ? CTRL_PRESSED : 0) |
        (is_hot ? CTRL_HOVER : 0), i};
      send_message(win, tbDrawItem, (uint32_t)item->ident, &draw);
      break;
    }
    case TOOLBAR_ITEM_BUTTON: {
      irect16_t local = {0, 0, r.w, r.h};
      // Derive each flag independently; let the theme decide rendering.
      ctrl_state_t state = CTRL_NORMAL;
      if (is_active)  state |= CTRL_SELECTED;
      if (is_pressed) state |= CTRL_PRESSED;
      if (is_hot)     state |= CTRL_HOVER;
      theme_part_t part = (tb->style & TOOLBAR_STYLE_SHOW_LABELS)
                                     ? THEME_PART_TOOLBAR_LABELED_BUTTON
                                     : THEME_PART_TOOLBAR_BUTTON;
      theme_draw(part, local, state);
      int poff = is_pressed ? th->press_icon_offset : 0;
      const char *icon_name = item->icon ? item->icon : "missing";
      irect16_t icon_rect = local;
      if (tb->style & TOOLBAR_STYLE_SHOW_LABELS)
        icon_rect.h = (tb->btn_size > 0) ? tb->btn_size : TB_SPACING;
      draw_toolbar_icon_in_rect(tb, icon_name, icon_rect, poff);
      if ((tb->style & TOOLBAR_STYLE_SHOW_LABELS) && item->text) {
        int tx = (local.w - text_strwidth(FONT_SMALLEST, item->text)) / 2 + poff;
        int ty = local.h - text_char_height(FONT_SMALLEST) - 2 + poff;
        draw_text(FONT_SMALLEST, item->text, tx, ty, get_sys_color(brToolbarForeground));
      }
      break;
    }
    case TOOLBAR_ITEM_DROPDOWN: {
      int aw = DROPDOWN_ARROW_W;
      irect16_t btn_part = {0, 0, r.w - aw, r.h};
      irect16_t arr_part = {r.w - aw, 0, aw, r.h};
      bool arrow_pressed = is_pressed && tb->pressed_in_arrow;
      bool btn_pressed   = is_pressed && !tb->pressed_in_arrow;

      ctrl_state_t btn_state = CTRL_NORMAL;
      if (is_active)   btn_state |= CTRL_SELECTED;
      if (btn_pressed) btn_state |= CTRL_PRESSED;
      if (is_hot)      btn_state |= CTRL_HOVER;
      theme_draw(THEME_PART_TOOLBAR_SPLIT_BUTTON, btn_part, btn_state);

      int btn_poff = btn_pressed ? th->press_icon_offset : 0;
      const char *icon_name = item->icon ? item->icon : "missing";
      irect16_t icon_rect = btn_part;
      if (tb->style & TOOLBAR_STYLE_SHOW_LABELS)
        icon_rect.h = (tb->btn_size > 0) ? tb->btn_size : TB_SPACING;
      draw_toolbar_icon_in_rect(tb, icon_name, icon_rect, btn_poff);
      if ((tb->style & TOOLBAR_STYLE_SHOW_LABELS) && item->text) {
        int tx = (btn_part.w - text_strwidth(FONT_SMALLEST, item->text)) / 2 + btn_poff;
        int ty = btn_part.h - text_char_height(FONT_SMALLEST) - 2 + btn_poff;
        draw_text(FONT_SMALLEST, item->text, tx, ty, get_sys_color(brToolbarForeground));
      }

      ctrl_state_t arr_state = CTRL_NORMAL;
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
      draw_text(FONT_SMALLEST, item->text ? item->text : "", 2, ty, get_sys_color(brToolbarForeground));
      break;
    }
    case TOOLBAR_ITEM_SLIDER:
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
        SAFE_DELETE(tb->strip_tex, R_DeleteTexture);
        SAFE_DELETE(tb->items, free);
        SAFE_DELETE(tb->item_tooltips, free);
        SAFE_DELETE(tb->item_icons, free);
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

      fprintf(stderr, "[tb] press win=%u index=%d ident=%d\n", win->parent->id, idx, item->ident);
      tb->pressed_item = idx;
      tb->pressed_in_arrow = (item->type == TOOLBAR_ITEM_DROPDOWN) &&
                             (tx >= tb->item_rects[idx].x + tb->item_rects[idx].w - DROPDOWN_ARROW_W);
      invalidate_window(win->parent);
      return true;
    }

    case evLeftButtonUp: {
      if (!tb || tb->pressed_item < 0) return false;
      int tx = (int16_t)LOWORD(wparam);
      int ty = (int16_t)HIWORD(wparam);
      int saved_idx = tb->pressed_item;
      bool saved_in_arrow = tb->pressed_in_arrow;

      tb->pressed_item = -1;
      tb->pressed_in_arrow = false;
      invalidate_window(win->parent);

      if (saved_idx >= tb->item_count) return true;
      int hit = toolbar_item_hit(tb, tx, ty);
      if (hit != saved_idx) return true;

      toolbar_item_t *item = &tb->items[saved_idx];
      fprintf(stderr, "[tb] click win=%u index=%d ident=%d\n", win->parent->id, saved_idx, item->ident);
      window_t *root = get_root_window(win);
      if (item->type == TOOLBAR_ITEM_DROPDOWN && saved_in_arrow) {
        send_message(root, evCommand,
                     MAKEDWORD((uint16_t)item->ident, (uint16_t)tbDropdown), win);
      } else {
        send_message(root, tbButtonClick, (uint32_t)item->ident, win);
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

    case evMouseLeave:
      if (tb && tb->hot_item >= 0) {
        tb->hot_item = -1;
        invalidate_window(win->parent);
      }
      return false;

    case evGetTooltipText: {
      if (!tb || !lparam) return false;
      int idx = toolbar_item_hit(tb, LOWORD(wparam), HIWORD(wparam));
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

toolbar_state_t *toolbar_get_state(window_t *win) {
  return window_toolbar_state(win);
}

int toolbar_effective_bsz(window_t const *win) {
  toolbar_state_t *tb = window_toolbar_state((window_t *)win);
  return (tb && tb->btn_size > 0) ? tb->btn_size : TB_SPACING;
}

int toolbar_effective_item_height(window_t const *win) {
  toolbar_state_t *tb = window_toolbar_state((window_t *)win);
  int height = toolbar_state_item_height(tb);
  if (tb && tb->orientation == TOOLBAR_VERTICAL && (tb->style & TOOLBAR_STYLE_GRIP))
    height += TOOLBAR_GRIP_HEIGHT;
  if (tb && tb->orientation == TOOLBAR_VERTICAL && tb->item_rects) {
    for (int i = 0; i < tb->item_count; i++)
      height = MAX(height, tb->item_rects[i].y + tb->item_rects[i].h -
                   TOOLBAR_PADDING - TOOLBAR_BEVEL_WIDTH);
  }
  return height;
}

void toolbar_draw_non_client(window_t *win) {
  if (!win || !(win->flags & WINDOW_TOOLBAR)) return;

  toolbar_state_t *tb = toolbar_ensure_state(win);
  window_t *root = get_root_window(win);
  int bsz = toolbar_effective_item_height(win);
  int title_h = (win->flags & WINDOW_NOTITLE) ? 0 : window_caption_height(win);
  int total_h = win->toolbar_dock == TOOLBAR_DOCK_LEFT ? win->frame.h
                : bsz + 2 * (TOOLBAR_PADDING + TOOLBAR_BEVEL_WIDTH);
  int root_x = window_screen_x(win) - root->frame.x;
  int root_y = window_screen_y(win) - root->frame.y;
  irect16_t tb_rect = {root_x, root_y + title_h, win->frame.w, total_h};

  set_viewport_for_fbo(root);
  set_projection(0, 0, root->frame.w, root->frame.h);
  theme_draw(THEME_PART_TOOLBAR, tb_rect, CTRL_NORMAL);

  if (tb && (tb->style & TOOLBAR_STYLE_GRIP)) {
    irect16_t grip = tb->orientation == TOOLBAR_VERTICAL
      ? rect_split_top(tb_rect, TOOLBAR_GRIP_HEIGHT)
      : rect_split_left(tb_rect, TOOLBAR_GRIP_WIDTH);
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

bool toolbar_handle_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case tbSetItems: {
      toolbar_state_t *tb = toolbar_ensure_state(win);
      if (!tb) return true;

      clear_toolbar_children(win);
      SAFE_DELETE(tb->items, free);
      SAFE_DELETE(tb->item_tooltips, free);
      SAFE_DELETE(tb->item_icons, free);
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

        compute_toolbar_item_rects(win, tb);

        window_t **tail = &tb->children;
        for (int i = 0; tb->items && i < n && tb->item_rects; i++) {
          toolbar_item_t *item = &tb->items[i];
          if (item->type != TOOLBAR_ITEM_COMBOBOX && item->type != TOOLBAR_ITEM_TEXTEDIT &&
              item->type != TOOLBAR_ITEM_SLIDER)
            continue;

          const char *cls = item->type == TOOLBAR_ITEM_COMBOBOX ? "ComboBox"
                            : item->type == TOOLBAR_ITEM_SLIDER ? "Slider" : "TextBox";
          irect16_t r = tb->item_rects[i];
          irect16_t rf = {r.x, r.y, r.w, r.h};
          window_t *tc = create_window(item->text ? item->text : "",
                                       WINDOW_NOTITLE | WINDOW_NOFILL |
                                       ((item->type == TOOLBAR_ITEM_SLIDER && tb->orientation == TOOLBAR_VERTICAL) ? SLIDER_VERTICAL : 0),
                                       &rf, win, cls, win->hinstance, NULL);
          if (!tc) continue;

          tc->id = (uint32_t)item->ident;
          tc->frame = r;

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
      }

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

    case tbSetActiveButton: {
      toolbar_state_t *tb = toolbar_get_state(win);
      uint32_t ident = wparam;
      fprintf(stderr, "[tb] active win=%u ident=%u count=%d\n", win->id, ident, tb ? tb->item_count : 0);
      if (tb && tb->items) {
        for (int i = 0; i < tb->item_count; i++) {
          bool active = ((uint32_t)tb->items[i].ident == ident);
          if (active)
            tb->items[i].flags |= TOOLBAR_BUTTON_FLAG_ACTIVE;
          else
            tb->items[i].flags &= ~TOOLBAR_BUTTON_FLAG_ACTIVE;
        }
      }
      for (window_t *tc = tb ? tb->children : NULL; tc; tc = tc->next)
        tc->value = (tc->id == ident);
      invalidate_window(win);
      return true;
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

    case tbSetOrientation: {
      if (wparam != TOOLBAR_HORIZONTAL && wparam != TOOLBAR_VERTICAL) {
        fprintf(stderr, "[tb] invalid orientation win=%u value=%u\n", win->id, wparam);
        fflush(stderr);
        return true;
      }
      toolbar_state_t *tb = toolbar_ensure_state(win);
      if (!tb) return true;
      if (tb->orientation != (toolbar_orientation_t)wparam) {
        fprintf(stderr, "[tb] orientation win=%u value=%u\n", win->id, wparam);
        tb->orientation = (toolbar_orientation_t)wparam;
        compute_toolbar_item_rects(win, tb);
        post_message(win, evRefreshStencil, 0, NULL);
        invalidate_window(get_root_window(win));
      }
      return true;
    }

    case tbSetStyle: {
      toolbar_state_t *tb = toolbar_ensure_state(win);
      if (!tb) return true;
      if (tb->style != wparam) {
        tb->style = wparam;
        compute_toolbar_item_rects(win, tb);
        post_message(win, evRefreshStencil, 0, NULL);
        invalidate_window(get_root_window(win));
      }
      return true;
    }

    case tbLoadStrip: {
      const char *path = (const char *)lparam;
      int tile_sz = (int)wparam;
      if (!path || tile_sz <= 0 || !g_ui_runtime.running) return true;

      toolbar_state_t *tb = toolbar_ensure_state(win);
      if (!tb) return true;

      int w = 0;
      int h = 0;
      uint8_t *src = load_image(path, &w, &h);
      if (!src) return true;
      if (w < tile_sz || h < tile_sz || (w % tile_sz) != 0 || (h % tile_sz) != 0) {
        image_free(src);
        return true;
      }

      R_DeleteTexture(tb->strip_tex);
      uint32_t tex = R_CreateTextureRGBA(w, h, src, R_FILTER_NEAREST, R_WRAP_CLAMP);
      image_free(src);

      tb->strip_tex = tex;
      tb->strip.tex = tex;
      tb->strip.icon_w = tile_sz;
      tb->strip.icon_h = tile_sz;
      tb->strip.cols = w / tile_sz;
      tb->strip.sheet_w = w;
      tb->strip.sheet_h = h;
      invalidate_window(win);
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
  int tb_x = sx - win->frame.x;
  int tb_y = sy - win->frame.y;

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
  if (!owner) {
    fprintf(stderr, "[tb] dock layout rejected: missing owner\n");
    fflush(stderr);
    return area;
  }
  for (int dock = TOOLBAR_DOCK_TOP; dock <= TOOLBAR_DOCK_LEFT; dock++) {
    for (window_t *bar = owner->children; bar; bar = bar->next) {
      if (bar->toolbar_dock != dock || !window_has_state(bar, WINDOW_STATE_VISIBLE)) continue;
      irect16_t old_frame = bar->frame;
      int size = titlebar_height(bar);
      if (dock == TOOLBAR_DOCK_LEFT) {
        toolbar_state_t *tb = toolbar_get_state(bar);
        bar->frame.h = area.h;
        compute_toolbar_item_rects(bar, tb);
        size = toolbar_effective_bsz(bar) + 2 * (TOOLBAR_PADDING + TOOLBAR_BEVEL_WIDTH);
        for (int i = 0; tb && tb->item_rects && i < tb->item_count; i++)
          size = MAX(size, tb->item_rects[i].x + tb->item_rects[i].w + TOOLBAR_PADDING + TOOLBAR_BEVEL_WIDTH);
      }
      irect16_t band = dock == TOOLBAR_DOCK_TOP ? rect_split_top(area, MIN(size, area.h))
                                               : rect_split_left(area, MIN(size, area.w));
      area = dock == TOOLBAR_DOCK_TOP ? rect_trim_top(area, band.h) : rect_trim_left(area, band.w);
      if (memcmp(&old_frame, &band, sizeof(band))) {
        bar->frame = band;
        send_message(bar, evResize, 0, NULL);
        invalidate_window(bar);
      }
    }
  }
  return area;
}

window_t *create_docked_toolbar(window_t *owner, toolbar_dock_t dock, winproc_t proc) {
  if (!owner || !proc || (dock != TOOLBAR_DOCK_TOP && dock != TOOLBAR_DOCK_LEFT)) {
    fprintf(stderr, "[tb] invalid dock owner=%p dock=%d proc=%p\n", (void *)owner, dock, (void *)proc);
    fflush(stderr);
    return NULL;
  }
  irect16_t area = get_client_rect(owner);
  window_t *bar = create_window("", WINDOW_TOOLBAR | WINDOW_NOTITLE | WINDOW_NORESIZE |
                                WINDOW_NODRAG | WINDOW_NOTRAYBUTTON,
                                &area, owner, proc, owner->hinstance, NULL);
  if (!bar) {
    fprintf(stderr, "[tb] dock allocation failed win=%u dock=%d\n", owner->id, dock);
    fflush(stderr);
    return NULL;
  }
  bar->toolbar_dock = dock;
  send_message(bar, tbSetOrientation, dock == TOOLBAR_DOCK_LEFT ? TOOLBAR_VERTICAL : TOOLBAR_HORIZONTAL, NULL);
  layout_docked_toolbars(owner, area);
  invalidate_window(owner);
  return bar;
}
