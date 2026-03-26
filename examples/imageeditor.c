// Image Editor – MacPaint-inspired with color support
// MDI architecture: floating tool palette, floating color palette,
// menu bar, and multiple document windows.
// PNG open/save via libpng.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <png.h>

#include "../ui.h"

extern bool running;

// ============================================================
// Constants
// ============================================================

#define CANVAS_W      320
#define CANVAS_H      200
#define CANVAS_SCALE  2   // display pixels per canvas pixel (640×400)

#define SCREEN_W      900
#define SCREEN_H      560

// Palette / tool floating windows (absolute screen coords)
#define PALETTE_WIN_X   4
#define PALETTE_WIN_Y  (MENUBAR_HEIGHT + 4)
#define PALETTE_WIN_W  64
#define TOOL_WIN_H    (14 + NUM_TOOLS * 24 + 22)   // header + tools + FG/BG swatch
#define COLOR_WIN_Y   (PALETTE_WIN_Y + TOOL_WIN_H + 4)
#define COLOR_WIN_H   (12 + 8 * 22)                // header + 8 rows × 22 px

// Document window starting position (client-area frame, no non-client chrome)
#define DOC_START_X   76
#define DOC_START_Y   60   // visible title starts at DOC_START_Y - 12 - 20 = 28
#define DOC_CASCADE   20

#define NUM_TOOLS   4
#define NUM_COLORS 16

// Menu item IDs
#define ID_FILE_NEW     1
#define ID_FILE_OPEN    2
#define ID_FILE_SAVE    3
#define ID_FILE_SAVEAS  4
#define ID_FILE_CLOSE   5
#define ID_FILE_QUIT    6

// ============================================================
// Types
// ============================================================

typedef enum {
  TOOL_PENCIL = 0,
  TOOL_BRUSH,
  TOOL_ERASER,
  TOOL_FILL,
} editor_tool_t;

typedef struct { uint8_t r, g, b, a; } rgba_t;

static bool rgba_eq(rgba_t a, rgba_t b) {
  return a.r==b.r && a.g==b.g && a.b==b.b && a.a==b.a;
}

static uint32_t rgba_to_col(rgba_t c) {
  return ((uint32_t)c.a<<24)|((uint32_t)c.b<<16)|((uint32_t)c.g<<8)|(uint32_t)c.r;
}

// Per-document state
typedef struct canvas_doc_s {
  uint8_t  pixels[CANVAS_H * CANVAS_W * 4];
  GLuint   canvas_tex;
  bool     canvas_dirty;
  bool     drawing;
  int      last_x, last_y;
  bool     modified;
  char     filename[512];
  window_t *win;        // document window (TOOLBAR+STATUSBAR)
  window_t *canvas_win; // canvas child window
  struct canvas_doc_s *next;
} canvas_doc_t;

// Global application state (shared by all window procs via g_app)
typedef struct {
  editor_tool_t  tool;
  rgba_t         fg_color;
  rgba_t         bg_color;
  canvas_doc_t  *active_doc;
  canvas_doc_t  *docs;           // linked list of open documents
  window_t      *menubar_win;
  window_t      *tool_win;
  window_t      *color_win;
  int            next_x;         // cascading position for new docs
  int            next_y;
} app_state_t;

static app_state_t *g_app = NULL;

// ============================================================
// Color palette (16 classic colours)
// ============================================================

static const rgba_t kPalette[NUM_COLORS] = {
  {0xFF,0xFF,0xFF,0xFF}, // white
  {0xCC,0xCC,0xCC,0xFF}, // light gray
  {0x88,0x88,0x88,0xFF}, // gray
  {0x44,0x44,0x44,0xFF}, // dark gray
  {0x00,0x00,0x00,0xFF}, // black
  {0xFF,0x00,0x00,0xFF}, // red
  {0xFF,0x88,0x00,0xFF}, // orange
  {0xFF,0xFF,0x00,0xFF}, // yellow
  {0x00,0xFF,0x00,0xFF}, // green
  {0x00,0x88,0x44,0xFF}, // dark green
  {0x00,0xFF,0xFF,0xFF}, // cyan
  {0x00,0x88,0xFF,0xFF}, // sky blue
  {0x00,0x00,0xFF,0xFF}, // blue
  {0x88,0x00,0xFF,0xFF}, // purple
  {0xFF,0x00,0xFF,0xFF}, // magenta
  {0xFF,0x44,0x88,0xFF}, // pink
};

static const char *kToolNames[NUM_TOOLS] = {
  "Pencil", "Brush", "Eraser", "Fill"
};

// ============================================================
// Canvas operations
// ============================================================

