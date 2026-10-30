#include <string.h>
#include <stdio.h>
#include <math.h>

#include <orion/user/user.h>
#include <orion/user/messages.h>
#include <orion/user/draw.h>

#include <mapview/sprites.h>
#include <mapview/map.h>

// External functions from game code
extern sprite_t *find_sprite(const char *name);
extern mapside_texture_t const *get_flat_texture(const char *name);
extern mapside_texture_t const *get_texture(const char *name);

// Helper function to fit sprite in target rect
irect16_t fit_sprite(sprite_t const *spr, irect16_t const *target) {
  float scale = fminf(1, fminf(((float)target->w) / spr->width,
                               ((float)target->h) / spr->height));
  return (irect16_t) {
    target->x+(target->w-spr->width*scale)/2,
    target->y+(target->h-spr->height*scale)/2,
    spr->width * scale,
    spr->height * scale
  };
}

// Sprite control window procedure
result_t win_sprite(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  sprite_t const *spr;
  mapside_texture_t const *tex;
  switch (msg) {
    case evPaint:
      fill_rect(g_ui_runtime.focused == win ? get_sys_color(brSelectionTop) : get_sys_color(brControlBg), R(win->frame.x-2, win->frame.y-2, win->frame.w+4, win->frame.h+4));
      draw_button(win->frame, 1, 1, true);
      if (!*win->title) return false;
      if ((spr = find_sprite(win->title))) {
        irect16_t r = fit_sprite(spr, &win->frame);
        draw_rect(spr->texture, r);
      } else if ((tex = get_flat_texture(win->title))||(tex = get_texture(win->title))) {
        float scale = fminf(1, fminf(((float)win->frame.w) / tex->width,
                                     ((float)win->frame.h) / tex->height));
        draw_rect(tex->texture, R(win->frame.x+(win->frame.w-tex->width*scale)/2,
                  win->frame.y+(win->frame.h-tex->height*scale)/2,
                  tex->width * scale,
                  tex->height * scale));
      }
      return true;
    case evLeftButtonUp:
      send_message(win->parent, evCommand, MAKEDWORD(win->id, btnClicked), NULL);
      return true;
  }
  return false;
}
