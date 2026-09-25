#ifndef __UI_COLOR_H__
#define __UI_COLOR_H__

#include <stdint.h>
#include <math.h>

// UI colors use sRGB-encoded RGB with linear alpha. Public packed colors store
// R in the low byte: 0xAABBGGRR. CSS #RRGGBB values are converted by WEB().
static inline float ui_srgb_to_linear(float x) {
  if (x <= 0.04045f) return x / 12.92f;
  return powf((x + 0.055f) / 1.055f, 2.4f);
}

static inline float ui_linear_to_srgb(float x) {
  if (x <= 0.0031308f) return 12.92f * x;
  return 1.055f * powf(x, 1.0f / 2.4f) - 0.055f;
}

static inline uint8_t ui_linear_to_srgb8(float x) {
  if (x < 0.0f) x = 0.0f;
  if (x > 1.0f) x = 1.0f;
  float encoded = ui_linear_to_srgb(x) * 255.0f;
  return (uint8_t)(encoded + 0.5f);
}

static inline float ui_srgb8_to_linear(uint8_t x) {
  return ui_srgb_to_linear((float)x / 255.0f);
}

static inline void ui_composite_srgba8(uint8_t dst[4], const uint8_t src[4],
                                       float opacity) {
  if (opacity < 0.0f) opacity = 0.0f;
  if (opacity > 1.0f) opacity = 1.0f;
  float sa = ((float)src[3] / 255.0f) * opacity;
  if (sa <= 0.0f) return;
  if (sa >= 1.0f || dst[3] == 0) {
    dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
    dst[3] = (uint8_t)(sa * 255.0f + 0.5f);
    return;
  }
  float da = (float)dst[3] / 255.0f;
  float oa = sa + da * (1.0f - sa);
  if (oa <= 0.0f) {
    dst[0] = dst[1] = dst[2] = dst[3] = 0;
    return;
  }
  for (int c = 0; c < 3; c++) {
    float source = ui_srgb8_to_linear(src[c]);
    float dest = ui_srgb8_to_linear(dst[c]);
    float result = (source * sa + dest * da * (1.0f - sa)) / oa;
    dst[c] = ui_linear_to_srgb8(result);
  }
  dst[3] = (uint8_t)(oa * 255.0f + 0.5f);
}

#endif
