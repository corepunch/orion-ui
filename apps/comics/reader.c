#include "comics.h"
#include <orion/user/svg_icon_loader.h>
#include <strings.h>

static const uint32_t comics_color_buttons[] = {
  ID_LIBRARY_CORAL, ID_LIBRARY_GOLD, ID_LIBRARY_LEAF, ID_LIBRARY_TEAL, ID_LIBRARY_SKY, ID_LIBRARY_PLUM, ID_LIBRARY_BROWN,
};
static void comics_visible(window_t *win, bool visible) {
  if (win && ((win->flags & WINDOW_STATE_VISIBLE) != 0) != visible) show_window(win, visible);
}

static void comics_show(window_t *win, uint32_t id, bool visible) {
  comics_visible(get_window_item(win, id), visible);
}

// Landscape shows facing pages as a printed book does: the cover alone on the right, then
// (1, 2), (3, 4) and so on. Portrait shows one page at a time.
static int comics_spread_first(const comics_reader_t *st, int page) {
  return st->spread && page > 0 ? ((page - 1) & ~1) + 1 : page;
}

static int comics_spread_end(const comics_reader_t *st, int first) {
  return MIN(comic_archive_pages(st->archive), first + (st->spread && first > 0 ? 2 : 1));
}

static int comics_first_page(const comics_reader_t *st) {
  return comics_spread_first(st, st->page);
}

static int comics_end_page(const comics_reader_t *st) {
  return comics_spread_end(st, comics_first_page(st));
}

static bool comics_paint_visible(const comics_reader_t *st) {
  for (int page = comics_first_page(st); st->story && page < comics_end_page(st); page++)
    for (int i = 0; i < 3; i++)
      if (st->story->pages[page][i].shot && st->story->pages[page][i].layer >= 0) return true;
  return false;
}

static void comics_set_panel(window_t *leaf, comics_reader_t *st, comic_beat_t beat,
                             uint32_t art_id, uint32_t bubble_id, uint32_t text_id, int page) {
  if (beat.speech) set_window_item_text(leaf, text_id, "%s", beat.speech);
  comics_show(leaf, art_id, beat.shot != FOG_SHOT_NONE);
  comics_show(leaf, bubble_id, beat.speech != NULL);
  bool top = art_id == ID_LEAF_TOP_PANEL;
  window_t *lower = get_window_item(leaf, ID_LEAF_BOTTOM_ROW);
  fog_comic_panel_t panel = {
    .shot = beat.shot,
    .layer = beat.shot && beat.layer >= 0 ? &st->paint.layers[beat.layer] : NULL,
    .pigment = st->color,
    .art = st->story ? &st->art : NULL,
    .archive = beat.shot == FOG_SHOT_ARCHIVE ? st->archive : NULL,
    .archive_page = beat.shot == FOG_SHOT_ARCHIVE ? page : 0,
    .stage = st->stage,
    .speech = get_window_item(leaf, bubble_id),
    .bubble = beat.bubble,
    .at_page_top = top,
    .at_page_bottom = !top || !lower || !window_is_visible(lower),
  };
  send_message(get_window_item(leaf, art_id), COMIC_SET_PANEL, 0, &panel);
}

static void comics_update_leaf(window_t *leaf, comics_reader_t *st, int page) {
  const comic_story_t *story = st->story;
  if (!story || story->pages[page][0].shot == FOG_SHOT_ARCHIVE) {
    comics_show(leaf, ID_LEAF_BOTTOM_ROW, false);
    comics_set_panel(leaf, st, (comic_beat_t){.shot = FOG_SHOT_ARCHIVE, .layer = -1},
                    ID_LEAF_TOP_PANEL, ID_LEAF_TOP_SPEECH, ID_LEAF_TOP_LINE, page);
    return;
  }
  const comic_beat_t *beats = story->pages[page];
  comics_show(leaf, ID_LEAF_BOTTOM_ROW, beats[1].shot != FOG_SHOT_NONE);
  comics_set_panel(leaf, st, beats[0], ID_LEAF_TOP_PANEL, ID_LEAF_TOP_SPEECH, ID_LEAF_TOP_LINE, page);
  comics_set_panel(leaf, st, beats[1], ID_LEAF_LEFT_PANEL, ID_LEAF_LEFT_SPEECH, ID_LEAF_LEFT_LINE, page);
  comics_set_panel(leaf, st, beats[2], ID_LEAF_RIGHT_PANEL, ID_LEAF_RIGHT_SPEECH, ID_LEAF_RIGHT_LINE, page);
}

