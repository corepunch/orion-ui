#ifndef __IMAGEEDITOR_H__
#define __IMAGEEDITOR_H__

#include <stdint.h>
#include <stdbool.h>

// ============================================================
// Constants
// ============================================================

#define CANVAS_W      320
#define CANVAS_H      200
#define CANVAS_SCALE  2

#define SCREEN_W      800
#define SCREEN_H      600

#define PALETTE_WIN_X   4
#define PALETTE_WIN_Y  (MENUBAR_HEIGHT + 4)
#define PALETTE_WIN_W  64
#define TOOL_WIN_H    (14 + NUM_TOOLS * 24 + 22)
#define COLOR_WIN_Y   (PALETTE_WIN_Y + TOOL_WIN_H + 4)
#define COLOR_WIN_H   (12 + 8 * 22)

#define DOC_START_X   76
#define DOC_START_Y   60
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

typedef struct { uint8_t r, g, b, a; } rgba_t;

typedef struct canvas_doc_s {
  uint8_t  pixels[CANVAS_H * CANVAS_W * 4];
  GLuint   canvas_tex;
  bool     canvas_dirty;
  bool     drawing;
  int      last_x, last_y;
  bool     modified;
  char     filename[512];
  window_t *win;
  window_t *canvas_win;
  struct canvas_doc_s *next;
} canvas_doc_t;

typedef struct {
  canvas_doc_t  *active_doc;
  canvas_doc_t  *docs;
  window_t      *menubar_win;
  window_t      *tool_win;
  window_t      *color_win;
  int            current_tool;
  rgba_t         fg_color;
  rgba_t         bg_color;
  int            next_x;
  int            next_y;
} app_state_t;

// ============================================================
// Tool plugin interface
// ============================================================

typedef struct tool_s {
  const char *name;
  void (*on_down)(canvas_doc_t *doc, int cx, int cy, rgba_t fg, rgba_t bg);
  void (*on_drag)(canvas_doc_t *doc, int x0, int y0, int cx, int cy, rgba_t fg, rgba_t bg);
} tool_t;

// ============================================================
// Global state
// ============================================================

extern app_state_t *g_app;

// Color palette
extern const rgba_t kPalette[NUM_COLORS];

// Tool registry
extern const tool_t *tools[];

// ============================================================
// Canvas helpers (used across files)
// ============================================================

static inline bool rgba_eq(rgba_t a, rgba_t b) {
  return a.r==b.r && a.g==b.g && a.b==b.b && a.a==b.a;
}

static inline uint32_t rgba_to_col(rgba_t c) {
  return ((uint32_t)c.a<<24)|((uint32_t)c.b<<16)|((uint32_t)c.g<<8)|(uint32_t)c.r;
}

static inline bool canvas_in_bounds(int x, int y) {
  return x >= 0 && x < CANVAS_W && y >= 0 && y < CANVAS_H;
}

// Forward declarations for canvas operations
void canvas_set_pixel(canvas_doc_t *doc, int x, int y, rgba_t c);
rgba_t canvas_get_pixel(const canvas_doc_t *doc, int x, int y);
void canvas_clear(canvas_doc_t *doc);
void canvas_draw_circle(canvas_doc_t *doc, int cx, int cy, int r, rgba_t c);
void canvas_draw_line(canvas_doc_t *doc, int x0, int y0, int x1, int y1, int radius, rgba_t c);
void canvas_flood_fill(canvas_doc_t *doc, int sx, int sy, rgba_t fill);

// Forward declarations for document management
canvas_doc_t *create_document(const char *filename);
void close_document(canvas_doc_t *doc);
void doc_update_title(canvas_doc_t *doc);

#endif // __IMAGEEDITOR_H__
