// Image Editor - MacPaint-inspired with color support
// Supports PNG file format for open/save
// Reuses the file browsing window pattern from filemanager.c

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
#define CANVAS_SCALE  2    // Canvas displayed at 2x → 640×400 on screen

#define PALETTE_W     64   // Width of the left tool/palette panel
#define SWATCH_COLS   2
#define SWATCH_SIZE   (PALETTE_W / SWATCH_COLS)  // 32 px per color swatch
#define NUM_COLORS    16
#define NUM_TOOLS     4
#define TOOL_AREA_H   (NUM_TOOLS * 22 + 8)  // 4 tools × 22 px + 8 px separator

// Window dimensions (logical pixels, i.e. before ×2 SDL scale)
#define SCREEN_W      760
#define SCREEN_H      480
#define WIN_X         28
#define WIN_Y         40
#define WIN_W         (PALETTE_W + CANVAS_W * CANVAS_SCALE)   // 64+640=704
#define WIN_H         (CANVAS_H * CANVAS_SCALE)               // 400

// Command IDs
#define ID_OPEN  101
#define ID_SAVE  102
#define ID_NEW   103

// File-picker dialog child IDs (assigned sequentially by parent->child_id)
#define PICKER_ID_LIST  1
#define PICKER_ID_EDIT  2
#define PICKER_ID_OK    3
#define PICKER_ID_CANCEL 4

// Icon indices (existing icons used in filemanager.c)
#define ICON_FOLDER 5
#define ICON_FILE   6
#define ICON_UP     7
#define COLOR_FOLDER 0xffa0d000u

// ============================================================
// Types
// ============================================================

typedef enum {
  TOOL_PENCIL = 0,
  TOOL_BRUSH,
  TOOL_ERASER,
  TOOL_FILL,
} editor_tool_t;

typedef struct {
  uint8_t r, g, b, a;
} rgba_t;

// Per-pixel color equality
static bool rgba_eq(rgba_t a, rgba_t b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// Convert RGBA to the uint32_t color format expected by fill_rect.
// fill_rect calls glTexSubImage2D with GL_RGBA, GL_UNSIGNED_BYTE, so on
// little-endian the bytes in memory are read as [R,G,B,A], meaning the
// uint32_t value must be 0xAABBGGRR (least-significant byte = R).
static uint32_t rgba_to_col(rgba_t c) {
  return ((uint32_t)c.a << 24) | ((uint32_t)c.b << 16) |
         ((uint32_t)c.g <<  8) | (uint32_t)c.r;
}

// Editor state (stored in main window's userdata)
typedef struct {
  uint8_t  pixels[CANVAS_H * CANVAS_W * 4]; // RGBA canvas pixels
  GLuint   canvas_tex;
  bool     canvas_dirty;
  bool     drawing;
  int      last_x, last_y;
  editor_tool_t tool;
  rgba_t   fg_color;  // foreground / draw color
  rgba_t   bg_color;  // background / eraser color (white)
  char     current_file[512];
  window_t *canvas_win;
  window_t *palette_win;
  window_t *main_win;
} editor_state_t;

// ============================================================
// Color palette  (16 colours matching early Mac / classic PC art)
// ============================================================

static const rgba_t kPalette[NUM_COLORS] = {
  {0xFF,0xFF,0xFF,0xFF},  // white
  {0xCC,0xCC,0xCC,0xFF},  // light gray
  {0x88,0x88,0x88,0xFF},  // gray
  {0x44,0x44,0x44,0xFF},  // dark gray
  {0x00,0x00,0x00,0xFF},  // black
  {0xFF,0x00,0x00,0xFF},  // red
  {0xFF,0x88,0x00,0xFF},  // orange
  {0xFF,0xFF,0x00,0xFF},  // yellow
  {0x00,0xFF,0x00,0xFF},  // green
  {0x00,0x88,0x44,0xFF},  // dark green
  {0x00,0xFF,0xFF,0xFF},  // cyan
  {0x00,0x88,0xFF,0xFF},  // sky blue
  {0x00,0x00,0xFF,0xFF},  // blue
  {0x88,0x00,0xFF,0xFF},  // purple
  {0xFF,0x00,0xFF,0xFF},  // magenta
  {0xFF,0x44,0x88,0xFF},  // pink
};

// ============================================================
// PNG I/O (libpng)
// ============================================================

// Save a 320×200 (or any size) RGBA image as PNG.
// `pixels` is a packed RGBA byte array, `w`×`h` pixels.
static bool png_save(const char *filename, const uint8_t *pixels, int w, int h) {
  FILE *fp = fopen(filename, "wb");
  if (!fp) { fprintf(stderr, "Cannot open %s for writing\n", filename); return false; }

  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png) { fclose(fp); return false; }

  png_infop info = png_create_info_struct(png);
  if (!info) { png_destroy_write_struct(&png, NULL); fclose(fp); return false; }

  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    fclose(fp);
    return false;
  }

  png_init_io(png, fp);
  png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGBA,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);

  for (int y = 0; y < h; y++) {
    png_write_row(png, (png_bytep)(pixels + (size_t)y * w * 4));
  }

  png_write_end(png, NULL);
  png_destroy_write_struct(&png, &info);
  fclose(fp);
  return true;
}