static bool is_png(const char *path) {
  if (!path) return false;
  size_t n = strlen(path);
  if (n < 5) return false;
  const char *ext = path + n - 4;
  return (ext[0]=='.' &&
          (ext[1]=='p'||ext[1]=='P') &&
          (ext[2]=='n'||ext[2]=='N') &&
          (ext[3]=='g'||ext[3]=='G'));
}

static bool canvas_in_bounds(int x, int y) {
  return x >= 0 && x < CANVAS_W && y >= 0 && y < CANVAS_H;
}

static void canvas_set_pixel(canvas_doc_t *doc, int x, int y, rgba_t c) {
  if (!canvas_in_bounds(x, y)) return;
  uint8_t *p = doc->pixels + ((size_t)y * CANVAS_W + x) * 4;
  p[0]=c.r; p[1]=c.g; p[2]=c.b; p[3]=c.a;
  doc->canvas_dirty = true;
  doc->modified     = true;
}

static rgba_t canvas_get_pixel(const canvas_doc_t *doc, int x, int y) {
  if (!canvas_in_bounds(x, y)) return (rgba_t){0,0,0,0};
  const uint8_t *p = doc->pixels + ((size_t)y * CANVAS_W + x) * 4;
  return (rgba_t){p[0],p[1],p[2],p[3]};
}

static void canvas_clear(canvas_doc_t *doc) {
  memset(doc->pixels, 0xFF, sizeof(doc->pixels));
  doc->canvas_dirty = true;
  doc->modified     = false;
}

static void canvas_draw_circle(canvas_doc_t *doc, int cx, int cy, int r, rgba_t c) {
  for (int dy = -r; dy <= r; dy++)
    for (int dx = -r; dx <= r; dx++)
      if (dx*dx + dy*dy <= r*r)
        canvas_set_pixel(doc, cx+dx, cy+dy, c);
}

static void canvas_draw_line(canvas_doc_t *doc, int x0, int y0, int x1, int y1,
                              int radius, rgba_t c) {
  int dx = abs(x1-x0), dy = abs(y1-y0);
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;
  while (true) {
    canvas_draw_circle(doc, x0, y0, radius, c);
    if (x0==x1 && y0==y1) break;
    int e2 = 2*err;
    if (e2 > -dy) { err -= dy; x0 += sx; }
    if (e2 <  dx) { err += dx; y0 += sy; }
  }
}

static void canvas_flood_fill(canvas_doc_t *doc, int sx, int sy, rgba_t fill) {
  rgba_t target = canvas_get_pixel(doc, sx, sy);
  if (rgba_eq(target, fill)) return;

  typedef struct { int16_t x, y; } pt_t;
  int capacity = CANVAS_W * CANVAS_H;
  pt_t *queue = malloc(sizeof(pt_t) * capacity);
  if (!queue) return;

  int head = 0, tail = 0;
  queue[tail++] = (pt_t){(int16_t)sx, (int16_t)sy};
  canvas_set_pixel(doc, sx, sy, fill);

  while (head < tail) {
    pt_t cur = queue[head++];
    int nx[4] = {cur.x+1, cur.x-1, cur.x,   cur.x};
    int ny[4] = {cur.y,   cur.y,   cur.y+1, cur.y-1};
    for (int i = 0; i < 4; i++) {
      if (canvas_in_bounds(nx[i], ny[i]) &&
          rgba_eq(canvas_get_pixel(doc, nx[i], ny[i]), target) &&
          tail < capacity) {
        canvas_set_pixel(doc, nx[i], ny[i], fill);
        queue[tail++] = (pt_t){(int16_t)nx[i], (int16_t)ny[i]};
      }
    }
  }
  free(queue);
}

// ============================================================
// PNG I/O (libpng)
// ============================================================

