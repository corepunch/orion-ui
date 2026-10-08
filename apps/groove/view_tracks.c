// VIEW: the track header column beside the sheet — one row per track holding its mute and
// solo buttons. Rows are windows and the buttons are ordinary plastic push-like Buttons, so
// they get hover, press, focus and tooltips from the framework; a click reaches
// app_command(ID_MUTE(t) / ID_SOLO(t)). Row heights follow the sheet's lanes (sheet_row_h).

#include "groove.h"

typedef struct { int track; window_t *mute, *solo; } track_row_t;

static window_t *make_toggle(window_t *row, int id, const char *icon, const char *tip) {
  window_t *b = create_window("", WINDOW_PLASTIC | BUTTON_PUSHLIKE | WINDOW_NOACTIVATE | WINDOW_NOTABSTOP,
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

static void toggle_sync(window_t *b, bool on, sys_color_idx_t role) {
  if (!b) return;
  b->value = on;
  uint32_t face = on ? get_sys_color(role) : get_sys_color(brControlBg);
  send_message(b, btnSetFaceColor, 0, &face);
}

static void row_layout(window_t *row) {
  track_row_t *tr = row->userdata;
  if (!tr) return;
  int size = MIN(24, (row->frame.h - 4) / 2);
  irect16_t pair = rect_center(R(0, 0, row->frame.w, row->frame.h), size, size * 2 + 2);
  irect16_t mute = rect_split_top(pair, size);
  if (tr->mute) { move_window(tr->mute, mute.x, mute.y); resize_window(tr->mute, mute.w, mute.h); }
  if (tr->solo) { move_window(tr->solo, mute.x, mute.y + size + 2); resize_window(tr->solo, mute.w, mute.h); }
}

static result_t win_track_header(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  track_row_t *tr = win->userdata;
  switch (msg) {
    case evCreate:
      if (!(tr = allocate_window_data(win, sizeof(*tr)))) return false;
      tr->track = (int)(intptr_t)lparam;
      tr->mute = make_toggle(win, ID_MUTE(tr->track), "phosphor-speaker-slash-fill", "Mute track");
      tr->solo = make_toggle(win, ID_SOLO(tr->track), "phosphor-headphones-fill", "Solo track");
      return true;
    case evResize: row_layout(win); return false;
    case evPaint: {
      if (!tr || !g_app) return true;
      irect16_t r = get_client_rect(win);
      fill_rect(get_sys_color(brPanelDarker), r);
      fill_rect(get_sys_color(brDarkEdge), R(0, r.h - 1, r.w, 1));
      fill_rect(track_color(tr->track), R(0, 2, 3, r.h - 5));
      toggle_sync(tr->mute, g_app->song.mute[tr->track], brTextError);
      toggle_sync(tr->solo, g_app->song.solo[tr->track], brTextWarning);
      for (window_t *c = win->children; c; c = c->next) send_message(c, evPaint, 0, NULL);
      return true;
    }
    case evGetTooltipText: return false;
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
    default: return false;
  }
}
