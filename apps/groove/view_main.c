// VIEW: main window — transport toolbar on top, the sheet filling the middle,
// the block bin (one tab per instrument family) docked at the bottom.

#include "groove.h"

#define BIN_H 168

void toolbar_refresh(window_t *win) {
  const song_t *s = &g_app->song;
  static toolbar_item_t items[] = {   // static: the toolbar keeps the pointer
    { TOOLBAR_ITEM_BUTTON,    ID_REWIND,   "rewind",  0, 0, NULL, "Rewind (Home)" },
    { TOOLBAR_ITEM_BUTTON,    ID_PLAY,     "play",    0, 0, NULL, "Play / pause (Space)" },
    { TOOLBAR_ITEM_BUTTON,    ID_STOP,     "square",  0, 0, NULL, "Stop" },
    { TOOLBAR_ITEM_BUTTON,    ID_LOOP,     "refresh", 0, 0, NULL, "Loop (L)" },
    { TOOLBAR_ITEM_SEPARATOR, 0,           NULL,      0, 0, NULL, NULL },
    { TOOLBAR_ITEM_BUTTON,    ID_BPM_DOWN, "minus",   0, 0, NULL, "Slower" },
    { TOOLBAR_ITEM_BUTTON,    ID_BPM_UP,   "plus",    0, 0, NULL, "Faster" },
    { TOOLBAR_ITEM_SPACER,    0,           NULL,      0, 0, NULL, NULL },
    { TOOLBAR_ITEM_BUTTON,    ID_DELETE,   "trash",   0, 0, NULL, "Remove selected block (Delete)" },
  };
  items[1].icon = s->playing ? "pause" : "play";
  send_message(win, tbSetItems, ARRAY_LEN(items), (void *)items);
  if (s->loop) send_message(win, tbSetActiveButton, ID_LOOP, NULL);
}

static void layout(window_t *win) {
  groove_t *app = g_app;
  irect16_t cr = get_client_rect(win), bin = rect_split_bottom(cr, BIN_H), top = rect_trim_bottom(cr, BIN_H);
  if (app->sheet) { move_window(app->sheet, top.x, top.y); resize_window(app->sheet, top.w, top.h); }
  if (app->tabs)  { move_window(app->tabs, bin.x, bin.y);  resize_window(app->tabs, bin.w, bin.h); }
}

static const accel_t kAccel[] = {
  { FVIRTKEY, AX_KEY_SPACE,     ID_PLAY    },
  { FVIRTKEY, AX_KEY_HOME,      ID_REWIND  },
  { FVIRTKEY, AX_KEY_L,         ID_LOOP    },
  { FVIRTKEY, AX_KEY_BACKSPACE, ID_DELETE  },
};

result_t main_win_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  groove_t *app = g_app;
  if (!app) return false; // late message after app_shutdown
  switch (msg) {
    case evCreate: {
      app->win = win;
      app->accel = load_accelerators(kAccel, ARRAY_LEN(kAccel));
      toolbar_refresh(win);
      irect16_t cr = get_client_rect(win);
      app->sheet = create_window("sheet", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_HSCROLL, MAKERECT(0, 0, cr.w, cr.h - BIN_H), win, win_sheet, 0, NULL);
      app->tabs  = create_window("bin", WINDOW_NOTITLE | WINDOW_NOFILL, MAKERECT(0, cr.h - BIN_H, cr.w, BIN_H), win, win_tabview, 0, NULL);
      app->tabs->id = ID_TABS;
      for (int c = 0; c < CAT_COUNT; c++)
        create_window(kCategoryName[c], WINDOW_NOTITLE | WINDOW_NOFILL, MAKERECT(0, 0, cr.w, BIN_H), app->tabs, win_bin, 0, (void *)(intptr_t)c);
      app->timer = axSetTimer(win, 33, NULL, true);
      layout(win);
      app_update_status();
      return true;
    }
    case evResize: layout(win); return false;
    case evTimer:
      if (app->song.playing) { invalidate_window(app->sheet); app_update_status(); }
      return true;
    case tbButtonClick: app_command((uint16_t)wparam); return true;
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
