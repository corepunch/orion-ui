// Transport and the library's family filter, hosted in the library header.
// Families are plain toolbar buttons showing their pictograms at full button
// size, so a finger can hit them; the checked one is the family the bin shows.

#include "groove.h"
#include <orion/user/toolbar.h>

#define FAMILY_FLAGS TOOLBAR_ITEM_FLAG_ARTWORK

static const toolbar_item_t kTransportItems[] = {
  { TOOLBAR_ITEM_BUTTON,    ID_REWIND,     "phosphor-rewind-fill",       0, 0, NULL, "Rewind (Home)" },
  { TOOLBAR_ITEM_BUTTON,    ID_PLAY,       "phosphor-play-fill",         0, CONTROL_SIZE_LARGE, NULL, "Play / pause (Space)", 0, "phosphor-pause-fill" },
  { TOOLBAR_ITEM_BUTTON,    ID_FORWARD,    "phosphor-fast-forward-fill", 0, 0, NULL, "Go to end (End)" },
  { TOOLBAR_ITEM_SPACER,    0,             NULL,      0, TOOLBAR_ITEM_FLAG_FLEXSPACE, NULL, NULL },
  // Genre filter, hidden for now: the library always shows every genre.
  // { TOOLBAR_ITEM_SPACER,    0,          NULL,      6, 0, NULL, NULL },
  // { TOOLBAR_ITEM_SEGMENTED, ID_GENRE,   NULL,      0, 0, "All|Dance|Hip Hop|Rave|Techno", "Genre" }, // "All", then kGenreName order
};

void transport_refresh(void) {
  g_app->shown_playing = g_app->song.playing;
  if (g_app->toolbar) send_message(g_app->toolbar, tbCheckButton, ID_LOOP, (void *)(intptr_t)g_app->song.loop); // compact toolbar
  window_t *win = g_app->library;
  if (!win) return;
  send_message(win, tbCheckButton, ID_PLAY, (void *)(intptr_t)g_app->shown_playing);
  for (int cat = 0; cat < CAT_COUNT; cat++)
    send_message(win, tbCheckButton, ID_FAMILY(cat), (void *)(intptr_t)(cat == g_app->category));
}

// The transport items, then one pictogram button per family in category_t order.
static void set_items(window_t *win) {
  enum { N = ARRAY_LEN(kTransportItems) };
  toolbar_item_t items[N + CAT_COUNT];
  char icons[CAT_COUNT][16];
  memcpy(items, kTransportItems, sizeof(kTransportItems));
  for (int cat = 0; cat < CAT_COUNT; cat++) {
    snprintf(icons[cat], sizeof(icons[cat]), "strip:%d", cat);
    items[N + cat] = (toolbar_item_t){ TOOLBAR_ITEM_BUTTON, ID_FAMILY(cat), icons[cat], 0, FAMILY_FLAGS, NULL, kCategoryName[cat] };
  }
  bitmap_strip_t strip = block_pictogram_strip();
  send_message(win, tbSetStrip, 0, &strip);
  send_message(win, tbSetItems, ARRAY_LEN(items), items); // copies the icon names and tooltips
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
      set_items(win);
      static const uint32_t silver = WEB(0xd0d0d0); // iTunes transport
      static const uint16_t transport[] = { ID_REWIND, ID_PLAY, ID_FORWARD };
      for (int i = 0; i < ARRAY_LEN(transport); i++) send_message(win, tbSetItemColor, transport[i], (void *)&silver);
      g_app->bin = create_window("Sounds", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL | WINDOW_NOACTIVATE,
                                 MAKERECT(0, 0, 1, 1), win, win_bin, win->hinstance, NULL);
      if (g_app->bin) dock_window(g_app->bin, DOCK_FILL, 0, DOCK_NOFLOAT, 0, 0);
      transport_refresh();
      return true;
    }
    case tbButtonClick: app_command((uint16_t)wparam); return true;
    // case evCommand:
    //   if (LOWORD(wparam) == ID_GENRE && HIWORD(wparam) == sgnSelChange) {
    //     int selected = (int)send_message((window_t *)lparam, sgGetSelection, 0, NULL); // segment 0 is "All"
    //     app_set_genre(selected > 0 ? (uint8_t)(1 << (selected - 1)) : 0);
    //     return true;
    //   }
    //   return false;
    case evPaint: return false;
    case evDestroy:
      if (g_app && g_app->library == win) g_app->library = NULL;
      return true;
    default: return false;
  }
}
