// Stock background browser: square thumbnails of the bundled plates in an
// IconGrid, packed into one texture sheet. Choosing a plate or a photo sets
// the Background layer's picture through the background commands.

#include "imageeditor.h"
#include "../commands/commands.h"
#include <ctype.h>

#if IMAGEEDITOR_BW

#define BG_STOCK_MAX     256
#define BG_STOCK_THUMB   128  // pixels per square tile in share/.../thumbs
#define BG_STOCK_ICON    96
#define BG_STOCK_CELL_W  112
#define BG_STOCK_COLS    8

typedef struct {
  canvas_doc_t  *doc;
  char           dir[MAX_FILENAME];
  char           names[BG_STOCK_MAX][64];  // file stems, sorted
  char           labels[BG_STOCK_MAX][64];
  int            count;
  int            selected;
  bitmap_strip_t strip;
  bool           accepted;
} bg_stock_state_t;

static bool bg_stock_dir(char *out, size_t size) {
#ifdef SHAREDIR
  int n = snprintf(out, size, "%s/" SHAREDIR "/penciltest", ui_get_exe_dir());
  return n > 0 && (size_t)n < size;
#else
  (void)out; (void)size;
  return false;
#endif
}

static int bg_stock_compare(const void *a, const void *b) {
  return strcmp((const char *)a, (const char *)b);
}

static void bg_stock_scan(bg_stock_state_t *st) {
  DIR *dir = bg_stock_dir(st->dir, sizeof(st->dir)) ? opendir(st->dir) : NULL;
  if (!dir) {
    fprintf(stderr, "[background] stock folder unavailable path=%s\n", st->dir);
    fflush(stderr);
    return;
  }
  for (struct dirent *e; (e = readdir(dir)) && st->count < BG_STOCK_MAX;) {
    size_t len = strlen(e->d_name);
    if (len < 5 || len - 4 >= sizeof(st->names[0]) || strcasecmp(e->d_name + len - 4, ".jpg")) continue;
    snprintf(st->names[st->count++], sizeof(st->names[0]), "%.*s", (int)(len - 4), e->d_name);
  }
  closedir(dir);
  qsort(st->names, st->count, sizeof(st->names[0]), bg_stock_compare);
  for (int i = 0; i < st->count; i++) {
    char *label = st->labels[i];
    snprintf(label, sizeof(st->labels[0]), "%s", st->names[i]);
    for (char *c = label; *c; c++) if (*c == '-') *c = ' ';
    for (char *c = label; *c; c++) if (c == label || c[-1] == ' ') *c = (char)toupper((unsigned char)*c);
  }
}

// Square thumbnail from thumbs/, or cropped from the plate when it is missing.
static uint8_t *bg_stock_thumbnail(const bg_stock_state_t *st, int i) {
  char path[MAX_FILENAME + 80];
  int w = 0, h = 0;
  snprintf(path, sizeof(path), "%s/thumbs/%s.jpg", st->dir, st->names[i]);
  uint8_t *rgba = load_image(path, &w, &h);
  if (rgba && w == BG_STOCK_THUMB && h == BG_STOCK_THUMB) return rgba;
  if (rgba) image_free(rgba);
  snprintf(path, sizeof(path), "%s/%s.jpg", st->dir, st->names[i]);
  if (!(rgba = load_image(path, &w, &h))) return NULL;
  int rect[4];
  image_cover_rect(w, h, 1, 1, rect);
  uint8_t *thumb = image_resample_srgba8(rgba, w, h, rect, BG_STOCK_THUMB, BG_STOCK_THUMB);
  image_free(rgba);
  return thumb;
}

static void bg_stock_bake(bg_stock_state_t *st) {
  if (!st->count) return;
  int cols = MIN(BG_STOCK_COLS, st->count), rows = (st->count + cols - 1) / cols;
  int sheet_w = cols * BG_STOCK_THUMB, sheet_h = rows * BG_STOCK_THUMB;
  uint8_t *sheet = calloc((size_t)sheet_w * sheet_h, 4);
  if (!sheet) {
    fprintf(stderr, "[background] thumbnail sheet allocation failed %dx%d\n", sheet_w, sheet_h);
    fflush(stderr);
    return;
  }
  for (int i = 0; i < st->count; i++) {
    uint8_t *thumb = bg_stock_thumbnail(st, i);
    if (!thumb) continue;
    uint8_t *tile = sheet + ((size_t)(i / cols) * BG_STOCK_THUMB * sheet_w + (size_t)(i % cols) * BG_STOCK_THUMB) * 4;
    for (int y = 0; y < BG_STOCK_THUMB; y++)
      memcpy(tile + (size_t)y * sheet_w * 4, thumb + (size_t)y * BG_STOCK_THUMB * 4, BG_STOCK_THUMB * 4);
    free(thumb);
  }
  st->strip.tex = R_CreateTextureSRGBA8(sheet_w, sheet_h, sheet, R_FILTER_LINEAR, R_WRAP_CLAMP);
  free(sheet);
  st->strip.icon_w = st->strip.icon_h = BG_STOCK_THUMB;
  st->strip.cols = cols;
  st->strip.sheet_w = sheet_w;
  st->strip.sheet_h = sheet_h;
}