static bool png_load(const char *path, uint8_t *out_pixels) {
  FILE *fp = fopen(path, "rb");
  if (!fp) return false;

  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                           NULL, NULL, NULL);
  if (!png) { fclose(fp); return false; }

  png_infop info = png_create_info_struct(png);
  if (!info) { png_destroy_read_struct(&png, NULL, NULL); fclose(fp); return false; }

  if (setjmp(png_jmpbuf(png))) {
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    return false;
  }

  png_init_io(png, fp);
  png_read_info(png, info);

  int w = (int)png_get_image_width(png, info);
  int h = (int)png_get_image_height(png, info);
  png_byte ct  = png_get_color_type(png, info);
  png_byte bd  = png_get_bit_depth(png, info);

  if (bd == 16) png_set_strip_16(png);
  if (ct == PNG_COLOR_TYPE_PALETTE)       png_set_palette_to_rgb(png);
  if (ct == PNG_COLOR_TYPE_GRAY && bd<8)  png_set_expand_gray_1_2_4_to_8(png);
  if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
  if (ct == PNG_COLOR_TYPE_RGB  ||
      ct == PNG_COLOR_TYPE_GRAY ||
      ct == PNG_COLOR_TYPE_PALETTE) png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
  if (ct == PNG_COLOR_TYPE_GRAY || ct == PNG_COLOR_TYPE_GRAY_ALPHA)
    png_set_gray_to_rgb(png);
  png_read_update_info(png, info);

  png_bytep *rows = malloc(sizeof(png_bytep) * h);
  if (!rows) { png_destroy_read_struct(&png, &info, NULL); fclose(fp); return false; }

  // Each row is a separate malloc so we can free independently
  for (int r = 0; r < h; r++)
    rows[r] = malloc(png_get_rowbytes(png, info));

  png_read_image(png, rows);

  // Copy to canvas (clip / pad to CANVAS dimensions)
  memset(out_pixels, 0xFF, CANVAS_H * CANVAS_W * 4);
  int copy_w = w < CANVAS_W ? w : CANVAS_W;
  int copy_h = h < CANVAS_H ? h : CANVAS_H;
  for (int row = 0; row < copy_h; row++)
    memcpy(out_pixels + row * CANVAS_W * 4, rows[row], (size_t)copy_w * 4);

  for (int r = 0; r < h; r++) free(rows[r]);
  free(rows);
  png_destroy_read_struct(&png, &info, NULL);
  fclose(fp);
  return true;
}

static bool png_save(const char *path, const uint8_t *pixels) {
  FILE *fp = fopen(path, "wb");
  if (!fp) return false;

  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                            NULL, NULL, NULL);
  if (!png) { fclose(fp); return false; }

  png_infop info = png_create_info_struct(png);
  if (!info) { png_destroy_write_struct(&png, NULL); fclose(fp); return false; }

  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    return false;
  }

  png_init_io(png, fp);
  png_set_IHDR(png, info, CANVAS_W, CANVAS_H, 8,
               PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);

  for (int r = 0; r < CANVAS_H; r++)
    png_write_row(png, (png_bytep)(pixels + r * CANVAS_W * 4));

  png_write_end(png, NULL);
  png_destroy_write_struct(&png, &info);
  fclose(fp);
  return true;
}

// ============================================================
// Canvas GL texture
// ============================================================

static void canvas_upload(canvas_doc_t *doc) {
  if (!doc->canvas_tex) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, CANVAS_W, CANVAS_H, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, doc->pixels);
    doc->canvas_tex = tex;
  } else if (doc->canvas_dirty) {
    glBindTexture(GL_TEXTURE_2D, doc->canvas_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, CANVAS_W, CANVAS_H,
                    GL_RGBA, GL_UNSIGNED_BYTE, doc->pixels);
  }
  doc->canvas_dirty = false;
}

// ============================================================
// File picker dialog (modal, PNG-filtered)
// ============================================================

#define PICKER_LIST_W  360
#define PICKER_LIST_H  220
#define PICKER_WIN_W   (PICKER_LIST_W + 8)
#define PICKER_WIN_H   (PICKER_LIST_H + 60)

#define ICON_FOLDER icon8_collapse
#define ICON_FILE   icon8_checkbox
#define COLOR_FOLDER 0xffa0d000u

typedef enum { PICKER_OPEN, PICKER_SAVE } picker_mode_t;

typedef struct {
  picker_mode_t  mode;
  char           path[512];
  char           result[512];
  bool           accepted;
  window_t      *list_win;
  window_t      *edit_win;
} picker_state_t;

static void picker_load_dir(window_t *list_win, picker_state_t *ps) {
  send_message(list_win, CVM_CLEAR, 0, NULL);

  // ".." entry
  send_message(list_win, CVM_ADDITEM, 0,
    &(columnview_item_t){"..", ICON_FOLDER, COLOR_FOLDER, 1});

  DIR *dir = opendir(ps->path);
  if (!dir) return;

  // Collect entries
  typedef struct { char name[256]; bool is_dir; } entry_t;
  entry_t *entries = NULL;
  int count = 0, cap = 0;

  struct dirent *ent;
  while ((ent = readdir(dir))) {
    if (ent->d_name[0] == '.') continue;
    char full[768];
    snprintf(full, sizeof(full), "%s/%s", ps->path, ent->d_name);
    struct stat st;
    if (stat(full, &st) != 0) continue;
    bool is_dir = S_ISDIR(st.st_mode);
    if (!is_dir && !is_png(ent->d_name)) continue;
    if (count >= cap) {
      cap = cap ? cap * 2 : 32;
      entries = realloc(entries, sizeof(entry_t) * cap);
    }
    strncpy(entries[count].name, ent->d_name, 255);
    entries[count].name[255] = '\0';
    entries[count].is_dir = is_dir;
    count++;
  }
  closedir(dir);

  // Sort: dirs before files, then alphabetical
  for (int i = 0; i < count - 1; i++) {
    for (int j = i + 1; j < count; j++) {
      bool swap = (!entries[i].is_dir && entries[j].is_dir) ||
                  (entries[i].is_dir == entries[j].is_dir &&
                   strcasecmp(entries[i].name, entries[j].name) > 0);
      if (swap) {
        entry_t tmp = entries[i];
        entries[i] = entries[j];
        entries[j] = tmp;
      }
    }
  }

  for (int i = 0; i < count; i++) {
    send_message(list_win, CVM_ADDITEM, 0,
      &(columnview_item_t){
        entries[i].name,
        entries[i].is_dir ? ICON_FOLDER : ICON_FILE,
        entries[i].is_dir ? COLOR_FOLDER : (uint32_t)COLOR_TEXT_NORMAL,
        (uint32_t)entries[i].is_dir
      });
  }
  free(entries);
  invalidate_window(list_win);
}

