#include "test_framework.h"
#include "apps/comics/comics.h"
#include <orion/user/scrollbar.h>
#include "apps/comics/components/comic_paint.c"
#include "apps/comics/components/comic_panel.c"
#include "apps/comics/components/comic_stage.c"
#include "apps/comics/components/comic_archive.c"
#include "apps/comics/components/comic_story.c"
#include "apps/comics/components/comic_shelf.c"

enum { FOG_COVER, FOG_DOCK, FOG_LESSON, FOG_KNOTS, FOG_HARBOR = 8, FOG_LAST = 20 };

static const char placed_bubbles_path[] = "apps/comics/tests/fixtures/interactive/bubble-positions.cbz";
static window_t *comics_test_open_target, *comics_test_main;

// Animations run on this clock, one 16 ms frame per repost_messages().
static uint32_t comics_test_now = 100000;
static uint32_t comics_test_clock(void) { return comics_test_now; }

static void comics_test_frame(void) {
  comics_test_now += 16;
  repost_messages();
}

static bool comics_test_moving(void) {
  comics_reader_t *reader = comics_test_main ? comics_test_main->userdata : NULL;
  return reader && (reader->transition != COMICS_STILL || comic_stage_turning(reader->stage) ||
                    window_is_animating(reader->stage));
}

// Runs frames until the cover bounce, a zoom or a page turn has come to rest.
static void comics_test_settle(window_t *win) {
  (void)win;
  for (int frame = 0; frame < 400 && (frame < 30 || comics_test_moving()); frame++) comics_test_frame();
}

static bool comics_test_open_url(const char *path) {
  return comics_open_cbz(comics_test_open_target, path);
}

static void comics_test_pointer(window_t *win, uint32_t msg, int x, int y, uint32_t flags) {
  ui_event_t event = {
    .message = msg, .pointer.flags = flags,
    .x = (window_screen_x(win) + x) * UI_WINDOW_SCALE,
    .y = (window_screen_y(win) + y) * UI_WINDOW_SCALE,
  };
  dispatch_message(&event);
}

static void comics_test_tap(window_t *win, int x, int y) {
  comics_test_pointer(win, kEventLeftButtonDown, x, y, AX_POINTER_TOUCH);
  comics_test_pointer(win, kEventLeftButtonUp, x, y, AX_POINTER_TOUCH);
  comics_test_settle(win);
}

static void comics_test_button(window_t *main, uint32_t id) {
  window_t *win = get_window_item(main, id);
  comics_test_tap(win, win->frame.w / 2, win->frame.h / 2);
}

// A swipe of dx points in `ms`, released while still moving.
static void comics_test_drag(window_t *win, int x, int y, int dx, int ms, uint32_t pointer) {
  comics_test_pointer(win, kEventLeftButtonDown, x, y, pointer);
  for (int t = 16; t <= ms; t += 16) {
    comics_test_now += 16;
    comics_test_pointer(win, kEventLeftButtonDragged, x + dx * t / ms, y, pointer);
  }
  comics_test_pointer(win, kEventLeftButtonUp, x + dx, y, pointer);
}

static void comics_test_swipe(window_t *win, int x, int y, int dx, uint32_t pointer) {
  comics_test_drag(win, x, y, dx, 64, pointer);
  repost_messages();
}

static void comics_test_screenshot(const char *name) {
  const char *dir = getenv("ORION_COMICS_QA");
  if (!dir) return;
  char path[1024];
  snprintf(path, sizeof(path), "%s/%s.jpg", dir, name);
  repost_messages();
  ui_save_screenshot(path, 92);
}

// A stroke across a panel, in fractions of its width and height.
static void comics_test_stroke(window_t *win, float x0, float y0, float x1, float y1, uint32_t kind) {
  irect16_t r = get_client_rect(win);
  comics_test_pointer(win, kEventLeftButtonDown, r.w * x0, r.h * y0, kind);
  comics_test_pointer(win, kEventLeftButtonDragged, r.w * (x0 + x1) / 2, r.h * (y0 + y1) / 2, kind);
  comics_test_pointer(win, kEventLeftButtonUp, r.w * x1, r.h * y1, kind);
  repost_messages();
}

