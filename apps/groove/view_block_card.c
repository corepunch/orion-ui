#include "groove.h"

#define CARD_SLOP 5

typedef struct {
  int block;
  ctrl_state_t state;
  ipoint16_t press;
  bool down, hover;
} block_card_t;

static void card_screen(window_t *win, int mx, int my, int *sx, int *sy) {
  *sx = window_screen_x(win) + mx;
  *sy = window_screen_y(win) + my;
}

void clip_skin_load(groove_t *app) {
  char path[1024];
  snprintf(path, sizeof(path), "%s/../share/groove/skins/clip-cards.png", ui_get_exe_dir());
  if (!image_atlas_load(&app->card_atlas, path)) return;
  const int columns[] = {23, 241, 459, 677, 895, 1113, 1332, 1550};
  const int rows[] = {96, 233, 371, 513, 656}, heights[] = {120, 122, 125, 125, 120};
  for (int col = 0; col < ARRAY_LEN(app->card_backgrounds); col++) {
    image_background_t *bg = &app->card_backgrounds[col];
    bg->atlas = &app->card_atlas;
    bg->source_border = R(28, 0, 28, 0);
    bg->border = R(8, 0, 8, 0);
    for (int row = 0; row < IMAGE_BG_COUNT; row++) bg->states[row] = R(columns[col], rows[row], 202, heights[row]);
    if (!image_background_validate(bg)) {
      image_atlas_free(&app->card_atlas);
      return;
    }
  }
}

static void paint_block_card(int block, const block_t *b, irect16_t r, uint32_t color, ctrl_state_t state) {
  r = rect_trim_bottom(rect_trim_top(rect_inset(r, 1), 1), 1);
  static const uint8_t skins[CAT_COUNT] = {0, 1, 2, 3, 4, 5, 7, 2, 5, 0, 5, 7, 1, 6, 7, 1};
  bool skinned = g_app->card_atlas.tex && draw_image_background(r, &g_app->card_backgrounds[skins[b->cat]], state);
  if (!skinned) draw_gradient_card(r, state, color);
  r = rect_inset(r, skinned ? 6 : get_theme()->card_ring_width);
  int radius = skinned ? 0 : MAX(0, get_theme()->card_corner_radius - get_theme()->card_ring_width);
  irect16_t wave = rect_offset(r, 0, skinned ? 5 : 0);
  uint32_t ink = color_with_alpha(get_sys_color(brTextOnColor), (color >> 24) * 0x99 / 255);
  if (wave.w > 0 && wave.h > 0) {
    uint32_t texture = waveform_texture(g_app, block, (ipoint16_t){wave.w, wave.h}, radius);
    if (texture) draw_sprite_region(texture, wave, NULL, ink, 0);
  }
  draw_text_ellipsized(FONT_SMALL, b->name, r.x + 4, r.y + 3, r.w - 8, color_with_alpha(get_sys_color(brTextOnColor), color >> 24));
}

result_t win_block_card(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  block_card_t *st = win->userdata;
  if (!g_app && msg != evDestroy) return false;
  switch (msg) {
    case evCreate:
      if (!(st = allocate_window_data(win, sizeof(*st)))) {
        fprintf(stderr, "[gr] card allocation failed win=%u\n", (unsigned)win->id);
        fflush(stderr);
        return false;
      }
      st->block = (int)(intptr_t)lparam;
      return true;
    case evPaint: {
      const block_t *b = block_get(st->block);
      if (!b) return true;
      bool lit = st->hover && !win->drag_visual;
      ctrl_state_t state = st->state | (st->down && !win->drag_visual ? CTRL_PRESSED : lit ? CTRL_HOVER : CTRL_NORMAL);
      if (window_has_state(win, WINDOW_STATE_DISABLED)) state |= CTRL_DISABLED;
      paint_block_card(st->block, b, get_client_rect(win), category_color(b->cat), state);
      return true;
    }
    case grCardSetBlock:
      if (!block_get((int)wparam)) {
        fprintf(stderr, "[gr] card block rejected win=%u block=%u\n", (unsigned)win->id, (unsigned)wparam);
        fflush(stderr);
        return false;
      }
      if (st->block != (int)wparam) { st->block = (int)wparam; invalidate_window(win); }
      return true;
    case grCardSetState:
      if (st->state != (ctrl_state_t)wparam) { st->state = (ctrl_state_t)wparam; invalidate_window(win); }
      return true;
    case evMeasure: {
      layout_measure_t *m = lparam;
      const block_t *b = block_get(st->block);
      if (m && b) {
        ipoint16_t size = clip_cell_size(g_app->sheet, b);
        m->desired_w = size.x; m->desired_h = size.y;
      }
      return true;
    }
    case evMouseMove: {
      if (win->parent == g_app->sheet) return false;
      int mx = (int16_t)LOWORD(wparam), my = (int16_t)HIWORD(wparam);
      bool inside = rect_contains_point(get_client_rect(win), (ipoint16_t){ (int16_t)mx, (int16_t)my });
      if (inside != st->hover) { st->hover = inside; invalidate_window(win); }
      track_mouse(win);
      if (!st->down) return true;
      int sx, sy;
      card_screen(win, mx, my, &sx, &sy);
      if (!win->drag_visual && abs(mx - st->press.x) + abs(my - st->press.y) > CARD_SLOP) {
        g_app->drag = (drag_t){ .active = true, .block = st->block, .from_clip = -1, .grab = st->press, .track = -1 };
        window_set_drag_visual(win, mx - st->press.x, my - st->press.y);
      }
      if (win->drag_visual) {
        window_set_drag_visual(win, mx - st->press.x, my - st->press.y);
        send_message(g_app->sheet, shDragOver, MAKEDWORD(sx, sy), NULL);
      }
      return true;
    }
    case evMouseLeave:
      if (win->parent == g_app->sheet) return false;
      if (st->hover) { st->hover = false; invalidate_window(win); }
      return true;
    case evLeftButtonDown:
      if (win->parent == g_app->sheet) return false;
      st->down = true;
      st->press = (ipoint16_t){ (int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam) };
      set_capture(win);
      invalidate_window(win);
      return true;
    case evLeftButtonUp: {
      if (win->parent == g_app->sheet) return false;
      if (!st->down) return false;
      int mx = (int16_t)LOWORD(wparam), my = (int16_t)HIWORD(wparam), sx, sy;
      card_screen(win, mx, my, &sx, &sy);
      if (win->drag_visual) send_message(g_app->sheet, shDrop, MAKEDWORD(sx, sy), NULL);
      else app_preview(st->block);
      window_clear_drag_visual(win);
      st->down = false;
      set_capture(NULL);
      invalidate_window(win);
      return true;
    }
    case evPointerCancel:
      if (win->parent == g_app->sheet) return false;
      if (!st->down) return false;
      if (win->drag_visual) send_message(g_app->sheet, shDragEnd, 0, NULL);
      window_clear_drag_visual(win);
      st->down = false;
      set_capture(NULL);
      invalidate_window(win);
      return true;
    case evDestroy:
      if (st && st->down) set_capture(NULL);
      free(st);
      win->userdata = NULL;
      return true;
    default: return false;
  }
}