static result_t picker_proc(window_t *win, uint32_t msg,
                             uint32_t wparam, void *lparam) {
  picker_state_t *ps = (picker_state_t *)win->userdata;
  switch (msg) {
    case kWindowMessageCreate: {
      ps = (picker_state_t *)lparam;
      win->userdata = ps;

      ps->list_win = create_window("", WINDOW_NOTITLE | WINDOW_VSCROLL,
          MAKERECT(2, 2, PICKER_LIST_W, PICKER_LIST_H),
          win, win_columnview, NULL);

      create_window("File:", WINDOW_NOTITLE,
          MAKERECT(2, PICKER_LIST_H + 6, 28, CONTROL_HEIGHT),
          win, win_label, NULL);

      ps->edit_win = create_window("", WINDOW_NOTITLE,
          MAKERECT(32, PICKER_LIST_H + 4, PICKER_LIST_W - 32, CONTROL_HEIGHT),
          win, win_textedit, NULL);

      create_window(ps->mode == PICKER_OPEN ? "Open" : "Save", 0,
          MAKERECT(2, PICKER_LIST_H + 22, 50, BUTTON_HEIGHT),
          win, win_button, NULL);
      create_window("Cancel", 0,
          MAKERECT(56, PICKER_LIST_H + 22, 50, BUTTON_HEIGHT),
          win, win_button, NULL);

      picker_load_dir(ps->list_win, ps);
      return true;
    }

    case kWindowMessageCommand: {
      uint16_t code = HIWORD(wparam);
      uint16_t idx  = LOWORD(wparam);

      if (code == CVN_SELCHANGE || code == CVN_DBLCLK) {
        columnview_item_t *item = (columnview_item_t *)lparam;
        if (!item || !item->text) return true;

        if (item->userdata) { // directory
          char newpath[512];
          if (strcmp(item->text, "..") == 0) {
            strncpy(newpath, ps->path, sizeof(newpath));
            char *slash = strrchr(newpath, '/');
            if (slash && slash != newpath) *slash = '\0';
            else { newpath[0]='/'; newpath[1]='\0'; }
          } else {
            snprintf(newpath, sizeof(newpath), "%s/%s", ps->path, item->text);
          }
          strncpy(ps->path, newpath, sizeof(ps->path) - 1);
          picker_load_dir(ps->list_win, ps);
        } else {
          strncpy(ps->edit_win->title, item->text,
                  sizeof(ps->edit_win->title) - 1);
          invalidate_window(ps->edit_win);
        }
        (void)idx;
        return true;
      }

      if (code == kButtonNotificationClicked) {
        window_t *btn = (window_t *)lparam;
        if (!btn) return true;
        if (strcmp(btn->title, "Cancel") == 0) {
          end_dialog(win, 0);
          return true;
        }
        const char *fname = ps->edit_win->title;
        if (fname[0]) {
          char full[600];
          snprintf(full, sizeof(full), "%s/%s", ps->path, fname);
          if (!is_png(full) && strlen(full) + 5 < sizeof(full))
            strcat(full, ".png");
          strncpy(ps->result, full, sizeof(ps->result) - 1);
          ps->accepted = true;
          end_dialog(win, 1);
        }
        return true;
      }
      return false;
    }

    default:
      return false;
  }
}

static bool show_file_picker(window_t *parent, bool save_mode,
                              char *out_path, size_t out_sz) {
  picker_state_t ps = {0};
  ps.mode = save_mode ? PICKER_SAVE : PICKER_OPEN;
  getcwd(ps.path, sizeof(ps.path));

  const char *title = save_mode ? "Save PNG" : "Open PNG";
  uint32_t result = show_dialog(title,
      MAKERECT(50, 50, PICKER_WIN_W, PICKER_WIN_H),
      parent, picker_proc, &ps);

  if (result && ps.accepted) {
    strncpy(out_path, ps.result, out_sz - 1);
    out_path[out_sz - 1] = '\0';
    return true;
  }
  return false;
}

