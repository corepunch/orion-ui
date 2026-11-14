// iPad My Songs: the songs datasource shown as a TileGrid of Cards, one song_card
// form per row, each with a preview drawn from the file. TileGrid has no
// datasource binding or item template yet, so this bridge fetches the rows and
// instantiates the declared card form for each one.

#include "groove.h"
#include <time.h>

typedef struct {
  song_entry_t *rows;   // index = tile index
  int count;
} songs_dlg_t;

static void songs_update_actions(window_t *win, const songs_dlg_t *st, int i) {
  bool selected = i >= 0 && i < st->count;
  enable_window(get_window_item(win, ID_SONG_BROWSER_DELETE), selected);
  int tile = (int)send_message(get_window_item(win, ID_SONG_BROWSER_SONGS), tgGetSelection, 0, NULL);
  enable_window(get_window_item(win, ID_SONG_BROWSER_OPEN), tile == 0 || (selected && st->rows[i].bpm));
}

static void card_fill(window_t *card, const song_entry_t *song) {
  label_create_params_t title = { .color_index = brTextNormal,    .font = LABEL_TEXT, .color_set = true, .truncate = true };
  label_create_params_t note  = { .color_index = brTextSecondary, .font = LABEL_TEXT,  .color_set = true, .truncate = true };
  send_message(get_window_item(card, ID_SONG_CARD_TITLE), lbSetStyle, 0, &title);
  send_message(get_window_item(card, ID_SONG_CARD_DETAIL), lbSetStyle, 0, &note);
  set_window_item_text(card, ID_SONG_CARD_TITLE, "%s", song->name);
  snprintf(card->title, sizeof(card->title), "%s", song->name);
  char iso[16] = "", when[32], bpm[16], bars[16];
  time_t modified = (time_t)song->modified;
  struct tm *tm = localtime(&modified);
  if (tm) strftime(iso, sizeof(iso), "%Y-%m-%d", tm);
  ui_format_date(iso, when, sizeof(when));
  ui_format_number(song->bpm, 0, bpm, sizeof(bpm));
  ui_format_number(song->bars, 0, bars, sizeof(bars));
  if (!song->bpm)
    set_window_item_text(card, ID_SONG_CARD_DETAIL, ui_tr("Unreadable song  -  %s"), when);
  else if (!song->bars)
    set_window_item_text(card, ID_SONG_CARD_DETAIL, ui_tr("%s BPM  -  empty  -  %s"), bpm, when);
  else
    set_window_item_text(card, ID_SONG_CARD_DETAIL, ui_tr("%s BPM  -  Bars: %s  -  %s"), bpm, bars, when);
  send_message(get_window_item(card, ID_SONG_CARD_PREVIEW), grPreviewSetSong, 0, (void *)song->path);
}

static void songs_rebuild(window_t *win, songs_dlg_t *st, int select) {
  window_t *grid = get_window_item(win, ID_SONG_BROWSER_SONGS);
  database_t *db = g_app->songs_db;
  free(st->rows);
  st->rows = NULL;
  st->count = 0;
  send_message(grid, tgClear, 0, NULL);
  window_t *new_card = create_window_from_form(&groove_new_song_card_form, 0, 0, grid, win_card, g_app->hinstance, NULL);
  if (!new_card) {
    fprintf(stderr, "[gr] new song card creation failed grid=%u\n", (unsigned)grid->id);
    fflush(stderr);
    return;
  }
  bool loaded = db && send_db_message(db, dbLoad, 0, NULL);
  result_node_t *rows = loaded ? (result_node_t *)send_db_message(db, dbFetch, MAKEDWORD(TABLE_SONGS, 0), NULL) : NULL;
  int n = count_result_list(rows);
  if (n && !(st->rows = calloc((size_t)n, sizeof(*st->rows)))) {
    fprintf(stderr, "[gr] song list allocation failed count=%d\n", n);
    fflush(stderr);
  }
  for (result_node_t *row = rows; row && st->rows; row = row->next) {
    window_t *card = create_window_from_form(&groove_song_card_form, 0, 0, grid, win_card, g_app->hinstance, NULL);
    if (!card) {
      fprintf(stderr, "[gr] song card creation failed index=%d grid=%u\n", st->count, (unsigned)grid->id);
      fflush(stderr);
      continue;
    }
    st->rows[st->count] = *(const song_entry_t *)row->data;
    if (select < 0 && !strcmp(g_app->filename, st->rows[st->count].path)) select = st->count;
    card_fill(card, &st->rows[st->count++]);
  }
  free_result_list(rows);
  if (!loaded)
    set_window_item_text(win, ID_SONG_BROWSER_EMPTY, "%s", ui_tr("Jamsheet's song storage is unavailable on this device."));
  show_window(get_window_item(win, ID_SONG_BROWSER_EMPTY), st->count == 0);
  window_layout_sync(win);
  int i = st->count ? CLAMP(select, 0, st->count - 1) : -1;
  send_message(grid, tgSetSelection, (uint32_t)(i + 1), NULL);
  songs_update_actions(win, st, i);
}

