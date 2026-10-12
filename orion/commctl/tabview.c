// Win32-style tab control. Direct children are pages; each child's title is
// used as its TabBar label and only the selected page is visible.
//
// Icons are set per-tab via tcSetImageStrip (bitmap_strip_t*) + tcSetTabIcon
// (tab_index, icon_index).  When icons are present the tab header height
// grows to MAX(TAB_CONTROL_HEIGHT, icon_h + 4).
//
// TAB_STYLE_SIDEBAR lists the tabs as rows down the left edge (a source list)
// instead of a header strip. The list is an internal child window with a
// built-in vertical scrollbar; it is not a page, so tcGetPage skips it.

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>
#include <orion/user/theme.h>
#include <orion/user/toolbar.h>
#include "commctl.h"
#include <stdio.h>

typedef struct tab_focus_s {
  window_t *page, *focus;
  struct tab_focus_s *next;
} tab_focus_t;

typedef struct {
  int selected, scroll;
  window_t *pressed_page;
  ipoint16_t press_point;
  tab_focus_t *focus;
  uint32_t style;
  bitmap_strip_t strip;
  int *tab_icons;
  int tab_icon_count;
  window_t *actions;
  window_t *list; // sidebar row list, NULL outside TAB_STYLE_SIDEBAR
} tabview_state_t;

#define SIDEBAR_PAD 6
#define TAB_TOP_PAD 1

static bool is_sidebar(const tabview_state_t *st) { return st && (st->style & TAB_STYLE_SIDEBAR); }

// Pages exclude the sidebar list and header toolbar.
static window_t *page_from(const tabview_state_t *st, window_t *c) {
  while (c && st && (c == st->list || c == st->actions)) c = c->next;
  return c;
}
#define FOR_EACH_PAGE(win, st, c) for (window_t *c = page_from(st, (win)->children); c; c = page_from(st, c->next))

static int tab_count(window_t *win) {
  tabview_state_t *st = win ? win->userdata : NULL;
  int n = 0;
  if (win) FOR_EACH_PAGE(win, st, c) n++;
  return n;
}

static window_t *tab_page(window_t *win, int index) {
  tabview_state_t *st = win->userdata;
  int i = 0;
  FOR_EACH_PAGE(win, st, c) if (i++ == index) return c;
  return NULL;
}

static void tab_remember_focus(window_t *win, window_t *focus) {
  tabview_state_t *st = win->userdata;
  if (!st || !(st->style & TAB_STYLE_RESTORE_FOCUS)) return;
  window_t *page = focus;
  while (page && page->parent != win) page = page->parent;
  if (!page || page == st->list || page == st->actions || page != tab_page(win, st->selected)) return;
  tab_focus_t *entry = st->focus;
  while (entry && entry->page != page) entry = entry->next;
  if (!entry) {
    entry = calloc(1, sizeof(*entry));
    if (!entry) {
      fprintf(stderr, "[tv] focus allocation failed win=%u page=%u\n", win->id, page->id);
      fflush(stderr); return;
    }
    entry->page = page; entry->next = st->focus; st->focus = entry;
  }
  entry->focus = focus;
}

static bool tab_focusable(window_t *page, window_t *focus) {
  if (!focus || !is_window(focus) || (focus->flags & WINDOW_NOTABSTOP)) return false;
  for (window_t *p = focus; p; p = p->parent) {
    if (!window_has_state(p, WINDOW_STATE_VISIBLE) || window_has_state(p, WINDOW_STATE_DISABLED)) return false;
    if (p == page) return true;
  }
  return false;
}

static window_t *tab_first_focus(window_t *page) {
  if (!window_has_state(page, WINDOW_STATE_VISIBLE) || window_has_state(page, WINDOW_STATE_DISABLED)) return NULL;
  for (window_t *c = page->children; c; c = c->next) {
    window_t *focus = tab_first_focus(c);
    if (focus) return focus;
  }
  return !(page->flags & WINDOW_NOTABSTOP) ? page : NULL;
}

static void tab_restore_focus(window_t *win) {
  tabview_state_t *st = win->userdata;
  if (!st || !(st->style & TAB_STYLE_RESTORE_FOCUS)) return;
  window_t *page = tab_page(win, st->selected), *focus = NULL;
  if (!page) return;
  for (tab_focus_t *entry = st->focus; entry; entry = entry->next)
    if (entry->page == page) focus = entry->focus;
  if (!tab_focusable(page, focus)) focus = tab_first_focus(page);
  set_focus(focus ? focus : win);
}

