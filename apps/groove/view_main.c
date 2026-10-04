// VIEW: main window — transport toolbar on top, the sheet filling the middle,
// the block bin (one tab per instrument family) docked at the bottom.

#include "groove.h"
#include <orion/gem.h>
#include <orion/user/toolbar.h>

#define BIN_H      280
#define LIBRARY_H  44

void toolbar_refresh(window_t *win) {
  const song_t *s = &g_app->song;
  static toolbar_item_t items[] = {
    { TOOLBAR_ITEM_BUTTON,    ID_REWIND,   "strip:0",  0, 0, NULL, "Rewind (Home)" },
    { TOOLBAR_ITEM_BUTTON,    ID_PLAY,     "strip:1",  0, 0, NULL, "Play / pause (Space)" },
    { TOOLBAR_ITEM_BUTTON,    ID_STOP,     "strip:3",  0, 0, NULL, "Stop" },
    { TOOLBAR_ITEM_BUTTON,    ID_LOOP,     "strip:4",  0, 0, NULL, "Loop (L)" },
    { TOOLBAR_ITEM_SEPARATOR, 0,           NULL,      0, 0, NULL, NULL },
    { TOOLBAR_ITEM_BUTTON,    ID_BPM_DOWN, "strip:5",  0, 0, NULL, "Slower" },
    { TOOLBAR_ITEM_BUTTON,    ID_BPM_UP,   "strip:6",  0, 0, NULL, "Faster" },
    { TOOLBAR_ITEM_SPACER,    0,           NULL,      0, 0, NULL, NULL },
    { TOOLBAR_ITEM_BUTTON,    ID_DELETE,   "strip:7",  0, 0, NULL, "Remove selected block (Delete)" },
  };
  items[1].icon = s->playing ? "strip:2" : "strip:1";
  send_message(win, tbSetItems, ARRAY_LEN(items), (void *)items);
  if (s->loop) send_message(win, tbSetActiveButton, ID_LOOP, NULL);
}

static void layout(window_t *win) {
  groove_t *app = g_app;
  irect16_t cr = get_client_rect(win), bin = rect_split_bottom(cr, BIN_H), top = rect_trim_bottom(cr, BIN_H);
  irect16_t head = rect_split_top(bin, LIBRARY_H), pages = rect_trim_top(bin, LIBRARY_H);
  if (app->sheet)   { move_window(app->sheet, top.x, top.y);     resize_window(app->sheet, top.w, top.h); }
  if (app->tabs)    { move_window(app->tabs, pages.x, pages.y);  resize_window(app->tabs, pages.w, pages.h); }
  if (app->library) { move_window(app->library, head.x, head.y); resize_window(app->library, head.w, head.h); }
}

static const accel_t kAccel[] = {
  { FVIRTKEY | FCONTROL, AX_KEY_N,         ID_FILE_NEW  },
  { FVIRTKEY | FCONTROL, AX_KEY_Q,         ID_FILE_QUIT },
  { FVIRTKEY,            AX_KEY_SPACE,     ID_PLAY      },
  { FVIRTKEY,            AX_KEY_HOME,      ID_REWIND    },
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

void create_menubar(void) {
  g_app->menubar_win = set_app_menu(app_menubar_proc, kMenus, kNumMenus, app_command, g_app->hinstance);
  g_app->accel = load_accelerators(kAccel, ARRAY_LEN(kAccel));
  if (g_app->menubar_win && g_app->accel) send_message(g_app->menubar_win, kMenuBarMessageSetAccelerators, 0, g_app->accel);
}

result_t main_win_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  groove_t *app = g_app;
  if (!app) return false; // late message after app_shutdown
  switch (msg) {
    case evGetWorkspaceRect:
      *(irect16_t *)lparam = rect_trim_top(*(irect16_t *)lparam, MENUBAR_HEIGHT);
      return true;
    case evCreate: {
      app->win = win;
      if (g_ui_runtime.running) clip_skin_load(app);
      char strip_path[1024];
      int n = snprintf(strip_path, sizeof(strip_path), "%s/../share/groove/icons/transport.png", ui_get_exe_dir());
      irect16_t regions[40];
      const int row_y[] = {28, 215, 417, 592, 780}, row_h[] = {177, 195, 168, 179, 177};
      for (int row = 0; row < 5; row++)
        for (int col = 0; col < 8; col++) regions[row * 8 + col] = R(17 + col * 195, row_y[row], 195, row_h[row]);
      toolbar_atlas_t atlas = {strip_path, 8, ARRAY_LEN(regions), regions};
      if (g_ui_runtime.running && n > 0 && (size_t)n < sizeof(strip_path)) send_message(win, tbLoadAtlas, 0, &atlas);
      send_message(win, tbSetStyle, TOOLBAR_STYLE_STATE_STRIP | TOOLBAR_STYLE_IMAGE_BUTTONS, NULL);
      send_message(win, tbSetButtonSize, 48, NULL);
      toolbar_refresh(win);
      irect16_t cr = get_client_rect(win);
      app->sheet = create_window("sheet", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_HSCROLL, MAKERECT(0, 0, cr.w, cr.h - BIN_H), win, win_sheet, 0, NULL);
      app->tabs  = create_window("bin", WINDOW_NOTITLE | WINDOW_NOFILL, MAKERECT(0, cr.h - BIN_H, cr.w, BIN_H), win, win_tabview, 0, NULL);
      app->tabs->id = ID_TABS;
      send_message(app->tabs, tcSetStyle, TAB_STYLE_SIDEBAR, NULL);
      for (int i = 0; i <= CAT_COUNT; i++) { // "All" first, then one page per family
        int c = i == 0 ? CAT_ALL : i - 1;
        create_window(c == CAT_ALL ? "All" : kCategoryName[c], WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL,
                      MAKERECT(0, 0, cr.w, BIN_H), app->tabs, win_bin, 0, (void *)(intptr_t)c);
      }
      app->library = create_window("library", WINDOW_NOTITLE | WINDOW_NOFILL, MAKERECT(0, cr.h - BIN_H, cr.w, LIBRARY_H), win, win_library, 0, NULL);
      app->timer = axSetTimer(win, 33, NULL, true);
      layout(win);
      app_update_status();
      return true;
    }
    case evResize: layout(win); return false;
    case evTimer:
      if (app->song.playing) { invalidate_window(app->sheet); app_update_status(); }
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
      return true;
    default: return false;
  }
}
