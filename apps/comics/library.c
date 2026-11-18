#include "comics.h"
#include <orion/user/file.h>
#include <errno.h>

static bool comics_progress_path(char *out, size_t size, const char *id, bool complete_issue) {
  const char *dir = axSettingsDirectory();
  if (!dir || !*dir || !id) return false;
  int n = snprintf(out, size, "%s/orion-comics-%s%s.paint", dir, id, complete_issue ? ".issue" : "");
  return n > 0 && (size_t)n < size;
}

// Progress: "OCP", version, page, page count, layer count, then the reader's paint layers.
// Version 2 also wrote one byte per page before the layers.
enum { COMICS_PROGRESS_HEADER = 7 };

static bool comics_complete_issue(const comic_story_t *story) {
  return !strcmp(story->id, COMICS_BUILTIN_ID) && story->pages[0][0].shot == FOG_SHOT_ARCHIVE;
}

static const fog_shot_t comics_preview_pages[] = {
  FOG_SHOT_BOAST, FOG_SHOT_LESSON, FOG_SHOT_HUT, FOG_SHOT_HARBOR, FOG_SHOT_ROWING,
};
static const fog_shot_t comics_preview_layers[] = {
  FOG_SHOT_DOCK, FOG_SHOT_KNOTS, FOG_SHOT_POINT, FOG_SHOT_ROWING,
};

static int comics_preview_page(const comic_story_t *story, int page) {
  for (int i = 0; i < story->count; i++)
    for (int panel = 0; panel < ARRAY_LEN(story->pages[i]); panel++)
      if (story->pages[i][panel].shot == comics_preview_pages[page]) return i;
  return 0;
}

bool comics_load_progress(comics_reader_t *reader) {
  const comic_story_t *story = reader->story;
  char path[1024];
  bool complete_issue = comics_complete_issue(story);
  if (!comics_progress_path(path, sizeof(path), story->id, complete_issue)) return false;
  FILE *file = fopen(path, "rb");
  bool preview = !file && errno == ENOENT && complete_issue;
  if (preview && comics_progress_path(path, sizeof(path), story->id, false)) file = fopen(path, "rb");
  if (!file) return false;
  uint8_t header[COMICS_PROGRESS_HEADER];
  int pages = preview ? ARRAY_LEN(comics_preview_pages) : story->count;
  int count = preview ? ARRAY_LEN(comics_preview_layers) : story->layer_count;
  comic_layer_t *layers = calloc(MAX(1, count), sizeof(*layers));
  bool valid = layers && fread(header, 1, sizeof(header), file) == sizeof(header) &&
               !memcmp(header, "OCP", 3) && (header[3] == 2 || header[3] == 3) &&
               header[4] < pages && header[5] == pages && header[6] == count;
  if (valid && header[3] == 2) {
    uint8_t discarded[COMIC_STORY_MAX_PAGES];
    valid = fread(discarded, 1, pages, file) == (size_t)pages;
  }
  for (int i = 0; valid && i < count; i++)
    valid = fread(layers[i].pixels, 1, sizeof(layers[i].pixels), file) == sizeof(layers[i].pixels) &&
            comic_layer_valid(layers[i].pixels);
  valid = valid && fgetc(file) == EOF && !ferror(file);
  fclose(file);
  if (valid) {
    reader->page = preview ? comics_preview_page(story, header[4]) : header[4];
    for (int i = 0; i < count; i++) {
      int target = i;
      if (preview)
        for (target = 0; target < story->layer_count; target++)
          if (story->layers[target] == comics_preview_layers[i]) break;
      if (target == story->layer_count) continue;
      memcpy(reader->paint.layers[target].pixels, layers[i].pixels, sizeof(layers[i].pixels));
      reader->paint.layers[target].revision++;
    }
    reader->paint.revision++;
  } else {
    fprintf(stderr, "[comics] invalid progress for book=%s\n", story->id);
    fflush(stderr);
  }
  free(layers);
  return valid;
}

bool comics_save_progress(const comics_reader_t *reader) {
  if (!reader->persistent || !reader->story) return true;
  const comic_story_t *story = reader->story;
  char path[1024], temporary[1040];
  if (!comics_progress_path(path, sizeof(path), story->id, comics_complete_issue(story))) return false;
  snprintf(temporary, sizeof(temporary), "%s.tmp", path);
  FILE *file = fopen(temporary, "wb");
  if (!file) {
    fprintf(stderr, "[comics] cannot save progress for book=%s: %s\n", story->id, strerror(errno));
    fflush(stderr);
    return false;
  }
  uint8_t header[COMICS_PROGRESS_HEADER] = {'O', 'C', 'P', 3, (uint8_t)reader->page, (uint8_t)story->count,
                                            (uint8_t)story->layer_count};
  bool ok = fwrite(header, 1, sizeof(header), file) == sizeof(header);
  for (int i = 0; ok && i < story->layer_count; i++)
    ok = fwrite(reader->paint.layers[i].pixels, 1, sizeof(reader->paint.layers[i].pixels), file) ==
         sizeof(reader->paint.layers[i].pixels);
  if (fclose(file) != 0) ok = false;
  if (ok) ok = file_replace(temporary, path);
  if (!ok) {
    remove(temporary);
    fprintf(stderr, "[comics] cannot write progress for book=%s\n", story->id);
    fflush(stderr);
  }
  return ok;
}