// Load a PNG file into a fixed CANVAS_W×CANVAS_H RGBA buffer.
// Clips or pads to fit exactly.  Returns false on error.
static bool png_load(const char *filename, uint8_t *pixels) {
  FILE *fp = fopen(filename, "rb");
  if (!fp) { fprintf(stderr, "Cannot open %s\n", filename); return false; }

  uint8_t sig[8];
  if (fread(sig, 1, 8, fp) != 8 || !png_check_sig(sig, 8)) {
    fclose(fp); return false;
  }

  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png) { fclose(fp); return false; }

  png_infop info = png_create_info_struct(png);
  if (!info) { png_destroy_read_struct(&png, NULL, NULL); fclose(fp); return false; }

  if (setjmp(png_jmpbuf(png))) {
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp); return false;
  }

  png_init_io(png, fp);
  png_set_sig_bytes(png, 8);
  png_read_info(png, info);

  int src_w = (int)png_get_image_width(png, info);
  int src_h = (int)png_get_image_height(png, info);
  png_byte color_type = png_get_color_type(png, info);
  png_byte bit_depth  = png_get_bit_depth(png, info);

  // Normalize to 8-bit RGBA
  if (bit_depth == 16) png_set_strip_16(png);
  if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
    png_set_expand_gray_1_2_4_to_8(png);
  if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
  if (color_type == PNG_COLOR_TYPE_RGB ||
      color_type == PNG_COLOR_TYPE_GRAY ||
      color_type == PNG_COLOR_TYPE_PALETTE)
    png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
  if (color_type == PNG_COLOR_TYPE_GRAY ||
      color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
    png_set_gray_to_rgb(png);
  png_read_update_info(png, info);

  // Allocate temporary buffer for source image
  uint8_t *tmp = malloc((size_t)src_w * src_h * 4);
  if (!tmp) { png_destroy_read_struct(&png, &info, NULL); fclose(fp); return false; }

  png_bytep *rows = malloc(sizeof(png_bytep) * src_h);
  if (!rows) { free(tmp); png_destroy_read_struct(&png, &info, NULL); fclose(fp); return false; }
  for (int y = 0; y < src_h; y++) rows[y] = tmp + (size_t)y * src_w * 4;
  png_read_image(png, rows);
  free(rows);
  png_destroy_read_struct(&png, &info, NULL);
  fclose(fp);

  // Copy/clip/pad into canvas buffer (white-fill first)
  memset(pixels, 0xFF, (size_t)CANVAS_W * CANVAS_H * 4);
  int copy_w = MIN(src_w, CANVAS_W);
  int copy_h = MIN(src_h, CANVAS_H);
  for (int y = 0; y < copy_h; y++) {
    memcpy(pixels + (size_t)y * CANVAS_W * 4,
           tmp    + (size_t)y * src_w  * 4,
           (size_t)copy_w * 4);
  }
  free(tmp);
  return true;
}

