// Ribbon — the application's command surface in place of a menu bar and a
// toolbar band, after the Windows Ribbon framework (UIRibbon): an
// application-menu button and tabs in the menu row; under them the active
// tab's commands, in captioned groups of large and small buttons.
//
// The definition is static data (ribbon_def_t, generated from <ribbon> in a
// .orion file). The control owns which tab shows, each command's state, and
// how the groups shrink when the window is too narrow (≈ a ScalingPolicy):
// from the last group back, small buttons drop their captions, then large
// buttons become small. RIBBON_LAYOUT_SIMPLIFIED is the single row of Office
// on a tablet. See docs/ribbon.md.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <orion/user/user.h>
#include <orion/user/accel.h>
#include <orion/user/i18n.h>
#include <orion/user/svg_icon_loader.h>
#include <orion/user/dock.h>
#include "commctl_internal.h"

typedef enum { RB_NONE, RB_MENU, RB_TAB, RB_ITEM, RB_TOGGLE, RB_RESTORE } rb_target_t;
typedef struct { rb_target_t kind; int index; } rb_hit_t;                 // index: tab, or item within the active tab
typedef enum { RB_FORM_LARGE, RB_FORM_SMALL, RB_FORM_ICON, RB_FORM_SEPARATOR } rb_form_t;
typedef struct { irect16_t rect; rb_form_t form; int group; } rb_cell_t;

typedef struct {
  const ribbon_def_t  *def;
  const accel_table_t *accel;       // not owned
  int                  active, home; // home: the last tab chosen that always shows, to return to
  uint32_t             contexts;    // whose contextual tabs show
  ribbon_layout_t      layout;
  bool                 minimized;
  uint8_t             *states;      // TBSTATE_* per item, tab after tab
  int                  item_total;
  rb_cell_t           *cells;       // the active tab's items, laid out for laid_w
  int                  cell_count, laid_w;
  irect16_t            groups[RIBBON_MAX_GROUPS];
  rb_hit_t             hot, pressed;
  window_t            *popup;       // the open application menu or drop-down
  rb_hit_t             popup_owner;
  window_t            *restore_pressed;
  const menu_def_t    *menus;       // every menu, for the operating system's menu bar (not owned)
  int                  menu_count;
  bool                 os_menu;     // the menus show there, so the application-menu button is not needed
} ribbon_state_t;

#define RB_ROWS 3
static const char *rb_sel(const window_t *win, const char *part) { return ui_selector(win, part); }
static bool rb_same(rb_hit_t a, rb_hit_t b) { return a.kind == b.kind && a.index == b.index; }

// ── definition walking ───────────────────────────────────────────────────────

static int rb_tab_items(const ribbon_tab_t *tab) {
  int n = 0;
  for (int g = 0; g < tab->count; g++) n += tab->groups[g].count;
  return n;
}

static int rb_tab_first(const ribbon_state_t *st, int tab) {
  int n = 0;
  for (int t = 0; t < tab; t++) n += rb_tab_items(&st->def->tabs[t]);
  return n;
}

static const ribbon_tab_t *rb_tab(const ribbon_state_t *st) {
  return st->def && st->active >= 0 && st->active < st->def->tab_count ? &st->def->tabs[st->active] : NULL;
}

static const ribbon_item_t *rb_item(const ribbon_state_t *st, int index) {
  const ribbon_tab_t *tab = rb_tab(st);
  for (int g = 0; tab && g < tab->count; index -= tab->groups[g++].count)
    if (index < tab->groups[g].count) return &tab->groups[g].items[index];
  return NULL;
}

static uint8_t *rb_item_state(const ribbon_state_t *st, int index) {
  return &st->states[rb_tab_first(st, st->active) + index];
}

// Every placement of a command shares its state, so a button on two tabs checks together.
static int rb_command_state(const ribbon_state_t *st, uint16_t ident, int set) {
  int found = -1, flat = 0;
  for (int t = 0; st->def && t < st->def->tab_count; t++)
    for (int g = 0; g < st->def->tabs[t].count; g++)
      for (int i = 0; i < st->def->tabs[t].groups[g].count; i++, flat++) {
        const ribbon_item_t *it = &st->def->tabs[t].groups[g].items[i];
        if (it->type == RIBBON_ITEM_SEPARATOR || it->ident != ident) continue;
        if (set >= 0) st->states[flat] = (uint8_t)set;
        found = st->states[flat];
      }
  return found;
}

static bool rb_tab_shown(const ribbon_state_t *st, int tab) {
  return !st->def->tabs[tab].context || (st->def->tabs[tab].context & st->contexts);
}

// ── metrics ──────────────────────────────────────────────────────────────────