// ============================================================
// Canvas window proc (child of document window)
// ============================================================

static void doc_update_title(canvas_doc_t *doc) {
  if (!doc->win) return;
  char title[64];
  const char *name = doc->filename[0] ? doc->filename : "Untitled";
  // Extract basename
  const char *slash = strrchr(name, '/');
  if (slash) name = slash + 1;
  snprintf(title, sizeof(title), "%s%s", name, doc->modified ? " *" : "");
  strncpy(doc->win->title, title, sizeof(doc->win->title) - 1);
  invalidate_window(doc->win);
}

static result_t canvas_proc(window_t *win, uint32_t msg,
                             uint32_t wparam, void *lparam) {
  canvas_doc_t *doc = (canvas_doc_t *)win->userdata;
  switch (msg) {
    case kWindowMessageCreate:
      doc = (canvas_doc_t *)lparam;
      win->userdata = doc;
      doc->canvas_win = win;
      return true;

    case kWindowMessageSetFocus:
      // This canvas is now the active document
      if (g_app && doc) g_app->active_doc = doc;
      return false;

    case kWindowMessagePaint: {
      if (!doc) return true;
      canvas_upload(doc);
      // Canvas is at root-relative (0,0); draw at (0,0) in root-local coords
      draw_rect(doc->canvas_tex,
                win->frame.x, win->frame.y,
                CANVAS_W * CANVAS_SCALE, CANVAS_H * CANVAS_SCALE);
      return true;
    }

    case kWindowMessageLeftButtonDown: {
      if (!doc || !g_app) return true;
      // wparam holds root-relative coords; canvas is at root-relative (0,0)
      int lx = (int16_t)LOWORD(wparam) - win->frame.x;
      int ly = (int16_t)HIWORD(wparam) - win->frame.y;
      int cx = lx / CANVAS_SCALE;
      int cy = ly / CANVAS_SCALE;
      doc->drawing = true;
      doc->last_x  = cx;
      doc->last_y  = cy;

      rgba_t color = (g_app->tool == TOOL_ERASER)
                     ? g_app->bg_color : g_app->fg_color;

      switch (g_app->tool) {
        case TOOL_PENCIL: canvas_draw_circle(doc, cx, cy, 0, color); break;
        case TOOL_BRUSH:  canvas_draw_circle(doc, cx, cy, 2, color); break;
        case TOOL_ERASER: canvas_draw_circle(doc, cx, cy, 3, color); break;
        case TOOL_FILL:   canvas_flood_fill(doc, cx, cy, color);     break;
      }
      invalidate_window(win);
      doc_update_title(doc);
      return true;
    }

    case kWindowMessageMouseMove: {
      if (!doc || !doc->drawing || !g_app) return true;
      int lx = (int16_t)LOWORD(wparam) - win->frame.x;
      int ly = (int16_t)HIWORD(wparam) - win->frame.y;
      int cx = lx / CANVAS_SCALE;
      int cy = ly / CANVAS_SCALE;
      if (cx == doc->last_x && cy == doc->last_y) return true;

      rgba_t color = (g_app->tool == TOOL_ERASER)
                     ? g_app->bg_color : g_app->fg_color;
      int radius = (g_app->tool == TOOL_BRUSH)  ? 2 :
                   (g_app->tool == TOOL_ERASER) ? 3 : 0;
      canvas_draw_line(doc, doc->last_x, doc->last_y, cx, cy, radius, color);
      doc->last_x = cx;
      doc->last_y = cy;
      invalidate_window(win);
      return true;
    }

    case kWindowMessageLeftButtonUp:
      if (doc) doc->drawing = false;
      return true;

    default:
      return false;
  }
}

// ============================================================
// Document window proc
// ============================================================

static result_t doc_win_proc(window_t *win, uint32_t msg,
                              uint32_t wparam, void *lparam) {
  canvas_doc_t *doc = (canvas_doc_t *)win->userdata;
  switch (msg) {
    case kWindowMessageCreate:
      return true;
    case kWindowMessagePaint:
      fill_rect(COLOR_PANEL_DARK_BG, 0, 0, win->frame.w, win->frame.h);
      return false; // let canvas child paint
    case kWindowMessageSetFocus:
      // Propagate active-doc to our canvas child
      if (g_app && doc) g_app->active_doc = doc;
      return false;
    default:
      return false;
  }
}

