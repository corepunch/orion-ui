#include "comics.h"
#include <orion/commctl/commctl_internal.h>
#include <strings.h>

static result_t comics_card_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: {
      comics_book_t *book = allocate_window_data(win, sizeof(*book));
      if (!book) return false;
      *book = *(const comics_book_t *)lparam;
      window_t *cover = get_window_item(win, ID_BOOK_CARD_COVER);
      set_window_item_text(win, ID_BOOK_CARD_COVER, "%s", book->title);
      send_message(cover, btnSetTooltip, 0, book->title);
      return true;
    }
    case evPaint: return false;
    case evDestroy: return true;
    default: return false;
  }
}

const char *comics_shelf_book_path(window_t *source) {
  if (!source || !window_is_class(source, COMIC_COVER_CLASS)) return NULL;
  window_t *card = source->parent;
  const comics_book_t *book = card ? card->userdata : NULL;
  return book ? book->path : NULL;
}

window_t *comics_shelf_cover(window_t *root, const char *path) {
  window_t *grid = get_window_item(root, ID_LIBRARY_BOOKS);
  for (window_t *card = grid && path ? grid->children : NULL; card; card = card->next) {
    const comics_book_t *book = card->userdata;
    if (book && !strcmp(book->path, path)) return get_window_item(card, ID_BOOK_CARD_COVER);
  }
  return NULL;
}

static void comics_add_book(window_t *grid, comics_book_t *book) {
  comic_cover_image_t image = {0};
  if (!comics_book_cover(book, &image)) return;
  window_t *card = create_window_from_form(&comics_book_card_form, 0, 0, grid, comics_card_proc,
                                           grid->hinstance, book);
  if (card)
    send_message(get_window_item(card, ID_BOOK_CARD_COVER), COMIC_COVER_SET, 0, &image);
  image_free((void *)image.pixels);
}

typedef struct {
  window_t *grid;
  const char *directory;
} comics_shelf_scan_t;

static bool_t comics_scan_item(const AXdirent *entry, void *data) {
  comics_shelf_scan_t *scan = data;
  const char *ext = strrchr(entry->name, '.');
  if (entry->is_directory || entry->is_hidden || !ext || strcasecmp(ext, ".cbz")) return true;
  comics_book_t candidate = {.size = entry->size, .modified = entry->modified};
  int n = snprintf(candidate.path, sizeof(candidate.path), "%s/%s", scan->directory, entry->name);
  if (n <= 0 || (size_t)n >= sizeof(candidate.path)) return true;

  for (window_t *card = scan->grid->children; card; card = card->next) {
    const comics_book_t *book = card->userdata;
    if (!book || strcmp(book->path, candidate.path)) continue;
    if (book->size == candidate.size && book->modified == candidate.modified) return true;
    destroy_window(card);
    break;
  }
  comics_add_book(scan->grid, &candidate);
  return true;
}

void comics_shelf_sync(window_t *root, const char *directory) {
  window_t *grid = get_window_item(root, ID_LIBRARY_BOOKS);
  if (!grid) return;
  if (!grid->children) {
    comics_book_t builtin = {.builtin = true};
    if (comic_archive_builtin_path(builtin.path, sizeof(builtin.path))) comics_add_book(grid, &builtin);
  }
  for (window_t *card = grid->children, *next; card; card = next) {
    next = card->next;
    const comics_book_t *book = card->userdata;
    if (book && !book->builtin && !axPathExists(book->path)) destroy_window(card);
  }
  if (directory && *directory) {
    comics_shelf_scan_t scan = {grid, directory};
    axListDir(directory, comics_scan_item, &scan);
  }
  int min_w = ui_px(ui_selector(grid, COMIC_SHELF_CLASS), "--tile-min-width");
  if (min_w > 0) send_message(grid, tgSetMinTileWidth, min_w, NULL);
}
