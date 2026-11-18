#ifndef __COMICS_H__
#define __COMICS_H__

#include <orion/ui.h>
#include "components/comic_panel.h"
#include "components/comic_stage.h"
#include "components/comic_shelf.h"

#ifndef ID_CONTROL_BASE
#define ID_CONTROL_BASE 1000
#endif
#ifndef ID_COMMAND_BASE
#define ID_COMMAND_BASE 100
#endif
#include "build/generated/apps/comics/comics.h"

#define COMICS_BUILTIN_ID "boat-in-the-fog"

typedef struct {
  char path[4096], title[256];
  size_t size;
  time_t modified;
  bool builtin;
} comics_book_t;

void comics_shelf_sync(window_t *root, const char *directory);
const char *comics_shelf_book_path(window_t *source);
window_t *comics_shelf_cover(window_t *root, const char *path);
bool comics_library_directory(char *path, size_t size);
bool comics_library_import(const comic_archive_t *archive, const comic_story_t *story, const char *source);
bool comics_book_cover(comics_book_t *book, comic_cover_image_t *cover);

// The reader grows out of the tapped cover and shrinks back into it (animate_window).
typedef enum { COMICS_STILL, COMICS_OPENING, COMICS_CLOSING } comics_transition_t;

typedef struct {
  comic_paint_t paint;
  int page, color;
  bool reading, spread;
  bool persistent;
  comic_archive_t *archive;
  comic_story_t *story;
  fog_comic_art_t art;
  window_t *stage;
  comic_stage_layout_t layout;
  window_t *leaves[2][2], **pages;
  int front, next_page;
  comics_transition_t transition;
  char path[4096];          // the open book, to find its cover when it closes
} comics_reader_t;

window_t *comics_open_app(hinstance_t instance, int width, int height, bool persistent);
bool comics_register_components(void);
bool comics_open_cbz(window_t *win, const char *path);
bool comics_load_progress(comics_reader_t *reader);
bool comics_save_progress(const comics_reader_t *reader);

#endif
