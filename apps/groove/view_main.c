// VIEW: the application chrome (menu bar and compact toolbar), then the sheet
// above the library: a transport and family toolbar over one sound bin.

#include "groove.h"
#include <orion/gem.h>
#include <orion/commctl/appchrome.h>

static const accel_t kAccel[] = {
  { FVIRTKEY | FCONTROL, AX_KEY_N,         ID_FILE_NEW  },
  { FVIRTKEY | FCONTROL, AX_KEY_O,         ID_FILE_OPEN },
  { FVIRTKEY | FCONTROL, AX_KEY_S,         ID_FILE_SAVE },
  { FVIRTKEY | FCONTROL, AX_KEY_Q,         ID_FILE_QUIT },
  { FVIRTKEY,            AX_KEY_SPACE,     ID_PLAY      },
  { FVIRTKEY,            AX_KEY_HOME,      ID_REWIND    },
  { FVIRTKEY,            AX_KEY_END,       ID_FORWARD   },
  { FVIRTKEY,            AX_KEY_L,         ID_LOOP      },
  { FVIRTKEY,            AX_KEY_BACKSPACE, ID_DELETE    },
  { FVIRTKEY,            AX_KEY_DEL,       ID_DELETE    },
};

result_t app_menubar_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  if (msg == evCommand && (HIWORD(wparam) == kMenuBarNotificationItemClick || HIWORD(wparam) == kAcceleratorNotification)) {
    app_command((uint16_t)LOWORD(wparam));
    return true;
  }
  return win_menubar(win, msg, wparam, lparam);
}

// The compact toolbar shares the menu row when it fits (groove.orion <toolbar>).
static result_t app_toolbar_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)lparam;
  switch (msg) {
    case evCreate: return true;
    case tbButtonClick: app_command((uint16_t)wparam); return true;
    case evDestroy:
      if (g_app && g_app->toolbar == win) g_app->toolbar = NULL;
      return false;
    default: return false;
  }
}

void create_menubar(void) {
#ifdef BUILD_AS_GEM
  g_app->menubar_win = set_app_menu(app_menubar_proc, kMenus, kNumMenus, app_command, g_app->hinstance);
  g_app->chrome = create_application_chrome("Groove Chrome", NULL, NULL, 0, app_toolbar_proc,
                                            &groove_application_toolbar, g_app->hinstance);
#else
  g_app->chrome = create_application_chrome("Groove Chrome", app_menubar_proc, kMenus, kNumMenus, app_toolbar_proc,
                                            &groove_application_toolbar, g_app->hinstance);
  g_app->menubar_win = app_chrome_menubar(g_app->chrome);
#endif
  g_app->toolbar = app_chrome_toolbar(g_app->chrome);
  g_app->accel = load_accelerators(kAccel, ARRAY_LEN(kAccel));
  if (g_app->menubar_win && g_app->accel) send_message(g_app->menubar_win, kMenuBarMessageSetAccelerators, 0, g_app->accel);
}

result_t main_win_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  groove_t *app = g_app;
  if (!app) return false; // late message after app_shutdown
  switch (msg) {
    case evCreate: {
      app->win = win;
      create_menubar();
      app->sheet = create_window("Arrangement", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_HSCROLL,
                                  MAKERECT(0, 0, 1, 1), win, win_sheet, app->hinstance, NULL);
      app->library = create_window("Library", WINDOW_TOOLBAR | WINDOW_TITLETOOLBAR | WINDOW_NORESIZE | WINDOW_NOCLOSE | WINDOW_NOCOLLAPSE,
                                    MAKERECT(0, 0, 800, 280), win, win_transport, app->hinstance, NULL);
      dock_window(app->library, DOCK_BOTTOM, DOCK_EDGE(DOCK_BOTTOM), DOCK_RESIZABLE | DOCK_NOFLOAT, 280, 100);
      dock_window(app->sheet, DOCK_FILL, 0, DOCK_NOFLOAT, 0, 100);
      app->timer = axSetTimer(win, 33, NULL, true);
      return true;
    }
    case evPaint: return false;
    case evResize: return false;
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
    case tbButtonClick:
      app_command((uint16_t)wparam);
      return true;
    case evCommand:
      if (HIWORD(wparam) == kAcceleratorNotification) { app_command(LOWORD(wparam)); return true; }
      return false;
    case evClose:
      ui_request_quit();
      return true;
    case evDestroy:
      if (app->accel) free_accelerators(app->accel);
      app->accel = NULL;
      if (app->chrome) destroy_window(app->chrome);
      app->chrome = NULL;
#ifndef BUILD_AS_GEM
      app->menubar_win = NULL; // owned by the chrome
#endif
      app->toolbar = NULL;
      return true;
    default: return false;
  }
}