// Where the window's title bar hides behind the content, the tab row at the screen's corner
// stands in for it: the row clears the window's own buttons and is as tall as they need.
static isize16_t rb_window_controls(const window_t *win) {
  return window_screen_x(win) || window_screen_y(win) ? (isize16_t){0, 0} : ui_native_window_controls();
}
static int rb_tabs_height(const window_t *win) { return MAX(ui_px(rb_sel(win, "Ribbon::tabs"), "height"), rb_window_controls(win).h); }
int ribbon_tabs_height(const window_t *ribbon) { return rb_tabs_height(ribbon); }
static bool rb_has_menu(const ribbon_state_t *st) { return st->def && st->def->menu_label && !st->os_menu; }
static bool rb_classic(const ribbon_state_t *st) { return st->layout == RIBBON_LAYOUT_CLASSIC; }
static int rb_rows(const ribbon_state_t *st) { return rb_classic(st) ? RB_ROWS : 1; }
static int rb_row_height(const window_t *win, const ribbon_state_t *st) {
  return ui_px(rb_sel(win, "Ribbon::button"), rb_classic(st) ? "height" : "--single-height");
}
static int rb_caption_height(const window_t *win, const ribbon_state_t *st) {
  return rb_classic(st) ? text_char_height(rb_sel(win, "Ribbon::group")) + ui_px(rb_sel(win, "Ribbon::group"), "padding-top") : 0;
}
static int rb_panel_height(const window_t *win, const ribbon_state_t *st) {
  return 2 * ui_px(rb_sel(win, "Ribbon"), "padding") + rb_rows(st) * rb_row_height(win, st) + rb_caption_height(win, st);
}
static irect16_t rb_panel_rect(const window_t *win, const ribbon_state_t *st) {
  return st->minimized ? R(0, 0, 0, 0) : R(0, rb_tabs_height(win), win->frame.w, rb_panel_height(win, st));
}
static int rb_height(const window_t *win, const ribbon_state_t *st) {
  return rb_tabs_height(win) + (st->minimized ? 0 : rb_panel_height(win, st));
}

// The layout switch sits at the trailing end of the panel, on its last row.
static irect16_t rb_toggle_rect(const window_t *win, const ribbon_state_t *st) {
  int size = ui_px(rb_sel(win, "Ribbon::toggle"), "width"), pad = ui_px(rb_sel(win, "Ribbon"), "padding");
  irect16_t panel = rect_inset(rb_panel_rect(win, st), pad);
  return rect_split_bottom(rect_split_right(panel, size), size);
}

// ── tab row ──────────────────────────────────────────────────────────────────

// Index -1 is the application-menu button; def->tab_count is the zero-width slot where the tabs end.
static irect16_t rb_tab_rect(const window_t *win, const ribbon_state_t *st, int index) {
  const char *tab = rb_sel(win, "Ribbon::tab");
  int pad = ui_px(tab, "padding-left"), h = rb_tabs_height(win);
  int x = ui_px(rb_sel(win, "Ribbon::tabs"), "padding") + rb_window_controls(win).w;
  if (!st->def) return R(x, 0, 0, h);
  if (rb_has_menu(st)) {
    int w = text_strwidth(tab, ui_tr(st->def->menu_label)) + 2 * pad;
    if (index < 0) return R(x, 0, w, h);
    x += w + ui_px(rb_sel(win, "Ribbon::tabs"), "gap");
  } else if (index < 0) return R(x, 0, 0, h);
  for (int i = 0; i < index && i < st->def->tab_count; i++)
    if (rb_tab_shown(st, i)) x += text_strwidth(tab, ui_tr(st->def->tabs[i].label)) + 2 * pad;
  bool shown = index < st->def->tab_count && rb_tab_shown(st, index);
  return R(x, 0, shown ? text_strwidth(tab, ui_tr(st->def->tabs[index].label)) + 2 * pad : 0, h);
}

static irect16_t rb_restore_rect(const window_t *win) {
  return rect_split_right(R(0, 0, win->frame.w, rb_tabs_height(win)), rb_tabs_height(win));
}

// ── command panel layout ─────────────────────────────────────────────────────

// A large button's caption breaks once, at the space that best balances its two lines.
static int rb_caption_break(const char *sel, const char *text) {
  int best = -1, best_w = text_strwidth(sel, text);
  for (int i = 0; text[i]; i++) {
    if (text[i] != ' ') continue;
    int w = MAX(text_strnwidth(sel, text, i), text_strwidth(sel, text + i + 1));
    if (w < best_w) { best = i; best_w = w; }
  }
  return best;
}

