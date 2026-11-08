// VIEW: the track header column beside the sheet — one row per track holding its mute and
// solo buttons. Rows are windows and the buttons are push-like Buttons, so
// they get hover, press, focus and tooltips from the framework; a click reaches
// app_command(ID_MUTE(t) / ID_SOLO(t)). Row heights follow the sheet's lanes (sheet_row_h).

#include "groove.h"

int track_family(int track) {
  if (!g_app || track < 0 || track >= GR_TRACKS) return -1;
  const song_t *song = &g_app->song;
  int first = -1;
  for (int i = 0; i < song->nclips; i++)
    if (song->clips[i].track == track && (first < 0 || song->clips[i].position < song->clips[first].position)) first = i;
  const block_t *block = first >= 0 ? block_get(song->clips[first].block) : NULL;
  return block ? (int)block->family : (int)g_app->song.track_family[track];
}

void app_reset_tracks(void) {
  if (!g_app) return;
  g_app->selected_track = 0;
}

void app_select_track(int track) {
  if (!g_app || track < 0 || track >= GR_TRACKS) {
    fprintf(stderr, "[tk] selection rejected track=%d\n", track);
    fflush(stderr);
    return;
  }
  g_app->selected_track = track;
  tracks_sync();
  if (g_app->sheet) invalidate_window(g_app->sheet);
}

bool track_accepts_block(int track, int block, int from_clip) {
  if (!g_app || track < 0 || track >= GR_TRACKS) return false;
  (void)from_clip;
  return block_get(block) != NULL;
}

typedef struct { int track; window_t *mute, *solo; } track_row_t;

static window_t *make_toggle(window_t *row, int id, const char *icon, const char *tip) {
  window_t *b = create_window("", BUTTON_PUSHLIKE | WINDOW_NOACTIVATE | WINDOW_NOTABSTOP,
                              MAKERECT(0, 0, 1, 1), row, win_button, row->hinstance, NULL);
  if (!b) {
    fprintf(stderr, "[tk] toggle allocation failed row=%u id=%d\n", row->id, id);
    fflush(stderr);
    return NULL;
  }
  b->id = (uint32_t)id;
  send_message(b, btnSetIconName, 0, (void *)icon);
  send_message(b, btnSetTooltip, 0, (void *)tip);
  return b;
}

static void row_layout(window_t *row) {
  track_row_t *tr = row->userdata;
  if (!tr) return;
  int size = MAX(1, MIN(20, (row->frame.h - 2) / 2));
  irect16_t pair = rect_center(rect_split_right(R(0, 0, row->frame.w, row->frame.h), size + 4), size, size * 2 + 2);
  irect16_t mute = rect_split_top(pair, size);
  if (tr->mute) { move_window(tr->mute, mute.x, mute.y); resize_window(tr->mute, mute.w, mute.h); }
  if (tr->solo) { move_window(tr->solo, mute.x, mute.y + size + 2); resize_window(tr->solo, mute.w, mute.h); }
}

static void track_popup(window_t *win, int track) {
  menu_item_t items[CONTEXT_MENU_TRACK_COUNT];
  memcpy(items, CONTEXT_MENU_TRACK_ITEMS, sizeof(items));
  bool full = !app_can_duplicate_track(track);
  items[0].disabled = full;
  items[1].disabled = true;
  if (!show_popup_menu(win, items, full ? CONTEXT_MENU_TRACK_COUNT : 1,
                       window_screen_x(win) + win->frame.w, window_screen_y(win))) {
    fprintf(stderr, "[tk] track menu unavailable track=%d\n", track);
    fflush(stderr);
  }
}