static void comics_update_spread(comics_reader_t *st, int slot, int first) {
  int end = comics_spread_end(st, first);
  // The cover is a right-hand page with nothing facing it.
  send_message(st->stage, COMIC_STAGE_RECTO, slot, (void *)(uintptr_t)(st->spread && first == 0));
  for (int i = 0; i < 2; i++) {
    comics_visible(st->leaves[slot][i], first + i < end);
    if (first + i < end) comics_update_leaf(st->leaves[slot][i], st, first + i);
  }
}

static void comics_refresh_shelf(window_t *win, const comics_reader_t *st) {
  char directory[4096];
  comics_shelf_sync(win, st->persistent && comics_library_directory(directory, sizeof(directory)) ? directory : NULL);
}

static void comics_refresh(window_t *win, comics_reader_t *st) {
  // While the reader grows out of a cover or shrinks into one, both lie in the overlay.
  bool moving = st->transition != COMICS_STILL;
  comics_show(win, ID_LIBRARY_SHELF, !st->reading || moving);
  comics_visible(st->stage, st->reading || moving);
  if (!st->reading) {
    comics_refresh_shelf(win, st);
    window_layout_sync(win);
    return;
  }
  int first = comics_first_page(st), end = comics_end_page(st);
  comics_update_spread(st, st->front, first);

  bool finish = end == comic_archive_pages(st->archive);
  window_t *next = get_window_item(win, ID_LIBRARY_NEXT);
  send_message(next, btnSetIconName, 0, finish ? "phosphor-check" : "phosphor-caret-right");
  send_message(next, btnSetTooltip, 0, (void *)ui_trc("library", finish ? "Finish" : "Next"));
  comics_show(win, ID_LIBRARY_PALETTE, comics_paint_visible(st));
  comics_show(win, ID_LIBRARY_BACK, first > 0);
  window_layout_sync(win);
  send_message(st->stage, evResize, 0, NULL);
  invalidate_window(win);
}

static void comics_slide(comics_reader_t *st, int direction, float progress) {
  comic_stage_slide_t slide = {st->front, direction, progress};
  send_message(st->stage, COMIC_STAGE_SLIDE, 0, &slide);
}

// The stage asks for the spread a turn leads to; it lies in the slot behind the front one.
static bool comics_prepare_turn(comics_reader_t *st, int direction) {
  int first = comics_first_page(st);
  if (direction < 0 && first == 0) return false;
  if (direction > 0 && comics_end_page(st) >= comic_archive_pages(st->archive)) return false;
  st->next_page = direction > 0 ? comics_end_page(st) : comics_spread_first(st, first - 1);
  comics_update_spread(st, 1 - st->front, st->next_page);
  comics_visible(st->layout.spread[1 - st->front], true);
  return true;
}

static void comics_finish_turn(window_t *win, comics_reader_t *st) {
  comics_visible(st->layout.spread[st->front], false);
  st->front = 1 - st->front;
  st->pages = st->leaves[st->front];
  st->page = st->next_page;
  comics_slide(st, 0, 0);
  comics_save_progress(st);
  comics_refresh(win, st);
}