// ============================================================
// Canvas helper functions
// ============================================================

static void canvas_clear(editor_state_t *s) {
  memset(s->pixels, 0xFF, sizeof(s->pixels)); // white canvas
  s->canvas_dirty = true;
}

static inline bool canvas_in_bounds(int x, int y) {
  return x >= 0 && x < CANVAS_W && y >= 0 && y < CANVAS_H;
}

static void canvas_set_pixel(editor_state_t *s, int x, int y, rgba_t c) {
  if (!canvas_in_bounds(x, y)) return;
  uint8_t *p = s->pixels + ((size_t)y * CANVAS_W + x) * 4;
  p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = c.a;
  s->canvas_dirty = true;
}

static rgba_t canvas_get_pixel(const editor_state_t *s, int x, int y) {
  if (!canvas_in_bounds(x, y)) return (rgba_t){0,0,0,0};
  const uint8_t *p = s->pixels + ((size_t)y * CANVAS_W + x) * 4;
  return (rgba_t){p[0], p[1], p[2], p[3]};
}

// Filled circle centred at (cx,cy) with radius r
static void canvas_draw_circle(editor_state_t *s, int cx, int cy, int r, rgba_t c) {
  for (int dy = -r; dy <= r; dy++)
    for (int dx = -r; dx <= r; dx++)
      if (dx*dx + dy*dy <= r*r)
        canvas_set_pixel(s, cx+dx, cy+dy, c);
}

