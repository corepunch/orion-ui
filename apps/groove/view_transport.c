// Transport toolbar hosted in the library header.

#include "groove.h"
#include <orion/user/toolbar.h>

static const toolbar_item_t kTransportItems[] = {
  { TOOLBAR_ITEM_BUTTON,    ID_REWIND,   "strip:0", 0, 0, NULL, "Rewind (Home)" },
  { TOOLBAR_ITEM_BUTTON,    ID_PLAY,     "strip:1", 0, 0, NULL, "Play / pause (Space)" },
  { TOOLBAR_ITEM_BUTTON,    ID_STOP,     "strip:3", 0, 0, NULL, "Stop" },
  { TOOLBAR_ITEM_BUTTON,    ID_LOOP,     "strip:4", 0, 0, NULL, "Loop (L)" },
  { TOOLBAR_ITEM_SEPARATOR, 0,          NULL,      0, 0, NULL, NULL },
  { TOOLBAR_ITEM_BUTTON,    ID_BPM_DOWN, "strip:5", 0, 0, NULL, "Slower" },
  { TOOLBAR_ITEM_BUTTON,    ID_BPM_UP,   "strip:6", 0, 0, NULL, "Faster" },
  { TOOLBAR_ITEM_SPACER,    0,          NULL,      0, 0, NULL, NULL },
  { TOOLBAR_ITEM_BUTTON,    ID_DELETE,   "strip:7", 0, 0, NULL, "Remove selected block (Delete)" },
  { TOOLBAR_ITEM_SPACER,    0,          NULL,      0, TOOLBAR_ITEM_FLAG_FLEXSPACE, NULL, NULL },
  { TOOLBAR_ITEM_TEXTEDIT,  ID_SEARCH,  "search", 240, 0, NULL, "Search sounds" },
};

void transport_refresh(void) {
  window_t *win = g_app->library;
  if (!win) return;
  send_message(win, tbSetItemIcon, ID_PLAY, (void *)(g_app->song.playing ? "strip:2" : "strip:1"));
  send_message(win, tbSetActiveButton, g_app->song.loop ? ID_LOOP : 0, NULL);
}

result_t win_transport(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      g_app->library = win;
      char strip_path[1024];
      int n = snprintf(strip_path, sizeof(strip_path), "%s/../share/groove/icons/transport.png", ui_get_exe_dir());
      irect16_t regions[40];
      const int row_y[] = {28, 215, 417, 592, 780}, row_h[] = {177, 195, 168, 179, 177};
      for (int row = 0; row < 5; row++)
        for (int col = 0; col < 8; col++) regions[row * 8 + col] = R(17 + col * 195, row_y[row], 195, row_h[row]);
      toolbar_atlas_t atlas = {strip_path, 8, ARRAY_LEN(regions), regions};
      if (g_ui_runtime.running && n > 0 && (size_t)n < sizeof(strip_path)) send_message(win, tbLoadAtlas, 0, &atlas);
      send_message(win, tbSetStyle, TOOLBAR_STYLE_STATE_STRIP | TOOLBAR_STYLE_IMAGE_BUTTONS, NULL);
#ifdef AX_PLATFORM_IOS
      send_message(win, tbSetButtonSize, BUTTON_HEIGHT + 4, NULL);
#else
      send_message(win, tbSetButtonSize, TB_SPACING, NULL);
#endif
      send_message(win, tbSetItems, ARRAY_LEN(kTransportItems), (void *)kTransportItems);
      window_t *search = get_window_item(win, ID_SEARCH);
      if (search) send_message(search, edSetPlaceholder, 0, "Search sounds...");
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
    case evPaint: return true;
    case evDestroy:
      if (g_app && g_app->library == win) g_app->library = NULL;
      return true;
    default: return false;
  }
}