static int rb_item_width(const window_t *win, const ribbon_item_t *it, rb_form_t form) {
  const char *btn = rb_sel(win, "Ribbon::button");
  const char *text = it->text ? ui_tr(it->text) : "";
  int pad = ui_px(btn, "padding-left"), gap = ui_px(btn, "gap");
  int arrow = it->type == RIBBON_ITEM_DROPDOWN ? gap + ui_px(rb_sel(win, "Ribbon::arrow"), "width") : 0;
  switch (form) {
    case RB_FORM_SEPARATOR: return ui_px(rb_sel(win, "Ribbon::separator"), "width");
    case RB_FORM_ICON:      return ui_px(btn, "--icon-size") + 2 * pad + arrow;
    case RB_FORM_SMALL:     return (it->icon ? ui_px(btn, "--icon-size") + gap : 0) + text_strwidth(btn, text) + 2 * pad + arrow;
    case RB_FORM_LARGE: {
      int at = rb_caption_break(btn, text);
      int w = at < 0 ? text_strwidth(btn, text) + arrow
                     : MAX(text_strnwidth(btn, text, at), text_strwidth(btn, text + at + 1) + arrow);
      return MAX(ui_px(btn, "min-width"), MAX(w, ui_px(btn, "--large-icon-size")) + 2 * pad);
    }
  }
  return 0;
}

// How an item presents itself in a group shrunk `scale` steps (0 = as declared).
static rb_form_t rb_item_form(const ribbon_state_t *st, const ribbon_item_t *it, int scale) {
  if (it->type == RIBBON_ITEM_SEPARATOR) return RB_FORM_SEPARATOR;
  bool large = (it->style & RIBBON_ITEM_LARGE) != 0;
  if (rb_classic(st)) {
    if (large && scale < 2) return RB_FORM_LARGE;
    return scale >= 1 && it->icon ? RB_FORM_ICON : RB_FORM_SMALL;
  }
  if (large && scale < 1) return RB_FORM_SMALL;   // one row: a large button keeps its caption beside the icon
  return it->icon ? RB_FORM_ICON : RB_FORM_SMALL;
}

// Lays the active tab out with each group at its scale. Returns the x where the groups end.
static int rb_layout_pass(window_t *win, ribbon_state_t *st, const int *scale) {
  const ribbon_tab_t *tab = rb_tab(st);
  irect16_t panel = rect_inset(rb_panel_rect(win, st), ui_px(rb_sel(win, "Ribbon"), "padding"));
  int gap = ui_px(rb_sel(win, "Ribbon"), "gap"), side = ui_px(rb_sel(win, "Ribbon::group"), "padding-left");
  int rows = rb_rows(st), row_h = rb_row_height(win, st), sep = ui_px(rb_sel(win, "Ribbon::separator"), "width");
  int x = panel.x, cell = 0;
  for (int g = 0; tab && g < tab->count; g++) {
    const ribbon_group_t *group = &tab->groups[g];
    int first = cell, cx = x + side, row = 0, column = cell, column_w = 0;
    for (int i = 0; i <= group->count; i++) {
      const ribbon_item_t *it = i < group->count ? &group->items[i] : NULL;
      rb_form_t form = it ? rb_item_form(st, it, scale[g]) : RB_FORM_SEPARATOR;
      bool stacks = it && (form == RB_FORM_ICON || form == RB_FORM_SMALL) && rows > 1;
      // A full column, a column-high item or the group's end closes the column of small buttons.
      if (row && (!stacks || row == rows)) {
        for (int c = column; c < cell; c++) st->cells[c].rect.w = column_w;
        cx += column_w + gap; row = 0; column_w = 0;
      }
      if (!it) break;
      int w = rb_item_width(win, it, form);
      if (stacks) {
        if (!row) column = cell;
        st->cells[cell++] = (rb_cell_t){R(cx, panel.y + row * row_h, w, row_h), form, g};
        column_w = MAX(column_w, w); row++;
      } else {
        st->cells[cell++] = (rb_cell_t){R(cx, panel.y, w, rows * row_h), form, g};
        cx += w + gap;
      }
    }
    int content = MAX(0, cx - gap - (x + side));
    int caption = rb_classic(st) && group->label ? text_strwidth(rb_sel(win, "Ribbon::group"), ui_tr(group->label)) : 0;
    int inner = MAX(content, caption);
    for (int c = first; c < cell; c++) st->cells[c].rect.x += (inner - content) / 2;   // a wide caption centres its commands
    st->groups[g] = R(x, panel.y, inner + 2 * side, panel.h);
    x += st->groups[g].w + sep;
  }
  st->cell_count = cell;
  return x;
}

static void rb_layout(window_t *win, ribbon_state_t *st) {
  if (st->laid_w == win->frame.w || !st->def) return;
  const ribbon_tab_t *tab = rb_tab(st);
  int scale[RIBBON_MAX_GROUPS] = {0};
  int limit = rb_toggle_rect(win, st).x - ui_px(rb_sel(win, "Ribbon"), "gap");
  int end = rb_layout_pass(win, st, scale);
  for (int step = 1; step <= 2 && end > limit; step++)
    for (int g = tab ? tab->count - 1 : -1; g >= 0 && end > limit; g--) {
      scale[g] = step;
      end = rb_layout_pass(win, st, scale);
    }
  st->laid_w = win->frame.w;
}

