#include "comics.h"
#include <libxml/parser.h>
#include <strings.h>

bool comics_library_directory(char *path, size_t size) {
#ifdef AX_PLATFORM_IOS
  const char *directory = axIOSDocumentsDirectory();
  if (!directory || !*directory) return false;
  int n = snprintf(path, size, "%s", directory);
#else
  const char *directory = axSettingsDirectory();
  if (!directory || !*directory) return false;
  int n = snprintf(path, size, "%s/Comics", directory);
#endif
  return n > 0 && (size_t)n < size;
}

static const char *comics_filename(const char *path) {
  const char *name = path;
  for (const char *p = path; *p; p++)
    if (*p == '/' || *p == '\\') name = p + 1;
  return name;
}

bool comics_library_import(const comic_archive_t *archive, const comic_story_t *story, const char *source) {
  if (story && !strcmp(story->id, COMICS_BUILTIN_ID)) return true;
  char directory[4096], destination[4096];
  if (!comics_library_directory(directory, sizeof(directory))) return false;
  size_t length = strlen(directory);
  if (!strncmp(source, directory, length) && source[length] == '/' &&
      !strchr(source + length + 1, '/')) return true;
  if (!axMkDir(directory)) return false;

  const char *filename = comics_filename(source);
  int stem = (int)strlen(filename) - 4;   // The reader has already checked .cbz.
  for (int copy = 1; copy <= 999; copy++) {
    int n = copy == 1 ? snprintf(destination, sizeof(destination), "%s/%s", directory, filename) :
                        snprintf(destination, sizeof(destination), "%s/%.*s %d.cbz", directory, stem, filename, copy);
    if (n <= 0 || (size_t)n >= sizeof(destination)) return false;
    if (!axPathExists(destination)) return comic_archive_copy_to(archive, destination);
    if (comic_archive_matches_file(archive, destination)) return true;
  }
  return false;
}

static bool comics_bundle_copy(const comic_archive_t *archive) {
  comic_story_t *story = comic_story_load(archive);
  bool duplicate = story && !strcmp(story->id, COMICS_BUILTIN_ID);
  comic_story_free(story);
  return duplicate;
}

static void comics_comicinfo_title(const comic_archive_t *archive, char *out, size_t capacity) {
  size_t size;
  uint8_t *bytes = comic_archive_read_entry(archive, "ComicInfo.xml", 64 * 1024, &size);
  if (!bytes) return;
  xmlDocPtr doc = xmlReadMemory((const char *)bytes, (int)size, "ComicInfo.xml", NULL,
                                XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
  free(bytes);
  if (!doc) return;
  xmlNodePtr root = xmlDocGetRootElement(doc);
  if (root && !doc->intSubset && !doc->extSubset && !xmlStrcmp(root->name, BAD_CAST "ComicInfo")) {
    for (xmlNodePtr node = root->children; node; node = node->next) {
      if (node->type != XML_ELEMENT_NODE || xmlStrcmp(node->name, BAD_CAST "Title")) continue;
      xmlChar *title = xmlNodeGetContent(node);
      if (title && *title && strlen((const char *)title) < capacity)
        snprintf(out, capacity, "%s", title);
      xmlFree(title);
      break;
    }
  }
  xmlFreeDoc(doc);
}

bool comics_book_cover(comics_book_t *book, comic_cover_image_t *cover) {
  comic_archive_t *archive = comic_archive_open(book->path);
  if (!archive) return false;
  if (!book->builtin && comics_bundle_copy(archive)) {
    comic_archive_close(archive);
    return false;
  }
  if (book->builtin) snprintf(book->title, sizeof(book->title), "%s", ui_tr("The Boat in the Fog"));
  else {
    const char *name = comics_filename(book->path);
    snprintf(book->title, sizeof(book->title), "%.*s", (int)strlen(name) - 4, name);
    comics_comicinfo_title(archive, book->title, sizeof(book->title));
  }
  int w = 0, h = 0;
  uint8_t *image = comic_archive_read_art(archive, "orion/cover.art", &w, &h);
  bool letter = image != NULL;
  if (!image) image = comic_archive_read_page(archive, 0, &w, &h);
  comic_archive_close(archive);
  if (image && (w > 440 || h > 640)) {
    float scale = MIN(440.0f / w, 640.0f / h);
    int thumb_w = MAX(1, (int)(w * scale)), thumb_h = MAX(1, (int)(h * scale));
    uint8_t *thumbnail = image_resample_srgba8(image, w, h, (int[4]){0, 0, w, h}, thumb_w, thumb_h);
    if (thumbnail) {
      image_free(image);
      image = thumbnail;
      w = thumb_w;
      h = thumb_h;
    }
  }
  *cover = (comic_cover_image_t){image, w, h, letter};
  return true;
}
