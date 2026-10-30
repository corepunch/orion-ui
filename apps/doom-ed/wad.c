#include "wad.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static uint32_t wad_u32(const unsigned char *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

void doom_wad_close(doom_wad_t *wad) {
  if (!wad) return;
  if (wad->file) fclose(wad->file);
  free(wad->lumps);
  memset(wad, 0, sizeof(*wad));
}

bool doom_wad_open(doom_wad_t *wad, const char *path) {
  unsigned char header[12], entry[16];
  doom_wad_t result = {0};
  long length;
  uint32_t directory;
  if (!wad || !path || wad->file || wad->lumps) return false;
  result.file = fopen(path, "rb");
  if (!result.file) return false;
  if (fseek(result.file, 0, SEEK_END) || (length = ftell(result.file)) < 12 ||
      fseek(result.file, 0, SEEK_SET) || fread(header, 1, sizeof(header), result.file) != sizeof(header) ||
      (memcmp(header, "IWAD", 4) && memcmp(header, "PWAD", 4))) goto fail;
  memcpy(result.type, header, 4);
  result.count = wad_u32(header + 4);
  directory = wad_u32(header + 8);
  if ((uint64_t)directory + (uint64_t)result.count * 16 > (uint64_t)length ||
      (uint64_t)result.count * sizeof(*result.lumps) > SIZE_MAX ||
      (uint64_t)directory > LONG_MAX) goto fail;
  if (result.count) {
    result.lumps = calloc(result.count, sizeof(*result.lumps));
    if (!result.lumps) goto fail;
  }
  if (fseek(result.file, (long)directory, SEEK_SET)) goto fail;
  for (uint32_t i = 0; i < result.count; i++) {
    if (fread(entry, 1, sizeof(entry), result.file) != sizeof(entry)) goto fail;
    result.lumps[i].offset = wad_u32(entry);
    result.lumps[i].size = wad_u32(entry + 4);
    memcpy(result.lumps[i].name, entry + 8, 8);
    if (result.lumps[i].size && ((uint64_t)result.lumps[i].offset + result.lumps[i].size > (uint64_t)length ||
        (uint64_t)result.lumps[i].offset > LONG_MAX)) goto fail;
  }
  *wad = result;
  return true;
fail:
  doom_wad_close(&result);
  return false;
}

int64_t doom_wad_find(const doom_wad_t *wad, const char *name) {
  if (!wad || !name || strlen(name) > 8) return -1;
  for (uint32_t i = 0; i < wad->count; i++)
    if (!strcmp(wad->lumps[i].name, name)) return i;
  return -1;
}

bool doom_wad_read(const doom_wad_t *wad, uint32_t index, void *buffer, size_t size) {
  if (!wad || !wad->file || index >= wad->count || size != wad->lumps[index].size || (size && !buffer)) return false;
  if (!size) return true;
  return !fseek(wad->file, (long)wad->lumps[index].offset, SEEK_SET) && fread(buffer, 1, size, wad->file) == size;
}

bool doom_wad_is_map(const doom_wad_t *wad, uint32_t index) {
  static const char *const names[] = {"THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS", "SSECTORS", "NODES", "SECTORS", "REJECT", "BLOCKMAP"};
  if (!wad || index >= wad->count || wad->count - index <= sizeof(names) / sizeof(names[0])) return false;
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    if (strcmp(wad->lumps[index + 1 + i].name, names[i])) return false;
  return true;
}
