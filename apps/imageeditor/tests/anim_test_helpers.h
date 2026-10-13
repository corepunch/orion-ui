#ifndef __ANIM_TEST_HELPERS_H__
#define __ANIM_TEST_HELPERS_H__

static inline int test_anim_cel_pixel(const anim_frame_t *frame, int cel, size_t pixel, int w, int h) {
  uint8_t *decoded = malloc((size_t)w * h);
  int value = -1;
  if (decoded && pixel < (size_t)w * h && anim_frame_expand_cel(frame, cel, decoded, w, h)) value = decoded[pixel];
  free(decoded);
  return value;
}

static inline bool test_anim_cel_matches(const anim_frame_t *frame, int cel, const uint8_t *expected, int w, int h) {
  uint8_t *decoded = malloc((size_t)w * h);
  bool ok = decoded && anim_frame_expand_cel(frame, cel, decoded, w, h) && !memcmp(decoded, expected, (size_t)w * h);
  free(decoded);
  return ok;
}

static inline int test_anim_index(const anim_frame_t *frame, size_t pixel, int w, int h) {
  uint8_t *decoded = malloc((size_t)w * h);
  int value = -1;
  if (decoded && pixel < (size_t)w * h && anim_frame_expand_indices(frame, decoded, w, h)) value = decoded[pixel];
  free(decoded);
  return value;
}

#endif