static void tab_forget_window(tabview_state_t *st, window_t *win) {
  for (tab_focus_t **p = &st->focus; *p;) {
    tab_focus_t *entry = *p;
    if (entry->page == win) { *p = entry->next; free(entry); continue; }
    if (entry->focus == win) entry->focus = NULL;
    p = &entry->next;
  }
}

static int tab_header_height(tabview_state_t *st) {
  return (st && st->strip.cols > 0) ? MAX(TAB_CONTROL_HEIGHT, st->strip.icon_h + 4) : TAB_CONTROL_HEIGHT;
}

static bool tab_has_icon(tabview_state_t *st, int idx) {
  return st && st->tab_icons && idx >= 0 && idx < st->tab_icon_count
         && st->tab_icons[idx] >= 0 && st->strip.cols > 0;
}

static int tab_icon_gap(tabview_state_t *st) {
  (void)st; return 3;
}

static int tab_width(window_t *page, tabview_state_t *st, int idx) {
  if (st && (st->style & TAB_STYLE_ICONS_ONLY))
    return tab_has_icon(st, idx) ? st->strip.icon_w + 8 : TAB_CONTROL_HEIGHT;
  int w = MAX(get_theme()->tab_min_width, strwidth(page ? page->title : "") + 2 * get_theme()->tab_padding);
  if (tab_has_icon(st, idx)) w += st->strip.icon_w + tab_icon_gap(st);
  return w;
}

// Width of the row column; the list window adds its scrollbar when one shows.
static int sidebar_rows_width(window_t *win, tabview_state_t *st) {
  int w = 96, i = 0;
  FOR_EACH_PAGE(win, st, c) {
    int iw = tab_has_icon(st, i) ? st->strip.icon_w + tab_icon_gap(st) : 0;
    w = MAX(w, strwidth(c->title) + iw + 4 * SIDEBAR_PAD);
    i++;
  }
  return w;
}

static int sidebar_width(window_t *win, tabview_state_t *st) {
  bool bar = st->list && st->list->vscroll.visible && !get_theme()->scrollbar_overlay;
  return sidebar_rows_width(win, st) + (bar ? SCROLLBAR_WIDTH : 0);
}

// Row rectangle in the list's content space.
static irect16_t sidebar_row(window_t *win, tabview_state_t *st, int idx) {
  return R(SIDEBAR_PAD, SIDEBAR_PAD + idx * TAB_SIDEBAR_ROW_HEIGHT,
           sidebar_rows_width(win, st) - 2 * SIDEBAR_PAD, TAB_SIDEBAR_ROW_HEIGHT);
}

static void sidebar_sync_scroll(window_t *win, tabview_state_t *st, int pos) {
  int content_h = tab_count(win) * TAB_SIDEBAR_ROW_HEIGHT + 2 * SIDEBAR_PAD;
  scroll_info_t si = { .fMask = SIF_RANGE | SIF_PAGE | SIF_POS, .nMin = 0, .nMax = content_h,
                       .nPage = get_client_rect(st->list).h, .nPos = pos };
  set_scroll_info(st->list, SB_VERT, &si, false);
}

static void sidebar_reveal(window_t *win, tabview_state_t *st, int idx) {
  irect16_t r = sidebar_row(win, st, idx);
  int pos = get_scroll_pos(st->list, SB_VERT), h = get_client_rect(st->list).h;
  if (r.y - SIDEBAR_PAD < pos) pos = r.y - SIDEBAR_PAD;
  else if (r.y + r.h + SIDEBAR_PAD > pos + h) pos = r.y + r.h + SIDEBAR_PAD - h;
  sidebar_sync_scroll(win, st, pos);
  invalidate_window(st->list);
}

