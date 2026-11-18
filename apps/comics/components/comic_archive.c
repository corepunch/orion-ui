#include "comic_archive.h"
#include <orion/user/zip.h>
#include <ctype.h>
#include <strings.h>

#define COMIC_ARCHIVE_MAX (128L * 1024 * 1024)
#define COMIC_ARCHIVE_MAX_ENTRY (32 * 1024 * 1024)
#define COMIC_ARCHIVE_MAX_PAGES 4096

typedef struct {
  int index;
  const char *name;
} comic_archive_page_t;

struct comic_archive_s {
  uint8_t *bytes;
  size_t length;
  zip_t *zip;
  comic_archive_page_t *pages;
  int count;
};

bool comic_archive_builtin_path(char *out, size_t size) {
  int n = snprintf(out, size, "%s/../share/comics/books/the-boat-in-the-fog.cbz", ui_get_exe_dir());
  return n > 0 && (size_t)n < size;
}

static const char *comic_cbz_basename(const char *path) {
  const char *slash = strrchr(path, '/'), *backslash = strrchr(path, '\\');
  if (backslash && (!slash || backslash > slash)) slash = backslash;
  return slash ? slash + 1 : path;
}

static bool comic_cbz_image(const char *path) {
  const char *name = comic_cbz_basename(path);
  const char *ext = strrchr(name, '.');
  if (!*name || *name == '.' || !ext || !strncmp(path, "__MACOSX/", 9)) return false;
  return !strcasecmp(ext, ".jpg") || !strcasecmp(ext, ".jpeg") ||
         !strcasecmp(ext, ".png") || !strcasecmp(ext, ".bmp");
}

static int comic_cbz_page_order(const void *left, const void *right) {
  const comic_archive_page_t *a = left, *b = right;
  const unsigned char *p = (const unsigned char *)a->name;
  const unsigned char *q = (const unsigned char *)b->name;
  while (*p && *q) {
    if (isdigit(*p) && isdigit(*q)) {
      while (*p == '0' && isdigit(p[1])) p++;
      while (*q == '0' && isdigit(q[1])) q++;
      const unsigned char *end_p = p, *end_q = q;
      while (isdigit(*end_p)) end_p++;
      while (isdigit(*end_q)) end_q++;
      if (end_p - p != end_q - q) return end_p - p < end_q - q ? -1 : 1;
      int cmp = memcmp(p, q, (size_t)(end_p - p));
      if (cmp) return cmp;
      p = end_p;
      q = end_q;
    } else {
      int cmp = tolower(*p++) - tolower(*q++);
      if (cmp) return cmp;
    }
  }
  return *p != *q ? (int)*p - *q : a->index - b->index;
}

comic_archive_t *comic_archive_open(const char *path) {
  FILE *file = path && *path ? fopen(path, "rb") : NULL;
  if (!file) return NULL;
  long size = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
  if (size < 22 || size > COMIC_ARCHIVE_MAX || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return NULL;
  }
  comic_archive_t *archive = calloc(1, sizeof(*archive));
  if (!archive) { fclose(file); return NULL; }
  archive->length = (size_t)size;
  archive->bytes = malloc(archive->length);
  bool read = archive->bytes && fread(archive->bytes, 1, (size_t)size, file) == (size_t)size;
  fclose(file);
  if (read) archive->zip = zip_open_memory(archive->bytes, (size_t)size);
  int entries = zip_entry_count(archive->zip);
  if (entries > 0)
    archive->pages = calloc((size_t)MIN(entries, COMIC_ARCHIVE_MAX_PAGES), sizeof(*archive->pages));
  if (archive->pages) {
    for (int i = 0; i < entries; i++) {
      const char *name = zip_entry_name(archive->zip, i);
      if (!comic_cbz_image(name)) continue;
      if (archive->count == COMIC_ARCHIVE_MAX_PAGES) {
        comic_archive_close(archive);
        return NULL;
      }
      archive->pages[archive->count++] = (comic_archive_page_t){i, name};
    }
  }
  if (!archive->count) { comic_archive_close(archive); return NULL; }
  qsort(archive->pages, (size_t)archive->count, sizeof(*archive->pages), comic_cbz_page_order);
  return archive;
}

void comic_archive_close(comic_archive_t *archive) {
  if (!archive) return;
  zip_close(archive->zip);
  free(archive->bytes);
  free(archive->pages);
  free(archive);
}

int comic_archive_pages(const comic_archive_t *archive) {
  return archive ? archive->count : 0;
}

static uint8_t *comic_archive_image(const comic_archive_t *archive, int index, int *w, int *h) {
  if (!archive || index < 0) return NULL;
  size_t size;
  uint8_t *data = zip_extract_limited(archive->zip, index, COMIC_ARCHIVE_MAX_ENTRY, &size);
  if (!data) return NULL;
  uint8_t *pixels = load_image_memory(data, size, w, h);
  free(data);
  return pixels;
}

uint8_t *comic_archive_read_page(const comic_archive_t *archive, int page, int *w, int *h) {
  if (!archive || page < 0 || page >= archive->count) return NULL;
  return comic_archive_image(archive, archive->pages[page].index, w, h);
}

uint8_t *comic_archive_read_entry(const comic_archive_t *archive, const char *name, size_t limit, size_t *size) {
  if (!archive || !name) return NULL;
  for (int i = 0; i < zip_entry_count(archive->zip); i++)
    if (!strcmp(zip_entry_name(archive->zip, i), name))
      return zip_extract_limited(archive->zip, i, limit, size);
  return NULL;
}

uint8_t *comic_archive_read_art(const comic_archive_t *archive, const char *name, int *w, int *h) {
  size_t size;
  uint8_t *data = comic_archive_read_entry(archive, name, COMIC_ARCHIVE_MAX_ENTRY, &size);
  if (!data) return NULL;
  uint8_t *pixels = load_image_memory(data, size, w, h);
  free(data);
  return pixels;
}

bool comic_archive_matches_file(const comic_archive_t *archive, const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) return false;
  bool same = !fseek(file, 0, SEEK_END) && ftell(file) == (long)archive->length &&
              !fseek(file, 0, SEEK_SET);
  uint8_t block[8192];
  for (size_t offset = 0; same && offset < archive->length;) {
    size_t count = MIN(sizeof(block), archive->length - offset);
    same = fread(block, 1, count, file) == count && !memcmp(block, archive->bytes + offset, count);
    offset += count;
  }
  fclose(file);
  return same;
}

bool comic_archive_copy_to(const comic_archive_t *archive, const char *path) {
  FILE *file = fopen(path, "wbx");
  if (!file) return false;
  bool ok = fwrite(archive->bytes, 1, archive->length, file) == archive->length;
  if (fclose(file) != 0) ok = false;
  if (!ok) remove(path);
  return ok;
}
