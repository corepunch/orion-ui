// VIEW: the arrangement above, the sound library below; the splitter between them is the only layout choice.

#include "groove.h"

static void show_more_actions(window_t *win) {
  irect16_t r = {0};
  if (!send_message(win, tbGetItemRect, ID_MAIN_WINDOW_MORE, &r) ||
      !show_popup_menu(win, CONTEXT_MENU_MORE_ITEMS, CONTEXT_MENU_MORE_COUNT,
                       window_screen_x(win) + r.x, window_screen_y(win) + toolbar_content_offset(win) + r.y + r.h)) {
    fprintf(stderr, "[groove] more actions menu unavailable win=%u\n", win->id);
    fflush(stderr);
  }
}

result_t main_win_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  groove_t *app = g_app;
  if (!app) return false; // late message after app_shutdown
  switch (msg) {
    case evCreate: {
      app->win = win;
      app->toolbar = win;
      app->accel = load_accelerators(groove_default_accels, ARRAY_LEN(groove_default_accels));
      toolbar_state_t *toolbar = toolbar_get_state(win);
      if (!toolbar || !toolbar->item_count)
        send_message(win, tbSetItems, (uint32_t)groove_main_window_form.toolbar_count, (void *)groove_main_window_form.toolbar_items);
#ifdef AX_PLATFORM_IOS
      send_message(win, tbSetButtonSize, BUTTON_HEIGHT + 4, NULL);
#endif
      if (app->sheet) {
        if (!app->tracks) {
          fprintf(stderr, "[groove] track headers unavailable parent=%u\n", (unsigned)win->id);
          fflush(stderr);
          return false;
        }
        app->library = create_window_from_form(&groove_library_window_form, 0, 0, win,
                                                win_transport, app->hinstance, NULL);
        if (!app->library) {
          fprintf(stderr, "[groove] library form creation failed parent=%u\n", (unsigned)win->id);
          fflush(stderr);
          return false;
        }
        dock_window(app->tracks, DOCK_LEFT, DOCK_EDGE(DOCK_LEFT), DOCK_NOFLOAT, GR_SHEET_HEADER_W, GR_SHEET_HEADER_W);
        dock_window(app->sheet, DOCK_FILL, 0, DOCK_NOFLOAT, 0, 100);
        dock_window(app->library, DOCK_BOTTOM, DOCK_EDGE(DOCK_BOTTOM) | DOCK_EDGE(DOCK_RIGHT), DOCK_RESIZABLE | DOCK_NOFLOAT | DOCK_OVERLAY_SPLITTER, 340, 100);
        app->timer = axSetTimer(win, 33, NULL, true);
        return true;
      }
      app->sheet = create_window("Arrangement", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_HSCROLL,
                                  MAKERECT(0, 0, 1, 1), win, win_sheet, app->hinstance, NULL);
      app->tracks = create_window("Tracks", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOACTIVATE | WINDOW_NOTABSTOP | WINDOW_NODRAG,
                                  MAKERECT(0, 0, GR_SHEET_HEADER_W, 1), win, win_tracks, app->hinstance, NULL);
      app->library = create_window("Library", WINDOW_NOTITLE | WINDOW_NORESIZE | WINDOW_NOCLOSE | WINDOW_NOCOLLAPSE,
                                    MAKERECT(0, 0, 800, 340), win, win_transport, app->hinstance, NULL);
      dock_window(app->library, DOCK_BOTTOM, DOCK_EDGE(DOCK_BOTTOM) | DOCK_EDGE(DOCK_RIGHT), DOCK_RESIZABLE | DOCK_NOFLOAT | DOCK_OVERLAY_SPLITTER, 340, 100);
      if (app->tracks) dock_window(app->tracks, DOCK_LEFT, DOCK_EDGE(DOCK_LEFT), DOCK_NOFLOAT, GR_SHEET_HEADER_W, GR_SHEET_HEADER_W);
      dock_window(app->sheet, DOCK_FILL, 0, DOCK_NOFLOAT, 0, 100);
      app->timer = axSetTimer(win, 33, NULL, true);
      return true;
    }
    case evPaint: return false;
    case evTimer:
      if (app->song.playing || app->shown_playing) invalidate_window(app->sheet);
      if (app->song.playing != app->shown_playing) transport_refresh(); // the mixer stopped at the song's end
      app->peak_credit = GR_PEAKS_PER_TICK;
      if (app->peaks_pending) {
        app->peaks_pending = false;
        if (app->library) invalidate_window(app->library);
        invalidate_window(app->sheet);
      }
      return true;
    case evCommand:
      if (HIWORD(wparam) == btnClicked && LOWORD(wparam) == ID_MAIN_WINDOW_MORE) { show_more_actions(win); return true; }
      if (HIWORD(wparam) == kAcceleratorNotification || HIWORD(wparam) == kMenuBarNotificationItemClick || HIWORD(wparam) == btnClicked) { app_command(LOWORD(wparam)); return true; }
      return false;
    case evClose:
#ifndef AX_PLATFORM_IOS
      if (app_confirm_replace_song(win)) ui_request_quit();
#endif
      return true;
    case evDestroy:
      if (app->accel) free_accelerators(app->accel);
      app->accel = NULL;
      app->toolbar = NULL;
      return true;
    default: return false;
  }
}
