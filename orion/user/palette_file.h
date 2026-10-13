#ifndef __UI_PALETTE_FILE_H__
#define __UI_PALETTE_FILE_H__

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static inline uint32_t riff_palette_u32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// RIFF PAL stores RGB plus Windows flags, not alpha. Output is opaque RGBA.
static inline bool riff_palette_load(const char *path, uint8_t rgba[256][4], int *count) {
  FILE *fp = path ? fopen(path, "rb") : NULL;
  uint8_t header[12], chunk[8], data[1028], result[256][4] = {{0}};
  bool ok = fp && rgba && count;
  long actual = -1;
  uint32_t remaining = 0;
  int colors = 0;
  if (ok) {
    ok = !fseek(fp, 0, SEEK_END) && (actual = ftell(fp)) >= 12 && !fseek(fp, 0, SEEK_SET) &&
         fread(header, 1, 12, fp) == 12 && !memcmp(header, "RIFF", 4) && !memcmp(header + 8, "PAL ", 4);
    if (ok) { remaining = riff_palette_u32(header + 4); ok = remaining >= 4 && (uint64_t)remaining + 8 == (uint64_t)actual; }
  }
  if (ok) remaining -= 4;
  while (ok && remaining) {
    ok = remaining >= 8 && fread(chunk, 1, 8, fp) == 8;
    if (!ok) break;
    remaining -= 8;
    uint32_t size = riff_palette_u32(chunk + 4);
    uint64_t padded = (uint64_t)size + (size & 1);
    if (padded > remaining) { ok = false; break; }
    if (!memcmp(chunk, "data", 4)) {
      ok = !colors && size >= 8 && size <= sizeof(data) && fread(data, 1, size, fp) == size;
      if (!ok) break;
      colors = data[2] | data[3] << 8;
      ok = data[0] == 0 && data[1] == 3 && colors >= 1 && colors <= 256 && size == 4u + 4u * colors;
      if (ok) for (int i = 0; i < colors; i++) {
        memcpy(result[i], data + 4 + i * 4, 3);
        result[i][3] = 255;
      }
      if (ok && (size & 1)) ok = fgetc(fp) != EOF;
    } else ok = !fseek(fp, (long)padded, SEEK_CUR);
    remaining -= (uint32_t)padded;
  }
  if (fp) fclose(fp);
  if (!ok || !colors) {
    fprintf(stderr, "[palette] cannot load RIFF PAL path=%s\n", path ? path : "(null)");
    fflush(stderr);
    return false;
  }
  memcpy(rgba, result, sizeof(result));
  *count = colors;
  return true;
}

#endif
