#include "groove.h"

static int waveform_bottom(int x, int w, int h, int radius) {
  if (radius <= 0) return h - 1;
  float px = x + 0.5f, dx = 0;
  if (px < radius) dx = radius - px;
  else if (px > w - radius) dx = px - (w - radius);
  if (dx <= 0) return h - 1;
  float dy = sqrtf((float)radius * radius - dx * dx);
  return CLAMP((int)((float)h - radius + dy - 0.5f), 0, h - 1);
}

static float waveform_peak(const block_t *b, int x, int w) {
  int np = b->audio.npeaks;
  float f = (x + 0.5f) * np / w - 0.5f;
  int k = f < 0 ? 0 : (int)f, k1 = k + 1 < np ? k + 1 : np - 1;
  float t = f < 0 ? 0 : f - k;
  return b->audio.peaks[k] + (b->audio.peaks[k1] - b->audio.peaks[k]) * t;
}

void draw_waveform(const block_t *b, irect16_t wave, int period, int offset, int radius, uint32_t color) {
  if (!b || b->audio.npeaks < 2 || wave.w <= 0 || wave.h <= 0 || period <= 0 || offset < 0) return;
  radius = CLAMP(radius, 0, MIN(wave.w, wave.h) / 2);
  for (int x = 0; x < wave.w; x++) {
    int src = (x + offset) % period;
    int h = MAX(1, (int)(waveform_peak(b, src, period) * MAX(1, wave.h - 14) / 255));
    int bottom = waveform_bottom(x, wave.w, wave.h, radius);
    int top = MAX(0, bottom + 1 - h);
    if (bottom >= top) fill_rect(color, R(wave.x + x, wave.y + top, 1, bottom - top + 1));
  }
}

uint32_t waveform_texture(groove_t *app, int block, ipoint16_t size, int radius, int period, int offset) {
  const block_t *b = block_get(block);
  if (!app || !b || size.x <= 0 || size.y <= 0 || period <= 0 || offset < 0) {
    fprintf(stderr, "[gr] waveform cache rejected app=%p block=%d size=%dx%d period=%d offset=%d\n", (void *)app, block, size.x, size.y, period, offset);
    fflush(stderr);
    return 0;
  }
  if (!g_ui_runtime.running || b->audio.npeaks < 2) return 0;
  radius = CLAMP(radius, 0, MIN(size.x, size.y) / 2);
  waveform_cache_t *cache = &app->waveforms[block];
  if (cache->texture && cache->size.x == size.x && cache->size.y == size.y &&
      cache->radius == radius && cache->period == period && cache->offset == offset &&
      cache->audio_revision == b->audio_revision) return cache->texture;
  uint8_t *mask = calloc((size_t)size.x * size.y, 1);
  if (!mask) {
    fprintf(stderr, "[gr] waveform mask allocation failed block=%d size=%dx%d\n", block, size.x, size.y);
    fflush(stderr);
    return 0;
  }
  for (int x = 0; x < size.x; x++) {
    int src = (x + offset) % period;
    int h = MAX(1, (int)(waveform_peak(b, src, period) * MAX(1, size.y - 14) / 255));
    int bottom = waveform_bottom(x, size.x, size.y, radius);
    for (int y = MAX(0, bottom + 1 - h); y <= bottom; y++) mask[(size_t)y * size.x + x] = 255;
  }
  uint32_t texture = R_CreateTextureR8(size.x, size.y, mask, R_FILTER_NEAREST, R_WRAP_CLAMP);
  free(mask);
  if (!texture) return 0;
  R_DeleteTexture(cache->texture);
  *cache = (waveform_cache_t){ .texture = texture, .size = size, .radius = radius,
                             .period = period, .offset = offset, .audio_revision = b->audio_revision };
  return texture;
}

void waveform_cache_free(groove_t *app) {
  if (!app) return;
  for (int i = 0; i < GR_MAX_BLOCKS; i++) if (app->waveforms[i].texture) R_DeleteTexture(app->waveforms[i].texture);
  memset(app->waveforms, 0, sizeof(app->waveforms));
}
