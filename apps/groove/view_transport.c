// Transport and the library's family filter.

#include "groove.h"
#include <orion/user/toolbar.h>

void transport_refresh(void) {
  g_app->shown_playing = g_app->song.playing;
  if (g_app->toolbar) {
    send_message(g_app->toolbar, tbCheckButton, ID_LOOP, (void *)(intptr_t)g_app->song.loop);
    send_message(g_app->toolbar, tbCheckButton, ID_PLAY, (void *)(intptr_t)g_app->shown_playing);
  }
  window_t *win = g_app->library;
  if (!win) return;
  window_t *families = get_window_item(win, ID_LIBRARY_WINDOW_FAMILIES);
  if (families) send_message(families, sgSetSelection, g_app->family, NULL);
}

// Each tab carries its family's colour, the colour its cards and clips are drawn in.
static void families_mark_colors(window_t *families) {
  for (int f = 0; f < FAMILY_COUNT; f++)
    send_message(families, sgSetSegmentColor, (uint32_t)f, (void *)(uintptr_t)family_color((family_t)f));
}

result_t win_transport(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      g_app->library = win;
      if (!g_app->bin) {
        const form_ctrl_def_t *families_def = NULL;
        for (int i = 0; i < groove_library_window_form.child_count; i++)
          if (groove_library_window_form.children[i].id == ID_LIBRARY_WINDOW_FAMILIES) families_def = &groove_library_window_form.children[i];
        window_t *families = families_def ? create_window(ui_tr(families_def->text), WINDOW_NOTITLE | WINDOW_NOFOCUS | CONTROL_SIZE_LARGE,
                                  MAKERECT(0, 0, 1, CONTROL_HEIGHT_LARGE), win, win_segmented, win->hinstance, NULL) : NULL;
        if (!families) {
          fprintf(stderr, "[gr] family selector creation failed win=%u\n", (unsigned)win->id);
          fflush(stderr);
          return false;
        }
        families->id = ID_LIBRARY_WINDOW_FAMILIES;
        dock_window(families, DOCK_TOP, DOCK_EDGE(DOCK_TOP), DOCK_NOFLOAT, CONTROL_HEIGHT_LARGE, CONTROL_HEIGHT_LARGE);
        g_app->bin = create_window("Sounds", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL | WINDOW_NOACTIVATE,
                                   MAKERECT(0, 0, 1, 1), win, win_bin, win->hinstance, NULL);
        if (g_app->bin) dock_window(g_app->bin, DOCK_FILL, 0, DOCK_NOFLOAT, 0, 0);
      }
      window_t *families = get_window_item(win, ID_LIBRARY_WINDOW_FAMILIES);
      if (families) families_mark_colors(families);
#if GROOVE_APP_STORE
      window_t *store_row = get_window_item(win, ID_LIBRARY_WINDOW_STORE_ROW);
      if (store_row) window_set_state(store_row, WINDOW_STATE_VISIBLE, true);
#endif
      if (g_app->bin) send_message(g_app->bin, binPopulate, 0, NULL);
      transport_refresh();
      return true;
    }
    case evCommand:
      if (HIWORD(wparam) == sgnSelChange && LOWORD(wparam) == ID_LIBRARY_WINDOW_FAMILIES) {
        int family = (int)send_message((window_t *)lparam, sgGetSelection, 0, NULL);
        if (family < 0 || family >= FAMILY_COUNT) return true;
        app_set_family(family);
        return true;
      }
      if (HIWORD(wparam) != btnClicked) return false;
      app_command(LOWORD(wparam)); return true;
    case evPaint: return false;
    case evDestroy:
      if (g_app && g_app->library == win) g_app->library = NULL;
      return true;
    default: return false;
  }
}
