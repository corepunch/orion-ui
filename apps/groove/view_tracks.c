// VIEW: the track header column beside the sheet — one row per track holding its mute and
// solo buttons. Rows are windows and the buttons are push-like Buttons, so
// they get hover, press, focus and tooltips from the framework; a click reaches
// app_command(ID_MUTE(t) / ID_SOLO(t)). Row heights follow the sheet's lanes (sheet_row_h).

#include "groove.h"
#include <orion/user/svg_icon_loader.h>

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
  app_set_family(track_family(track));
  tracks_sync();
  if (g_app->sheet) invalidate_window(g_app->sheet);
}

bool track_accepts_block(int track, int block, int from_clip) {
  if (!g_app || track < 0 || track >= GR_TRACKS) return false;
  const block_t *sample = block_get(block);
  if (!sample) return false;
  if (from_clip >= 0 && from_clip < g_app->song.nclips &&
      g_app->song.clips[from_clip].track == track && g_app->song.clips[from_clip].block == block) return true;
  return track_family(track) == (int)sample->family;
}

static const char *track_icon(int family, bool suggested) {
  static const char *const regular[FAMILY_COUNT] = {
    "phosphor-waveform", "phosphor-waveform", "phosphor-guitar", "phosphor-guitar", "phosphor-piano-keys",
    "phosphor-piano-keys", "phosphor-sparkle", "phosphor-microphone", "phosphor-vinyl-record", "phosphor-sparkle", "phosphor-music-note",
  };
  static const char *const filled[FAMILY_COUNT] = {
    "phosphor-waveform-fill", "phosphor-waveform-fill", "phosphor-guitar-fill", "phosphor-guitar-fill", "phosphor-piano-keys-fill",
    "phosphor-piano-keys-fill", "phosphor-sparkle-fill", "phosphor-microphone-fill", "phosphor-vinyl-record-fill", "phosphor-sparkle-fill", "phosphor-music-note-fill",
  };
  return family >= 0 && family < FAMILY_COUNT ? (suggested ? filled[family] : regular[family]) : "phosphor-music-note";
}

typedef struct { int track; window_t *mute, *solo; } track_row_t;
typedef struct { uint32_t icons; } tracks_t;

static void tracks_load_icons(window_t *win) {
  if (!g_ui_runtime.running) return;
  tracks_t *st = win->userdata;
  char path[1024];
  int n = snprintf(path, sizeof(path), "%s/../share/groove/track-icons/instruments.png", ui_get_exe_dir());
  if (n < 0 || (size_t)n >= sizeof(path)) {
    fprintf(stderr, "[tk] icon atlas path too long win=%u length=%d\n", win->id, n);
    fflush(stderr);
    return;
  }
  int w = 0, h = 0;
  uint8_t *pixels = load_image(path, &w, &h);
  if (!pixels || w != 1448 || h != 1086) {
    fprintf(stderr, "[tk] icon atlas unavailable win=%u path=%s size=%dx%d\n", win->id, path, w, h);
    fflush(stderr);
    if (pixels) image_free(pixels);
    return;
  }
  st->icons = R_CreateTextureSRGBA8(w, h, pixels, R_FILTER_LINEAR, R_WRAP_CLAMP);
  image_free(pixels);
  if (!st->icons) { fprintf(stderr, "[tk] icon atlas texture allocation failed win=%u\n", win->id); fflush(stderr); }
}

static bool draw_track_icon(window_t *win, int family, irect16_t rect) {
  tracks_t *st = win->parent ? win->parent->userdata : NULL;
  if (!st || !st->icons || family < 0 || family >= FAMILY_COUNT) return false;
  static const int row_y[] = {35, 390, 724};
  int x = (family % 4) * 362, y = row_y[family / 4];
  draw_sprite_region(st->icons, rect, UV_RECT(x / 1448.0f, y / 1086.0f,
                     (x + 362) / 1448.0f, (y + 362) / 1086.0f), 0xFFFFFFFFu, 0);
  return true;
}

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
      int family = track_family(tr->track);
      int icon_size = MIN(36, MIN(MAX(1, r.w - 26), MAX(1, r.h - 4)));
      irect16_t icon = rect_center(rect_trim_right(r, 24), icon_size, icon_size);
      if (!draw_track_icon(win, family, icon))
        draw_sysicon(track_icon(family, suggested), icon.x, icon.y, icon_size,
                     get_sys_color(suggested ? brAccentText : brTextNormal));
      if (tr->mute) tr->mute->value = g_app->song.mute[tr->track];
      if (tr->solo) tr->solo->value = g_app->song.solo[tr->track];
      for (window_t *c = win->children; c; c = c->next) send_message(c, evPaint, 0, NULL);
      return true;
    }
    case evGetTooltipText: {
      if (!tr || !lparam) return false;
      int family = track_family(tr->track);
      const char *name = family >= 0 ? kFamilyName[family] : "Empty track";
      snprintf(lparam, 256, "%s", ui_tr(name));
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
      if (!allocate_window_data(win, sizeof(tracks_t))) {
        fprintf(stderr, "[tk] track column allocation failed win=%u\n", win->id);
        fflush(stderr);
        return false;
      }
      tracks_load_icons(win);
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
      tracks_t *st = win->userdata;
      if (st && st->icons) R_DeleteTexture(st->icons);
      free(st); win->userdata = NULL;
      if (g_app && g_app->tracks == win) g_app->tracks = NULL;
      return true;
    }
    default: return false;
  }
}
