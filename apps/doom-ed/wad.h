#ifndef __DOOM_ED_WAD_H__
#define __DOOM_ED_WAD_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
  uint32_t offset, size;
  char name[9];
} doom_wad_lump_t;

typedef struct {
  FILE *file;
  doom_wad_lump_t *lumps;
  uint32_t count;
  char type[5];
} doom_wad_t;

bool doom_wad_open(doom_wad_t *wad, const char *path);
void doom_wad_close(doom_wad_t *wad);
int64_t doom_wad_find(const doom_wad_t *wad, const char *name);
bool doom_wad_read(const doom_wad_t *wad, uint32_t index, void *buffer, size_t size);
bool doom_wad_is_map(const doom_wad_t *wad, uint32_t index);

#endif
