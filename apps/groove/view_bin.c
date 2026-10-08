// VIEW: query library.blocks, bind rows to the GrooveBlockCard template declared
// in groove.orion, and let FlowView measure and wrap the visible cards.

#include "groove.h"

#define TILE_GAP   0
#define TILE_PAD   0
#define MAX_TILES  GR_MAX_BLOCKS

typedef struct {
  window_t *flow;
  int ids[MAX_TILES], count;
} bin_t;

static int vpos(window_t *win) { return get_scroll_pos(win, SB_VERT); }

static bool block_source_load(bin_t *st, const char *source) {
  const char *dot = source ? strchr(source, '.') : NULL;
  if (!dot || dot == source || !dot[1]) {
    fprintf(stderr, "[bin] datasource rejected win=%u source=%s\n", (unsigned)g_app->bin->id, source ? source : "<null>");
    fflush(stderr);
    return false;
  }
  char db_name[64], table_name[64];
  size_t db_len = (size_t)(dot - source), table_len = strlen(dot + 1);
  if (db_len >= sizeof(db_name) || table_len >= sizeof(table_name)) {
    fprintf(stderr, "[bin] datasource name too long win=%u source=%s\n", (unsigned)g_app->bin->id, source);
    fflush(stderr);
    return false;
  }
  memcpy(db_name, source, db_len); db_name[db_len] = 0;
  memcpy(table_name, dot + 1, table_len + 1);
  database_t *db = get_database_by_name(db_name);
  const db_schema_def_t *schema = db ? (const db_schema_def_t *)send_db_message(db, dbGetSchema, 0, NULL) : NULL;
  int table_id = -1;
  for (int i = 0; schema && i < schema->table_count; i++)
    if (schema->tables[i].name && strcmp(schema->tables[i].name, table_name) == 0) { table_id = schema->tables[i].table_id; break; }
  if (table_id < 0 || strcmp(db_name, "library") || strcmp(table_name, "blocks")) {
    fprintf(stderr, "[bin] datasource unavailable win=%u source=%s db=%p table=%d\n", (unsigned)g_app->bin->id, source, (void *)db, table_id);
    fflush(stderr);
    return false;
  }
  result_node_t *rows = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(table_id, 0), NULL);
  for (result_node_t *row = rows; row && st->count < MAX_TILES; row = row->next)
    st->ids[st->count++] = ((library_block_t *)row->data)->id;
  free_result_list(rows);
  return true;
}

// Shows the cards that pass the bin's filter, hides the rest.
static int apply_filter(window_t *win, bin_t *st) {
  int i = 0, shown = 0;
  window_t *parent = st->flow ? st->flow : win;
  for (window_t *c = parent->children; c && i < st->count; c = c->next, i++) {
    bool match = block_visible(st->ids[i]);
    if (match != window_has_state(c, WINDOW_STATE_VISIBLE)) show_window(c, match);
    shown += match;
  }
  return shown;
}

// Keep one FlowView child sized to its measured content and shift it with the bin viewport.
static void layout_tiles(window_t *win, bin_t *st) {
  irect16_t cr = get_client_rect(win);
  if (!st->flow) {
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
    return;
  }
  layout_measure_t measure = { .avail_w = cr.w, .avail_h = cr.h };
  send_message(st->flow, evMeasure, 0, &measure);
  int content_h = MAX(cr.h, measure.desired_h);
  move_window(st->flow, 0, -vpos(win));
  resize_window(st->flow, cr.w, content_h);
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
      g_app->bin = win;
      for (window_t *c = win->children; c; c = c->next)
        if (window_is_class(c, "FlowView")) { st->flow = c; break; }
      const form_ctrl_def_t *def = (uintptr_t)lparam > 0x100000u ? lparam : NULL;
      if (def && def->source) {
        if (!block_source_load(st, def->source)) return false;
      } else {
        // Keep direct creation useful in framework tests that do not initialize the app datasource.
        database_t *db = g_app->library_db;
        if (db) {
          result_node_t *rows = (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(TABLE_BLOCKS, 0), NULL);
          for (result_node_t *row = rows; row && st->count < MAX_TILES; row = row->next)
            st->ids[st->count++] = ((library_block_t *)row->data)->id;
          free_result_list(rows);
        } else {
          for (int cat = 0; cat < CAT_COUNT; cat++) st->count += blocks_in_category(cat, st->ids + st->count, MAX_TILES - st->count);
        }
      }
      window_t *template = st->flow ? st->flow->children : NULL;
      if (template && st->count) {
        send_message(template, grCardSetBlock, st->ids[0], NULL);
        show_window(template, true);
      }
      for (int i = template && st->count ? 1 : 0; i < st->count; i++) {
        window_t *parent = st->flow ? st->flow : win;
        if (!create_window("", GR_CARD_FLAGS, MAKERECT(0, 0, 1, 1), parent, win_block_card, 0, (void *)(intptr_t)st->ids[i])) {
          fprintf(stderr, "[bin] tile allocation failed index=%d block=%d\n", i, st->ids[i]);
          fflush(stderr);
        }
      }
      if (template && !st->count) show_window(template, false);
      apply_filter(win, st);
      return true;
    case evPaint: {
      layout_tiles(win, st);
      irect16_t cr = get_client_rect(win);
      fill_rect(get_sys_color(brControlBg), cr);
      if (!apply_filter(win, st))
        draw_text(FONT_SYSTEM, "No sounds in this family", TILE_PAD + 4, TILE_PAD + 4, get_sys_color(brTextSecondary));
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
      if (g_app && g_app->bin == win) g_app->bin = NULL;
      free(st);
      win->userdata = NULL;
      return true;
    default: return false;
  }
}