static void comics_test_model(void) {
  TEST("A brush lays a continuous wash on a panel's layer, kept within the layer and restorable");
  comic_layer_t *layer = calloc(1, sizeof(*layer));
  ASSERT_NOT_NULL(layer);
  ASSERT_FALSE(comic_layer_painted(layer));
  ASSERT_TRUE(comic_layer_stroke(layer, (comic_point_t){-20, 60}, (comic_point_t){80, 60}, FOG_CORAL));
  ASSERT_EQUAL(layer->pixels[60 * COMIC_LAYER_SIZE + 0].color, FOG_CORAL);
  ASSERT_EQUAL(layer->pixels[60 * COMIC_LAYER_SIZE + 48].color, FOG_CORAL);
  ASSERT_TRUE(layer->pixels[60 * COMIC_LAYER_SIZE + 48].coverage >= 100);
  ASSERT_EQUAL(layer->pixels[90 * COMIC_LAYER_SIZE + 48].color, FOG_UNPAINTED);
  ASSERT_FALSE(comic_layer_stroke(layer, (comic_point_t){10, 10}, (comic_point_t){20, 10}, FOG_PIGMENT_COUNT));
  ASSERT_TRUE(comic_layer_stroke(layer, (comic_point_t){40, 60}, (comic_point_t){40, 62}, FOG_SKY));
  ASSERT_EQUAL(layer->pixels[60 * COMIC_LAYER_SIZE + 40].color, FOG_SKY);
  ASSERT_TRUE(comic_layer_valid(layer->pixels));
  uint8_t *rgba = malloc((size_t)COMIC_LAYER_PIXELS * 4);
  ASSERT_NOT_NULL(rgba);
  comic_layer_render(layer, rgba);
  const uint8_t *paper = rgba + ((size_t)200 * COMIC_LAYER_SIZE + 200) * 4, *wash = rgba + ((size_t)60 * COMIC_LAYER_SIZE + 60) * 4;
  ASSERT_TRUE(paper[0] > 225 && paper[1] > 220);
  ASSERT_TRUE(wash[0] > wash[2] + 40);   // coral reads warm on the paper
  free(rgba);
  layer->pixels[0].color = FOG_PIGMENT_COUNT;
  ASSERT_FALSE(comic_layer_valid(layer->pixels));
  free(layer);
  PASS();
}

static void comics_test_archive(void) {
  TEST("CBZ sorts deflated pages and loads bubble placement and paintable panels from preview archives");
  comic_archive_t *archive = comic_archive_open("apps/comics/tests/fixtures/natural-order.cbz");
  ASSERT_NOT_NULL(archive);
  ASSERT_EQUAL(comic_archive_pages(archive), 3);
  ASSERT_NULL(comic_story_load(archive));
  for (int i = 0; i < 3; i++) {
    int w, h;
    uint8_t *pixels = comic_archive_read_page(archive, i, &w, &h);
    ASSERT_NOT_NULL(pixels);
    ASSERT_EQUAL(w, 4);
    ASSERT_EQUAL(h, 5);
    for (int channel = 0; channel < 3; channel++) ASSERT_EQUAL(pixels[channel], i == channel ? 255 : 0);
    image_free(pixels);
  }
  comic_archive_close(archive);

  archive = comic_archive_open(placed_bubbles_path);
  ASSERT_NOT_NULL(archive);
  comic_story_t *story = comic_story_load(archive);
  ASSERT_NOT_NULL(story);
  ASSERT_EQUAL(story->pages[0][0].bubble.edge, COMIC_BUBBLE_TOP);
  ASSERT_EQUAL(story->pages[0][0].bubble.x, 100);
  ASSERT_EQUAL(story->pages[0][0].bubble.inset, 45);
  ASSERT_EQUAL(story->pages[1][0].bubble.edge, COMIC_BUBBLE_DEFAULT);
  ASSERT_EQUAL(story->pages[1][0].bubble.x, -1);
  ASSERT_EQUAL(story->pages[1][0].bubble.inset, 0);
  ASSERT_EQUAL(story->pages[2][0].bubble.edge, COMIC_BUBBLE_BOTTOM);
  ASSERT_EQUAL(story->layer_count, 1);
  ASSERT_EQUAL(story->pages[1][0].layer, 0);
  ASSERT_EQUAL(story->pages[0][0].layer, -1);
  comic_story_free(story);
  comic_archive_close(archive);
  PASS();
}

