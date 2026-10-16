#ifndef __PENCILTEST_PALETTE_H__
#define __PENCILTEST_PALETTE_H__

#include <stdint.h>
#include <string.h>

#define PENCILTEST_DEFAULT_COLORS 32
extern const uint8_t disney_extended_32[PENCILTEST_DEFAULT_COLORS][3];

static inline void pencil_palette_copy_default(uint8_t rgba[256][4]) {
  memset(rgba, 0, 256 * 4);
  for (int i = 0; i < PENCILTEST_DEFAULT_COLORS; i++) {
    memcpy(rgba[i], disney_extended_32[i], 3);
    rgba[i][3] = 255;
  }
}

#endif
