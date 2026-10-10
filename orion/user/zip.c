#include "zip.h"
#include "image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct { char *name; uint32_t method, csize, usize, local; } zip_entry_t;
struct zip_s { const uint8_t *data; size_t size; zip_entry_t *entries; int count; };

static uint32_t zip_rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t zip_rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static void zip_reject(const char *what, size_t size) {
  fprintf(stderr, "[zip] %s size=%zu\n", what, size);
  fflush(stderr);
}

static const char *zip_base_name(const char *path) {
  const char *s = strrchr(path, '/'), *b = strrchr(path, '\\');
  if (b && (!s || b > s)) s = b;
  return s ? s + 1 : path;
}

zip_t *zip_open_memory(const void *data, size_t size) {
  const uint8_t *z = data;
  if (!z || size < 22) { zip_reject("archive too small", size); return NULL; }
  size_t eocd = size - 22;
  while (eocd > 0 && zip_rd32(z + eocd) != 0x06054b50) eocd--;
  if (zip_rd32(z + eocd) != 0x06054b50) { zip_reject("end of central directory not found", size); return NULL; }
  uint32_t count = zip_rd16(z + eocd + 10), at = zip_rd32(z + eocd + 16);
  zip_t *zip = calloc(1, sizeof(*zip));
  if (!zip) { zip_reject("allocation failed", size); return NULL; }
  zip->data = z; zip->size = size;
  zip->entries = calloc(count ? count : 1, sizeof(zip_entry_t));
  if (!zip->entries) { free(zip); zip_reject("allocation failed", size); return NULL; }
  for (uint32_t i = 0; i < count && at + 46 <= size; i++) {
    const uint8_t *cd = z + at;
    if (zip_rd32(cd) != 0x02014b50) break;
    uint32_t nlen = zip_rd16(cd + 28), xlen = zip_rd16(cd + 30), clen = zip_rd16(cd + 32);
    if (at + 46 + nlen > size) break;
    zip_entry_t *e = &zip->entries[zip->count];
    e->method = zip_rd16(cd + 10); e->csize = zip_rd32(cd + 20); e->usize = zip_rd32(cd + 24); e->local = zip_rd32(cd + 42);
    e->name = malloc(nlen + 1);
    if (!e->name) break;
    memcpy(e->name, cd + 46, nlen);
    e->name[nlen] = 0;
    zip->count++;
    at += 46 + nlen + xlen + clen;
  }
  return zip;
}

void zip_close(zip_t *zip) {
  if (!zip) return;
  for (int i = 0; i < zip->count; i++) free(zip->entries[i].name);
  free(zip->entries);
  free(zip);
}

int zip_entry_count(const zip_t *zip) { return zip ? zip->count : 0; }

const char *zip_entry_name(const zip_t *zip, int index) {
  if (!zip || index < 0 || index >= zip->count) { fprintf(stderr, "[zip] entry index %d out of range count=%d\n", index, zip ? zip->count : 0); fflush(stderr); return NULL; }
  return zip->entries[index].name;
}

int zip_find(const zip_t *zip, const char *name) {
  for (int i = 0; zip && name && i < zip->count; i++)
    if (!strcasecmp(zip_base_name(zip->entries[i].name), name)) return i;
  return -1;
}

uint8_t *zip_extract(const zip_t *zip, int index, size_t *out_size) {
  if (!zip || index < 0 || index >= zip->count || !out_size) { fprintf(stderr, "[zip] extract rejected index=%d count=%d\n", index, zip ? zip->count : 0); fflush(stderr); return NULL; }
  const zip_entry_t *e = &zip->entries[index];
  if ((size_t)e->local + 30 > zip->size) { zip_reject("local header out of range", zip->size); return NULL; }
  const uint8_t *lh = zip->data + e->local;
  size_t at = (size_t)e->local + 30 + zip_rd16(lh + 26) + zip_rd16(lh + 28);
  if (at + e->csize > zip->size) { zip_reject("entry data out of range", zip->size); return NULL; }
  uint8_t *out = NULL;
  size_t n = 0;
  if (e->method == 0) {
    out = malloc((size_t)e->csize + 1);
    if (out) { memcpy(out, zip->data + at, e->csize); n = e->csize; }
  } else if (e->method == 8) {
    size_t len = 0;
    uint8_t *raw = inflate_raw(zip->data + at, e->csize, &len);
    if (raw && (out = realloc(raw, len + 1))) n = len;
    else free(raw);
    if (out && n != e->usize) fprintf(stderr, "[zip] entry %s inflated to %zu of %u bytes\n", e->name, n, e->usize), fflush(stderr);
  } else {
    fprintf(stderr, "[zip] entry %s uses unsupported method %u\n", e->name, e->method);
    fflush(stderr);
    return NULL;
  }
  if (out) { out[n] = 0; *out_size = n; }
  return out;
}