// Bresenham line, painting a circle (radius r) at each step for thick strokes
static void canvas_draw_line(editor_state_t *s,
                             int x0, int y0, int x1, int y1,
                             rgba_t c, int r) {
  int dx = abs(x1-x0), sx = x0<x1 ? 1:-1;
  int dy = -abs(y1-y0), sy = y0<y1 ? 1:-1;
  int err = dx+dy;
  for (;;) {
    if (r == 0) canvas_set_pixel(s, x0, y0, c);
    else        canvas_draw_circle(s, x0, y0, r, c);
    if (x0==x1 && y0==y1) break;
    int e2 = 2*err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

// BFS flood fill
static void canvas_flood_fill(editor_state_t *s, int sx, int sy, rgba_t fill) {
  rgba_t target = canvas_get_pixel(s, sx, sy);
  if (rgba_eq(target, fill)) return;

  int total = CANVAS_W * CANVAS_H;
  int *stack_x = malloc(sizeof(int) * total);
  int *stack_y = malloc(sizeof(int) * total);
  bool *visited = calloc(total, sizeof(bool));
  if (!stack_x || !stack_y || !visited) {
    free(stack_x); free(stack_y); free(visited); return;
  }

  int top = 0;
  stack_x[top] = sx; stack_y[top] = sy; top++;

  const int ddx[] = {1,-1,0,0};
  const int ddy[] = {0,0,1,-1};

  while (top > 0) {
    top--;
    int x = stack_x[top], y = stack_y[top];
    if (!canvas_in_bounds(x, y)) continue;
    int idx = y * CANVAS_W + x;
    if (visited[idx]) continue;
    if (!rgba_eq(canvas_get_pixel(s, x, y), target)) continue;
    visited[idx] = true;
    canvas_set_pixel(s, x, y, fill);
    for (int d = 0; d < 4 && top < total; d++) {
      stack_x[top] = x + ddx[d];
      stack_y[top] = y + ddy[d];
      top++;
    }
  }
  free(stack_x); free(stack_y); free(visited);
}

// ============================================================
// File picker dialog  (based on filemanager.c, filtered to .png)
// ============================================================

// Result written here by the dialog proc
static char picker_result[512];
static bool picker_is_save;

typedef struct {
  char path[512];
  window_t *list;
  window_t *name_edit;
} picker_state_t;

static bool is_png(const char *name) {
  size_t n = strlen(name);
  return n >= 5 && strcasecmp(name + n - 4, ".png") == 0;
}

static void picker_load_dir(window_t *list_win, picker_state_t *ps) {
  DIR *dir = opendir(ps->path);
  if (!dir) return;
  send_message(list_win, CVM_CLEAR, 0, NULL);
  // ".." entry
  send_message(list_win, CVM_ADDITEM, 0,
    &(columnview_item_t){"..", ICON_UP, COLOR_FOLDER, 0});

  // Collect and sort entries
  struct {
    char name[256];
    struct stat st;
  } *entries = NULL;
  int count = 0, cap = 0;
  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL) {
    if (ent->d_name[0] == '.') continue;
    char fullpath[768];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", ps->path, ent->d_name);
    struct stat st;
    if (stat(fullpath, &st) != 0) continue;
    bool is_dir = S_ISDIR(st.st_mode);
    if (!is_dir && !is_png(ent->d_name)) continue; // only dirs and .png files
    if (count >= cap) {
      cap = cap ? cap*2 : 32;
      entries = realloc(entries, sizeof(*entries) * cap);
    }
    strncpy(entries[count].name, ent->d_name, 255);
    entries[count].name[255] = '\0';
    entries[count].st = st;
    count++;
  }
  closedir(dir);

  // Sort alphabetically (dirs before files) – simple insertion sort
  for (int i = 0; i < count-1; i++) {
    for (int j = i+1; j < count; j++) {
      bool ia = S_ISDIR(entries[i].st.st_mode);
      bool ja = S_ISDIR(entries[j].st.st_mode);
      if ((!ia && ja) || ((ia==ja) && strcasecmp(entries[i].name, entries[j].name) > 0)) {
        char   tmp_name[256];
        struct stat tmp_st;
        memcpy(tmp_name, entries[i].name, sizeof(tmp_name));
        memcpy(&tmp_st,  &entries[i].st,  sizeof(tmp_st));
        memcpy(entries[i].name, entries[j].name, sizeof(tmp_name));
        memcpy(&entries[i].st,  &entries[j].st,  sizeof(tmp_st));
        memcpy(entries[j].name, tmp_name, sizeof(tmp_name));
        memcpy(&entries[j].st,  &tmp_st,  sizeof(tmp_st));
      }
    }
  }

  for (int i = 0; i < count; i++) {
    bool is_dir = S_ISDIR(entries[i].st.st_mode);
    send_message(list_win, CVM_ADDITEM, 0,
      &(columnview_item_t){
        entries[i].name,
        is_dir ? ICON_FOLDER : ICON_FILE,
        is_dir ? (uint32_t)COLOR_FOLDER : (uint32_t)0xffffffff,
        (uint32_t)is_dir
      });
  }
  free(entries);
}

static result_t picker_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  picker_state_t *ps = (picker_state_t *)win->userdata;

  switch (msg) {
    case kWindowMessageCreate: {
      ps = allocate_window_data(win, sizeof(picker_state_t));
      getcwd(ps->path, sizeof(ps->path));

      // File list (columnview) — takes up most of the dialog
      window_t *list = create_window("", WINDOW_NOTITLE|WINDOW_VSCROLL,
                                     MAKERECT(2, 2, 356, 220), win,
                                     win_columnview, NULL);
      list->id = PICKER_ID_LIST;
      ps->list = list;
      picker_load_dir(list, ps);

      // Filename label + edit box
      create_window("File:", WINDOW_NOTITLE,
                    MAKERECT(2, 228, 30, 10), win, win_label, NULL);
      window_t *edit = create_window(
        picker_is_save ? "untitled.png" : "",
        WINDOW_NOTITLE, MAKERECT(34, 224, 244, 13), win,
        win_textedit, NULL);
      edit->id = PICKER_ID_EDIT;
      ps->name_edit = edit;

      // OK / Cancel buttons
      window_t *ok = create_window(
        picker_is_save ? "Save" : "Open",
        WINDOW_NOTITLE, MAKERECT(282, 222, 40, 0), win,
        win_button, NULL);
      ok->id = PICKER_ID_OK;

      window_t *cancel = create_window(
        "Cancel", WINDOW_NOTITLE, MAKERECT(326, 222, 40, 0), win,
        win_button, NULL);
      cancel->id = PICKER_ID_CANCEL;
      return true;
    }

    case kWindowMessageCommand: {
      uint16_t notif = HIWORD(wparam);
      uint16_t id    = LOWORD(wparam);

      if (notif == CVN_DBLCLK && id == PICKER_ID_LIST) {
        // Double-click in the file list
        columnview_item_t *item = (columnview_item_t *)lparam;
        if (!item) return false;
        if (item->userdata) {
          // Navigate into directory
          if (strcmp(item->text, "..") == 0) {
            char *slash = strrchr(ps->path, '/');
            if (slash && slash != ps->path) *slash = '\0';
            else if (strcmp(ps->path, "/") != 0) strcpy(ps->path, "/");
          } else {
            char tmp[512];
            snprintf(tmp, sizeof(tmp), "%s/%s", ps->path, item->text);
            strncpy(ps->path, tmp, sizeof(ps->path)-1);
          }
          picker_load_dir(ps->list, ps);
        } else if (is_png(item->text)) {
          // Select the file name
          set_window_item_text(win, PICKER_ID_EDIT, "%s", item->text);
        }
        return true;
      }

      if (notif == CVN_SELCHANGE && id == PICKER_ID_LIST) {
        // Single-click → populate filename field
        columnview_item_t *item = (columnview_item_t *)lparam;
        if (item && !item->userdata && is_png(item->text)) {
          set_window_item_text(win, PICKER_ID_EDIT, "%s", item->text);
        }
        return true;
      }

      if (notif == kButtonNotificationClicked && id == PICKER_ID_OK) {
        window_t *edit = get_window_item(win, PICKER_ID_EDIT);
        if (edit && edit->title[0]) {
          const char *name = edit->title;
          if (is_png(name)) {
            snprintf(picker_result, sizeof(picker_result), "%s/%s", ps->path, name);
          } else {
            snprintf(picker_result, sizeof(picker_result), "%s/%s.png", ps->path, name);
          }
        } else {
          picker_result[0] = '\0';
        }
        end_dialog(win, 1);
        return true;
      }

      if (notif == kButtonNotificationClicked && id == PICKER_ID_CANCEL) {
        picker_result[0] = '\0';
        end_dialog(win, 0);
        return true;
      }
      return false;
    }

    case kWindowMessageDestroy:
      return true;

    default:
      return false;
  }
}