static void rb_relayout(window_t *win, ribbon_state_t *st) {
  st->laid_w = -1;
  st->hot = st->pressed = (rb_hit_t){RB_NONE, 0};
  invalidate_window(win);
}

// The band's height changed: the host lays its docked panes out again.
static void rb_resized(window_t *win, ribbon_state_t *st) {
  rb_relayout(win, st);
  if (win->parent && win->dock) send_message(win->parent, evDockChanged, 0, NULL);
  else if (win->frame.h != rb_height(win, st)) resize_window(win, win->frame.w, rb_height(win, st));
}

// ── hit testing and state ────────────────────────────────────────────────────

static rb_hit_t rb_hit(window_t *win, ribbon_state_t *st, ipoint16_t p) {
  if (!st->def) return (rb_hit_t){RB_NONE, 0};
  if (p.y < rb_tabs_height(win)) {
    if (menu_row_maximized_window(win) && rect_contains_point(rb_restore_rect(win), p)) return (rb_hit_t){RB_RESTORE, 0};
    if (rb_has_menu(st) && rect_contains_point(rb_tab_rect(win, st, -1), p)) return (rb_hit_t){RB_MENU, 0};
    for (int i = 0; i < st->def->tab_count; i++)
      if (rb_tab_shown(st, i) && rect_contains_point(rb_tab_rect(win, st, i), p)) return (rb_hit_t){RB_TAB, i};
    return (rb_hit_t){RB_NONE, 0};
  }
  if (st->minimized) return (rb_hit_t){RB_NONE, 0};
  if (rect_contains_point(rb_toggle_rect(win, st), p)) return (rb_hit_t){RB_TOGGLE, 0};
  rb_layout(win, st);
  int limit = rb_toggle_rect(win, st).x;
  for (int i = 0; i < st->cell_count; i++)
    if (st->cells[i].form != RB_FORM_SEPARATOR && st->cells[i].rect.x + st->cells[i].rect.w <= limit &&
        rect_contains_point(st->cells[i].rect, p)) return (rb_hit_t){RB_ITEM, i};
  return (rb_hit_t){RB_NONE, 0};
}

static irect16_t rb_hit_rect(window_t *win, ribbon_state_t *st, rb_hit_t hit) {
  switch (hit.kind) {
    case RB_MENU:    return rb_tab_rect(win, st, -1);
    case RB_TAB:     return rb_tab_rect(win, st, hit.index);
    case RB_ITEM:    return st->cells[hit.index].rect;
    case RB_TOGGLE:  return rb_toggle_rect(win, st);
    case RB_RESTORE: return rb_restore_rect(win);
    default:         return R(0, 0, 0, 0);
  }
}

static bool rb_popup_open(ribbon_state_t *st, rb_hit_t owner) {
  if (st->popup && !is_window(st->popup)) st->popup = NULL;
  return st->popup && rb_same(st->popup_owner, owner);
}

static ctrl_state_t rb_state(ribbon_state_t *st, rb_hit_t target, bool disabled, bool checked) {
  if (disabled) return CTRL_DISABLED;
  ctrl_state_t state = checked || rb_popup_open(st, target) ? CTRL_SELECTED : CTRL_NORMAL;
  if (rb_same(st->pressed, target)) state |= CTRL_PRESSED;
  if (rb_same(st->hot, target)) state |= CTRL_HOVER;
  return state;
}

static void rb_open_menu(window_t *win, ribbon_state_t *st, rb_hit_t owner, const menu_item_t *items, int count) {
  irect16_t r = rb_hit_rect(win, st, owner);
  st->popup = show_popup_menu_accel(win, items, count, st->accel, window_screen_x(win) + r.x, window_screen_y(win) + r.y + r.h);
  st->popup_owner = owner;
  invalidate_window(win);
}

static bool rb_select_tab(window_t *win, ribbon_state_t *st, int index) {
  if (!st->def || index < 0 || index >= st->def->tab_count) {
    fprintf(stderr, "[ribbon] tab rejected win=%u index=%d count=%d\n", win->id, index, st->def ? st->def->tab_count : 0);
    fflush(stderr);
    return false;
  }
  if (!rb_tab_shown(st, index)) {
    fprintf(stderr, "[ribbon] tab rejected win=%u index=%d: its context 0x%x is not among 0x%x\n", win->id, index,
            (unsigned)st->def->tabs[index].context, (unsigned)st->contexts);
    fflush(stderr);
    return false;
  }
  if (!st->def->tabs[index].context) st->home = index;
  if (index == st->active) return true;
  st->active = index;
  rb_relayout(win, st);
  return true;
}