// Where the open book sits on the shelf, in the stage's coordinates: the cover it grows out of and
// shrinks back into. A book without a cover in view zooms from the middle of the screen instead.
static irect16_t comics_cover_rect(window_t *win, comics_reader_t *st) {
  irect16_t stage = get_client_rect(st->stage), book;
  window_t *cover = comics_shelf_cover(win, st->path);
  window_t *grid = get_window_item(win, ID_LIBRARY_BOOKS);
  if (cover && grid && window_is_visible(cover) && send_message(cover, COMIC_COVER_GET_RECT, 0, &book) &&
      book.w > 0 && book.h > 0) {
    irect16_t r = map_window_rect(cover, st->stage, book);
    irect16_t view = map_window_rect(grid, st->stage, get_client_rect(grid));
    irect16_t seen = rect_intersect(r, view);
    if (seen.w == r.w && seen.h == r.h) return r;
  }
  return rect_center(stage, stage.w * 4 / 5, stage.h * 4 / 5);
}

static void comics_present(window_t *win, comics_reader_t *st, bool open) {
  comics_transition_t transition = open ? COMICS_OPENING : COMICS_CLOSING;
  st->transition = transition;
  comics_refresh(win, st);
  uint32_t flags = AW_ZOOM | AW_BLEND | (open ? 0 : AW_HIDE);
  bool started = animate_window(st->stage, comics_cover_rect(win, st), flags, UI_CURVE_ZOOM);
  // A transition this one replaced has just reported its end; this one is still running.
  if (started && st->transition == transition) return;
  st->transition = started ? transition : COMICS_STILL;
  comics_refresh(win, st);
}

static bool comics_create_views(window_t *win, comics_reader_t *st) {
  window_t *shelf = get_window_item(win, ID_LIBRARY_BOOKS);
  st->stage = get_window_item(win, ID_LIBRARY_READER);
  st->layout = (comic_stage_layout_t){
    .spread = {get_window_item(win, ID_LIBRARY_SPREAD), get_window_item(win, ID_LIBRARY_INCOMING)},
    .shelf = get_window_item(win, ID_LIBRARY_LIBRARY), .back = get_window_item(win, ID_LIBRARY_BACK),
    .palette = get_window_item(win, ID_LIBRARY_PALETTE), .forward = get_window_item(win, ID_LIBRARY_FORWARD),
  };
  if (!shelf || !st->stage || !st->layout.spread[0] || !st->layout.spread[1]) return false;
  for (int slot = 0; slot < 2; slot++) {
    for (int i = 0; i < 2; i++) {
      window_t *leaf = create_window_from_form(&comics_leaf_form, 0, 0, st->layout.spread[slot],
                                              win_stackview, win->hinstance, NULL);
      if (!leaf) return false;
      leaf->layout.v_align = LAYOUT_ALIGN_STRETCH;
      st->leaves[slot][i] = leaf;
    }
  }
  st->pages = st->leaves[0];
  send_message(st->stage, COMIC_STAGE_CONFIGURE, 0, &st->layout);
  for (int i = 0; i < ARRAY_LEN(comics_color_buttons); i++)
    window_set_style_var_color(get_window_item(win, comics_color_buttons[i]), "--pigment", comic_pigment_color(i + 1));
  send_message(get_window_item(win, ID_LIBRARY_CORAL), btnSetCheck, btnStateChecked, NULL);
  comics_refresh(win, st);
  return true;
}

static void comics_release_book(comics_reader_t *st) {
  fog_art_clear(&st->art);
  comic_story_free(st->story);
  comic_archive_close(st->archive);
}