// ============================================================
// Tool palette proc (top-level floating window)
// ============================================================

#define TOOL_HEADER_H  14
#define TOOL_ROW_H     24
#define SWATCH_H       22

static result_t tool_palette_proc(window_t *win, uint32_t msg,
                                   uint32_t wparam, void *lparam) {
  switch (msg) {
    case kWindowMessageCreate:
      return true;

    case kWindowMessagePaint: {
      fill_rect(COLOR_PANEL_DARK_BG, 0, 0, win->frame.w, win->frame.h);
      // Right & bottom border
      fill_rect(COLOR_DARK_EDGE, win->frame.w - 1, 0, 1, win->frame.h);
      fill_rect(COLOR_DARK_EDGE, 0, win->frame.h - 1, win->frame.w, 1);

      // Header
      draw_text_small("Tools", 4, 3, COLOR_TEXT_DISABLED);
      fill_rect(COLOR_DARK_EDGE, 0, TOOL_HEADER_H - 1, win->frame.w, 1);

      // Tool buttons
      for (int i = 0; i < NUM_TOOLS; i++) {
        int ty = TOOL_HEADER_H + i * TOOL_ROW_H;
        bool active = g_app && (g_app->tool == (editor_tool_t)i);
        if (active)
          fill_rect(COLOR_FOCUSED, 1, ty, win->frame.w - 2, TOOL_ROW_H - 1);
        draw_text_small(kToolNames[i], 4, ty + 7,
                        active ? COLOR_PANEL_BG : COLOR_TEXT_NORMAL);
      }

      // FG / BG colour swatches
      int sy = TOOL_HEADER_H + NUM_TOOLS * TOOL_ROW_H + 2;
      draw_text_small("FG", 2, sy, COLOR_TEXT_DISABLED);
      draw_text_small("BG", 34, sy, COLOR_TEXT_DISABLED);
      sy += 8;
      if (g_app) {
        fill_rect(rgba_to_col(g_app->fg_color), 2,  sy, 26, 12);
        fill_rect(rgba_to_col(g_app->bg_color), 34, sy, 26, 12);
        fill_rect(COLOR_DARK_EDGE, 1,  sy - 1, 28, 14); // outline FG
        fill_rect(rgba_to_col(g_app->fg_color), 2,  sy, 26, 12);
        fill_rect(COLOR_DARK_EDGE, 33, sy - 1, 28, 14); // outline BG
        fill_rect(rgba_to_col(g_app->bg_color), 34, sy, 26, 12);
      }
      return true;
    }

    case kWindowMessageLeftButtonDown: {
      if (!g_app) return true;
      int ly = (int16_t)HIWORD(wparam);
      if (ly >= TOOL_HEADER_H && ly < TOOL_HEADER_H + NUM_TOOLS * TOOL_ROW_H) {
        int idx = (ly - TOOL_HEADER_H) / TOOL_ROW_H;
        if (idx >= 0 && idx < NUM_TOOLS) {
          g_app->tool = (editor_tool_t)idx;
          invalidate_window(win);
        }
      }
      return true;
    }

    default:
      return false;
  }
}

// ============================================================
// Color palette proc (top-level floating window)
// ============================================================

#define COLOR_HEADER_H  12
#define SWATCH_COLS      2
#define SWATCH_W        (PALETTE_WIN_W / SWATCH_COLS)  // 32
#define SWATCH_ROW_H    22

static result_t color_palette_proc(window_t *win, uint32_t msg,
                                    uint32_t wparam, void *lparam) {
  switch (msg) {
    case kWindowMessageCreate:
      return true;

    case kWindowMessagePaint: {
      fill_rect(COLOR_PANEL_DARK_BG, 0, 0, win->frame.w, win->frame.h);
      fill_rect(COLOR_DARK_EDGE, win->frame.w - 1, 0, 1, win->frame.h);

      draw_text_small("Colors", 4, 2, COLOR_TEXT_DISABLED);
      fill_rect(COLOR_DARK_EDGE, 0, COLOR_HEADER_H - 1, win->frame.w, 1);

      for (int i = 0; i < NUM_COLORS; i++) {
        int col = i % SWATCH_COLS;
        int row = i / SWATCH_COLS;
        int sx = col * SWATCH_W + 1;
        int sy = COLOR_HEADER_H + row * SWATCH_ROW_H + 1;
        fill_rect(rgba_to_col(kPalette[i]), sx, sy, SWATCH_W - 2, SWATCH_ROW_H - 2);

        // Highlight the active foreground colour
        if (g_app && rgba_eq(g_app->fg_color, kPalette[i]))
          fill_rect(COLOR_FOCUSED, sx, sy, SWATCH_W - 2, 2);
      }
      return true;
    }

    case kWindowMessageLeftButtonDown: {
      if (!g_app) return true;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      if (ly < COLOR_HEADER_H) return true;
      int col = lx / SWATCH_W;
      int row = (ly - COLOR_HEADER_H) / SWATCH_ROW_H;
      int idx = row * SWATCH_COLS + col;
      if (idx >= 0 && idx < NUM_COLORS) {
        g_app->fg_color = kPalette[idx];
        invalidate_window(win);
        if (g_app->tool_win) invalidate_window(g_app->tool_win);
      }
      return true;
    }

    case kWindowMessageRightButtonDown: {
      if (!g_app) return true;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      if (ly < COLOR_HEADER_H) return true;
      int col = lx / SWATCH_W;
      int row = (ly - COLOR_HEADER_H) / SWATCH_ROW_H;
      int idx = row * SWATCH_COLS + col;
      if (idx >= 0 && idx < NUM_COLORS) {
        g_app->bg_color = kPalette[idx];
        if (g_app->tool_win) invalidate_window(g_app->tool_win);
      }
      return true;
    }

    default:
      return false;
  }
}

