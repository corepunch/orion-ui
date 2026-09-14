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
    if (tx >= r.x && tx < r.x + r.w && ty >= r.y && ty < r.y + r.h)
      return i;
  }
  return -1;
}

static int toolbar_state_item_height(const toolbar_state_t *tb) {
  int bsz = (tb && tb->btn_size > 0) ? tb->btn_size : TB_SPACING;
  return bsz + ((tb && (tb->style & TOOLBAR_STYLE_SHOW_LABELS)) ? text_char_height(FONT_SMALLEST) + 2 : 0);
}

static void compute_toolbar_item_rects(window_t *parent, toolbar_state_t *tb) {
  if (!tb || !tb->items) return;

  free(tb->item_rects);
  tb->item_rects = tb->item_count > 0 ? malloc((size_t)tb->item_count * sizeof(irect16_t)) : NULL;

  int bsz = (tb->btn_size > 0) ? tb->btn_size : TB_SPACING;
  int item_h = toolbar_state_item_height(tb);
  int x = TOOLBAR_BEVEL_WIDTH + TOOLBAR_PADDING;
  int base_y = TOOLBAR_BEVEL_WIDTH + TOOLBAR_PADDING;
  int field_y = base_y + 2;
  int field_h = bsz > 4 ? (bsz - 4) : bsz;

  for (int i = 0; i < tb->item_count; i++) {
    toolbar_item_t *item = &tb->items[i];
    int w = 0;
    int y = base_y;
    int h = item_h;

    switch (item->type) {
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

    if (tb->item_rects)
      tb->item_rects[i] = (irect16_t){x, y, w, h};
    x += w + TOOLBAR_SPACING;
  }

  for (window_t *tc = tb->children; tc; tc = tc->next) {
    for (int i = 0; i < tb->item_count; i++) {
      if ((uint32_t)tb->items[i].ident == tc->id && tb->item_rects) {
        tc->frame = tb->item_rects[i];
        break;
      }
    }
  }

  (void)parent;
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

static void draw_toolbar_item_at_origin(toolbar_state_t *tb, int i) {
  toolbar_item_t *item = &tb->items[i];
  irect16_t r = tb->item_rects[i];
  bool is_pressed = (tb->pressed_item == i);
  bool is_active  = (item->flags & TOOLBAR_BUTTON_FLAG_ACTIVE) != 0;
  bool is_hot     = (tb->hot_item == i);
  theme_t *th = get_theme();

  switch (item->type) {
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
      }
      return true;

    case evLeftButtonDown: {
      if (!tb || !tb->item_rects) return false;
      int tx = (int16_t)LOWORD(wparam);
      int ty = (int16_t)HIWORD(wparam);
      int idx = toolbar_item_hit(tb, tx, ty);
      if (idx < 0) return false;

      toolbar_item_t *item = &tb->items[idx];
      if (item->type != TOOLBAR_ITEM_BUTTON && item->type != TOOLBAR_ITEM_DROPDOWN)
        return false;

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
  return toolbar_state_item_height(window_toolbar_state((window_t *)win));
}

void toolbar_draw_non_client(window_t *win) {
  if (!win || !(win->flags & WINDOW_TOOLBAR)) return;

  toolbar_state_t *tb = toolbar_ensure_state(win);
  window_t *root = get_root_window(win);
  int bsz = toolbar_effective_item_height(win);
  int title_h = (win->flags & WINDOW_NOTITLE) ? 0 : TITLEBAR_HEIGHT;
  int total_h = bsz + 2 * (TOOLBAR_PADDING + TOOLBAR_BEVEL_WIDTH);
  int root_x = window_screen_x(win) - root->frame.x;
  int root_y = window_screen_y(win) - root->frame.y;
  irect16_t tb_rect = {root_x, root_y + title_h, win->frame.w, total_h};
  irect16_t rect = rect_inset(tb_rect, TOOLBAR_BEVEL_WIDTH);

  theme_draw(THEME_PART_TOOLBAR, rect, CTRL_NORMAL);

  set_viewport(tb_rect);
  if (tb && tb->items && tb->item_rects) {
    for (int i = 0; i < tb->item_count; i++) {
      irect16_t r = tb->item_rects[i];
      set_projection(-r.x, -r.y, win->frame.w - r.x, total_h - r.y);
      draw_toolbar_item_at_origin(tb, i);
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
        for (int i = 0; i < n && tb->item_rects; i++) {
          toolbar_item_t *item = &tb->items[i];
          if (item->type != TOOLBAR_ITEM_COMBOBOX && item->type != TOOLBAR_ITEM_TEXTEDIT)
            continue;

          const char *cls = (item->type == TOOLBAR_ITEM_COMBOBOX) ? "ComboBox" : "TextBox";
          irect16_t r = tb->item_rects[i];
          irect16_t rf = {r.x, r.y, r.w, r.h};
          window_t *tc = create_window(item->text ? item->text : "",
                                       WINDOW_NOTITLE | WINDOW_NOFILL,
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