static void draw_tab_icon(tabview_state_t *st, int idx, int x, int y, int h) {
  if (!tab_has_icon(st, idx)) return;
  int ic = st->tab_icons[idx];
  int col = ic % st->strip.cols;
  int row = ic / st->strip.cols;
  float u0 = (float)(col * st->strip.icon_w) / (float)st->strip.sheet_w;
  float v0 = (float)(row * st->strip.icon_h) / (float)st->strip.sheet_h;
  float u1 = u0 + (float)st->strip.icon_w / (float)st->strip.sheet_w;
  float v1 = v0 + (float)st->strip.icon_h / (float)st->strip.sheet_h;
  int iy = y + (h - st->strip.icon_h) / 2;
  draw_sprite_region((int)st->strip.tex, R(x, iy, st->strip.icon_w, st->strip.icon_h),
                     UV_RECT(u0, v0, u1, v1), 0xFFFFFFFF, 0);
}

static void draw_tab_item(window_t *page, int x, bool selected, tabview_state_t *st, int idx) {
  int w = tab_width(page, st, idx);
  int th = tab_header_height(st);
  // 1px of tab-bar chrome above the tabs, so a face does not meet the panel above.
  // Selected tab stays flush with the pane. Inactive tabs keep a 2px shelf above it.
  int y = (selected ? 0 : 2) + TAB_TOP_PAD;
  int h = selected ? th - y : th - y - 2;
  ctrl_state_t state = selected ? CTRL_SELECTED : CTRL_NORMAL;
  theme_draw(THEME_PART_TAB, R(x, y, w, h), state);
  bool has_icon = tab_has_icon(st, idx);
  if (st->style & TAB_STYLE_ICONS_ONLY) {
    draw_tab_icon(st, idx, x + (w - st->strip.icon_w) / 2, y, h);
    return;
  }
  int sw = strwidth(page->title), iw = has_icon ? st->strip.icon_w + tab_icon_gap(st) : 0;
  int content_w = iw + sw;
  int cx = x + (w - content_w) / 2;
  if (has_icon) { draw_tab_icon(st, idx, cx, y, h); cx += iw; }
  draw_text_small(page->title, cx, y + (h - CHAR_HEIGHT) / 2,
                  theme_foreground(THEME_PART_TAB, state));
}

static irect16_t tab_page_rect(window_t *win, tabview_state_t *st, irect16_t r) {
  if (is_sidebar(st)) return rect_trim_left(r, sidebar_width(win, st));
  irect16_t page_rect = rect_trim_top(r, tab_header_height(st));
  int frame = get_theme()->tab_pane_frame;
  page_rect.x += frame; page_rect.w = MAX(0, page_rect.w - 2 * frame);
  page_rect.h = MAX(0, page_rect.h - frame);
  return page_rect;
}

static int tab_available_width(window_t *win, tabview_state_t *st) {
  return st->actions && !is_sidebar(st) ? st->actions->frame.x : get_client_rect(win).w;
}

static result_t tab_actions_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  if (msg == evCreate || msg == evDestroy) return true;
  if (msg == evPaint) return false;
  if (msg != evCommand) return false;
  window_t *tabs = win->parent;
  tab_action_t action = {tabs, lparam, wparam};
  return tabs->parent ? send_message(tabs->parent, evCommand, MAKEDWORD(tabs->id, tcnAction), &action) : false;
}

static void tab_arrange(window_t *win) {
  tabview_state_t *st = (tabview_state_t *)win->userdata;
  if (!st) return;
  if (st->actions) {
    show_window(st->actions, !is_sidebar(st));
    int height = tab_header_height(st);
    send_message(st->actions, tbSetButtonSize, MAX(8, height - 2 * toolbar_effective_padding(st->actions)), NULL);
    isize16_t size = {0};
    send_message(st->actions, tbGetIdealSize, 0, &size);
    irect16_t header = rect_split_top(get_client_rect(win), height);
    st->actions->frame = rect_split_right(header, MIN(header.w, size.w));
    send_message(st->actions, evResize, 0, NULL);
  }
  int count = tab_count(win);
  if (st->selected >= count) st->selected = MAX(0, count - 1);
  irect16_t page_rect = tab_page_rect(win, st, get_client_rect(win));
  if (is_sidebar(st) && st->list) {
    st->list->frame = rect_split_left(get_client_rect(win), sidebar_width(win, st));
    sidebar_sync_scroll(win, st, get_scroll_pos(st->list, SB_VERT));
    st->list->frame.w = sidebar_width(win, st); // the scrollbar may have just appeared
    page_rect = tab_page_rect(win, st, get_client_rect(win));
  }
  if (!is_sidebar(st)) {
    int x = 2, selected_x = 2, selected_w = 0, idx = 0, width = tab_available_width(win, st);
    FOR_EACH_PAGE(win, st, c) {
      int w = tab_width(c, st, idx);
      if (idx++ == st->selected) { selected_x = x; selected_w = w; }
      x += w + 1;
    }
    if (selected_x - st->scroll < 2) st->scroll = selected_x - 2;
    else if (selected_x + selected_w - st->scroll > width)
      st->scroll = selected_x + selected_w - width;
    st->scroll = MAX(0, MIN(st->scroll, MAX(0, x - width)));
  }
  int i = 0;
  FOR_EACH_PAGE(win, st, c) {
    bool visible = i++ == st->selected;
    window_set_state(c, WINDOW_STATE_VISIBLE, visible);
    c->frame = page_rect;
    if (visible) { layout_arrange_t a = {page_rect}; send_message(c, evArrange, 0, &a); }
  }
}

