#include "comic_shelf.h"
#include <orion/commctl/commctl_internal.h>

typedef struct {
  uint32_t back, trim;
} comic_shelf_textures_t;

static uint32_t comic_shelf_load_texture(const window_t *win, const char *name) {
  char path[4096];
  int n = snprintf(path, sizeof(path), "%s/../share/comics/shelf/%s", ui_get_exe_dir(), name);
  int w, h;
  uint8_t *pixels = n > 0 && (size_t)n < sizeof(path) ? load_image(path, &w, &h) : NULL;
  uint32_t texture = pixels ? R_CreateTextureSRGBA8(w, h, pixels, R_FILTER_LINEAR, R_WRAP_CLAMP) : 0;
  image_free(pixels);
  if (!texture) {
    fprintf(stderr, "[comic_shelf] window=%u could not load texture '%s'\n", win->id, name);
    fflush(stderr);
  }
  return texture;
}

static void comic_wood_paint(uint32_t texture, irect16_t rect, const frect_t *uv,
                            uint32_t tint, uint32_t fallback) {
  if (rect.w <= 0 || rect.h <= 0) return;
  if (texture) draw_sprite_region(texture, rect, uv, tint, NO_ALPHA);
  else fill_rect(fallback, rect);
}

result_t comic_shelf_header_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate:
      if (!win_grid(win, msg, wparam, lparam)) return false;
      win->userdata2 = (void *)(uintptr_t)comic_shelf_load_texture(win, "pine-trim.jpg");
      return true;
    case evPaint: {
      const char *style = ui_selector(win, COMIC_SHELF_HEADER_CLASS);
      irect16_t view = get_client_rect(win);
      uint32_t shade = ui_color(style, "--header-shade");
      comic_wood_paint((uint32_t)(uintptr_t)win->userdata2, view, UV_RECT(0, .19f, 1, .56f),
                       ui_color(style, "--wood-tint"), ui_color(style, "background"));
      fill_gradient_rounded_rect(color_with_alpha(shade, 0), shade, view, 0);
      fill_rect(ui_color(style, "--header-edge"), rect_split_bottom(view, 1));
      layout_paint_children(win);
      return true;
    }
    case evDestroy:
      if (win->userdata2) R_DeleteTexture((uint32_t)(uintptr_t)win->userdata2);
      return true;
    default:
      return win_grid(win, msg, wparam, lparam);
  }
}

static void comic_shelves_paint(window_t *win) {
  const comic_shelf_textures_t *textures = win->userdata2;
  const char *style = ui_selector(win, COMIC_SHELF_CLASS);
  irect16_t view = get_client_rect(win);
  window_t *first = win->children;
  int height = first ? first->frame.h : ui_px(style, "--row-height");
  int gap = layout_spacing_for(win);
  uint32_t back = ui_color(style, "background");
  if (height <= 0 || !textures) {
    fill_rect(back, view);
    return;
  }
  int side = MIN(ui_px(style, "--wall-width"), view.w / 2);
  int ceiling = MIN(ui_px(style, "--ceiling-depth"), height);
  int contact = MIN(ui_px(style, "--contact-depth"), height);
  int floor = MIN(ui_px(style, "--floor-depth"), height);
  int deck = MIN(ui_px(style, "--deck-depth"), gap);
  uint32_t back_tint = ui_color(style, "--back-tint"), trim_tint = ui_color(style, "--wood-tint");
  uint32_t wall = ui_color(style, "--wall-shadow"), wall_clear = color_with_alpha(wall, 0);
  uint32_t ceiling_shadow = ui_color(style, "--ceiling-shadow"), contact_shadow = ui_color(style, "--contact-shadow");
  uint32_t floor_shadow = ui_color(style, "--floor-shadow"), deck_shadow = ui_color(style, "--deck-shadow");
  uint32_t trim = ui_color(style, "--trim-color"), face_shade = ui_color(style, "--face-shade");
  uint32_t bevel = ui_color(style, "--bevel-highlight");
  int step = height + gap;
  int y = window_scroll_rect_to_client(win, window_scroll_content_rect(win)).y;
  y %= step;
  if (y > 0) y -= step;
  for (; y < view.y + view.h; y += step) {
    irect16_t cubby = R(view.x, y, view.w, height);
    irect16_t shelf = R(view.x, y + height, view.w, gap);
    comic_wood_paint(textures->back, cubby, NULL, back_tint, back);
    if (side > 0) {
      draw_gradient_rect(rect_split_left(cubby, side), wall, wall_clear);
      draw_gradient_rect(rect_split_right(cubby, side), wall_clear, wall);
    }
    fill_gradient_rounded_rect(ceiling_shadow, color_with_alpha(ceiling_shadow, 0), rect_split_top(cubby, ceiling), 0);
    fill_gradient_rounded_rect(contact_shadow, color_with_alpha(contact_shadow, 0), rect_split_top(cubby, contact), 0);
    fill_gradient_rounded_rect(color_with_alpha(floor_shadow, 0), floor_shadow, rect_split_bottom(cubby, floor), 0);

    comic_wood_paint(textures->trim, shelf, UV_RECT(0, .62f, 1, .82f), trim_tint, trim);
    fill_gradient_rounded_rect(deck_shadow, color_with_alpha(deck_shadow, 0), rect_split_top(shelf, deck), 0);
    irect16_t face = rect_trim_top(shelf, deck);
    if (face.h > 0) {
      fill_gradient_rounded_rect(color_with_alpha(face_shade, 0), face_shade, face, 0);
      fill_rect(bevel, rect_split_top(face, 1));
    }
  }
}