// Show modal open/save dialog.  Returns true if a file was selected.
static bool show_file_picker(window_t *parent, bool is_save, char *out_path, size_t path_size) {
  picker_is_save = is_save;
  picker_result[0] = '\0';

  const char *title = is_save ? "Save PNG" : "Open PNG";
  uint32_t result = show_dialog(title,
                                MAKERECT(120, 60, 376, 260),
                                parent, picker_proc, NULL);
  (void)result;
  if (picker_result[0]) {
    strncpy(out_path, picker_result, path_size-1);
    out_path[path_size-1] = '\0';
    return true;
  }
  return false;
}

// ============================================================
// Palette / tool panel window proc
// ============================================================

static const char *kToolNames[NUM_TOOLS] = {"Pencil", "Brush", "Eraser", "Fill"};

result_t palette_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  editor_state_t *state = (editor_state_t *)win->userdata;
  (void)lparam;

  switch (msg) {
    case kWindowMessageCreate:
      return true;

    case kWindowMessagePaint: {
      if (!state) return true;
      // Panel background
      fill_rect(COLOR_PANEL_DARK_BG, win->frame.x, win->frame.y,
                win->frame.w, win->frame.h);

      // Tool buttons
      int ty = win->frame.y;
      for (int i = 0; i < NUM_TOOLS; i++) {
        bool sel = ((int)state->tool == i);
        uint32_t bg = sel ? COLOR_FOCUSED : COLOR_BUTTON_BG;
        fill_rect(bg, win->frame.x+2, ty+2, win->frame.w-4, 18);
        draw_text_small(kToolNames[i], win->frame.x+5, ty+6, COLOR_TEXT_NORMAL);
        ty += 22;
      }

      // Separator
      fill_rect(COLOR_DARK_EDGE, win->frame.x, ty+2, win->frame.w, 1);
      ty += 6;

      // Color swatches (2-column grid)
      for (int i = 0; i < NUM_COLORS; i++) {
        int col = i % SWATCH_COLS;
        int row = i / SWATCH_COLS;
        int sx = win->frame.x + col * SWATCH_SIZE;
        int sy = ty + row * SWATCH_SIZE;

        // Swatch fill
        fill_rect(rgba_to_col(kPalette[i]), sx+2, sy+2, SWATCH_SIZE-4, SWATCH_SIZE-4);

        // Selection indicator (white border for selected fg color)
        if (rgba_eq(kPalette[i], state->fg_color)) {
          fill_rect(COLOR_LIGHT_EDGE, sx,   sy,   SWATCH_SIZE, 2);
          fill_rect(COLOR_LIGHT_EDGE, sx,   sy,   2, SWATCH_SIZE);
          fill_rect(COLOR_DARK_EDGE,  sx+SWATCH_SIZE-2, sy, 2, SWATCH_SIZE);
          fill_rect(COLOR_DARK_EDGE,  sx,   sy+SWATCH_SIZE-2, SWATCH_SIZE, 2);
        }
      }

      // Active color preview (fg / bg)
      int py = ty + (NUM_COLORS/SWATCH_COLS) * SWATCH_SIZE + 4;
      fill_rect(rgba_to_col(state->bg_color), win->frame.x+2,  py+6, 22, 22);
      fill_rect(rgba_to_col(state->fg_color), win->frame.x+10, py,   22, 22);
      draw_text_small("FG", win->frame.x+14, py+2, COLOR_TEXT_NORMAL);
      draw_text_small("BG", win->frame.x+4,  py+18, COLOR_PANEL_BG);
      return true;
    }

    case kWindowMessageLeftButtonDown: {
      if (!state) return true;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);

      // Check tool buttons
      for (int i = 0; i < NUM_TOOLS; i++) {
        if (ly >= i*22 && ly < i*22+22) {
          state->tool = (editor_tool_t)i;
          invalidate_window(win);
          return true;
        }
      }

      // Check color swatches
      int swatches_top = NUM_TOOLS * 22 + 6;
      if (ly >= swatches_top) {
        int sy = ly - swatches_top;
        int row = sy / SWATCH_SIZE;
        int col = lx / SWATCH_SIZE;
        int idx = row * SWATCH_COLS + col;
        if (idx >= 0 && idx < NUM_COLORS) {
          state->fg_color = kPalette[idx];
          invalidate_window(win);
        }
      }
      return true;
    }

    case kWindowMessageDestroy:
      return true;

    default:
      return false;
  }
}