static bool tab_select(window_t *win, int index, bool notify) {
  tabview_state_t *st = (tabview_state_t *)win->userdata;
  int count = tab_count(win);
  if (!st) {
    fprintf(stderr, "[tv] select rejected win=%u reason=no_state index=%d count=%d\n",
            win ? (unsigned)win->id : 0, index, count);
    fflush(stderr);
    return false;
  }
  if (index < 0 || index >= count) {
    fprintf(stderr, "[tv] select rejected win=%u index=%d count=%d selected=%d\n",
            (unsigned)win->id, index, count, st->selected);
    fflush(stderr);
    return false;
  }
  tab_remember_focus(win, g_ui_runtime.focused);
  if (index == st->selected) { tab_arrange(win); tab_restore_focus(win); return false; }
  st->selected = index;
  if (is_sidebar(st) && st->list) sidebar_reveal(win, st, index);
  tab_arrange(win);
  tab_restore_focus(win);
  invalidate_window(win);
  if (notify && win->parent)
    send_message(win->parent, evCommand, MAKEDWORD((uint16_t)win->id, tcnSelChange), win);
  return true;
}

// The sidebar row list: paints the tab rows, scrolls through the framework,
// and selects on its owner tabview.
static result_t win_tabview_list(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  window_t *tabs = win->parent;
  tabview_state_t *st = tabs ? tabs->userdata : NULL;
  if (!st) return false;
  switch (msg) {
    case evPaint: {
      int scroll = get_scroll_pos(win, SB_VERT), h = get_client_rect(win).h, i = 0;
      theme_draw(THEME_PART_SIDEBAR, get_client_rect(win), CTRL_NORMAL);
      FOR_EACH_PAGE(tabs, st, c) {
        irect16_t r = rect_offset(sidebar_row(tabs, st, i), 0, -scroll);
        if (r.y + r.h >= 0 && r.y <= h) {
          ctrl_state_t state = i == st->selected ? CTRL_SELECTED : CTRL_NORMAL;
          // Same capsule as a menu item; the theme insets it, so widen the rect to land on the row.
          if (i == st->selected) theme_draw(THEME_PART_MENU_ITEM, rect_inset_xy(r, -MENU_CAPSULE_INSET, 0), state);
          int x = r.x + SIDEBAR_PAD;
          if (tab_has_icon(st, i)) { draw_tab_icon(st, i, x, r.y, r.h); x += st->strip.icon_w + tab_icon_gap(st); }
          draw_text_ellipsized(FONT_SYSTEM, c->title, x, r.y + (r.h - text_char_height(FONT_SYSTEM)) / 2,
                               r.x + r.w - SIDEBAR_PAD - x, theme_foreground(THEME_PART_MENU_ITEM, state));
        }
        i++;
      }
      return true;
    }
    case evLeftButtonDown: {
      ipoint16_t pt = { (int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam) };
      tab_remember_focus(tabs, g_ui_runtime.focused);
      set_focus(tabs);
      for (int i = 0, n = tab_count(tabs); i < n; i++)
        if (rect_contains_point(sidebar_row(tabs, st, i), pt)) { tab_select(tabs, i, true); break; }
      return true;
    }
    case evVScroll: invalidate_window(win); return true;
    default: return false;
  }
}