result_t comic_shelves_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      comic_shelf_textures_t *textures = calloc(1, sizeof(*textures));
      if (!textures) {
        fprintf(stderr, "[comic_shelf] window=%u could not allocate shelf textures\n", win->id);
        fflush(stderr);
        return false;
      }
      if (!win_tilegrid(win, msg, wparam, lparam)) { free(textures); return false; }
      set_scroll_bounce(win, SB_VERT, true);
      textures->back = comic_shelf_load_texture(win, "pine-back.jpg");
      textures->trim = comic_shelf_load_texture(win, "pine-trim.jpg");
      win->userdata2 = textures;
      return true;
    }
    case evDestroy: {
      comic_shelf_textures_t *textures = win->userdata2;
      if (textures) {
        if (textures->back) R_DeleteTexture(textures->back);
        if (textures->trim) R_DeleteTexture(textures->trim);
        free(textures);
        win->userdata2 = NULL;
      }
      return win_tilegrid(win, msg, wparam, lparam);
    }
    case evPaint:
      comic_shelves_paint(win);
      layout_paint_children(win);
      return true;
    default:
      return win_tilegrid(win, msg, wparam, lparam);
  }
}

// A cover is a button that answers a tap the way a book on a shelf would: it sinks under the
// finger, springs back, and only then reports the click, so the press reads even on a quick tap.
// The finished book (artwork, hardcover spine and lettered title) is painted once into the cover's
// bitmap cache; the shelf shows that image, and the press scales the image, never the layout.
typedef struct {
  uint32_t texture;
  int width, height;
  bool letter_title;
  ui_anim_t scale;
  bool release_pending;    // released while still sinking: spring back once it is down
  bool click_pending;      // report btnClicked when it is back up
} comic_cover_view_t;

static irect16_t comic_cover_book_rect(window_t *win, const comic_cover_view_t *cover) {
  const char *style = ui_selector(win, COMIC_COVER_CLASS);
  irect16_t slot = rect_inset_xy(get_client_rect(win), ui_px(style, "--book-inset"), 0);
  if (slot.w <= 0 || slot.h <= 0) return R(0, 0, 0, 0);
  int width = slot.w, height = slot.h;
  if (cover->width > 0 && cover->height > 0) {
    width = MAX(1, MIN(width, (int)((int64_t)height * cover->width / cover->height)));
    height = MAX(1, MIN(height, (int)((int64_t)width * cover->height / cover->width)));
  }
  return rect_center(rect_split_bottom(slot, height), width, height);
}

static void comic_cover_paint(window_t *win, const comic_cover_view_t *cover) {
  const char *style = ui_selector(win, COMIC_COVER_CLASS);
  irect16_t book = comic_cover_book_rect(win, cover);
  if (book.w <= 0 || book.h <= 0) return;
  ui_shadow_t shadow = ui_shadow(style, "box-shadow");
  draw_rect_shadow(book, 1, shadow.blur, shadow.offset, shadow.color);
  if (cover->texture) draw_rect(cover->texture, book);
  else fill_rect(ui_color(style, "background"), book);

  int hinge = MIN(ui_px(style, "--hinge-width"), book.w);
  if (hinge > 1) {
    irect16_t spine = rect_split_left(book, hinge);
    uint32_t crease = ui_color(style, "--spine-shadow");
    draw_gradient_rect(spine, crease, color_with_alpha(crease, 0));
    fill_rect(ui_color(style, "--hinge-light"), rect_split_left(spine, 1));
    fill_rect(crease, rect_split_right(spine, 1));
  }
  fill_rect(ui_color(style, "--hinge-light"), rect_split_top(book, 1));
  if (cover->letter_title || !cover->texture) {
    irect16_t title = rect_inset(rect_split_top(book, book.h / 3), 12);
    theme_draw_text_wrapped(style, win->title, title, ui_color(style, "color"), CTRL_NORMAL);
  }
  if (window_has_focus(win))
    stroke_rounded_rect(ui_color(style, "--focus-ring"), book, 1, 2);
}