static int songs_selection(window_t *win, const songs_dlg_t *st) {
  int i = (int)send_message(get_window_item(win, ID_SONG_BROWSER_SONGS), tgGetSelection, 0, NULL);
  return i > 0 && i <= st->count ? i - 1 : -1;
}

static void songs_dlg_open(window_t *win, songs_dlg_t *st, int i);

static void songs_dlg_activate(window_t *win, songs_dlg_t *st, int index) {
  if (index == 0) {
    if (!app_confirm_replace_song(win)) return;
    app_new_song();
    end_dialog(win, 1);
  } else if (index > 0 && index <= st->count) {
    songs_dlg_open(win, st, index - 1);
  }
}

static void songs_dlg_open(window_t *win, songs_dlg_t *st, int i) {
  if (i < 0 || !st->rows[i].bpm || !app_confirm_replace_song(win)) return;
  if (!app_open_song(st->rows[i].path)) {
    message_box(win, ui_tr("Could not open this Jamsheet song. The file may be damaged or use an unsupported version."), ui_tr("Open failed"), MB_OK);
    return;
  }
  end_dialog(win, 1);
}

static void songs_dlg_delete(window_t *win, songs_dlg_t *st, int i) {
  if (i < 0) return;
  char prompt[256];
  snprintf(prompt, sizeof(prompt), ui_tr("Delete \"%s\"? This cannot be undone."), st->rows[i].name);
  if (message_box(win, prompt, ui_tr("Delete Song"), MB_YESNO) != IDYES) return;
  if (!send_db_message(g_app->songs_db, dbDelete, TABLE_SONGS, (void *)(intptr_t)st->rows[i].id)) {
    message_box(win, ui_tr("Could not delete this song."), ui_tr("Delete failed"), MB_OK);
    return;
  }
  if (!strcmp(g_app->filename, st->rows[i].path)) {
    g_app->filename[0] = 0;
    g_app->song_dirty = true;
  }
  songs_rebuild(win, st, i);
}

static result_t songs_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  songs_dlg_t *st = win->userdata;
  switch (msg) {
    case evCreate: {
      if (!(st = allocate_window_data(win, sizeof(*st)))) {
        fprintf(stderr, "[gr] songs screen allocation failed win=%u\n", (unsigned)win->id);
        fflush(stderr);
        return false;
      }
      window_t *grid = get_window_item(win, ID_SONG_BROWSER_SONGS);
      send_message(grid, tgSetMinTileWidth, 220, NULL);
      songs_rebuild(win, st, -1);
      set_focus(grid);
      return true;
    }
    case evCommand:
      switch (HIWORD(wparam)) {
        case tgnActivate: songs_dlg_activate(win, st, LOWORD(wparam)); return true;
        case tgnSelChange: songs_update_actions(win, st, songs_selection(win, st)); return true;
        case btnClicked:
          switch (LOWORD(wparam)) {
            case ID_SONG_BROWSER_OPEN:   songs_dlg_activate(win, st, (int)send_message(get_window_item(win, ID_SONG_BROWSER_SONGS), tgGetSelection, 0, NULL)); return true;
            case ID_SONG_BROWSER_DELETE: songs_dlg_delete(win, st, songs_selection(win, st)); return true;
            case ID_SONG_BROWSER_CANCEL: end_dialog(win, 0);                              return true;
            default: return false;
          }
        default: return false;
      }
    case evDestroy:
      if (st) free(st->rows);
      free(st);
      win->userdata = NULL;
      return true;
    default: return false;
  }
}

bool app_show_songs(window_t *owner) {
  return g_app && show_dialog_from_form(&groove_song_browser_form, NULL, owner, songs_proc, NULL) == 1;
}

bool app_show_startup_songs(window_t *owner) {
  window_t *win = create_window_from_form(&groove_song_browser_form, 0, 0, NULL, songs_proc, g_app->hinstance, NULL);
  if (!win) {
    fprintf(stderr, "[gr] startup song browser creation failed\n");
    fflush(stderr);
    return false;
  }
  if (owner) window_set_owner(win, owner);
  show_window(get_window_item(win, ID_SONG_BROWSER_CANCEL), false);
  window_layout_sync(win);
  show_window(win, true);
  maximize_window(win);
  return true;
}
