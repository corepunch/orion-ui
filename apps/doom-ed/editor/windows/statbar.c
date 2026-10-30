#include <mapview/map.h>
#include <mapview/sprites.h>

result_t win_statbar(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
#ifdef HEXEN
      load_sprite("H2BAR");
      load_sprite("H2TOP");
      load_sprite("INVBAR");
      load_sprite("STATBAR");
      load_sprite("KEYBAR");
#else
      load_sprite("STBAR");
#endif
      break;
    case evPaint: {
#ifdef HEXEN
      sprite_t* STBAR = find_sprite("H2BAR");
      if (STBAR) {
        draw_sprite("H2BAR", 0, 134, 1, 1);
        //    draw_sprite("INVBAR", 38, 162, 1, 1);
      }
#else
      sprite_t* STBAR = find_sprite("STBAR");
      if (STBAR) {
        draw_sprite("STBAR", 0, DOOM_HEIGHT-STBAR->height, 1, 1.0f);
      }
#endif
      break;
    }
    default:
      break;
  }
  return false;
}
