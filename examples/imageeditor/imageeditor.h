#ifndef __IMAGEEDITOR_H__
#define __IMAGEEDITOR_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <png.h>

#include "../../ui.h"

// ============================================================
// Constants
// ============================================================

#define CANVAS_W      320
#define CANVAS_H      200

#define SCREEN_W      512
#define SCREEN_H      342

#define PALETTE_WIN_X   4
#define PALETTE_WIN_W  (TB_SPACING*2)
#define TOOL_ICON_W    16
#define TOOL_ICON_H    16
#define TOOL_ICON_ROW_H 24

// Compute the toolbar height for the tool palette (wraps based on window width and TB_SPACING).
// buttons_per_row = PALETTE_WIN_W / TB_SPACING = 64/20 = 3
// num_rows = ceil(NUM_TOOLS / buttons_per_row) = ceil(5/3) = 2
// TOOL_TOOLBAR_H = num_rows * TOOLBAR_HEIGHT = 2*20 = 40
//
// NOTE: These macros replicate the runtime wrapping formula from
// draw_impl.c:titlebar_height() as compile-time constants so that window
// positions can be expressed in terms of layout constants.  If TB_SPACING,
// TOOLBAR_HEIGHT, PALETTE_WIN_W, or NUM_TOOLS change, verify that these
// macros still match the runtime computation.
#define TOOL_BTN_PER_ROW  ((PALETTE_WIN_W) / (TB_SPACING))
#define TOOL_TOOLBAR_ROWS (((NUM_TOOLS) + (TOOL_BTN_PER_ROW) - 1) / (TOOL_BTN_PER_ROW))
#define TOOL_TOOLBAR_H    ((TOOL_TOOLBAR_ROWS) * (TOOLBAR_HEIGHT))

// PALETTE_WIN_Y is the frame.y (top of client area) of the tool window.
// Position it so that its title bar sits 4px below the menu bar.
// title bar top = frame.y - titlebar_height = frame.y - (TITLEBAR_HEIGHT + TOOL_TOOLBAR_H)
// We want title bar top = MENUBAR_HEIGHT + 4
// So: frame.y = MENUBAR_HEIGHT + 4 + TITLEBAR_HEIGHT + TOOL_TOOLBAR_H
#define PALETTE_WIN_Y  (MENUBAR_HEIGHT + 4 + TITLEBAR_HEIGHT + TOOL_TOOLBAR_H)

// Client area of the tool palette only needs to show the FG/BG color swatches:
//   2px label + 8px text height + 13px swatch height + 2px padding = 25px
#define SWATCH_CLIENT_H 26
#define TOOL_WIN_H    SWATCH_CLIENT_H
#define COLOR_WIN_Y   (PALETTE_WIN_Y + TOOL_WIN_H + TITLEBAR_HEIGHT + 4)
#define COLOR_WIN_H   (8 * 22)

#define DOC_START_X   76
#define DOC_START_Y   60
#define DOC_CASCADE   20

#define NUM_COLORS 16
#define NUM_TOOLS   5
#define NUM_COLORS      16
#define NUM_USER_COLORS  8

#define UNDO_MAX   20

// Menu item IDs
#define ID_FILE_NEW     1
#define ID_FILE_OPEN    2
#define ID_FILE_SAVE    3
#define ID_FILE_SAVEAS  4
#define ID_FILE_CLOSE   5
#define ID_FILE_QUIT    6

#define ID_EDIT_UNDO   10
#define ID_EDIT_REDO   11

#define ID_VIEW_ZOOM_IN   40
#define ID_VIEW_ZOOM_OUT  41
#define ID_VIEW_ZOOM_1X   42
#define ID_VIEW_ZOOM_2X   43
#define ID_VIEW_ZOOM_4X   44
#define ID_VIEW_ZOOM_6X   45
#define ID_VIEW_ZOOM_8X   46

#define ID_HELP_ABOUT  30

// Tool command IDs – these are the authoritative tool identifiers used everywhere
#define ID_TOOL_PENCIL  20
#define ID_TOOL_BRUSH   21
#define ID_TOOL_ERASER  22
#define ID_TOOL_FILL    23
#define ID_TOOL_SELECT  24

// ============================================================
// Types
// ============================================================

typedef struct { uint8_t r, g, b, a; } rgba_t;

typedef struct canvas_doc_s {
  uint8_t  pixels[CANVAS_H * CANVAS_W * 4];
  GLuint   canvas_tex;
  bool     canvas_dirty;
  bool     drawing;
  point_t  last;
  bool     modified;
  char     filename[512];
  window_t *win;
  window_t *canvas_win;
  struct canvas_doc_s *next;
  // Undo/redo history (heap-allocated pixel snapshots)
  uint8_t *undo_states[UNDO_MAX];
  int      undo_count;
  uint8_t *redo_states[UNDO_MAX];
  int      redo_count;
  bool     sel_active;
  point_t  sel_start;
  point_t  sel_end;
} canvas_doc_t;

typedef struct {
  canvas_doc_t *doc;
  int           scale;
  int           pan_x;   // horizontal pan offset in screen pixels
  int           pan_y;   // vertical pan offset in screen pixels
} canvas_win_state_t;

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
  accel_table_t *accel;
  rgba_t         user_palette[NUM_USER_COLORS];
  int            num_user_colors;
} app_state_t;

// ============================================================
// Global state
// ============================================================

extern app_state_t *g_app;

// Color palette
extern const rgba_t kPalette[NUM_COLORS];

// Tool display names (in ID_TOOL_PENCIL..ID_TOOL_SELECT order; index with tool - ID_TOOL_PENCIL)
extern const char *tool_names[NUM_TOOLS];

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

static inline bool canvas_in_selection(const canvas_doc_t *doc, int x, int y) {
  if (!doc->sel_active) return true;
  int x0 = MIN(doc->sel_start.x, doc->sel_end.x);
  int y0 = MIN(doc->sel_start.y, doc->sel_end.y);
  int x1 = MAX(doc->sel_start.x, doc->sel_end.x);
  int y1 = MAX(doc->sel_start.y, doc->sel_end.y);
  return x >= x0 && x <= x1 && y >= y0 && y <= y1;
}

// Forward declarations for canvas operations
void canvas_set_pixel(canvas_doc_t *doc, int x, int y, rgba_t c);
rgba_t canvas_get_pixel(const canvas_doc_t *doc, int x, int y);
void canvas_clear(canvas_doc_t *doc);
void canvas_draw_circle(canvas_doc_t *doc, int cx, int cy, int r, rgba_t c);
void canvas_draw_line(canvas_doc_t *doc, int x0, int y0, int x1, int y1, int radius, rgba_t c);
void canvas_flood_fill(canvas_doc_t *doc, int sx, int sy, rgba_t fill);

// Undo/redo
void doc_push_undo(canvas_doc_t *doc);
bool doc_undo(canvas_doc_t *doc);
bool doc_redo(canvas_doc_t *doc);
void doc_free_undo(canvas_doc_t *doc);

// Forward declarations for document management
canvas_doc_t *create_document(const char *filename);
void close_document(canvas_doc_t *doc);
void doc_update_title(canvas_doc_t *doc);

// Window procedures
result_t editor_menubar_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_canvas_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_tool_palette_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_color_palette_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

// Zoom support
void canvas_win_set_zoom(window_t *canvas_win, int new_scale);

// Color picker dialog
bool show_color_picker(window_t *parent, rgba_t initial, rgba_t *out);

// About dialog
void show_about_dialog(window_t *parent);

// Menu definitions
extern const menu_def_t kMenus[];

#endif // __IMAGEEDITOR_H__
