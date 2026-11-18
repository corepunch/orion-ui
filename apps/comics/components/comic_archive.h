#ifndef __COMIC_ARCHIVE_H__
#define __COMIC_ARCHIVE_H__

#include <orion/ui.h>

typedef struct comic_archive_s comic_archive_t;

comic_archive_t *comic_archive_open(const char *path);
void comic_archive_close(comic_archive_t *archive);
int comic_archive_pages(const comic_archive_t *archive);
uint8_t *comic_archive_read_page(const comic_archive_t *archive, int page, int *w, int *h);
uint8_t *comic_archive_read_entry(const comic_archive_t *archive, const char *name, size_t limit, size_t *size);
uint8_t *comic_archive_read_art(const comic_archive_t *archive, const char *name, int *w, int *h);
bool comic_archive_matches_file(const comic_archive_t *archive, const char *path);
bool comic_archive_copy_to(const comic_archive_t *archive, const char *path);
bool comic_archive_builtin_path(char *out, size_t size);

#endif