static int comics_test_library(window_t *main, const char *directory) {
  comics_shelf_sync(main, directory);
  window_t *grid = get_window_item(main, ID_LIBRARY_BOOKS);
  int count = 0;
  for (window_t *card = grid->children; card; card = card->next) count++;
  return count;
}

static void comics_test_reader(void) {
  TEST("Opening the full issue keeps localized dialogue and one coloring panel per landscape spread");
  if (!ui_init_graphics(UI_INIT_HIDDEN, "Comics QA", 1080, 810)) SKIP("Offscreen graphics unavailable");
  register_commctl_classes();
  ASSERT_TRUE(comics_register_components());
  ui_set_language("ru");
  ASSERT_TRUE(ui_add_string_catalog_dir("apps/comics/share/locale"));
  ui_animation_set_clock(comics_test_clock);
  window_t *main = comics_open_app(0, 626, 810, false);
  ASSERT_NOT_NULL(main);
  comics_test_main = main;
  show_window(main, true);
  repost_messages();
  comics_reader_t *reader = main->userdata;
  window_t *cover = get_window_item(main, ID_BOOK_CARD_COVER);
  ASSERT_NOT_NULL(cover);
  ASSERT_STR_EQUAL(cover->title, "Лодка в тумане");
  ASSERT_NOT_NULL(comics_shelf_book_path(cover));
  ASSERT_TRUE(window_is_visible(cover));
  ASSERT_TRUE(cover->frame.h > cover->frame.w);
  ASSERT_EQUAL(cover->frame.y, 20);
  ASSERT_EQUAL(cover->frame.y + cover->frame.h, cover->parent->frame.h);
  resize_window(main, 1080, 810);
  comics_test_screenshot("shelf");
  ASSERT_TRUE(get_window_item(main, ID_LIBRARY_OPEN)->frame.h >= 44);
  const char *qa_directory = getenv("ORION_COMICS_QA_LIBRARY");
  ASSERT_EQUAL(comics_test_library(main, qa_directory ? qa_directory : "apps/comics/tests/fixtures"), qa_directory ? 8 : 2);
  window_t *grid = get_window_item(main, ID_LIBRARY_BOOKS);
  if (qa_directory) {
    window_t *last = grid->children;
    while (last->next) last = last->next;
    ASSERT_TRUE(last->frame.y > grid->children->frame.y);
    comics_test_screenshot("shelf-collection");
    resize_window(main, 626, 810);
    comics_test_screenshot("shelf-collection-portrait");
    resize_window(main, 626, 500);
    repost_messages();
    comics_test_pointer(grid, kEventLeftButtonDown, 28, 350, AX_POINTER_TOUCH);
    comics_test_pointer(grid, kEventLeftButtonDragged, 28, 250, AX_POINTER_TOUCH);
    comics_test_pointer(grid, kEventLeftButtonUp, 28, 250, AX_POINTER_TOUCH);
    ASSERT_TRUE(grid->vscroll.pos > 0);
    scrollbar_stop_fling(grid);
    scroll_info_t top = {.fMask = SIF_POS};
    set_scroll_info(grid, SB_VERT, &top, true);
    send_message(grid, evVScroll, 0, NULL);
    resize_window(main, 626, 810);
    repost_messages();
  }
  window_t *imported = get_window_item(grid->children->next, ID_BOOK_CARD_COVER);
  ASSERT_NOT_NULL(imported);
  comics_test_tap(imported, imported->frame.w / 2, imported->frame.h / 2);
  ASSERT_TRUE(reader->reading);
  ASSERT_NULL(reader->story);
  comics_test_button(main, ID_LIBRARY_LIBRARY);
  ASSERT_FALSE(reader->reading);

  resize_window(main, 626, 810);
  // The cover sinks and springs back before the book opens, then the reader grows out of it.
  window_t *builtin = get_window_item(main, ID_BOOK_CARD_COVER);
  comics_test_pointer(builtin, kEventLeftButtonDown, builtin->frame.w / 2, builtin->frame.h / 2, AX_POINTER_TOUCH);
  comics_test_pointer(builtin, kEventLeftButtonUp, builtin->frame.w / 2, builtin->frame.h / 2, AX_POINTER_TOUCH);
  ASSERT_FALSE(reader->reading);
  for (int frame = 0; frame < 6; frame++) comics_test_frame();
  ASSERT_TRUE(((comic_cover_view_t *)builtin->userdata2)->scale.to < 1 ||
              ui_anim_value(&((comic_cover_view_t *)builtin->userdata2)->scale) < 1);
  // The bounce scales the baked cover image; the cover is not painted again.
  window_visual_t press;
  ASSERT_TRUE(window_get_visual(builtin, &press));
  ASSERT_TRUE(press.rect.w < builtin->frame.w && press.source.w == builtin->frame.w);
  ASSERT_TRUE(window_cache_current(builtin));
  comics_test_screenshot("press");
  for (int frame = 0; frame < 60 && !reader->reading; frame++) comics_test_frame();
  ASSERT_TRUE(reader->reading);
  ASSERT_EQUAL(reader->transition, COMICS_OPENING);
  ASSERT_TRUE(window_is_visible(get_window_item(main, ID_LIBRARY_SHELF)));
  window_visual_t zoom;
  ASSERT_TRUE(window_get_visual(reader->stage, &zoom));
  ASSERT_TRUE(zoom.rect.w < reader->stage->frame.w / 2);
  for (int frame = 1; frame <= 30 && comics_test_moving(); frame++) {
    comics_test_frame();
    if (frame % 3 == 0) {
      char name[32];
      snprintf(name, sizeof(name), "open-%02d", frame);
      comics_test_screenshot(name);
    }
  }
  comics_test_settle(main);
  ASSERT_EQUAL(reader->transition, COMICS_STILL);
  ASSERT_FALSE(window_get_visual(reader->stage, NULL));
  ASSERT_FALSE(window_is_visible(get_window_item(main, ID_LIBRARY_SHELF)));
  ASSERT_FALSE(reader->spread);
  ASSERT_TRUE(window_is_visible(reader->pages[0]));
  ASSERT_FALSE(window_is_visible(reader->pages[1]));
  ASSERT_EQUAL(reader->stage->frame.w, get_client_rect(main).w);
  ASSERT_EQUAL(reader->stage->frame.h, get_client_rect(main).h);
  ASSERT_EQUAL(reader->pages[0]->frame.w, reader->stage->frame.w);
  ASSERT_EQUAL(reader->pages[0]->frame.h, reader->stage->frame.h);
  window_t *shelf_button = get_window_item(main, ID_LIBRARY_LIBRARY);
  ASSERT_EQUAL(shelf_button->frame.w, shelf_button->frame.h);
  char tooltip[256] = {0};
  send_message(shelf_button, evGetTooltipText, 0, tooltip);
  ASSERT_STR_EQUAL(tooltip, "Книжная полка");
  ASSERT_EQUAL(reader->page, FOG_COVER);
  ASSERT_FALSE(window_is_visible(get_window_item(main, ID_LIBRARY_PALETTE)));
  comics_test_screenshot("cover");
  comics_test_button(main, ID_LIBRARY_NEXT);
  ASSERT_EQUAL(reader->page, FOG_DOCK);
  window_t *speech = get_window_item(reader->pages[0], ID_LEAF_TOP_SPEECH);
  ASSERT_TRUE(window_is_class(speech, "SpeechBubble"));
  ASSERT_TRUE(window_is_visible(speech));
  ASSERT_TRUE(speech->frame.h > 20);
  ASSERT_TRUE(speech->frame.y > get_client_rect(speech->parent).h / 2);
  ASSERT_TRUE(get_window_item(reader->pages[0], ID_LEAF_TOP_LINE)->frame.y >=
              speech_bubble_body_rect_in(speech, get_client_rect(speech), TOOLTIP_TAIL_TOP).y);
  ASSERT_TRUE((ui_color(ui_selector(speech, "SpeechBubble"), "background") & 0xffu) > 230);
  comics_test_screenshot("portrait");

  char path[4096];
  ASSERT_TRUE(comic_archive_builtin_path(path, sizeof(path)));
  ASSERT_NOT_NULL(reader->story);
  // Closing shrinks the reader back into its cover over the shelf, then hides it.
  window_t *library = get_window_item(main, ID_LIBRARY_LIBRARY);
  comics_test_pointer(library, kEventLeftButtonDown, library->frame.w / 2, library->frame.h / 2, AX_POINTER_TOUCH);
  comics_test_pointer(library, kEventLeftButtonUp, library->frame.w / 2, library->frame.h / 2, AX_POINTER_TOUCH);
  ASSERT_FALSE(reader->reading);
  ASSERT_EQUAL(reader->transition, COMICS_CLOSING);
  ASSERT_TRUE(window_is_visible(reader->stage));
  for (int frame = 1; frame <= 24 && comics_test_moving(); frame++) {
    comics_test_frame();
    if (frame % 4 == 0) {
      char name[32];
      snprintf(name, sizeof(name), "close-%02d", frame);
      comics_test_screenshot(name);
    }
  }
  comics_test_settle(main);
  ASSERT_FALSE(window_is_visible(reader->stage));
  ASSERT_TRUE(window_is_visible(get_window_item(main, ID_LIBRARY_SHELF)));
  comics_test_open_target = main;
  ui_register_open_file_handler(comics_test_open_url);
  ui_event_t open_event = {.message = kEventOpenFile, .lParam = path};
  dispatch_message(&open_event);
  ASSERT_TRUE(reader->reading);
  comics_test_settle(main);
  ASSERT_NOT_NULL(reader->story);
  ASSERT_EQUAL(comic_archive_pages(reader->archive), FOG_LAST + 1);
  ASSERT_EQUAL(reader->story->layer_count, 10);
  ASSERT_TRUE(reader->art.sheets[FOG_SHEET_PANELS] != 0);
  ASSERT_EQUAL(reader->page, FOG_DOCK);
  ASSERT_STR_EQUAL(get_window_item(reader->pages[0], ID_LEAF_TOP_LINE)->title, "Я нашу лодку с закрытыми глазами узнаю!");

  comics_test_swipe(reader->stage, 500, 240, -210, AX_POINTER_STYLUS);
  ASSERT_FALSE(comic_stage_turning(reader->stage));
  // A slow drag a third of the way across follows the finger, then springs back.
  comics_test_pointer(reader->stage, kEventLeftButtonDown, 500, 240, AX_POINTER_TOUCH);
  comics_test_now += 400;
  comics_test_pointer(reader->stage, kEventLeftButtonDragged, 300, 240, AX_POINTER_TOUCH);
  ASSERT_TRUE(comic_stage_turning(reader->stage));
  int dragged = 200 - COMIC_PAN_SLOP;
  ASSERT_EQUAL(reader->layout.spread[reader->front]->frame.x, -dragged);
  ASSERT_EQUAL(reader->layout.spread[1 - reader->front]->frame.x, reader->stage->frame.w - dragged);
  ASSERT_TRUE(window_is_visible(reader->layout.spread[1 - reader->front]));
  comics_test_screenshot("drag");
  comics_test_now += 200;
  comics_test_pointer(reader->stage, kEventLeftButtonUp, 300, 240, AX_POINTER_TOUCH);
  comics_test_settle(main);
  ASSERT_EQUAL(reader->page, FOG_DOCK);
  ASSERT_EQUAL(reader->layout.spread[reader->front]->frame.x, 0);
  ASSERT_FALSE(window_is_visible(reader->layout.spread[1 - reader->front]));
  // A flick of the same length carries on to the next page.
  comics_test_swipe(reader->stage, 500, 240, -210, AX_POINTER_TOUCH);
  ASSERT_TRUE(comic_stage_turning(reader->stage));
  ASSERT_EQUAL(reader->page, FOG_DOCK);
  for (int frame = 0; frame < 4; frame++) comics_test_frame();
  ASSERT_TRUE(reader->layout.spread[reader->front]->frame.x < -210);
  ASSERT_TRUE(reader->layout.spread[1 - reader->front]->frame.x > 0);
  comics_test_settle(main);
  ASSERT_EQUAL(reader->page, FOG_LESSON);
  window_t *next = get_window_item(main, ID_LIBRARY_NEXT);
  ASSERT_TRUE(window_is_enabled(next));

  ASSERT_FALSE(window_is_visible(get_window_item(main, ID_LIBRARY_PALETTE)));
  comics_test_button(main, ID_LIBRARY_NEXT);
  ASSERT_EQUAL(reader->page, FOG_KNOTS);
  // The knots panel is line art for the reader to colour, with the palette over the page.
  window_t *knots = get_window_item(reader->pages[0], ID_LEAF_TOP_PANEL);
  fog_art_state_t *knots_view = knots->userdata;
  ASSERT_EQUAL(knots_view->panel.shot, FOG_SHOT_KNOTS);
  ASSERT_NOT_NULL(knots_view->panel.layer);
  ASSERT_TRUE(reader->art.lineart[FOG_SHOT_KNOTS] != 0);
  ASSERT_NULL(((fog_art_state_t *)get_window_item(reader->pages[0], ID_LEAF_LEFT_PANEL)->userdata)->panel.layer);
  ASSERT_TRUE(window_is_visible(get_window_item(main, ID_LIBRARY_PALETTE)));
  comics_test_stroke(knots, .2f, .3f, .7f, .35f, AX_POINTER_TOUCH);
  ASSERT_TRUE(comic_layer_painted(knots_view->panel.layer));
  ASSERT_FALSE(comic_stage_turning(reader->stage));
  ASSERT_EQUAL(reader->page, FOG_KNOTS);
  comics_test_button(main, ID_LIBRARY_SKY);
  ASSERT_EQUAL(reader->color, FOG_SKY);
  comics_test_stroke(knots, .3f, .6f, .6f, .62f, AX_POINTER_STYLUS);
  int sky = 0;
  for (int i = 0; i < COMIC_LAYER_PIXELS; i++) sky += knots_view->panel.layer->pixels[i].color == FOG_SKY;
  ASSERT_TRUE(sky > 200);

  resize_window(main, 1080, 810);
  repost_messages();
  ASSERT_TRUE(reader->spread);
  ASSERT_TRUE(window_is_visible(reader->pages[1]));
  ASSERT_EQUAL(reader->page, FOG_KNOTS);
  int knots_layer = reader->story->pages[FOG_KNOTS][0].layer;
  ASSERT_TRUE(comic_layer_painted(&reader->paint.layers[knots_layer]));
  comics_test_screenshot("spread");
  comics_test_button(main, ID_LIBRARY_BACK);

  static const uint32_t panels[] = {ID_LEAF_TOP_PANEL, ID_LEAF_LEFT_PANEL, ID_LEAF_RIGHT_PANEL};
  for (int first = FOG_DOCK; first <= FOG_LAST; first += 2) {
    ASSERT_EQUAL(reader->page, first);
    ASSERT_TRUE(window_is_visible(reader->pages[1]));
    window_t *palette = get_window_item(main, ID_LIBRARY_PALETTE);
    ASSERT_TRUE(window_is_visible(palette));
    int paintable = 0;
    for (int leaf = 0; leaf < 2; leaf++)
      for (int i = 0; i < ARRAY_LEN(panels); i++) {
        window_t *panel = get_window_item(reader->pages[leaf], panels[i]);
        fog_art_state_t *view = panel->userdata;
        if (window_is_visible(panel) && view->panel.layer) paintable++;
      }
    ASSERT_EQUAL(paintable, 1);

    if (first + 1 == FOG_HARBOR) {
      window_t *harbor = get_window_item(reader->pages[1], ID_LEAF_TOP_PANEL);
      fog_art_state_t *view = harbor->userdata;
      ASSERT_EQUAL(view->panel.shot, FOG_SHOT_HARBOR);
      irect16_t bubble = map_window_rect(harbor, reader->stage, view->panel.speech->frame);
      ASSERT_TRUE(bubble.y >= palette->frame.y + palette->frame.h);
      ASSERT_TRUE(view->panel.speech->frame.y < get_client_rect(harbor).h / 4);
      comics_test_screenshot("harbor");
    }
    ASSERT_TRUE(window_is_enabled(next));
    if (first + 1 < FOG_LAST) comics_test_button(main, ID_LIBRARY_NEXT);
  }
  comics_test_button(main, ID_LIBRARY_NEXT);
  ASSERT_FALSE(reader->reading);
  ASSERT_TRUE(window_is_visible(cover));
  comics_test_settle(main);
  ASSERT_FALSE(window_is_visible(reader->stage));

  dispatch_message(&open_event);
  comics_test_settle(main);
  ASSERT_TRUE(reader->reading && reader->story);
  ASSERT_TRUE(comic_layer_painted(&reader->paint.layers[knots_layer]));
  ASSERT_TRUE(comics_open_cbz(main, "apps/comics/tests/fixtures/natural-order.cbz"));
  ASSERT_NULL(reader->story);
  // The cover lies alone on the right-hand half, where it would in an open book.
  ASSERT_FALSE(window_is_visible(reader->pages[1]));
  window_t *cover_spread = reader->layout.spread[reader->front];
  ASSERT_EQUAL(cover_spread->frame.x + cover_spread->frame.w, reader->stage->frame.w);
  ASSERT_TRUE(cover_spread->frame.x >= reader->stage->frame.w / 2);
  window_t *archive_panel = get_window_item(reader->pages[0], ID_LEAF_TOP_PANEL);
  ASSERT_TRUE(((fog_art_state_t *)archive_panel->userdata)->page_texture != 0);
  ASSERT_FALSE(window_is_visible(get_window_item(main, ID_LIBRARY_PALETTE)));
  ASSERT_FALSE(comics_open_cbz(main, "/nonexistent.cbz"));
  comics_test_screenshot("cbz-spread");
  comics_test_swipe(reader->stage, 800, 300, -260, AX_POINTER_TOUCH);
  comics_test_settle(main);
  ASSERT_EQUAL(reader->page, 1);
  ASSERT_TRUE(window_is_visible(reader->pages[1]));
  ASSERT_EQUAL(reader->layout.spread[reader->front]->frame.x, 0);
  ASSERT_EQUAL(reader->layout.spread[reader->front]->frame.w, reader->stage->frame.w);
  comics_test_screenshot("cbz-pages");
  comics_test_swipe(reader->stage, 220, 300, 250, AX_POINTER_TOUCH);
  comics_test_settle(main);
  ASSERT_EQUAL(reader->page, 0);
  comics_test_button(main, ID_LIBRARY_LIBRARY);
  comics_test_button(main, ID_BOOK_CARD_COVER);
  ASSERT_TRUE(reader->reading && reader->story);

  ASSERT_TRUE(comics_open_cbz(main, placed_bubbles_path));
  window_t *moved = get_window_item(reader->pages[0], ID_LEAF_TOP_SPEECH);
  irect16_t moved_panel = get_client_rect(moved->parent);
  ASSERT_TRUE(moved->frame.y > moved_panel.h / 8 && moved->frame.y < moved_panel.h / 2);
  ASSERT_EQUAL(moved->frame.x + moved->frame.w, moved_panel.w -
               ui_px(ui_selector(moved->parent, COMIC_PANEL_CLASS), "--speech-inset"));
  comics_test_button(main, ID_LIBRARY_NEXT);
  comics_test_settle(main);
  ASSERT_EQUAL(reader->page, 1);
  window_t *defaulted = get_window_item(reader->pages[0], ID_LEAF_TOP_SPEECH);
  ASSERT_TRUE(defaulted->frame.y > get_client_rect(defaulted->parent).h / 2);
  window_t *moved_harbor = get_window_item(reader->pages[1], ID_LEAF_TOP_PANEL);
  fog_art_state_t *moved_view = moved_harbor->userdata;
  ASSERT_TRUE(moved_view->panel.speech->frame.y > get_client_rect(moved_harbor).h / 2);
  ui_register_open_file_handler(NULL);
  destroy_window(main);
  fe_unload_component_plugins();
  ui_shutdown_graphics();
  PASS();
}

int main(void) {
  TEST_START("Comics");
  comics_test_model();
  comics_test_archive();
  comics_test_reader();
  TEST_END();
}
