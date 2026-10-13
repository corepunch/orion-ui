#ifndef __PENCILTEST_PALETTE_H__
#define __PENCILTEST_PALETTE_H__

#include "../../orion/user/palette_file.h"

#define PENCILTEST_DEFAULT_COLORS 32
#define PENCILTEST_PALETTE_RESOURCE "../share/imageeditor/palettes/default.pal"

static inline bool pencil_palette_load_default(const char *exe_dir, uint8_t rgba[256][4], int *count) {
  char path[4096];
  int n = snprintf(path, sizeof(path), "%s/%s", exe_dir, PENCILTEST_PALETTE_RESOURCE);
  if (n < 0 || (size_t)n >= sizeof(path)) return false;
  if (!riff_palette_load(path, rgba, count)) return false;
  if (*count == PENCILTEST_DEFAULT_COLORS) return true;
  fprintf(stderr, "[palette] default palette has %d colors; expected %d path=%s\n", *count, PENCILTEST_DEFAULT_COLORS, path);
  fflush(stderr);
  return false;
}

#endif