static void sidebar_set(window_t *win, tabview_state_t *st, bool on) {
  if (on == (st->list != NULL)) return;
  if (!on) { window_t *list = st->list; st->list = NULL; destroy_window(list); return; }
  st->list = create_window("", WINDOW_NOTITLE | WINDOW_VSCROLL | WINDOW_NOTABSTOP, MAKERECT(0, 0, 1, 1),
                           win, win_tabview_list, win->hinstance, NULL);
  if (!st->list) {
    fprintf(stderr, "[tv] sidebar list creation failed win=%u\n", (unsigned)win->id);
    fflush(stderr);
  }
}

static bool tab_paint(window_t *win) {
  tabview_state_t *st = (tabview_state_t *)win->userdata;
  if (is_sidebar(st)) return false; // the list and the selected page paint as children
  irect16_t cr = get_client_rect(win);
  int th = st ? tab_header_height(st) : TAB_CONTROL_HEIGHT;
  theme_draw(THEME_PART_TAB_BAR, rect_split_top(cr, th), CTRL_NORMAL);
  if (!st) return true;

  set_clip_rect(win, R(0, 0, tab_available_width(win, st), th));
  int selected_x = 2 - st->scroll;
  window_t *selected = NULL;
  int x = 2 - st->scroll, i = 0;
  FOR_EACH_PAGE(win, st, c) {
    if (i == st->selected) { selected = c; selected_x = x; }
    else draw_tab_item(c, x, false, st, i);
    x += tab_width(c, st, i) + 1;
    i++;
  }

  if (selected) draw_tab_item(selected, selected_x, true, st, st->selected);
  set_clip_rect(win, cr);
  irect16_t page = rect_trim_top(cr, th);
  theme_draw(THEME_PART_TAB_PANE, page, CTRL_NORMAL);
  return false; // The window system paints the selected page and the toolbar children.
}

static void tab_cleanup_icons(tabview_state_t *st) {
  if (st->tab_icons) { free(st->tab_icons); st->tab_icons = NULL; }
  st->tab_icon_count = 0;
  memset(&st->strip, 0, sizeof(st->strip));
}

static bool tab_set_tab_icon(tabview_state_t *st, int count, int idx, int ico) {
  if (idx < 0 || idx >= count) return false;
  if (!st->tab_icons) {
    st->tab_icon_count = count;
    st->tab_icons = calloc((size_t)count, sizeof(int));
    if (!st->tab_icons) return false;
    for (int j = 0; j < count; j++) st->tab_icons[j] = -1;
  } else if (idx >= st->tab_icon_count) {
    int old = st->tab_icon_count;
    st->tab_icon_count = idx + 1;
    int *icons = realloc(st->tab_icons, (size_t)st->tab_icon_count * sizeof(int));
    if (!icons) return false;
    st->tab_icons = icons;
    for (int j = old; j < st->tab_icon_count; j++) st->tab_icons[j] = -1;
  }
  st->tab_icons[idx] = ico;
  return true;
}