// The tabs of the contexts in `mask` show. A tab whose context left gives way to the tab the
// user came from; with `open`, the first tab this reveals becomes the active one.
static void rb_set_contexts(window_t *win, ribbon_state_t *st, uint32_t mask, bool open) {
  uint32_t revealed = mask & ~st->contexts;
  st->contexts = mask;
  if (!st->def) return;
  if (st->active < st->def->tab_count && !rb_tab_shown(st, st->active)) st->active = st->home;
  for (int i = 0; open && i < st->def->tab_count; i++)
    if (st->def->tabs[i].context & revealed) { st->active = i; break; }
  rb_relayout(win, st);
}

static bool rb_set_ribbon(window_t *win, ribbon_state_t *st, const ribbon_def_t *def) {
  int total = 0, widest = 0;
  for (int t = 0; def && t < def->tab_count; t++) {
    if (def->tabs[t].count > RIBBON_MAX_GROUPS) {
      fprintf(stderr, "[ribbon] definition rejected win=%u tab=%d groups=%d limit=%d\n", win->id, t, def->tabs[t].count, RIBBON_MAX_GROUPS);
      fflush(stderr);
      return false;
    }
    total += rb_tab_items(&def->tabs[t]);
    widest = MAX(widest, rb_tab_items(&def->tabs[t]));
  }
  uint8_t *states = calloc(MAX(1, total), sizeof(*states));
  rb_cell_t *cells = calloc(MAX(1, widest), sizeof(*cells));
  if (!def || !states || !cells) {
    fprintf(stderr, "[ribbon] definition rejected win=%u def=%p items=%d\n", win->id, (void *)def, total);
    fflush(stderr);
    free(states); free(cells);
    return false;
  }
  free(st->states); free(st->cells);
  st->def = def; st->states = states; st->cells = cells; st->item_total = total;
  st->active = st->home = 0; st->layout = def->layout; st->cell_count = 0;
  for (int t = 0; t < def->tab_count; t++)
    if (!def->tabs[t].context) { st->active = st->home = t; break; }
  rb_resized(win, st);
  return true;
}

// ── painting ─────────────────────────────────────────────────────────────────

static void rb_draw_icon(const window_t *win, const char *name, irect16_t r, int size, uint32_t color) {
  sysicon_resolved_t icon;
  if (!sysicon_resolve_size(name, size, &icon)) return;
  (void)win;
  draw_sprite_region((int)icon.tex, rect_center(r, size, size), UV_RECT(icon.u0, icon.v0, icon.u1, icon.v1), color, 0);
}

static void rb_draw_item(window_t *win, ribbon_state_t *st, int index) {
  const ribbon_item_t *it = rb_item(st, index);
  const rb_cell_t *cell = &st->cells[index];
  const char *btn = rb_sel(win, "Ribbon::button");
  irect16_t r = cell->rect;
  if (cell->form == RB_FORM_SEPARATOR) {
    theme_draw_in(win, THEME_PART_RIBBON_SEPARATOR, r, CTRL_NORMAL);
    return;
  }
  uint8_t bits = *rb_item_state(st, index);
  bool checked = (bits & TBSTATE_CHECKED) != 0;
  ctrl_state_t state = rb_state(st, (rb_hit_t){RB_ITEM, index}, (bits & TBSTATE_DISABLED) != 0, checked);
  uint32_t ink = theme_foreground_in(win, THEME_PART_RIBBON_BUTTON, state);
  const char *icon = checked && it->checked_icon ? it->checked_icon : it->icon;
  const char *text = it->text ? ui_tr(it->text) : "";
  int pad = ui_px(btn, "padding-left"), gap = ui_px(btn, "gap"), line = text_char_height(btn);
  int arrow_w = it->type == RIBBON_ITEM_DROPDOWN ? ui_px(rb_sel(win, "Ribbon::arrow"), "width") : 0;
  irect16_t arrow = R(0, 0, 0, 0);
  theme_draw_in(win, THEME_PART_RIBBON_BUTTON, r, state);
  if (cell->form == RB_FORM_LARGE) {
    int size = ui_px(btn, "--large-icon-size"), at = rb_caption_break(btn, text);
    irect16_t body = rect_inset_xy(r, pad, gap);
    if (icon) rb_draw_icon(win, icon, rect_split_top(body, size), size, ink);
    irect16_t caption = rect_split_top(rect_trim_top(body, size + gap), line);
    irect16_t last = caption;
    if (at >= 0) {
      char first[128];
      snprintf(first, sizeof(first), "%.*s", MIN(at, (int)sizeof(first) - 1), text);
      draw_text_clipped(btn, first, &caption, ink, TEXT_ALIGN_CENTER);
      last = rect_offset(caption, 0, line);
      text += at + 1;
    }
    int w = text_strwidth(btn, text) + (arrow_w ? gap + arrow_w : 0);
    irect16_t run = rect_center(last, MIN(w, last.w), line);
    draw_text_ellipsized(btn, text, run.x, run.y, run.w - (arrow_w ? gap + arrow_w : 0), ink);
    arrow = rect_split_right(run, arrow_w);
  } else {
    irect16_t body = rect_inset_xy(r, pad, 0);
    if (arrow_w) { arrow = rect_split_right(body, arrow_w); body = rect_trim_right(body, arrow_w + gap); }
    int size = ui_px(btn, "--icon-size");
    if (icon) {
      rb_draw_icon(win, icon, rect_split_left(body, size), size, ink);
      body = rect_trim_left(body, size + gap);
    }
    if (cell->form == RB_FORM_SMALL) {
      irect16_t label = rect_center(body, body.w, line);
      draw_text_ellipsized(btn, text, label.x, label.y, label.w, ink);
    }
  }
  if (arrow_w) theme_draw_in(win, THEME_PART_RIBBON_ARROW, arrow, state & CTRL_DISABLED);
}