static result_t win_track_header(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  track_row_t *tr = win->userdata;
  switch (msg) {
    case evCreate:
      if (!(tr = allocate_window_data(win, sizeof(*tr)))) {
        fprintf(stderr, "[tk] row state allocation failed win=%u\n", (unsigned)win->id);
        fflush(stderr);
        return false;
      }
      tr->track = (int)(intptr_t)lparam;
      tr->mute = make_toggle(win, ID_MUTE(tr->track), "phosphor-speaker-slash-fill", ui_tr("Mute track"));
      tr->solo = make_toggle(win, ID_SOLO(tr->track), "phosphor-headphones-fill", ui_tr("Solo track"));
      return true;
    case evResize: row_layout(win); return false;
    case evPaint: {
      if (!tr || !g_app) return true;
      irect16_t r = get_client_rect(win);
      fill_rect(get_sys_color(brPanelDarker), r);
      bool suggested = g_app->selected_track == tr->track;
      if (suggested) fill_rect(color_with_alpha(get_sys_color(brAccent), 0x50), r);
      fill_rect(get_sys_color(brDarkEdge), R(0, r.h - 1, r.w, 1));
      fill_rect(track_color(tr->track), R(0, 2, 3, r.h - 5));
      char number[16];
      snprintf(number, sizeof(number), "%d", tr->track + 1);
      irect16_t label = rect_trim_right(r, 24);
      draw_text(FONT_SYSTEM, number, label.x + label.w / 2 - text_strwidth(FONT_SYSTEM, number) / 2,
                label.y + (label.h - text_char_height(FONT_SYSTEM)) / 2, get_sys_color(brTextNormal));
      if (tr->mute) tr->mute->value = g_app->song.mute[tr->track];
      if (tr->solo) tr->solo->value = g_app->song.solo[tr->track];
      for (window_t *c = win->children; c; c = c->next) send_message(c, evPaint, 0, NULL);
      return true;
    }
    case evGetTooltipText: {
      if (!tr || !lparam) return false;
      snprintf(lparam, 256, ui_tr("Track %d"), tr->track + 1);
      return true;
    }
    case evGetTooltipPlacement:
      if (!lparam) return false;
      *(tooltip_placement_t *)lparam = TOOLTIP_PLACE_RIGHT;
      return true;
    case evHitTest: return true;
    case evLeftButtonDown: if (tr) app_select_track(tr->track); return true;
    case evQueryLongPress: return tr && (int16_t)LOWORD(wparam) < win->frame.w - 24;
    case evLongPress: if (tr) track_popup(win, tr->track); return true;
    case evRightButtonDown: if (tr && (int16_t)LOWORD(wparam) < win->frame.w - 24) { track_popup(win, tr->track); return true; } return false;
    case evCommand:
      if (tr && HIWORD(wparam) == kMenuBarNotificationItemClick && LOWORD(wparam) == ID_TRACK_DUPLICATE) {
        app_duplicate_track(tr->track);
        return true;
      }
      return false;
    case evDestroy: free(tr); win->userdata = NULL; return true;
    default: return false;
  }
}

// Row t sits level with the sheet's lane t, below the ruler.
static void tracks_layout(window_t *win) {
  int row_h = sheet_row_h(g_app->sheet), i = 0;
  for (window_t *c = win->children; c; c = c->next, i++) {
    move_window(c, 0, GR_RULER_H + i * row_h);
    resize_window(c, win->frame.w, row_h);
  }
}

void tracks_sync(void) {
  if (g_app && g_app->tracks) { tracks_layout(g_app->tracks); invalidate_window(g_app->tracks); }
}

result_t win_tracks(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      if (g_app) g_app->tracks = win;
      for (int t = 0; t < GR_TRACKS; t++)
        if (!create_window("", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOACTIVATE | WINDOW_NOTABSTOP,
                           MAKERECT(0, 0, 1, 1), win, win_track_header, win->hinstance, (void *)(intptr_t)t)) {
          fprintf(stderr, "[tk] row allocation failed track=%d\n", t);
          fflush(stderr);
          return false;
        }
      return true;
    case evResize: if (g_app && g_app->sheet) tracks_layout(win); return false;
    case evPaint: {
      irect16_t r = get_client_rect(win);
      fill_rect(get_sys_color(brPanelDarker), r);
      fill_rect(get_sys_color(brPanelDark), R(0, 0, r.w, GR_RULER_H));
      for (window_t *c = win->children; c; c = c->next)
        if (window_has_state(c, WINDOW_STATE_VISIBLE)) send_message(c, evPaint, 0, NULL);
      return true;
    }
    case evHitTest: return false;
    case evDestroy: {
      if (g_app && g_app->tracks == win) g_app->tracks = NULL;
      return true;
    }
    default: return false;
  }
}