static void comic_cover_press(window_t *win, comic_cover_view_t *cover) {
  float pressed = ui_number(ui_selector(win, COMIC_COVER_CLASS), "--press-scale");
  window_set_state(win, WINDOW_STATE_PRESSED, true);
  cover->release_pending = cover->click_pending = false;
  ui_anim_retarget(&cover->scale, pressed > 0 ? pressed : 1, UI_CURVE_PRESS);
  request_animate(win);
}

static void comic_cover_release(window_t *win, comic_cover_view_t *cover, bool click) {
  window_set_state(win, WINDOW_STATE_PRESSED, false);
  cover->click_pending = click;
  cover->release_pending = cover->scale.running;
  if (!cover->release_pending) ui_anim_retarget(&cover->scale, 1, UI_CURVE_RELEASE);
  request_animate(win);
}

// Scales the cached cover about its centre; at rest the visual is the identity again.
static void comic_cover_scale(window_t *win, float scale) {
  float w = win->frame.w, h = win->frame.h;
  window_visual_t visual = {{w * (1 - scale) / 2, h * (1 - scale) / 2, w * scale, h * scale}, {0, 0, w, h}, 1};
  window_set_visual(win, scale == 1 ? NULL : &visual);
  invalidate_window(win);
}

static void comic_cover_animate(window_t *win, comic_cover_view_t *cover) {
  bool rising = cover->scale.to == 1;
  float scale = ui_anim_step(&cover->scale);
  if (!cover->scale.running && cover->release_pending) {
    cover->release_pending = false;
    ui_anim_retarget(&cover->scale, 1, UI_CURVE_RELEASE);
  }
  comic_cover_scale(win, scale);
  if (cover->scale.running) request_animate(win);
  if (cover->click_pending && rising && (scale >= .995f || !cover->scale.running)) {
    cover->click_pending = false;
    send_message(get_root_window(win), evCommand, MAKEDWORD(win->id, btnClicked), win);
  }
}

result_t comic_cover_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  comic_cover_view_t *cover = win->userdata2;
  switch (msg) {
    case evCreate:
      win->userdata2 = cover = calloc(1, sizeof(comic_cover_view_t));
      if (!cover) return false;
      ui_anim_set(&cover->scale, 1);
      window_set_cache(win, true);
      return true;
    case evDestroy:
      if (cover && cover->texture) R_DeleteTexture(cover->texture);
      free(cover);
      win->userdata2 = NULL;
      return win_button(win, msg, wparam, lparam);
    case COMIC_COVER_SET: {
      const comic_cover_image_t *image = lparam;
      if (!cover || !image) return false;
      if (cover->texture) R_DeleteTexture(cover->texture);
      cover->texture = image->pixels ? R_CreateTextureSRGBA8(image->width, image->height,
                                                              image->pixels, R_FILTER_LINEAR, R_WRAP_CLAMP) : 0;
      cover->width = image->width;
      cover->height = image->height;
      cover->letter_title = image->letter_title;
      window_invalidate_cache(win);
      return true;
    }
    case COMIC_COVER_GET_RECT:
      if (!cover || !lparam) return false;
      *(irect16_t *)lparam = comic_cover_book_rect(win, cover);
      return true;
    case evPaint:
      if (cover) comic_cover_paint(win, cover);
      return true;
    case evSetFocus:
    case evKillFocus:
      window_invalidate_cache(win);   // the focus ring is part of the image
      return win_button(win, msg, wparam, lparam);
    case evAnimate:
      if (cover) comic_cover_animate(win, cover);
      return true;
    case evLeftButtonDown:
      if (cover) comic_cover_press(win, cover);
      return true;
    case evLeftButtonUp:
      // A cancelled press arrives outside the cover and springs back without a click.
      if (cover) comic_cover_release(win, cover, rect_contains_point(get_client_rect(win),
                                                    (ipoint16_t){(int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)}));
      return true;
    case evKeyDown:
      if (!cover || (wparam != AX_KEY_ENTER && wparam != AX_KEY_SPACE)) return win_button(win, msg, wparam, lparam);
      comic_cover_press(win, cover);
      return true;
    case evKeyUp:
      if (!cover || (wparam != AX_KEY_ENTER && wparam != AX_KEY_SPACE)) return win_button(win, msg, wparam, lparam);
      comic_cover_release(win, cover, true);
      return true;
    case evMeasure:
    case evArrange:
      return false;
    default:
      return win_button(win, msg, wparam, lparam);
  }
}