// ============================================================
// Document management
// ============================================================

static canvas_doc_t *create_document(const char *filename) {
  if (!g_app) return NULL;

  canvas_doc_t *doc = calloc(1, sizeof(canvas_doc_t));
  if (!doc) return NULL;
  canvas_clear(doc);
  doc->modified = false;
  if (filename)
    strncpy(doc->filename, filename, sizeof(doc->filename) - 1);

  // Cascading position
  int wx = g_app->next_x;
  int wy = g_app->next_y;
  g_app->next_x += DOC_CASCADE;
  g_app->next_y += DOC_CASCADE;
  // Wrap if out of screen
  if (g_app->next_x + CANVAS_W * CANVAS_SCALE > SCREEN_W) {
    g_app->next_x = DOC_START_X;
    g_app->next_y = DOC_START_Y;
  }

  // Document window (client area = canvas)
  window_t *dwin = create_window(
      filename ? filename : "Untitled",
      WINDOW_TOOLBAR | WINDOW_STATUSBAR,
      MAKERECT(wx, wy, CANVAS_W * CANVAS_SCALE, CANVAS_H * CANVAS_SCALE),
      NULL, doc_win_proc, NULL);
  dwin->userdata = doc;
  doc->win = dwin;

  // Canvas child (fills the document window's client area)
  window_t *cwin = create_window(
      "", WINDOW_NOTITLE | WINDOW_NOFILL,
      MAKERECT(0, 0, CANVAS_W * CANVAS_SCALE, CANVAS_H * CANVAS_SCALE),
      dwin, canvas_proc, doc);
  cwin->notabstop = false;
  doc->canvas_win = cwin;

  show_window(dwin, true);

  // Add to doc list
  doc->next  = g_app->docs;
  g_app->docs = doc;
  g_app->active_doc = doc;

  doc_update_title(doc);
  send_message(dwin, kWindowMessageStatusBar, 0,
               filename ? filename : "New image");
  return doc;
}

static void close_document(canvas_doc_t *doc) {
  if (!doc || !g_app) return;

  if (g_app->active_doc == doc)
    g_app->active_doc = NULL;

  // Remove from list
  if (g_app->docs == doc) {
    g_app->docs = doc->next;
  } else {
    for (canvas_doc_t *d = g_app->docs; d; d = d->next) {
      if (d->next == doc) { d->next = doc->next; break; }
    }
  }

  if (doc->canvas_tex)
    glDeleteTextures(1, &doc->canvas_tex);

  if (doc->win && is_window(doc->win))
    destroy_window(doc->win);

  free(doc);
}

// ============================================================
// Menu bar proc (chains to win_menubar; handles commands here)
// ============================================================

static const menu_item_t kFileItems[] = {
  {"New",        ID_FILE_NEW},
  {"Open...",    ID_FILE_OPEN},
  {NULL,         0},
  {"Save",       ID_FILE_SAVE},
  {"Save As...", ID_FILE_SAVEAS},
  {NULL,         0},
  {"Close",      ID_FILE_CLOSE},
  {NULL,         0},
  {"Quit",       ID_FILE_QUIT},
};

static const menu_def_t kMenus[] = {
  {"File", kFileItems, (int)(sizeof(kFileItems)/sizeof(kFileItems[0]))},
};

