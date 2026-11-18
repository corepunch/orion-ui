#ifndef __COMIC_SHELF_H__
#define __COMIC_SHELF_H__

#include <orion/ui.h>

#define COMIC_SHELF_HEADER_CLASS "ComicShelfHeader"
#define COMIC_SHELF_CLASS "ComicShelves"
#define COMIC_COVER_CLASS "ComicCover"

// COMIC_COVER_GET_RECT: lparam = irect16_t* that receives the book's rectangle in the cover's coordinates.
enum { COMIC_COVER_SET = evUser + 50, COMIC_COVER_GET_RECT };

typedef struct {
  const uint8_t *pixels;
  int width, height;
  bool letter_title;
} comic_cover_image_t;

result_t comic_shelf_header_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t comic_shelves_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t comic_cover_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

#endif