bool tabview_move_page(window_t *source, int index, window_t *target, int before) {
  bool valid = source && target && is_window(source) && is_window(target) &&
               source->proc == win_tabview && target->proc == win_tabview;
  tabview_state_t *src = valid ? source->userdata : NULL, *dst = valid ? target->userdata : NULL;
  int count = valid ? tab_count(source) : 0, target_count = valid ? tab_count(target) : 0;
  if (before == -1) before = target_count;
  if (!src || !dst || index < 0 || index >= count || before < 0 || before > target_count ||
      get_root_window(source) != get_root_window(target)) {
    fprintf(stderr, "[tv] move_page rejected source=%p target=%p index=%d before=%d\n",
            (void *)source, (void *)target, index, before);
    fflush(stderr); return false;
  }
  window_t *page = tab_page(source, index);
  for (window_t *p = target; p; p = p->parent) if (p == page) {
    fprintf(stderr, "[tv] move_page rejected win=%u reason=cycle\n", source->id);
    fflush(stderr); return false;
  }
  if (source == target && (before == index || before == index + 1)) {
    tab_select(source, index, false); return true;
  }
  int icon = index < src->tab_icon_count ? src->tab_icons[index] : -1;
  if (source != target && target_count && icon >= 0 && memcmp(&src->strip, &dst->strip, sizeof(src->strip))) {
    fprintf(stderr, "[tv] move_page rejected win=%u target=%u reason=different_icon_strips\n", source->id, target->id);
    fflush(stderr); return false;
  }
  int new_count = target_count + (source != target);
  int *icons = malloc((size_t)new_count * sizeof(*icons));
  if (!icons) {
    fprintf(stderr, "[tv] move_page allocation failed win=%u count=%d\n", source->id, new_count);
    fflush(stderr); return false;
  }
  int n = 0;
  for (int i = 0; i <= target_count; i++) {
    if (i == before) icons[n++] = icon;
    if (i == target_count || (source == target && i == index)) continue;
    icons[n++] = i < dst->tab_icon_count ? dst->tab_icons[i] : -1;
  }
  tab_remember_focus(source, g_ui_runtime.focused);
  tab_remember_focus(target, g_ui_runtime.focused);
  window_t *selected = tab_page(source, src->selected);
  tab_focus_t *memory = NULL;
  for (tab_focus_t **p = &src->focus; *p; p = &(*p)->next)
    if ((*p)->page == page) { memory = *p; *p = memory->next; break; }
  if (!window_reparent(page, target, before < target_count ? tab_page(target, before) : NULL)) {
    if (memory) { memory->next = src->focus; src->focus = memory; }
    free(icons); return false;
  }
  if (memory) { tab_forget_window(dst, page); memory->next = dst->focus; dst->focus = memory; }
  if (source != target) {
    if (index < src->tab_icon_count) {
      memmove(src->tab_icons + index, src->tab_icons + index + 1, (size_t)(src->tab_icon_count - index - 1) * sizeof(int));
      src->tab_icon_count--;
    }
    if (index < src->selected) src->selected--;
    if (selected == page) src->selected = MIN(index, count - 2);
    src->selected = MAX(0, src->selected);
    tab_arrange(source); invalidate_window(source);
  }
  if (source != target && !target_count) dst->strip = src->strip;
  free(dst->tab_icons); dst->tab_icons = icons; dst->tab_icon_count = new_count;
  dst->selected = before - (source == target && index < before);
  tab_arrange(target); tab_restore_focus(target); invalidate_window(target);
  if (source->parent) send_message(source->parent, evCommand, MAKEDWORD(source->id, tcnSelChange), source);
  if (source != target && target->parent) send_message(target->parent, evCommand, MAKEDWORD(target->id, tcnSelChange), target);
  return true;
}

