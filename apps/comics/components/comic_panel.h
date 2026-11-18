#ifndef __COMIC_PANEL_H__
#define __COMIC_PANEL_H__

#include "comic_paint.h"
#include "comic_story.h"

#define COMIC_PANEL_CLASS "ComicPanel"

// COMIC_PAINT_FINISHED: evCommand to the root when a stroke on a line-art panel ends.
enum { COMIC_SET_PANEL = evUser, COMIC_PAINT_FINISHED = 1 };

#define FOG_SHEET_ENTRY(key, name) FOG_SHEET_##key,
enum {
  FOG_SHEET_PANELS, FOG_SHEET_INSERTS, FOG_SHEET_HARBOR,
  FOG_STORY_SHEETS(FOG_SHEET_ENTRY)
  FOG_SHEET_COUNT,
};
#undef FOG_SHEET_ENTRY

typedef struct {
  uint32_t sheets[FOG_SHEET_COUNT], boat;
  frect_t boat_uv;
  uint32_t lineart[FOG_SHOT_COUNT];   // ink of the shots left for the reader to paint
} fog_comic_art_t;

bool fog_art_load(fog_comic_art_t *art, const comic_archive_t *archive, const comic_story_t *story);
void fog_art_clear(fog_comic_art_t *art);

typedef struct {
  fog_shot_t shot;
  comic_layer_t *layer;     // the reader's colours under the shot's line art, or NULL for watercolour
  int pigment;              // the colour a stroke lays down
  window_t *speech;
  comic_bubble_position_t bubble;
  bool at_page_top, at_page_bottom;
  const fog_comic_art_t *art;
  const comic_archive_t *archive;
  int archive_page;
  window_t *stage;          // its ComicStage: panels take no input while the spreads move
} fog_comic_panel_t;

result_t comic_panel_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

#endif
