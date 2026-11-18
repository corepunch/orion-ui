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
  uint32_t count = zip_rd16(z + eocd + 10);
  size_t at = zip_rd32(z + eocd + 16);
  zip_t *zip = calloc(1, sizeof(*zip));
  if (!zip) { zip_reject("allocation failed", size); return NULL; }
  zip->data = z; zip->size = size;
  zip->entries = calloc(count ? count : 1, sizeof(zip_entry_t));
  if (!zip->entries) { free(zip); zip_reject("allocation failed", size); return NULL; }
  for (uint32_t i = 0; i < count && at <= eocd && eocd - at >= 46; i++) {
    const uint8_t *cd = z + at;
    if (zip_rd32(cd) != 0x02014b50) break;
    uint32_t nlen = zip_rd16(cd + 28), xlen = zip_rd16(cd + 30), clen = zip_rd16(cd + 32);
    if (nlen + xlen + clen > eocd - at - 46) break;
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

uint8_t *zip_extract_limited(const zip_t *zip, int index, size_t max_size, size_t *out_size) {
  if (!zip || index < 0 || index >= zip->count || !out_size) { fprintf(stderr, "[zip] extract rejected index=%d count=%d\n", index, zip ? zip->count : 0); fflush(stderr); return NULL; }
  const zip_entry_t *e = &zip->entries[index];
  if (max_size == SIZE_MAX) max_size--;
  if (e->usize > max_size) { zip_reject("entry exceeds size limit", e->usize); return NULL; }
  if (e->local > zip->size || zip->size - e->local < 30) { zip_reject("local header out of range", zip->size); return NULL; }
  const uint8_t *lh = zip->data + e->local;
  size_t header = 30 + zip_rd16(lh + 26) + zip_rd16(lh + 28);
  if (zip_rd32(lh) != 0x04034b50 || header > zip->size - e->local ||
      e->csize > zip->size - e->local - header) {
    zip_reject("entry data out of range", zip->size);
    return NULL;
  }
  const uint8_t *data = lh + header;
  uint8_t *out = NULL;
  if (e->method == 0 && e->csize == e->usize) {
    out = malloc((size_t)e->usize + 1);
    if (out) memcpy(out, data, e->usize);
  } else if (e->method == 8) {
    size_t n = 0;
    uint8_t *raw = inflate_raw(data, e->csize, (size_t)e->usize + 1, &n);
    if (!raw) return NULL;
    if (n == e->usize) out = realloc(raw, n + 1);
    else zip_reject("inflated entry size mismatch", n);
    if (!out) free(raw);
  } else {
    fprintf(stderr, "[zip] entry %s rejected method=%u stored=%u expected=%u\n",
            e->name, e->method, e->csize, e->usize);
    fflush(stderr);
    return NULL;
  }
  if (out) { out[e->usize] = 0; *out_size = e->usize; }
  return out;
}

uint8_t *zip_extract(const zip_t *zip, int index, size_t *out_size) {
  return zip_extract_limited(zip, index, SIZE_MAX, out_size);
}