static void bg_stock_accept(window_t *win, bg_stock_state_t *st) {
  if (st->selected < 0 || st->selected >= st->count) return;
  char path[MAX_FILENAME + 80];
  snprintf(path, sizeof(path), "%s/%s.jpg", st->dir, st->names[st->selected]);
  st->accepted = cmd_background_load(st->doc, path);
  if (st->accepted) end_dialog(win, 1);
}

static void bg_stock_photo(window_t *win, bg_stock_state_t *st) {
  char path[MAX_FILENAME];
  if (!show_image_picker(win, path, sizeof(path))) return;
  st->accepted = cmd_background_load(st->doc, path);
  if (st->accepted) end_dialog(win, 1);
  else message_box(win, "This picture could not be opened.", "Background Image", MB_OK);
}

static result_t bg_stock_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  bg_stock_state_t *st = (bg_stock_state_t *)win->userdata;
  switch (msg) {
    case evCreate: {
      st = (bg_stock_state_t *)lparam;
      win->userdata = st;
      bg_stock_scan(st);
      bg_stock_bake(st);
      window_t *grid = get_window_item(win, ID_BACKGROUND_STOCK_PLATES);
      if (grid) {
        send_message(grid, RVM_SETVIEWMODE, RVM_VIEW_LARGE_ICON, NULL);
        send_message(grid, RVM_SETCOLUMNWIDTH, BG_STOCK_CELL_W, NULL);
        send_message(grid, RVM_SETICONSIZE, BG_STOCK_ICON, NULL);
        if (st->strip.tex) send_message(grid, RVM_SETICONSTRIP, 0, &st->strip);
        send_message(grid, RVM_SETREDRAW, 0, NULL);
        for (int i = 0; i < st->count; i++) {
          reportview_item_t item = {.text = st->labels[i], .icon = i, .color = 0xffffffff};
          send_message(grid, RVM_ADDITEM, 0, &item);
        }
        send_message(grid, RVM_SETREDRAW, 1, NULL);
        if (st->count) send_message(grid, RVM_SETSELECTION, (uint32_t)(st->selected = 0), NULL);
      }
      enable_window(get_window_item(win, ID_BACKGROUND_STOCK_OK), st->count > 0);
      window_layout_sync(win);
      return true;
    }
    case evPaint:
      return false;
    case evCommand: {
      window_t *src = (window_t *)lparam;
      uint16_t code = HIWORD(wparam);
      if ((code == RVN_SELCHANGE || code == RVN_DBLCLK) && src && src->id == ID_BACKGROUND_STOCK_PLATES) {
        st->selected = (int)(uint16_t)LOWORD(wparam);
        if (code == RVN_DBLCLK) bg_stock_accept(win, st);
        return true;
      }
      if (code != btnClicked) return false;
      switch (LOWORD(wparam)) {
        case ID_BACKGROUND_STOCK_OK:     bg_stock_accept(win, st); return true;
        case ID_BACKGROUND_STOCK_PHOTO:  bg_stock_photo(win, st);  return true;
        case ID_BACKGROUND_STOCK_CANCEL: end_dialog(win, 0);       return true;
      }
      return false;
    }
    case evDestroy:
      if (st && st->strip.tex) { R_DeleteTexture(st->strip.tex); st->strip.tex = 0; }
      return false;
  }
  return false;
}

bool show_background_stock_dialog(window_t *parent, canvas_doc_t *doc) {
  if (!pencil_has_layers(doc)) return false;
  bg_stock_state_t *st = calloc(1, sizeof(*st));
  if (!st) return false;
  st->doc = doc;
  st->selected = -1;
  show_dialog_from_form_ex(&imageeditor_background_stock_form, "Background Image", parent,
                           WINDOW_DIALOG | WINDOW_NOTRAYBUTTON, bg_stock_proc, st);
  bool accepted = st->accepted;
  free(st);
  return accepted;
}

#endif // IMAGEEDITOR_BW