static void handle_menu_command(uint16_t id) {
  if (!g_app) return;
  canvas_doc_t *doc = g_app->active_doc;

  switch (id) {
    case ID_FILE_NEW:
      create_document(NULL);
      break;

    case ID_FILE_OPEN: {
      char path[512] = {0};
      if (show_file_picker(g_app->menubar_win, false, path, sizeof(path))) {
        canvas_doc_t *ndoc = create_document(path);
        if (ndoc) {
          if (!png_load(path, ndoc->pixels)) {
            send_message(ndoc->win, kWindowMessageStatusBar, 0,
                         "Failed to open file");
          } else {
            ndoc->canvas_dirty = true;
            ndoc->modified = false;
            doc_update_title(ndoc);
            send_message(ndoc->win, kWindowMessageStatusBar, 0, path);
            invalidate_window(ndoc->canvas_win);
          }
        }
      }
      break;
    }

    case ID_FILE_SAVE:
      if (!doc) break;
      if (!doc->filename[0]) goto do_save_as;
      if (png_save(doc->filename, doc->pixels)) {
        doc->modified = false;
        doc_update_title(doc);
        send_message(doc->win, kWindowMessageStatusBar, 0, "Saved");
      } else {
        send_message(doc->win, kWindowMessageStatusBar, 0, "Save failed");
      }
      break;

    do_save_as:
    case ID_FILE_SAVEAS: {
      if (!doc) break;
      char path[512] = {0};
      if (show_file_picker(g_app->menubar_win, true, path, sizeof(path))) {
        strncpy(doc->filename, path, sizeof(doc->filename)-1);
        if (png_save(path, doc->pixels)) {
          doc->modified = false;
          doc_update_title(doc);
          send_message(doc->win, kWindowMessageStatusBar, 0, path);
        } else {
          send_message(doc->win, kWindowMessageStatusBar, 0, "Save failed");
        }
      }
      break;
    }

    case ID_FILE_CLOSE:
      if (doc) close_document(doc);
      break;

    case ID_FILE_QUIT:
      running = false;
      break;
  }
}

static result_t editor_menubar_proc(window_t *win, uint32_t msg,
                                     uint32_t wparam, void *lparam) {
  // Intercept menu-item selection notifications
  if (msg == kWindowMessageCommand &&
      HIWORD(wparam) == kMenuBarNotificationItemClick) {
    handle_menu_command(LOWORD(wparam));
    return true;
  }
  // Delegate UI behaviour to the generic win_menubar
  return win_menubar(win, msg, wparam, lparam);
}

// ============================================================
// Application init
// ============================================================

static void create_app_windows(void) {
  int sw = ui_get_system_metrics(kSystemMetricScreenWidth);

  // Menu bar (full screen width, ALWAYSONTOP)
  window_t *mb = create_window(
      "menubar",
      WINDOW_NOTITLE | WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
      MAKERECT(0, 0, sw, MENUBAR_HEIGHT),
      NULL, editor_menubar_proc, NULL);
  send_message(mb, kMenuBarMessageSetMenus,
               sizeof(kMenus)/sizeof(kMenus[0]), (void *)kMenus);
  show_window(mb, true);
  g_app->menubar_win = mb;

  // Tool palette
  window_t *tp = create_window(
      "Tools",
      WINDOW_NOTITLE | WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
      MAKERECT(PALETTE_WIN_X, PALETTE_WIN_Y, PALETTE_WIN_W, TOOL_WIN_H),
      NULL, tool_palette_proc, NULL);
  show_window(tp, true);
  g_app->tool_win = tp;

  // Color palette
  window_t *cp = create_window(
      "Colors",
      WINDOW_NOTITLE | WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
      MAKERECT(PALETTE_WIN_X, COLOR_WIN_Y, PALETTE_WIN_W, COLOR_WIN_H),
      NULL, color_palette_proc, NULL);
  show_window(cp, true);
  g_app->color_win = cp;
}

// ============================================================
// main
// ============================================================

int main(void) {
  g_app = calloc(1, sizeof(app_state_t));
  if (!g_app) return 1;

  g_app->tool     = TOOL_PENCIL;
  g_app->fg_color = kPalette[4]; // black
  g_app->bg_color = kPalette[0]; // white
  g_app->next_x   = DOC_START_X;
  g_app->next_y   = DOC_START_Y;

  if (!ui_init_graphics(0, "Orion Image Editor",
                        SCREEN_W, SCREEN_H)) {
    free(g_app);
    return 1;
  }

  create_app_windows();

  // Open one blank document at startup
  create_document(NULL);

  while (running) {
    ui_handle_events();
  }

  // Cleanup documents
  while (g_app->docs) {
    canvas_doc_t *next = g_app->docs->next;
    if (g_app->docs->canvas_tex)
      glDeleteTextures(1, &g_app->docs->canvas_tex);
    // destroy_window already called by close sequence, so just free:
    free(g_app->docs);
    g_app->docs = next;
  }
  free(g_app);
  g_app = NULL;

  ui_shutdown_graphics();
  return 0;
}