static void rb_paint(window_t *win, ribbon_state_t *st) {
  const char *tab_sel = rb_sel(win, "Ribbon::tab");
  irect16_t row = R(0, 0, win->frame.w, rb_tabs_height(win));
  theme_draw_in(win, THEME_PART_RIBBON_TABS, row, CTRL_NORMAL);
  if (!st->def) return;
  window_t *maximized = menu_row_maximized_window(win);
  if (maximized) draw_theme_icon_in_rect(THEME_ICON_RESTORE, rb_restore_rect(win), get_sys_color(brTextNormal));
  if (rb_has_menu(st)) {
    irect16_t r = rb_tab_rect(win, st, -1);
    ctrl_state_t state = rb_state(st, (rb_hit_t){RB_MENU, 0}, false, false);
    theme_draw_in(win, THEME_PART_RIBBON_MENU, r, state);
    draw_text_clipped(tab_sel, ui_tr(st->def->menu_label), &r, theme_foreground_in(win, THEME_PART_RIBBON_MENU, state), TEXT_ALIGN_CENTER);
  }
  for (int i = 0; i < st->def->tab_count; i++) {
    irect16_t r = rb_tab_rect(win, st, i);
    if (!rb_tab_shown(st, i)) continue;
    if (maximized && r.x + r.w > rb_restore_rect(win).x) break;
    ctrl_state_t state = rb_state(st, (rb_hit_t){RB_TAB, i}, false, i == st->active && !st->minimized);
    theme_part_t part = st->def->tabs[i].context ? THEME_PART_RIBBON_CONTEXT_TAB : THEME_PART_RIBBON_TAB;
    theme_draw_in(win, part, r, state);
    draw_text_clipped(tab_sel, ui_tr(st->def->tabs[i].label), &r, theme_foreground_in(win, part, state), TEXT_ALIGN_CENTER);
  }
  if (st->minimized) return;
  const ribbon_tab_t *tab = rb_tab(st);
  const char *group_sel = rb_sel(win, "Ribbon::group");
  theme_draw_in(win, THEME_PART_RIBBON, rb_panel_rect(win, st), CTRL_NORMAL);
  rb_layout(win, st);
  irect16_t toggle = rb_toggle_rect(win, st);
  int sep = ui_px(rb_sel(win, "Ribbon::separator"), "width");
  for (int i = 0; i < st->cell_count; i++)
    if (st->cells[i].rect.x + st->cells[i].rect.w <= toggle.x) rb_draw_item(win, st, i);
  for (int g = 0; tab && g < tab->count; g++) {
    irect16_t r = st->groups[g];
    if (r.x + r.w > toggle.x) break;
    if (rb_classic(st) && tab->groups[g].label) {
      irect16_t caption = rect_split_bottom(r, text_char_height(group_sel));
      draw_text_clipped(group_sel, ui_tr(tab->groups[g].label), &caption,
                        theme_foreground_in(win, THEME_PART_RIBBON_GROUP, CTRL_NORMAL), TEXT_ALIGN_CENTER);
    }
    if (g + 1 < tab->count) theme_draw_in(win, THEME_PART_RIBBON_SEPARATOR, R(r.x + r.w, r.y, sep, r.h), CTRL_NORMAL);
  }
  theme_draw_in(win, THEME_PART_RIBBON_TOGGLE, toggle, rb_state(st, (rb_hit_t){RB_TOGGLE, 0}, false, !rb_classic(st)));
}

