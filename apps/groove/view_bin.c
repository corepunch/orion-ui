// VIEW: the sound library — a header (titles + search field) above a sidebar
// tabview whose pages are bins. A bin holds the sample cards of one instrument
// family (or all of them), filtered by the search text. Each card is a child
// window. Pressing it auditions the block; dragging lifts that same window with
// window_set_drag_visual.

#include "groove.h"

#define TILE_GAP   0
#define TILE_PAD   0
#define MAX_TILES  GR_MAX_BLOCKS
#define SEARCH_W   240
#define HEADER_PAD 14

typedef struct {
  category_t cat;
  int ids[MAX_TILES], count;
} bin_t;

static int vpos(window_t *win) { return get_scroll_pos(win, SB_VERT); }

bool block_matches(int id, const char *query) {
  const block_t *b = block_get(id);
  if (!b) return false;
  if (!query || !query[0]) return true;
  const char *hay[] = { b->name, kCategoryName[b->cat] };
  for (int h = 0; h < 2; h++)
    for (const char *p = hay[h]; *p; p++) {
      int k = 0;
      while (query[k] && p[k] && tolower((unsigned char)p[k]) == tolower((unsigned char)query[k])) k++;
      if (!query[k]) return true;
    }
  return false;
}

// Shows the cards that match the search, hides the rest.
static int apply_filter(window_t *win, bin_t *st) {
  int i = 0, shown = 0;
  for (window_t *c = win->children; c && i < st->count; c = c->next, i++) {
    bool match = block_matches(st->ids[i], g_app->search);
    if (match != window_has_state(c, WINDOW_STATE_VISIBLE)) show_window(c, match);
    shown += match;
  }
  return shown;
}

// Content-space rows, frames in the bin's viewport (content y minus scroll).
static void layout_tiles(window_t *win, bin_t *st) {
  irect16_t cr = get_client_rect(win);
  int x = TILE_PAD, y = TILE_PAD, i = 0, bottom = 0, row_height = 0;
  for (window_t *c = win->children; c && i < st->count; c = c->next, i++) {
    if (!window_has_state(c, WINDOW_STATE_VISIBLE)) continue;
    layout_measure_t measure = {0};
    send_message(c, evMeasure, 0, &measure);
    ipoint16_t size = {measure.desired_w, measure.desired_h};
    if (x + size.x > cr.w - TILE_PAD && x > TILE_PAD) { x = TILE_PAD; y += row_height + TILE_GAP; row_height = 0; }
    card_place(c, R(x, y - vpos(win), size.x, size.y));
    row_height = MAX(row_height, size.y);
    bottom = y + row_height;
    x += size.x + TILE_GAP;
  }
  int content_h = bottom + TILE_PAD;
  scroll_info_t si = { .fMask = SIF_RANGE | SIF_PAGE | SIF_POS, .nMin = 0, .nMax = content_h, .nPage = cr.h, .nPos = vpos(win) };
  set_scroll_info(win, SB_VERT, &si, false);
}

result_t win_bin(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  bin_t *st = win->userdata;
  if (!g_app && msg != evDestroy) return false;
  switch (msg) {
    case evCreate:
      if (!(st = allocate_window_data(win, sizeof(*st)))) {
        fprintf(stderr, "[bin] allocation failed win=%u\n", (unsigned)win->id);
        fflush(stderr);
        return false;
      }
      st->cat = (category_t)(intptr_t)lparam;
      if (st->cat == CAT_ALL) for (int i = 0; i < blocks_count() && i < MAX_TILES; i++) st->ids[st->count++] = i;
      else st->count = blocks_in_category(st->cat, st->ids, MAX_TILES);
      for (int i = 0; i < st->count; i++) {
        if (!create_window("", GR_CARD_FLAGS, MAKERECT(0, 0, 1, 1), win, win_block_card, 0, (void *)(intptr_t)st->ids[i])) {
          fprintf(stderr, "[bin] tile allocation failed cat=%d index=%d\n", (int)st->cat, i);
          fflush(stderr);
        }
      }
      apply_filter(win, st);
      return true;
    case evPaint: {
      layout_tiles(win, st);
      irect16_t cr = get_client_rect(win);
      fill_rect(get_sys_color(brControlBg), cr);
      if (g_app->search[0] && !apply_filter(win, st)) {
        char msg[96];
        snprintf(msg, sizeof(msg), "No sounds match \"%s\"", g_app->search);
        draw_text(FONT_SYSTEM, msg, TILE_PAD + 4, TILE_PAD + 4, get_sys_color(brTextSecondary));
      }
      return false;
    }
    case binFilter:
      apply_filter(win, st);
      set_scroll_info(win, SB_VERT, &(scroll_info_t){ .fMask = SIF_POS, .nPos = 0 }, false);
      layout_tiles(win, st);
      invalidate_window(win);
      return true;
    case evVScroll:
    case evResize:
      layout_tiles(win, st);
      invalidate_window(win);
      return msg == evVScroll;
    case evDestroy:
      free(st);
      win->userdata = NULL;
      return true;
    default: return false;
  }
}

