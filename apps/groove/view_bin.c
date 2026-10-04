// VIEW: the sound library — a header (titles + search field) above a sidebar
// tabview whose pages are bins. A bin holds the sample cards of one instrument
// family (or all of them), filtered by the search text. Each card is a child
// window. Pressing it auditions the block; dragging lifts that same window with
// window_set_drag_visual.

#include "groove.h"

#define TILE_W     150
#define TILE_H     100
#define TILE_GAP   12
#define TILE_PAD   12
#define TILE_INSET 10
#define BIN_SLOP   5
#define MAX_TILES  GR_MAX_BLOCKS
#define SEARCH_W   240
#define HEADER_PAD 14

typedef struct {
  category_t cat;
  int ids[MAX_TILES], count;
} bin_t;

typedef struct {
  int block;
  ipoint16_t press;
  bool down, hover;
} tile_t;

static int vpos(window_t *win) { return get_scroll_pos(win, SB_VERT); }

static void tile_screen(window_t *win, int mx, int my, int *sx, int *sy) {
  *sx = window_screen_x(win) + mx;
  *sy = window_screen_y(win) + my;
}

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
  int x = TILE_PAD, y = TILE_PAD, i = 0, bottom = 0;
  for (window_t *c = win->children; c && i < st->count; c = c->next, i++) {
    if (!window_has_state(c, WINDOW_STATE_VISIBLE)) continue;
    if (x + TILE_W > cr.w - TILE_PAD && x > TILE_PAD) { x = TILE_PAD; y += TILE_H + TILE_GAP; }
    card_place(c, R(x, y - vpos(win), TILE_W, TILE_H));
    bottom = y + TILE_H;
    x += TILE_W + TILE_GAP;
  }
  int content_h = bottom + TILE_PAD;
  scroll_info_t si = { .fMask = SIF_RANGE | SIF_PAGE | SIF_POS, .nMin = 0, .nMax = content_h, .nPage = cr.h, .nPos = vpos(win) };
  set_scroll_info(win, SB_VERT, &si, false);
}

// Sample card: name on top, a mirrored waveform across the middle, family and
// length along the bottom, all on one gradient card in the family colour.
static void draw_tile(const block_t *b, irect16_t r, ctrl_state_t state) {
  uint32_t color = category_color(b->cat), on = get_sys_color(brTextOnColor);
  draw_gradient_card(r, state, color);
  irect16_t c = rect_inset(r, get_theme()->card_ring_width + TILE_INSET);
  int name_h = text_char_height(FONT_SYSTEM), foot_h = text_char_height(FONT_SMALL);
  draw_text_ellipsized(FONT_SYSTEM, b->name, c.x, c.y, c.w, on);
  char bars[16];
  snprintf(bars, sizeof(bars), "%d bar%s", b->bars, b->bars == 1 ? "" : "s");
  int bars_w = text_strwidth(FONT_SMALL, bars), foot_y = c.y + c.h - foot_h;
  uint32_t dim = color_with_alpha(on, 0xc8);
  draw_text_ellipsized(FONT_SMALL, kCategoryName[b->cat], c.x, foot_y, c.w - bars_w - 8, dim);
  draw_text(FONT_SMALL, bars, c.x + c.w - bars_w, foot_y, dim);
  irect16_t wave = R(c.x, c.y + name_h + 4, c.w, foot_y - 4 - (c.y + name_h + 4));
  int mid = wave.y + wave.h / 2, half = MAX(1, wave.h / 2);
  uint32_t ink = color_with_alpha(on, 0x90);
  for (int x = 0; x < wave.w && b->audio.npeaks > 1; x++) {
    int h = MAX(1, (int)(block_peak(b, x, wave.w) * half / 255));
    fill_rect(ink, R(wave.x + x, mid - h, 1, 2 * h));
  }
}

static result_t win_tile(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  tile_t *st = win->userdata;
  if (!g_app && msg != evDestroy) return false;
  switch (msg) {
    case evCreate:
      if (!(st = allocate_window_data(win, sizeof(*st)))) {
        fprintf(stderr, "[bin] tile allocation failed win=%u\n", (unsigned)win->id);
        fflush(stderr);
        return false;
      }
      st->block = (int)(intptr_t)lparam;
      return true;
    case evPaint: {
      const block_t *b = block_get(st->block);
      bool lit = st->hover && !win->drag_visual;
      draw_tile(b, get_client_rect(win), lit ? CTRL_HOVER : CTRL_NORMAL);
      return true;
    }
    case evMouseMove: {
      int mx = (int16_t)LOWORD(wparam), my = (int16_t)HIWORD(wparam);
      bool inside = rect_contains_point(get_client_rect(win), (ipoint16_t){ (int16_t)mx, (int16_t)my });
      if (inside != st->hover) { st->hover = inside; invalidate_window(win); }
      track_mouse(win);
      if (!st->down) return true;
      int sx, sy;
      tile_screen(win, mx, my, &sx, &sy);
      if (!win->drag_visual && abs(mx - st->press.x) + abs(my - st->press.y) > BIN_SLOP) {
        GR_TRACE("bin drag win=%p selected=%d block=%d", (void *)win, g_app->selected_clip, st->block);
        g_app->drag = (drag_t){ .active = true, .block = st->block, .from_clip = -1, .track = -1 };
        window_set_drag_visual(win, mx - st->press.x, my - st->press.y);
      }
      if (win->drag_visual) {
        window_set_drag_visual(win, mx - st->press.x, my - st->press.y);
        send_message(g_app->sheet, shDragOver, MAKEDWORD(sx, sy), NULL);
      }
      return true;
    }
    case evMouseLeave:
      if (st->hover) { st->hover = false; invalidate_window(win); }
      return true;
    case evLeftButtonDown:
      GR_TRACE("bin press win=%p selected=%d block=%d", (void *)win, g_app->selected_clip, st->block);
      st->down = true;
      st->press = (ipoint16_t){ (int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam) };
      set_capture(win);
      return true;
    case evLeftButtonUp: {
      if (!st->down) return false;
      GR_TRACE("bin release win=%p selected=%d block=%d dragging=%d", (void *)win, g_app->selected_clip, st->block, win->drag_visual);
      int mx = (int16_t)LOWORD(wparam), my = (int16_t)HIWORD(wparam), sx, sy;
      tile_screen(win, mx, my, &sx, &sy);
      if (win->drag_visual) send_message(g_app->sheet, shDrop, MAKEDWORD(sx, sy), NULL);
      else app_preview(st->block);
      window_clear_drag_visual(win);
      st->down = false;
      set_capture(NULL);
      return true;
    }
    case evPointerCancel:
      if (!st->down) return false;
      if (win->drag_visual) send_message(g_app->sheet, shDragEnd, 0, NULL);
      window_clear_drag_visual(win);
      st->down = false;
      set_capture(NULL);
      return true;
    case evDestroy:
      if (st && st->down) set_capture(NULL);
      free(st);
      win->userdata = NULL;
      return true;
    default: return false;
  }
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
        if (!create_window("", GR_CARD_FLAGS, MAKERECT(0, 0, 1, 1), win, win_tile, 0, (void *)(intptr_t)st->ids[i])) {
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