static bool comics_open_book(window_t *win, const char *path, bool add_to_shelf) {
  const char *ext = path ? strrchr(path, '.') : NULL;
  if (!win || !win->userdata || !ext || strcasecmp(ext, ".cbz")) return false;
  comic_archive_t *archive = comic_archive_open(path);
  if (!archive) return false;
  comic_story_t *story = comic_story_load(archive);
  fog_comic_art_t art = {0};
  if (story && !fog_art_load(&art, archive, story)) {
    fog_art_clear(&art);
    comic_story_free(story);
    story = NULL;
  }

  comics_reader_t *st = win->userdata;
  if (add_to_shelf && st->persistent && !comics_library_import(archive, story, path))
    message_box(win, ui_trc("library", "The comic could not be added to the bookshelf."),
                ui_trc("library", "Open CBZ"), MB_OK);
  send_message(st->stage, COMIC_STAGE_SETTLE, 0, NULL);
  bool was_reading = st->reading;
  if (!was_reading) comics_visible(st->stage, false);
  bool resume = story && st->story && !strcmp(story->id, st->story->id) &&
                story->count == st->story->count && story->layer_count == st->story->layer_count;
  for (int slot = 0; slot < 2; slot++)
    for (int i = 0; i < 2; i++)
      send_message(get_window_item(st->leaves[slot][i], ID_LEAF_TOP_PANEL), COMIC_SET_PANEL, 0,
                   &(fog_comic_panel_t){0});
  comics_release_book(st);
  st->archive = archive;
  st->story = story;
  st->art = art;
  if (!resume) {
    uint32_t revision = st->paint.revision;
    memset(&st->paint, 0, sizeof(st->paint));
    st->paint.revision = revision + 1;
    st->page = 0;
    if (story && st->persistent) comics_load_progress(st);
  }
  snprintf(st->path, sizeof(st->path), "%s", path);
  st->reading = true;
  if (was_reading) comics_refresh(win, st);
  else comics_present(win, st, true);
  return true;
}

bool comics_open_cbz(window_t *win, const char *path) {
  return comics_open_book(win, path, true);
}

static void comics_pick_archive(window_t *win) {
  char path[4096] = {0}, filter[192];
  int size = snprintf(filter, sizeof(filter), "%s", ui_trc("library", "Comic Book ZIP Archives"));
  if (size < 0 || (size_t)size + sizeof("*.cbz\0") >= sizeof(filter)) return;
  memcpy(filter + size + 1, "*.cbz\0", sizeof("*.cbz\0"));
  openfilename_t picker = {
    .lStructSize = sizeof(picker), .hwndOwner = win, .lpstrFile = path, .nMaxFile = sizeof(path),
    .lpstrFilter = filter, .nFilterIndex = 1, .Flags = OFN_FILEMUSTEXIST,
  };
  if (get_open_filename(&picker) && !comics_open_cbz(win, path))
    message_box(win, ui_trc("library", "This comic archive could not be opened."),
                ui_trc("library", "Open CBZ"), MB_OK);
}

static bool comics_close_book(window_t *win, comics_reader_t *st) {
  send_message(st->stage, COMIC_STAGE_SETTLE, 0, NULL);
  st->reading = false;
  comics_save_progress(st);
  comics_present(win, st, false);
  return true;
}