// Library header: "Sounds" over the sidebar, "Library" and the search field
// over the bin pages. Aligned to the tabview's page area via tcAdjustRect.
static window_t *child_by_id(window_t *win, uint32_t id) {
  for (window_t *c = win->children; c; c = c->next) if (c->id == id) return c;
  return NULL;
}

enum { ID_SOUNDS_TITLE = 1, ID_LIBRARY_TITLE };

static irect16_t library_page(window_t *win) {
  irect16_t r = get_client_rect(win);
  if (g_app->tabs) send_message(g_app->tabs, tcAdjustRect, 0, &r);
  return r;
}

static void library_layout(window_t *win) {
  irect16_t cr = get_client_rect(win), page = library_page(win);
  window_t *sounds = child_by_id(win, ID_SOUNDS_TITLE), *library = child_by_id(win, ID_LIBRARY_TITLE), *search = child_by_id(win, ID_SEARCH);
  if (sounds)  move_window(sounds,  HEADER_PAD, (cr.h - sounds->frame.h) / 2);
  if (library) move_window(library, page.x + HEADER_PAD, (cr.h - library->frame.h) / 2);
  if (search) {
    irect16_t f = rect_split_right(rect_trim_right(cr, HEADER_PAD), MIN(SEARCH_W, page.w / 2));
    f = rect_center(f, f.w, search->frame.h);
    move_window(search, f.x, f.y);
    resize_window(search, f.w, f.h);
  }
}

static void library_title(window_t *win, const char *text, uint32_t id) {
  window_t *label = create_window(text, WINDOW_NOTITLE, MAKERECT(0, 0, 120, 20), win, win_label, 0, NULL);
  if (!label) { fprintf(stderr, "[bin] title allocation failed text=%s\n", text); fflush(stderr); return; }
  label->id = id;
  label_create_params_t style = { brTextNormal, FONT_SYSTEM, true, false };
  send_message(label, lbSetStyle, 0, &style);
}

result_t win_library(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  if (!g_app && msg != evDestroy) return false;
  switch (msg) {
    case evCreate: {
      library_title(win, "Sounds", ID_SOUNDS_TITLE);
      library_title(win, "Library", ID_LIBRARY_TITLE);
      window_t *search = create_window("", 0, MAKERECT(0, 0, SEARCH_W, 24), win, win_textedit, 0, NULL);
      if (!search) { fprintf(stderr, "[bin] search allocation failed win=%u\n", (unsigned)win->id); fflush(stderr); return true; }
      search->id = ID_SEARCH;
      send_message(search, edSetPlaceholder, 0, "Search sounds...");
      return true;
    }
    case evResize: library_layout(win); return false;
    case evPaint: {
      irect16_t cr = get_client_rect(win), page = library_page(win);
      library_layout(win);
      theme_draw(THEME_PART_PANEL, rect_split_left(cr, page.x), CTRL_NORMAL);
      fill_rect(get_sys_color(brControlBg), rect_trim_left(cr, page.x));
      return false;
    }
    case evCommand:
      if (LOWORD(wparam) == ID_SEARCH && HIWORD(wparam) == ednChange) {
        char text[sizeof(g_app->search)];
        send_message((window_t *)lparam, edGetText, sizeof(text), text);
        app_set_search(text);
        return true;
      }
      return false;
    case evDestroy: return true;
    default: return false;
  }
}
