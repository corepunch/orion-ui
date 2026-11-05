// Transport and the library's family filter.

#include "groove.h"
#include <orion/user/toolbar.h>

void transport_refresh(void) {
  g_app->shown_playing = g_app->song.playing;
  if (g_app->toolbar) send_message(g_app->toolbar, tbCheckButton, ID_LOOP, (void *)(intptr_t)g_app->song.loop); // compact toolbar
  window_t *win = g_app->library;
  if (!win) return;
  send_message(win, tbCheckButton, ID_PLAY, (void *)(intptr_t)g_app->shown_playing);
  window_t *families = get_window_item(win, ID_LIBRARY_WINDOW_FAMILIES);
  if (families) send_message(families, sgSetSelection, g_app->family, NULL);
}

// A library window created without the form (tests) takes the form's toolbar.
static void set_items(window_t *win) {
  send_message(win, tbSetItems, (uint32_t)groove_library_window_form.toolbar_count, (void *)groove_library_window_form.toolbar_items);
}

result_t win_transport(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      g_app->library = win;
#ifdef AX_PLATFORM_IOS
      send_message(win, tbSetButtonSize, BUTTON_HEIGHT + 4, NULL);
#else
      send_message(win, tbSetButtonSize, TB_SPACING, NULL);
#endif
      if (!toolbar_get_state(win)->item_count) {
        set_items(win);
        send_message(win, tbSetStyle, groove_library_window_form.toolbar_style, NULL);
      }
      if (!g_app->bin) {
        const form_ctrl_def_t *families_def = NULL;
        for (int i = 0; i < groove_library_window_form.child_count; i++)
          if (groove_library_window_form.children[i].id == ID_LIBRARY_WINDOW_FAMILIES) families_def = &groove_library_window_form.children[i];
        window_t *families = families_def ? create_window(ui_tr(families_def->text), WINDOW_NOTITLE | CONTROL_SIZE_LARGE,
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
      if (g_app->bin) send_message(g_app->bin, binPopulate, 0, NULL);
      transport_refresh();
      return true;
    }
    case evCommand:
      if (HIWORD(wparam) == sgnSelChange && LOWORD(wparam) == ID_LIBRARY_WINDOW_FAMILIES) {
        int family = (int)send_message((window_t *)lparam, sgGetSelection, 0, NULL);
        if (family < 0 || family >= FAMILY_COUNT) return true;
        bool occupied[GR_TRACKS] = {0};
        for (int i = 0; i < g_app->song.nclips; i++) occupied[g_app->song.clips[i].track] = true;
        int target = -1;
        if (track_family(g_app->selected_track) == family) target = g_app->selected_track;
        for (int t = 0; t < GR_TRACKS && target < 0; t++) if (track_family(t) == family) target = t;
        if (target < 0 && !occupied[g_app->selected_track]) target = g_app->selected_track;
        for (int t = 0; t < GR_TRACKS && target < 0; t++) if (!occupied[t]) target = t;
        if (target < 0) {
          transport_refresh();
          message_box(win, ui_tr("All tracks are in use. Empty a track to choose this family."), ui_tr("Sounds"), MB_OK);
          return true;
        }
        if (!occupied[target] && g_app->song.track_family[target] != (family_t)family) {
          g_app->song.track_family[target] = (family_t)family;
          g_app->song_dirty = true;
        }
        app_select_track(target);
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