// ============================================================
// Canvas window proc
// ============================================================

// Upload pixel buffer to canvas texture (create if needed)
static void canvas_upload(editor_state_t *s) {
  if (!s->canvas_tex) {
    glGenTextures(1, &s->canvas_tex);
    glBindTexture(GL_TEXTURE_2D, s->canvas_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, CANVAS_W, CANVAS_H, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, s->pixels);
  } else if (s->canvas_dirty) {
    glBindTexture(GL_TEXTURE_2D, s->canvas_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, CANVAS_W, CANVAS_H,
                    GL_RGBA, GL_UNSIGNED_BYTE, s->pixels);
  }
  glBindTexture(GL_TEXTURE_2D, 0);
  s->canvas_dirty = false;
}

result_t canvas_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  editor_state_t *state = (editor_state_t *)win->userdata;
  (void)lparam;

  switch (msg) {
    case kWindowMessageCreate:
      return true;

    case kWindowMessagePaint: {
      if (!state) return true;
      // Upload texture if needed
      canvas_upload(state);
      if (state->canvas_tex) {
        draw_rect(state->canvas_tex,
                  win->frame.x, win->frame.y,
                  win->frame.w, win->frame.h);
      }
      return true;
    }

    case kWindowMessageLeftButtonDown: {
      if (!state) return true;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      int cx = lx / CANVAS_SCALE;
      int cy = ly / CANVAS_SCALE;
      state->drawing = true;
      state->last_x  = cx;
      state->last_y  = cy;
      set_capture(win);

      rgba_t color = (state->tool == TOOL_ERASER) ? state->bg_color : state->fg_color;
      int radius    = (state->tool == TOOL_BRUSH)  ? 2 : 0;

      if (state->tool == TOOL_FILL) {
        canvas_flood_fill(state, cx, cy, state->fg_color);
      } else {
        canvas_draw_circle(state, cx, cy, radius, color);
      }
      invalidate_window(win);
      return true;
    }

    case kWindowMessageMouseMove: {
      if (!state || !state->drawing) return true;
      int lx = (int16_t)LOWORD(wparam);
      int ly = (int16_t)HIWORD(wparam);
      int cx = lx / CANVAS_SCALE;
      int cy = ly / CANVAS_SCALE;

      if (state->tool != TOOL_FILL) {
        rgba_t color = (state->tool == TOOL_ERASER) ? state->bg_color : state->fg_color;
        int radius   = (state->tool == TOOL_BRUSH)  ? 2 : 0;
        canvas_draw_line(state, state->last_x, state->last_y, cx, cy, color, radius);
        invalidate_window(win);
      }
      state->last_x = cx;
      state->last_y = cy;
      return true;
    }

    case kWindowMessageLeftButtonUp: {
      if (!state) return true;
      state->drawing = false;
      set_capture(NULL);
      return true;
    }

    case kWindowMessageDestroy: {
      if (state && state->canvas_tex) {
        glDeleteTextures(1, &state->canvas_tex);
        state->canvas_tex = 0;
      }
      return true;
    }

    default:
      return false;
  }
}

