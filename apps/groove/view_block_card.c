#include "groove.h"

#define CARD_SLOP 5
#define PICTOGRAM_COLS 6
#define PICTOGRAM_ROWS 3
#define PICTOGRAM_CELL 128
#define CARD_LABEL_PADDING 4

bool block_pictograms_load(groove_t *app) {
  if (!g_ui_runtime.running || app->pictograms) return true;
  char path[1024];
  int n = snprintf(path, sizeof(path), "%s/../share/groove/icons/class-pictograms.png", ui_get_exe_dir());
  if (n < 0 || (size_t)n >= sizeof(path)) {
    fprintf(stderr, "[gr] pictogram path too long length=%d capacity=%zu\n", n, sizeof(path));
    fflush(stderr);
    return false;
  }
  int w = 0, h = 0;
  uint8_t *pixels = load_image(path, &w, &h);
  if (!pixels) {
    fprintf(stderr, "[gr] pictogram atlas load failed path=%s\n", path);
    fflush(stderr);
    return false;
  }
  if (w != PICTOGRAM_COLS * PICTOGRAM_CELL || h != PICTOGRAM_ROWS * PICTOGRAM_CELL || CAT_COUNT != PICTOGRAM_COLS * PICTOGRAM_ROWS) {
    fprintf(stderr, "[gr] pictogram atlas rejected path=%s size=%dx%d categories=%d\n", path, w, h, CAT_COUNT);
    fflush(stderr);
    image_free(pixels);
    return false;
  }
  app->pictograms = R_CreateTextureSRGBA8(w, h, pixels, R_FILTER_LINEAR, R_WRAP_CLAMP);
  image_free(pixels);
  if (!app->pictograms) {
    fprintf(stderr, "[gr] pictogram texture allocation failed size=%dx%d\n", w, h);
    fflush(stderr);
    return false;
  }
  return true;
}

bitmap_strip_t block_pictogram_strip(void) {
  return (bitmap_strip_t){ .tex = g_app ? g_app->pictograms : 0, .icon_w = PICTOGRAM_CELL, .icon_h = PICTOGRAM_CELL, .cols = PICTOGRAM_COLS,
                           .sheet_w = PICTOGRAM_COLS * PICTOGRAM_CELL, .sheet_h = PICTOGRAM_ROWS * PICTOGRAM_CELL };
}

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

// Cards scrolled out of their parent do not ask for a waveform.
static bool card_on_screen(const window_t *win) {
  irect16_t view = get_client_rect(win->parent);
  return win->frame.x < view.w && win->frame.x + win->frame.w > 0 && win->frame.y < view.h && win->frame.y + win->frame.h > 0;
}

static void paint_card_label(const block_t *b, irect16_t r, int icon_width, int visible_width, uint32_t color) {
  char fitted[512];
  int text_h = text_char_height(FONT_SMALLEST);
  int area_w = MAX(0, MIN(r.w, visible_width) - 2 * CARD_LABEL_PADDING);
  int max_text_w = MAX(0, area_w - 2 * CARD_LABEL_PADDING);
  int text_w = text_ellipsize(FONT_SMALLEST, b->name, max_text_w, fitted, sizeof(fitted));
  if (!text_w || text_w > max_text_w) return;
  int label_w = text_w + 2 * CARD_LABEL_PADDING;
  int text_x = r.x + icon_width + CARD_LABEL_PADDING;

  int label_right = r.x + MIN(r.w, visible_width) - CARD_LABEL_PADDING;
  if (text_x + label_w > label_right) text_x = label_right - label_w;
  text_x = MAX(r.x + CARD_LABEL_PADDING, text_x);
  irect16_t label = R(text_x, r.y + CARD_LABEL_PADDING, label_w, text_h + 1);
  fill_rounded_rect(color_with_alpha(0xFF000000u, 0xB8), label,
                    MIN(get_theme()->card_corner_radius, label.h / 2));
  draw_text_clipped(FONT_SMALLEST, fitted, &label, color_with_alpha(0xFFFFFFFFu, color >> 24), TEXT_ALIGN_CENTER);
}

static void paint_block_card(int block, const block_t *b, irect16_t r, int visible_width, uint32_t color, ctrl_state_t state) {
  draw_plastic_card(r, state, color); // its shadow margin is the only gap between neighbouring cards
  int icon_size = r.h * 3 / 4;
  r = rect_inset(r, MIN(2, get_theme()->plastic_shadow_size) + get_theme()->card_ring_width);
  int radius = MAX(0, get_theme()->card_corner_radius - get_theme()->card_ring_width);
  irect16_t wave = r;
  uint32_t ink = color_with_alpha(get_sys_color(brTextOnColor), (color >> 24) * 0x99 / 255);
  if (wave.w > 0 && wave.h > 0) {
    uint32_t texture = waveform_texture(g_app, block, (ipoint16_t){wave.w, wave.h}, radius);
    if (texture) draw_sprite_region(texture, wave, NULL, ink, 0);
  }
  int icon_width = 0;
  icon_size = MIN(icon_size, MIN(r.w - 8, r.h));
  if (icon_size > 0) {
    irect16_t icon = rect_center(rect_split_left(r, icon_size + 8), icon_size, icon_size);
    int col = b->cat % PICTOGRAM_COLS, row = b->cat / PICTOGRAM_COLS;
    if (g_app->pictograms) draw_sprite_region(g_app->pictograms, icon,
      UV_RECT((float)col / PICTOGRAM_COLS, (float)row / PICTOGRAM_ROWS,
              (float)(col + 1) / PICTOGRAM_COLS, (float)(row + 1) / PICTOGRAM_ROWS),
      color_with_alpha(0xFFFFFFFFu, color >> 24), 0);
    icon_width = icon_size + 8;
  }
  paint_card_label(b, r, icon_width, visible_width, color);
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
      irect16_t r = get_client_rect(win);
      int visible_width = r.w;
      if (win->parent == g_app->sheet) r.w = clip_cell_size(g_app->sheet, b).x;
      if (win->parent && card_on_screen(win)) app_block_peaks(st->block); // a stale overview stays up until the new one is ready
      paint_block_card(st->block, b, r, visible_width, category_color(b->cat), state);
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
        window_set_drag_copy(win, mx - st->press.x, my - st->press.y);
      }
      if (win->drag_visual) {
        window_set_drag_copy(win, mx - st->press.x, my - st->press.y);
        send_message(g_app->sheet, shDragOver, MAKEDWORD(sx, sy), NULL);
      }
      return true;
    }
    case evMouseLeave:
      if (win->parent == g_app->sheet) return false;
      if (st->hover) { st->hover = false; invalidate_window(win); }
      return true;
    case evQueryDrag: return win->parent == g_app->sheet ? DRAG_NONE : DRAG_AFTER_HOLD; // swipe scrolls the library, hold picks up
    case evLeftButtonDown:
      if (win->parent == g_app->sheet) return false;
      st->down = true;
      st->press = (ipoint16_t){ (int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam) };
      set_capture(win);
      invalidate_window(win);
      app_preview(st->block);
      return true;
    case evLeftButtonUp: {
      if (win->parent == g_app->sheet) return false;
      if (!st->down) return false;
      int mx = (int16_t)LOWORD(wparam), my = (int16_t)HIWORD(wparam), sx, sy;
      card_screen(win, mx, my, &sx, &sy);
      if (win->drag_visual) send_message(g_app->sheet, shDrop, MAKEDWORD(sx, sy), NULL);
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