// ── window procedure ─────────────────────────────────────────────────────────

static void rb_click(window_t *win, ribbon_state_t *st, rb_hit_t hit) {
  switch (hit.kind) {
    case RB_TAB:
      if (st->minimized) { st->minimized = false; rb_resized(win, st); }
      rb_select_tab(win, st, hit.index);
      break;
    case RB_TOGGLE:
      st->layout = rb_classic(st) ? RIBBON_LAYOUT_SIMPLIFIED : RIBBON_LAYOUT_CLASSIC;
      rb_resized(win, st);
      break;
    case RB_ITEM: {
      const ribbon_item_t *it = rb_item(st, hit.index);
      if (it->style & RIBBON_ITEM_CHECK) rb_command_state(st, it->ident, *rb_item_state(st, hit.index) ^ TBSTATE_CHECKED);
      send_message(win, evCommand, MAKEDWORD(it->ident, btnClicked), win);
      break;
    }
    default: break;
  }
}

result_t win_ribbon(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  ribbon_state_t *st = win->userdata;
  ipoint16_t point = {(int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)};
  if (!st && msg != evCreate) return false;
  switch (msg) {
    case evCreate:
      if (!(st = allocate_window_data(win, sizeof(*st)))) {
        fprintf(stderr, "[ribbon] allocation failed win=%u\n", win->id);
        fflush(stderr);
        return false;
      }
      st->laid_w = -1;
      return !lparam || rb_set_ribbon(win, st, lparam);
    case evDestroy:
      if (g_ui_runtime.captured == win) set_capture(NULL);
      free(st->states); free(st->cells); free(st);
      win->userdata = NULL;
      return true;
    case evPaint:
      rb_paint(win, st);
      return true;
    case evResize:
    case evThemeChanged:
      rb_relayout(win, st);
      return true;
    case evDockMeasure:
      if (!lparam) return false;
      *(ipoint16_t *)lparam = (ipoint16_t){win->frame.w, rb_height(win, st)};
      return true;
    case evDisplayChange:
      if (!win->dock && !win->parent) resize_window(win, LOWORD(wparam), rb_height(win, st));
      return false;
    case evMouseMove: {
      rb_hit_t hot = rb_hit(win, st, point);
      track_mouse(win);
      if (!rb_same(hot, st->hot)) { st->hot = hot; invalidate_window(win); }
      return true;
    }
    case evMouseLeave:
      if (st->hot.kind != RB_NONE) { st->hot = (rb_hit_t){RB_NONE, 0}; invalidate_window(win); }
      return false;
    case evLeftButtonDown: {
      rb_hit_t hit = rb_hit(win, st, point);
      const ribbon_item_t *it = hit.kind == RB_ITEM ? rb_item(st, hit.index) : NULL;
      if (it && (*rb_item_state(st, hit.index) & TBSTATE_DISABLED)) return true;
      // Menus open on the press, as a menu bar's do; the popup takes the pointer from here.
      if (hit.kind == RB_MENU) { rb_open_menu(win, st, hit, st->def->menu_items, st->def->menu_count); return true; }
      if (it && it->type == RIBBON_ITEM_DROPDOWN) { rb_open_menu(win, st, hit, it->menu, it->menu_count); return true; }
      if (hit.kind == RB_RESTORE) st->restore_pressed = menu_row_maximized_window(win);
      if (hit.kind == RB_NONE) {
        if (point.y < rb_tabs_height(win) && rb_window_controls(win).h) ui_drag_native_window();
        return true;
      }
      st->pressed = hit;
      set_capture(win);
      invalidate_window(win);
      return true;
    }
    case evLeftButtonUp: {
      if (st->pressed.kind == RB_NONE) return false;
      rb_hit_t pressed = st->pressed;
      st->pressed = (rb_hit_t){RB_NONE, 0};
      if (g_ui_runtime.captured == win) set_capture(NULL);
      invalidate_window(win);
      if (!rb_same(rb_hit(win, st, point), pressed)) return true;
      if (pressed.kind == RB_RESTORE) {
        if (st->restore_pressed == menu_row_maximized_window(win)) restore_window(st->restore_pressed);
        st->restore_pressed = NULL;
      } else rb_click(win, st, pressed);
      return true;
    }
    case evLeftButtonDoubleClick:
      // A double click on a tab folds the ribbon to its tab row, and unfolds it.
      if (rb_hit(win, st, point).kind != RB_TAB) return false;
      st->minimized = !st->minimized;
      rb_resized(win, st);
      return true;
    case evPointerCancel:
      if (st->pressed.kind == RB_NONE) return false;
      st->pressed = (rb_hit_t){RB_NONE, 0};
      if (g_ui_runtime.captured == win) set_capture(NULL);
      invalidate_window(win);
      return true;
    case evGetTooltipRect: {
      rb_hit_t hit = rb_hit(win, st, point);
      if (hit.kind != RB_ITEM || st->cells[hit.index].form != RB_FORM_ICON || !lparam) return false;
      *(irect16_t *)lparam = st->cells[hit.index].rect;
      return true;
    }
    case evGetTooltipText: {
      rb_hit_t hit = rb_hit(win, st, point);
      // A tooltip names a bare icon; a button that shows its caption has none.
      const ribbon_item_t *it = hit.kind == RB_ITEM && st->cells[hit.index].form == RB_FORM_ICON ? rb_item(st, hit.index) : NULL;
      const char *tip = it ? ui_tr(it->tooltip ? it->tooltip : it->text) : NULL;
      if (!tip || !lparam) return false;
      const accel_t *key = it->ident && !strchr(tip, '(') ? accel_find_cmd(st->accel, it->ident) : NULL;
      char shortcut[64] = "";
      if (key) accel_format(key, shortcut, sizeof(shortcut));
      if (shortcut[0]) snprintf(lparam, 256, "%s (%s)", tip, shortcut);
      else snprintf(lparam, 256, "%s", tip);
      return true;
    }
    // The application's whole menu goes to the operating system's menu bar where there is one;
    // the ribbon then drops its application-menu button, as Office does on a Mac.
    case kMenuBarMessageSetMenus:
      st->menus = lparam; st->menu_count = (int)wparam;
      st->os_menu = st->menus && menu_install_native(win, st->menus, st->menu_count, st->accel);
      rb_relayout(win, st);
      return true;
    case kMenuBarMessageSetAccelerators:
      st->accel = lparam;
      if (st->os_menu) menu_install_native(win, st->menus, st->menu_count, st->accel);
      return true;
    case kMenuBarMessageGetContentWidth:
      return (result_t)(rb_tab_rect(win, st, st->def ? st->def->tab_count : 0).x + ui_px(rb_sel(win, "Ribbon::tabs"), "padding"));
    case rbSetRibbon:    return rb_set_ribbon(win, st, lparam);
    case rbGetActiveTab: return (result_t)st->active;
    case rbSetActiveTab: return rb_select_tab(win, st, (int)wparam);
    case rbGetState:     return (result_t)rb_command_state(st, (uint16_t)wparam, -1);
    case rbSetState: {
      uint32_t bits = (uint32_t)(uintptr_t)lparam;
      if ((bits & ~(TBSTATE_CHECKED | TBSTATE_DISABLED)) || rb_command_state(st, (uint16_t)wparam, (int)bits) < 0) {
        fprintf(stderr, "[ribbon] state rejected win=%u ident=%u state=0x%x\n", win->id, (unsigned)wparam, (unsigned)bits);
        fflush(stderr);
        return false;
      }
      invalidate_window(win);
      return true;
    }
    case rbGetLayout:    return (result_t)st->layout;
    case rbSetLayout:
      if (wparam != RIBBON_LAYOUT_CLASSIC && wparam != RIBBON_LAYOUT_SIMPLIFIED) {
        fprintf(stderr, "[ribbon] layout rejected win=%u layout=%u\n", win->id, (unsigned)wparam);
        fflush(stderr);
        return false;
      }
      if (st->layout != (ribbon_layout_t)wparam) { st->layout = (ribbon_layout_t)wparam; rb_resized(win, st); }
      return true;
    case rbGetMinimized: return st->minimized;
    case rbSetMinimized:
      if (st->minimized != (wparam != 0)) { st->minimized = wparam != 0; rb_resized(win, st); }
      return true;
    case rbGetItemRect: {
      rb_layout(win, st);
      for (int i = 0; lparam && !st->minimized && i < st->cell_count; i++) {
        const ribbon_item_t *it = rb_item(st, i);
        if (it->type == RIBBON_ITEM_SEPARATOR || it->ident != (uint16_t)wparam) continue;
        *(irect16_t *)lparam = st->cells[i].rect;
        return true;
      }
      return false;
    }
    case rbGetTabRect:
      if (!lparam || !st->def || (int)wparam < -1 || (int)wparam >= st->def->tab_count) {
        fprintf(stderr, "[ribbon] tab rect rejected win=%u index=%d out=%p\n", win->id, (int)wparam, lparam);
        fflush(stderr);
        return false;
      }
      *(irect16_t *)lparam = rb_tab_rect(win, st, (int)wparam);
      return (int)wparam < 0 ? rb_has_menu(st) : rb_tab_shown(st, (int)wparam);
    case rbGetContexts:  return (result_t)st->contexts;
    case rbSetContexts:
      rb_set_contexts(win, st, wparam, lparam != NULL);
      return true;
    default:
      return false;
  }
}