// ============================================================
// Main editor window proc
// ============================================================

result_t editor_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  editor_state_t *state = (editor_state_t *)win->userdata;

  switch (msg) {
    case kWindowMessageCreate: {
      state = allocate_window_data(win, sizeof(editor_state_t));
      state->main_win = win;
      state->tool     = TOOL_PENCIL;
      state->fg_color = kPalette[4]; // black
      state->bg_color = kPalette[0]; // white
      canvas_clear(state);

      // Toolbar buttons
      static toolbar_button_t toolbar[] = {
        {0, ID_NEW,  false},
        {6, ID_OPEN, false},
        {5, ID_SAVE, false},
      };
      send_message(win, kToolBarMessageAddButtons,
                   sizeof(toolbar)/sizeof(toolbar[0]), toolbar);

      // Palette panel (left strip)
      window_t *pal = create_window("", WINDOW_NOTITLE|WINDOW_NOFILL,
                                    MAKERECT(0, 0, PALETTE_W, WIN_H),
                                    win, palette_proc, NULL);
      pal->userdata = state;
      pal->notabstop = false;
      state->palette_win = pal;

      // Canvas display area
      window_t *cv = create_window("", WINDOW_NOTITLE|WINDOW_NOFILL,
                                   MAKERECT(PALETTE_W, 0,
                                            CANVAS_W*CANVAS_SCALE,
                                            CANVAS_H*CANVAS_SCALE),
                                   win, canvas_proc, NULL);
      cv->userdata = state;
      cv->notabstop = false;
      state->canvas_win = cv;

      send_message(win, kWindowMessageStatusBar, 0, "Image Editor – Ready");
      return true;
    }

    case kWindowMessagePaint:
      // Fill the area to the right of the canvas with a dark bg
      // (visible if window is larger than canvas)
      fill_rect(COLOR_PANEL_DARK_BG,
                win->frame.x, win->frame.y,
                win->frame.w, win->frame.h);
      return false; // let children paint

    case kToolBarMessageButtonClick: {
      toolbar_button_t *btn = (toolbar_button_t *)lparam;
      if (!btn) return false;

      if (btn->ident == ID_NEW) {
        canvas_clear(state);
        state->current_file[0] = '\0';
        send_message(win, kWindowMessageStatusBar, 0, "Image Editor – New image");
        invalidate_window(state->canvas_win);
        return true;
      }

      if (btn->ident == ID_OPEN) {
        char path[512] = {0};
        if (show_file_picker(win, false, path, sizeof(path))) {
          if (png_load(path, state->pixels)) {
            strncpy(state->current_file, path, sizeof(state->current_file)-1);
            state->canvas_dirty = true;
            char msg_buf[576];
            snprintf(msg_buf, sizeof(msg_buf), "Image Editor – %s", path);
            send_message(win, kWindowMessageStatusBar, 0, msg_buf);
            invalidate_window(state->canvas_win);
          } else {
            send_message(win, kWindowMessageStatusBar, 0,
                         "Image Editor – Failed to open file");
          }
        }
        return true;
      }

      if (btn->ident == ID_SAVE) {
        char path[512];
        if (state->current_file[0]) {
          strncpy(path, state->current_file, sizeof(path)-1);
          path[sizeof(path)-1] = '\0';
        } else {
          if (!show_file_picker(win, true, path, sizeof(path))) return true;
        }
        if (png_save(path, state->pixels, CANVAS_W, CANVAS_H)) {
          strncpy(state->current_file, path, sizeof(state->current_file)-1);
          char msg_buf[576];
          snprintf(msg_buf, sizeof(msg_buf), "Image Editor – Saved: %s", path);
          send_message(win, kWindowMessageStatusBar, 0, msg_buf);
        } else {
          send_message(win, kWindowMessageStatusBar, 0,
                       "Image Editor – Failed to save file");
        }
        return true;
      }
      return false;
    }

    case kWindowMessageDestroy:
      if (state) free(state);
      running = false;
      return true;

    default:
      return false;
  }
}

// ============================================================
// Main
// ============================================================

int main(int argc, char *argv[]) {
  (void)argc; (void)argv;

  printf("Image Editor – MacPaint-inspired, PNG support\n");

  if (!ui_init_graphics(UI_INIT_DESKTOP|UI_INIT_TRAY,
                        "Image Editor", SCREEN_W, SCREEN_H)) {
    fprintf(stderr, "Failed to initialize graphics\n");
    return 1;
  }

  window_t *win = create_window(
    "Image Editor",
    WINDOW_TOOLBAR | WINDOW_STATUSBAR | WINDOW_NORESIZE,
    MAKERECT(WIN_X, WIN_Y, WIN_W, WIN_H),
    NULL, editor_proc, NULL);

  if (!win) {
    fprintf(stderr, "Failed to create window\n");
    ui_shutdown_graphics();
    return 1;
  }

  show_window(win, true);

  ui_event_t e;
  while (running) {
    while (get_message(&e)) dispatch_message(&e);
    repost_messages();
  }

  destroy_window(win);
  ui_shutdown_graphics();
  printf("Goodbye!\n");
  return 0;
}
