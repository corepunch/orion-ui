#ifndef __COMIC_STORY_H__
#define __COMIC_STORY_H__

#include "comic_archive.h"

#define COMIC_STORY_MAX_PAGES 32
#define COMIC_STORY_MAX_LAYERS 16
#define FOG_SHEET_CELLS 6

// The added artwork is laid out in sheets of six shots, in reading order.
#define FOG_STORY_SHEETS(X) \
  X(MOORINGS, "moorings") \
  X(LANTERN_WALK, "lantern-walk") \
  X(SEARCH, "search") \
  X(RETURN, "return") \
  X(HOME, "home")

#define FOG_SHOT_SHEET(key, name) FOG_SHOT_##key##_1, FOG_SHOT_##key##_6 = FOG_SHOT_##key##_1 + FOG_SHEET_CELLS - 1,

typedef enum {
  FOG_SHOT_NONE, FOG_SHOT_BOAST, FOG_SHOT_DOCK, FOG_SHOT_LESSON, FOG_SHOT_KNOTS,
  FOG_SHOT_HUT, FOG_SHOT_POINT, FOG_SHOT_LANTERN, FOG_SHOT_HARBOR,
  FOG_SHOT_ROWING, FOG_SHOT_SAIL,
  FOG_STORY_SHEETS(FOG_SHOT_SHEET)
  FOG_SHOT_ARCHIVE, FOG_SHOT_COUNT,
} fog_shot_t;
#undef FOG_SHOT_SHEET

typedef enum { COMIC_BUBBLE_DEFAULT, COMIC_BUBBLE_TOP, COMIC_BUBBLE_BOTTOM } comic_bubble_edge_t;

typedef struct {
  comic_bubble_edge_t edge;
  int x, inset;
} comic_bubble_position_t;

typedef struct {
  fog_shot_t shot;
  char *speech;
  comic_bubble_position_t bubble;
  int layer;              // the paint layer of a panel left as line art for the reader, or -1
} comic_beat_t;

typedef struct {
  char id[64];
  int count;
  comic_beat_t pages[COMIC_STORY_MAX_PAGES][3];
  // A shot that reappears keeps the reader's colours, so layers belong to shots, not to panels.
  fog_shot_t layers[COMIC_STORY_MAX_LAYERS];
  int layer_count;
} comic_story_t;

comic_story_t *comic_story_load(const comic_archive_t *archive);
void comic_story_free(comic_story_t *story);
const char *comic_shot_name(fog_shot_t shot);

#endif