static bool comics_action(window_t *win, comics_reader_t *st, uint16_t id, uint16_t code, window_t *source) {
  if (source == st->stage && code == awnFinished) {
    st->transition = COMICS_STILL;
    comics_refresh(win, st);
    return true;
  }
  if (st->transition != COMICS_STILL) return true;
  if (id == ID_LIBRARY_OPEN && code == btnClicked) {
    comics_pick_archive(win);
    return true;
  }
  if (id == ID_BOOK_CARD_COVER && code == btnClicked)
    return comics_open_book(win, comics_shelf_book_path(source), false);
  if (source == get_window_item(win, ID_LIBRARY_BOOKS) && code == tgnActivate) {
    window_t *card = source->children;
    for (int index = 0; card && index < id; index++) card = card->next;
    return card && comics_open_book(win, comics_shelf_book_path(get_window_item(card, ID_BOOK_CARD_COVER)), false);
  }
  if (!st->reading) return false;
  if (source == st->stage) {
    switch (code) {
      case COMIC_TURN_QUERY_NEXT: return comics_prepare_turn(st, 1);
      case COMIC_TURN_QUERY_BACK: return comics_prepare_turn(st, -1);
      case COMIC_TURN_DONE:       comics_finish_turn(win, st); return true;
      case COMIC_TURN_CANCEL:     comics_visible(st->layout.spread[1 - st->front], false); return true;
      default:                    return false;
    }
  }
  if (st->story && source && window_is_class(source, COMIC_PANEL_CLASS)) {
    if (code != COMIC_PAINT_FINISHED) return false;
    st->paint.revision++;
    comics_save_progress(st);
    return true;
  }
  if (code != btnClicked) return false;
  for (int i = 0; i < ARRAY_LEN(comics_color_buttons); i++) {
    if (id != comics_color_buttons[i]) continue;
    st->color = FOG_CORAL + i;
    comics_refresh(win, st);
    return true;
  }
  switch (id) {
    case ID_LIBRARY_LIBRARY: return comics_close_book(win, st);
    case ID_LIBRARY_NEXT:
      if (comics_end_page(st) < comic_archive_pages(st->archive)) {
        send_message(st->stage, COMIC_STAGE_TURN, 1, NULL);
        return true;
      }
      st->page = 0;
      return comics_close_book(win, st);
    case ID_LIBRARY_BACK:
      send_message(st->stage, COMIC_STAGE_TURN, (uint32_t)-1, NULL);
      return true;
    default: return false;
  }
}

static result_t comics_window_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  comics_reader_t *st = win->userdata;
  switch (msg) {
    case evCreate: {
      st = allocate_window_data(win, sizeof(*st));
      if (!st) return false;
      st->persistent = lparam != NULL;
      st->color = FOG_CORAL;
      st->spread = win->frame.w > win->frame.h;
      if (!comics_create_views(win, st)) {
        fprintf(stderr, "[comics] could not construct reader win=%u\n", win->id);
        fflush(stderr);
        return false;
      }
      return true;
    }
    case evResize: {
      bool spread = win->frame.w > win->frame.h;
      if (st && st->spread != spread) {
        st->spread = spread;
        send_message(st->stage, COMIC_STAGE_SETTLE, 0, NULL);
        comics_refresh(win, st);
      }
      return false;
    }
    case evActivate:
      if (st && !st->reading && wparam == WA_ACTIVE) comics_refresh_shelf(win, st);
      return false;
    case evPaint: return false;
    case evDestroy:
      if (st) comics_release_book(st);
      return true;
    case evClose: ui_request_quit(); return true;
    case evDropFile: return comics_open_cbz(win, lparam);
    case evCommand: return st && comics_action(win, st, LOWORD(wparam), HIWORD(wparam), lparam);
    default: return false;
  }
}

window_t *comics_open_app(hinstance_t instance, int width, int height, bool persistent) {
  char icons[4096];
  int n = snprintf(icons, sizeof(icons), "%s/../share/comics/icons", ui_get_exe_dir());
  if (n > 0 && (size_t)n < sizeof(icons)) svg_add_icons_dir(instance, icons);
  form_def_t form = comics_library_form;
  form.width = width;
  form.height = height;
  window_t *win = create_window_from_form(&form, 0, 0, NULL, comics_window_proc, instance,
                                          (void *)(uintptr_t)persistent);
  if (win && (!win->userdata || !((comics_reader_t *)win->userdata)->pages)) {
    destroy_window(win);
    return NULL;
  }
  return win;
}

bool comics_register_components(void) {
#ifdef AX_PLATFORM_IOS
  extern int fe_plugin_class_count(void);
  extern const fe_component_desc_t *fe_plugin_class_desc(int);
  for (int i = 0; i < fe_plugin_class_count(); i++)
    if (!register_window_class(fe_plugin_class_desc(i))) return false;
  return true;
#else
  char path[4096];
  int n = snprintf(path, sizeof(path), "%s/../lib/comics_components%s", ui_get_exe_dir(), AX_DYNLIB_EXT);
  return n > 0 && (size_t)n < sizeof(path) && fe_load_component_plugin(path);
#endif
}