result_t win_tabview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  tabview_state_t *st = (tabview_state_t *)win->userdata;
  switch (msg) {
    case evCreate:
      st = allocate_window_data(win, sizeof(*st));
      if (!st) {
        fprintf(stderr, "[tv] create failed win=%u reason=state_allocation\n",
                win ? (unsigned)win->id : 0);
        fflush(stderr);
        return false;
      }
      st->selected = 0;
      return true;
    case evDescendantFocus:
      tab_remember_focus(win, lparam); return true;
    case evDescendantDestroy: case evDescendantDetach:
      if (st) {
        tab_forget_window(st, lparam);
        if (st->pressed_page == lparam) {
          st->pressed_page = NULL;
          if (g_ui_runtime.captured == win) set_capture(NULL);
        }
      }
      return true;
    case evSetFocus:
      if (g_ui_runtime.focused == win) tab_restore_focus(win);
      return true;
    case evMeasure: {
      layout_measure_t *m = (layout_measure_t *)lparam;
      if (m) { m->desired_w = 200; m->desired_h = 200; }
      return true;
    }
    case evArrange: {
      layout_arrange_t *a = (layout_arrange_t *)lparam;
      if (a) win->frame = a->rect;
      tab_arrange(win);
      return true;
    }
    case evThemeChanged: case evResize: tab_arrange(win); return true;
    case evPaint: return tab_paint(win);
    case evLeftButtonDown: {
      if (!st) {
        fprintf(stderr, "[tv] mousedown rejected win=%u reason=no_state\n",
                win ? (unsigned)win->id : 0);
        fflush(stderr);
        return false;
      }
      int mx = (int16_t)LOWORD(wparam), my = (int16_t)HIWORD(wparam);
      if (is_sidebar(st)) return false;
      int th = tab_header_height(st);
      if (my < 0 || my >= th || mx < 0 || mx >= tab_available_width(win, st)) return false;
      int left = 2 - st->scroll, i = 0;
      FOR_EACH_PAGE(win, st, c) {
        int w = tab_width(c, st, i);
        if (mx >= left && mx < left + w) {
          tab_remember_focus(win, g_ui_runtime.focused); set_focus(win); tab_select(win, i, true);
          if (st->style & TAB_STYLE_DRAG_TABS) {
            st->pressed_page = c; st->press_point = (ipoint16_t){mx, my}; set_capture(win);
          }
          return true;
        }
        left += w + 1;
        i++;
      }
      return true;
    }
    case evMouseMove: {
      if (!st || !st->pressed_page || g_ui_runtime.captured != win) return false;
      ipoint16_t pt = {(int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)};
      if (abs(pt.x - st->press_point.x) + abs(pt.y - st->press_point.y) < 6) return true;
      tab_drag_t drag = {st->pressed_page, pt};
      st->pressed_page = NULL;
      if (win->parent) send_message(win->parent, evCommand, MAKEDWORD(win->id, tcnBeginDrag), &drag);
      if (g_ui_runtime.captured == win) set_capture(NULL);
      return true;
    }
    case evPointerCancel: case evLeftButtonUp:
      if (!st || !st->pressed_page) return false;
      st->pressed_page = NULL;
      if (g_ui_runtime.captured == win) set_capture(NULL);
      return true;
    case evKeyDown:
      if (!st) {
        fprintf(stderr, "[tv] keydown rejected win=%u reason=no_state key=%u\n",
                win ? (unsigned)win->id : 0, (unsigned)wparam);
        fflush(stderr);
        return false;
      }
      if (wparam == (is_sidebar(st) ? AX_KEY_UPARROW : AX_KEY_LEFTARROW))    return tab_select(win, st->selected - 1, true);
      if (wparam == (is_sidebar(st) ? AX_KEY_DOWNARROW : AX_KEY_RIGHTARROW)) return tab_select(win, st->selected + 1, true);
      return false;
    case tcGetToolbar: return st ? (result_t)st->actions : 0;
    case tcSetToolbar: {
      if (!st || wparam > 4096 || (wparam && !lparam)) {
        fprintf(stderr, "[tv] toolbar rejected win=%u count=%u\n", win->id, wparam);
        fflush(stderr); return false;
      }
      if (!wparam) {
        if (st->actions) destroy_window(st->actions);
        st->actions = NULL;
      } else {
        if (!st->actions) st->actions = create_window("", WINDOW_NOTITLE | WINDOW_TOOLBAR | WINDOW_NODRAG |
          WINDOW_NOACTIVATE | WINDOW_NOTABSTOP, MAKERECT(0, 0, 1, 1), win, tab_actions_proc, win->hinstance, NULL);
        if (!st->actions) {
          fprintf(stderr, "[tv] toolbar allocation failed win=%u\n", win->id);
          fflush(stderr); return false;
        }
        send_message(st->actions, tbSetItems, wparam, lparam);
      }
      tab_arrange(win); invalidate_window(win); return true;
    }
    case tcGetStyle: return st ? st->style : 0;
    case tcGetSelection: return st ? st->selected : -1;
    case tcSetSelection: return tab_select(win, (int)wparam, false) || (st && st->selected == (int)wparam);
    case tcGetCount: return tab_count(win);
    case tcGetPage: {
      window_t *page = tab_page(win, (int)wparam);
      if (!page) {
        fprintf(stderr, "[tv] get_page rejected win=%u index=%d count=%d\n",
                (unsigned)win->id, (int)wparam, tab_count(win));
        fflush(stderr);
      }
      return (result_t)(intptr_t)page;
    }
    case tcGetTabRect: {
      int index = (int)wparam, x = 2 - (st ? st->scroll : 0), i = 0;
      if (!st || !lparam || index < 0 || index >= tab_count(win)) {
        fprintf(stderr, "[tv] get_tab_rect rejected win=%u index=%d\n", win->id, index);
        fflush(stderr); return false;
      }
      if (is_sidebar(st)) {
        *(irect16_t *)lparam = rect_offset(sidebar_row(win, st, index), 0, -get_scroll_pos(st->list, SB_VERT));
        return true;
      }
      FOR_EACH_PAGE(win, st, c) {
        int width = tab_width(c, st, i);
        if (i++ == index) { *(irect16_t *)lparam = R(x, 0, width, tab_header_height(st)); return true; }
        x += width + 1;
      }
      return false;
    }
    case tcMovePage: {
      tab_move_t *move = lparam;
      if (!move) {
        fprintf(stderr, "[tv] move_page rejected win=%u index=%u reason=no_parameters\n", win->id, wparam);
        fflush(stderr); return false;
      }
      return tabview_move_page(win, (int)wparam, move->target, move->before);
    }
    case tcDeletePage: {
      int index = (int)wparam;
      window_t *page = st ? tab_page(win, index) : NULL;
      if (!page) {
        fprintf(stderr, "[tv] delete_page rejected win=%u index=%d count=%d\n", win->id, index, tab_count(win));
        fflush(stderr); return false;
      }
      bool had_focus = false;
      for (window_t *p = g_ui_runtime.focused; p; p = p->parent) had_focus |= p == win;
      if (index < st->selected) st->selected--;
      if (index < st->tab_icon_count) {
        memmove(st->tab_icons + index, st->tab_icons + index + 1,
                (size_t)(st->tab_icon_count - index - 1) * sizeof(int));
        st->tab_icon_count--;
      }
      destroy_window(page);
      tab_arrange(win);
      if (had_focus) tab_restore_focus(win);
      invalidate_window(win);
      if (win->parent) send_message(win->parent, evCommand, MAKEDWORD(win->id, tcnSelChange), win);
      return true;
    }
    case tcSetStyle: {
      const uint32_t known = TAB_STYLE_ICONS_ONLY | TAB_STYLE_SIDEBAR | TAB_STYLE_RESTORE_FOCUS | TAB_STYLE_DRAG_TABS;
      if (!st || (wparam & ~known)) {
        fprintf(stderr, "[tv] set_style rejected win=%u style=0x%x reason=%s\n",
                win ? (unsigned)win->id : 0, (unsigned)wparam,
                st ? "unknown_flags" : "no_state");
        fflush(stderr);
        return false;
      }
      st->style = wparam;
      sidebar_set(win, st, is_sidebar(st));
      tab_arrange(win); invalidate_window(win);
      return true;
    }
    case tcSetImageStrip: {
      if (!st || !lparam) {
        fprintf(stderr, "[tv] set_image_strip rejected win=%u reason=%s\n",
                win ? (unsigned)win->id : 0, st ? "null_strip" : "no_state");
        fflush(stderr);
        return false;
      }
      memcpy(&st->strip, lparam, sizeof(bitmap_strip_t));
      tab_arrange(win); invalidate_window(win);
      return true;
    }
    case tcSetTabIcon: {
      if (!st) {
        fprintf(stderr, "[tv] set_tab_icon rejected win=%u reason=no_state index=%u\n",
                win ? (unsigned)win->id : 0, (unsigned)wparam);
        fflush(stderr);
        return false;
      }
      int idx = (int)wparam, ico = (int)(intptr_t)lparam;
      int count = tab_count(win);
      if (idx < 0 || idx >= count) {
        fprintf(stderr, "[tv] set_tab_icon rejected win=%u index=%d count=%d icon=%d\n",
                (unsigned)win->id, idx, count, ico);
        fflush(stderr);
        return false;
      }
      if (!tab_set_tab_icon(st, count, idx, ico)) {
        fprintf(stderr, "[tv] set_tab_icon failed win=%u index=%d count=%d icon=%d reason=allocation\n",
                (unsigned)win->id, idx, count, ico);
        fflush(stderr);
        return false;
      }
      tab_arrange(win); invalidate_window(win);
      return true;
    }
    case tcAdjustRect:
      if (!st || !lparam) {
        fprintf(stderr, "[tv] adjust_rect rejected win=%u reason=%s\n",
                win ? (unsigned)win->id : 0, st ? "null_rect" : "no_state");
        fflush(stderr);
        return false;
      }
      *(irect16_t *)lparam = tab_page_rect(win, st, *(irect16_t *)lparam);
      return true;
    case evParentNotify: return win->parent ? send_message(win->parent, msg, wparam, lparam) : false;
    case evDestroy:
      if (st) {
        tab_cleanup_icons(st);
        while (st->focus) { tab_focus_t *entry = st->focus; st->focus = entry->next; free(entry); }
        free(st); win->userdata = NULL;
      }
      return true;
    default: return false;
  }
}
