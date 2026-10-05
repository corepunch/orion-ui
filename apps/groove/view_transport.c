// Transport toolbar hosted in the library header.

#include "groove.h"
#include <orion/user/toolbar.h>

static const toolbar_item_t kTransportItems[] = {
  { TOOLBAR_ITEM_BUTTON,    ID_REWIND,   "lucide-rewind", 0, 0, NULL, "Rewind (Home)" },
  { TOOLBAR_ITEM_BUTTON,    ID_PLAY,     "lucide-play", 0, 0, NULL, "Play / pause (Space)" },
  { TOOLBAR_ITEM_BUTTON,    ID_STOP,     "lucide-square", 0, 0, NULL, "Stop" },
  { TOOLBAR_ITEM_BUTTON,    ID_LOOP,     "lucide-repeat", 0, 0, NULL, "Loop (L)" },
  { TOOLBAR_ITEM_SEPARATOR, 0,          NULL,      0, 0, NULL, NULL },
  { TOOLBAR_ITEM_BUTTON,    ID_BPM_DOWN, "lucide-minus", 0, 0, NULL, "Slower" },
  { TOOLBAR_ITEM_BUTTON,    ID_BPM_UP,   "lucide-plus", 0, 0, NULL, "Faster" },
  { TOOLBAR_ITEM_SPACER,    0,          NULL,      0, 0, NULL, NULL },
  { TOOLBAR_ITEM_BUTTON,    ID_DELETE,   "lucide-trash-2", 0, 0, NULL, "Remove selected block (Delete)" },
  { TOOLBAR_ITEM_SPACER,    0,          NULL,      0, TOOLBAR_ITEM_FLAG_FLEXSPACE, NULL, NULL },
  { TOOLBAR_ITEM_TEXTEDIT,  ID_SEARCH,  "search", 240, 0, NULL, "Search sounds" },
};

void transport_refresh(void) {
  window_t *win = g_app->library;
  if (!win) return;
  send_message(win, tbSetItemIcon, ID_PLAY, (void *)(g_app->song.playing ? "lucide-pause" : "lucide-play"));
  send_message(win, tbSetActiveButton, g_app->song.loop ? ID_LOOP : 0, NULL);
}

result_t win_transport(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      g_app->library = win;
      send_message(win, tbSetStyle, TOOLBAR_STYLE_PLASTIC, NULL);
#ifdef AX_PLATFORM_IOS
      send_message(win, tbSetButtonSize, BUTTON_HEIGHT + 4, NULL);
#else
      send_message(win, tbSetButtonSize, TB_SPACING, NULL);
#endif
      send_message(win, tbSetItems, ARRAY_LEN(kTransportItems), (void *)kTransportItems);
      static const struct { uint16_t id; uint32_t color; } colors[] = {
        { ID_REWIND, WEB(0x3689da) }, { ID_PLAY, WEB(0x48aa36) }, { ID_STOP, WEB(0xdb4960) },
        { ID_LOOP, WEB(0xe4a42d) }, { ID_BPM_DOWN, WEB(0x3689da) }, { ID_BPM_UP, WEB(0x3689da) },
        { ID_DELETE, WEB(0xa365ce) },
      };
      for (int i = 0; i < ARRAY_LEN(colors); i++) send_message(win, tbSetItemColor, colors[i].id, (void *)&colors[i].color);
      window_t *search = get_window_item(win, ID_SEARCH);
      if (search) send_message(search, edSetPlaceholder, 0, "Search sounds...");
      g_app->tabs = create_window("Sounds", WINDOW_NOTITLE | WINDOW_NOFILL,
                                  MAKERECT(0, 0, 1, 1), win, win_tabview, win->hinstance, NULL);
      g_app->tabs->id = ID_TABS;
      send_message(g_app->tabs, tcSetStyle, TAB_STYLE_SIDEBAR, NULL);
      for (int i = 0; i <= CAT_COUNT; i++) {
        int category = i == 0 ? CAT_ALL : i - 1;
        create_window(category == CAT_ALL ? "All" : kCategoryName[category], WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL,
                      MAKERECT(0, 0, 1, 1), g_app->tabs, win_bin, win->hinstance, (void *)(intptr_t)category);
      }
      dock_window(g_app->tabs, DOCK_FILL, 0, DOCK_NOFLOAT, 0, 0);
      transport_refresh();
      return true;
    }
    case tbButtonClick: app_command((uint16_t)wparam); return true;
    case evCommand:
      if (LOWORD(wparam) == ID_SEARCH && HIWORD(wparam) == ednChange) {
        char text[sizeof(g_app->search)];
        send_message((window_t *)lparam, edGetText, sizeof(text), text);
        app_set_search(text);
        return true;
      }
      return false;
    case evPaint: return false;
    case evDestroy:
      if (g_app && g_app->library == win) g_app->library = NULL;
      return true;
    default: return false;
  }
}
