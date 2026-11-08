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
  window_t *list = get_window_item(win, ID_LIBRARY_WINDOW_FAMILY_LIST);
  if (list) send_message(list, cbSetCurrentSelection, g_app->family, NULL);
}

static void library_selector_layout(window_t *win) {
  window_t *families = get_window_item(win, ID_LIBRARY_WINDOW_FAMILIES);
  window_t *list = get_window_item(win, ID_LIBRARY_WINDOW_FAMILY_LIST);
  if (!families || !list || !win->dock) return;
  bool compact = win->dock->side == DOCK_RIGHT;
  if (window_has_state(families, WINDOW_STATE_VISIBLE) == !compact) return;
  show_window(families, !compact);
  show_window(list, compact);
}

result_t win_transport(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      g_app->library = win;
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
        window_t *list = create_window("", WINDOW_NOTITLE | WINDOW_HIDDEN | CONTROL_SIZE_LARGE,
                                       MAKERECT(0, 0, 1, CONTROL_HEIGHT_LARGE), win, win_combobox, win->hinstance, NULL);
        if (list) {
          list->id = ID_LIBRARY_WINDOW_FAMILY_LIST;
          dock_window(list, DOCK_TOP, DOCK_EDGE(DOCK_TOP), DOCK_NOFLOAT, CONTROL_HEIGHT_LARGE, CONTROL_HEIGHT_LARGE);
        }
        g_app->bin = create_window("Sounds", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL | WINDOW_NOACTIVATE,
                                   MAKERECT(0, 0, 1, 1), win, win_bin, win->hinstance, NULL);
        if (g_app->bin) dock_window(g_app->bin, DOCK_FILL, 0, DOCK_NOFLOAT, 0, 0);
      }
      window_t *families = get_window_item(win, ID_LIBRARY_WINDOW_FAMILIES);
      window_t *list = get_window_item(win, ID_LIBRARY_WINDOW_FAMILY_LIST);
      if (families && list) {
        const char *p = families->title;
        while (*p) {
          const char *end = strchr(p, '|');
          char label[SEGMENTED_LABEL_MAX];
          size_t n = end ? (size_t)(end - p) : strlen(p);
          n = MIN(n, sizeof(label) - 1); memcpy(label, p, n); label[n] = 0;
          send_message(list, cbAddString, 0, label);
          if (!end) break;
          p = end + 1;
        }
      }
      if (g_app->bin) send_message(g_app->bin, binPopulate, 0, NULL);
      transport_refresh();
      return true;
    }
    case evResize: library_selector_layout(win); return false;
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
