#ifndef __COMIC_PAINT_H__
#define __COMIC_PAINT_H__

#include <orion/ui.h>
#include "components/comic_story.h"

// A panel left as line art is coloured on a layer that covers its whole art cell, so the colours
// stay put however the panel crops the cell. The reader's pigment lies on paper as a watercolour
// wash, under the ink, and a shot that reappears later shows the same layer.
#define COMIC_LAYER_SIZE 256
#define COMIC_LAYER_PIXELS (COMIC_LAYER_SIZE * COMIC_LAYER_SIZE)

enum {
  FOG_UNPAINTED, FOG_CORAL, FOG_GOLD, FOG_LEAF, FOG_TEAL, FOG_SKY, FOG_PLUM, FOG_BROWN,
  FOG_PIGMENT_COUNT
};
typedef struct { float x, y; } comic_point_t;   // layer pixels

typedef struct {
  uint8_t color, coverage;
} comic_pigment_t;

typedef struct {
  comic_pigment_t pixels[COMIC_LAYER_PIXELS];
  uint32_t revision;
} comic_layer_t;

typedef struct {
  comic_layer_t layers[COMIC_STORY_MAX_LAYERS];
  uint32_t revision;
} comic_paint_t;

bool comic_layer_stroke(comic_layer_t *layer, comic_point_t from, comic_point_t to, int color);
bool comic_layer_valid(const comic_pigment_t *pixels);
// Paper and wash as straight sRGB RGBA, COMIC_LAYER_SIZE square.
void comic_layer_render(const comic_layer_t *layer, uint8_t *rgba);
bool comic_layer_painted(const comic_layer_t *layer);
uint32_t comic_pigment_color(int index);

#endif
