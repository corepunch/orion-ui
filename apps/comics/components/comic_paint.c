#include "comic_paint.h"
#include <math.h>

#define COMIC_BRUSH_RADIUS 9.5f

// Pigments are art data. The UI uses the same palette for the swatches.
static const uint8_t comic_paper[] = {255, 249, 233};
static const uint8_t comic_dye[FOG_PIGMENT_COUNT][3] = {
  {255, 249, 233}, {224, 100, 82}, {236, 178, 62}, {122, 168, 84}, {66, 153, 145},
  {104, 156, 214}, {150, 104, 170}, {152, 104, 66},
};

uint32_t comic_pigment_color(int index) {
  if (index < FOG_UNPAINTED || index >= FOG_PIGMENT_COUNT) return 0;
  const uint8_t *c = comic_dye[index];
  return 0xff000000u | (uint32_t)c[2] << 16 | (uint32_t)c[1] << 8 | c[0];
}

static float comic_paper_noise(unsigned x, unsigned y) {
  uint32_t h = (uint32_t)x * UINT32_C(0x9e3779b9) ^ (uint32_t)y * UINT32_C(0x85ebca6b);
  h ^= h >> 16;
  h *= UINT32_C(0x7feb352d);
  h ^= h >> 15;
  return (float)(h & 0xffffu) / 65535.0f;
}

static float comic_mix(float a, float b, float t) {
  return a + (b - a) * t;
}

static float comic_wash_noise(float x, float y) {
  unsigned col = (unsigned)x, row = (unsigned)y;
  float u = x - col, v = y - row;
  u = u * u * (3 - 2 * u);
  v = v * v * (3 - 2 * v);
  return comic_mix(comic_mix(comic_paper_noise(col, row), comic_paper_noise(col + 1, row), u),
                   comic_mix(comic_paper_noise(col, row + 1), comic_paper_noise(col + 1, row + 1), u), v);
}

static bool comic_layer_dab(comic_layer_t *layer, float x, float y, uint8_t color) {
  int left = MAX(0, (int)floorf(x - COMIC_BRUSH_RADIUS));
  int top = MAX(0, (int)floorf(y - COMIC_BRUSH_RADIUS));
  int right = MIN(COMIC_LAYER_SIZE - 1, (int)ceilf(x + COMIC_BRUSH_RADIUS));
  int bottom = MIN(COMIC_LAYER_SIZE - 1, (int)ceilf(y + COMIC_BRUSH_RADIUS));
  bool changed = false;
  for (int row = top; row <= bottom; row++) {
    for (int col = left; col <= right; col++) {
      float dx = col + .5f - x, dy = row + .5f - y;
      float edge = CLAMP((COMIC_BRUSH_RADIUS - sqrtf(dx * dx + dy * dy)) / 2.4f, 0, 1);
      uint8_t coverage = (uint8_t)(edge * (200 + 55 * comic_paper_noise(col, row)));
      comic_pigment_t *pixel = &layer->pixels[row * COMIC_LAYER_SIZE + col];
      if (!coverage || (pixel->color == color && pixel->coverage >= coverage)) continue;
      if (pixel->color && pixel->color != color && coverage < 52) continue;
      *pixel = (comic_pigment_t){color, coverage};
      changed = true;
    }
  }
  return changed;
}

bool comic_layer_stroke(comic_layer_t *layer, comic_point_t from, comic_point_t to, int color) {
  if (!layer || color <= FOG_UNPAINTED || color >= FOG_PIGMENT_COUNT) return false;
  int steps = MAX(1, (int)ceilf(fmaxf(fabsf(to.x - from.x), fabsf(to.y - from.y)) / 2));
  bool changed = false;
  for (int i = 1; i <= steps; i++) {
    float t = (float)i / steps;
    changed |= comic_layer_dab(layer, comic_mix(from.x, to.x, t), comic_mix(from.y, to.y, t), (uint8_t)color);
  }
  if (changed) layer->revision++;
  return changed;
}

bool comic_layer_valid(const comic_pigment_t *pixels) {
  for (int i = 0; i < COMIC_LAYER_PIXELS; i++)
    if (pixels[i].color >= FOG_PIGMENT_COUNT || (pixels[i].color == 0) != (pixels[i].coverage == 0)) return false;
  return true;
}

bool comic_layer_painted(const comic_layer_t *layer) {
  for (int i = 0; layer && i < COMIC_LAYER_PIXELS; i++)
    if (layer->pixels[i].coverage) return true;
  return false;
}

void comic_layer_render(const comic_layer_t *layer, uint8_t *rgba) {
  for (int y = 0; y < COMIC_LAYER_SIZE; y++) {
    for (int x = 0; x < COMIC_LAYER_SIZE; x++) {
      comic_pigment_t p = layer->pixels[y * COMIC_LAYER_SIZE + x];
      float grain = comic_paper_noise(x, y);
      float clouds = comic_wash_noise(x / 27.0f, y / 18.0f);
      float blooms = comic_wash_noise(x / 7.0f, y / 6.0f);
      // Pigment pools where a stroke thins out, as a wash dries darker at its edge.
      float pooling = p.coverage && p.coverage < 160 ? .18f : 0;
      float wash = CLAMP(.30f + .55f * clouds + .17f * blooms + .09f * grain + pooling, 0, 1);
      uint8_t *pixel = rgba + ((size_t)y * COMIC_LAYER_SIZE + x) * 4;
      for (int i = 0; i < 3; i++) {
        float base = comic_paper[i] - (1 - grain) * 17;
        pixel[i] = (uint8_t)comic_mix(base, comic_dye[p.color][i], wash * p.coverage / 255.0f);
      }
      pixel[3] = 255;
    }
  }
}
