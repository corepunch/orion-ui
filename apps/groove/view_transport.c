// Transport and the library's family filter, hosted in the library header.
// Families are plain toolbar buttons showing their pictograms at full button
// size, so a finger can hit them; the checked one is the family the bin shows.

#include "groove.h"
#include <orion/user/toolbar.h>

void transport_refresh(void) {
  g_app->shown_playing = g_app->song.playing;
  if (g_app->toolbar) send_message(g_app->toolbar, tbCheckButton, ID_LOOP, (void *)(intptr_t)g_app->song.loop); // compact toolbar
  window_t *win = g_app->library;
  if (!win) return;
  send_message(win, tbCheckButton, ID_PLAY, (void *)(intptr_t)g_app->shown_playing);
  send_message(win, tbCheckButton, ID_FAMILY(g_app->category), (void *)(intptr_t)1);
}

// A library window created without the form (tests) takes the form's own toolbar:
// the transport, then one pictogram button per family in category_t order.
static void set_items(window_t *win) {
  send_message(win, tbSetItems, (uint32_t)groove_library_window_form.toolbar_count, (void *)groove_library_window_form.toolbar_items);
}

// The iTunes-style transport buttons take the theme's neutral plastic.
static void transport_tint(window_t *win) {
  static const uint16_t transport[] = { ID_REWIND, ID_PLAY, ID_FORWARD };
  uint32_t neutral = get_sys_color(brPlasticNeutral);
  for (int i = 0; i < ARRAY_LEN(transport); i++) send_message(win, tbSetItemColor, transport[i], &neutral);
}

result_t win_transport(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      g_app->library = win;
      send_message(win, tbSetStyle, TOOLBAR_STYLE_PLASTIC, NULL);
      block_pictograms_load(g_app);
      bitmap_strip_t strip = block_pictogram_strip();
      send_message(win, tbSetStrip, 0, &strip);
#ifdef AX_PLATFORM_IOS
      send_message(win, tbSetButtonSize, BUTTON_HEIGHT + 4, NULL);
#else
      send_message(win, tbSetButtonSize, TB_SPACING, NULL);
#endif
      if (!toolbar_get_state(win)->item_count) set_items(win);
      transport_tint(win);
      if (!g_app->bin) {
        g_app->bin = create_window("Sounds", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_VSCROLL | WINDOW_NOACTIVATE,
                                   MAKERECT(0, 0, 1, 1), win, win_bin, win->hinstance, NULL);
        if (g_app->bin) dock_window(g_app->bin, DOCK_FILL, 0, DOCK_NOFLOAT, 0, 0);
      }
      transport_refresh();
      return true;
    }
    case evCommand: if (HIWORD(wparam) != btnClicked) return false; app_command(LOWORD(wparam)); return true;
    // case evCommand:
    //   if (LOWORD(wparam) == ID_GENRE && HIWORD(wparam) == sgnSelChange) {
    //     int selected = (int)send_message((window_t *)lparam, sgGetSelection, 0, NULL); // segment 0 is "All"
    //     app_set_genre(selected > 0 ? (uint8_t)(1 << (selected - 1)) : 0);
    //     return true;
    //   }
    //   return false;
    case evThemeChanged: transport_tint(win); return false;
    case evPaint: return false;
    case evDestroy:
      if (g_app && g_app->library == win) g_app->library = NULL;
      return true;
    default: return false;
  }
}
