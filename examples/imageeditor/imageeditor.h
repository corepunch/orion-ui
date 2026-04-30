#ifndef __IMAGEEDITOR_H__
#define __IMAGEEDITOR_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../ui.h"
#include "../../user/icons.h"

#ifndef IMAGEEDITOR_DEBUG
#define IMAGEEDITOR_DEBUG 1
#endif

#if IMAGEEDITOR_DEBUG
#define IE_DEBUG(...) do { \
  axLog("[imageeditor] " __VA_ARGS__); \
} while (0)
#else
#define IE_DEBUG(...) ((void)0)
#endif

// ============================================================
// Constants
// ============================================================

#define CANVAS_W      320
#define CANVAS_H      200

// Maximum number of layers per document.
#define LAYER_MAX     32

#if UI_WINDOW_SCALE > 1
#define SCREEN_W      512
#define SCREEN_H      342
#else
#define SCREEN_W      1024
#define SCREEN_H      768
#endif

#define PALETTE_WIN_X   4
#define TOOL_PALETTE_BTN_SIZE (TOOLBOX_BTN_SIZE + 4)
// Tool palette window width: always 2 toolbox columns wide.
#define PALETTE_WIN_W   (TOOLBOX_COLS * TOOL_PALETTE_BTN_SIZE)

#define TOOL_ICON_W    16
#define TOOL_ICON_H    16

// Toolbox grid rows and height: ceil(NUM_TOOLS / 2) rows x button size.
#define TOOL_TOOLBAR_ROWS (((NUM_TOOLS) + TOOLBOX_COLS - 1) / TOOLBOX_COLS)
#define TOOL_TOOLBAR_H    ((TOOL_TOOLBAR_ROWS) * TOOL_PALETTE_BTN_SIZE)

// PALETTE_WIN_Y is now the window top (title bar top) of the tool palette.
// frame.y = window top = MENUBAR_HEIGHT + 4 (title bar sits 4px below the menu bar)
#define PALETTE_WIN_Y  (MENUBAR_HEIGHT + 4)

#define TOOL_SWATCH_BOX_H    PALETTE_WIN_W

// Total frame height for the tool palette:
//   title bar + toolbar rows (non-client) + client content (swatches only)
//   The fill/outline toggle has moved to the tool options palette.
#define SWATCH_CLIENT_H TOOL_SWATCH_BOX_H
#define TOOL_WIN_H    (TITLEBAR_HEIGHT + TOOL_TOOLBAR_H + SWATCH_CLIENT_H)

// Tool options palette — sits below the tool palette.
// Content height accommodates both the brush-size panel and the shape-mode panel.
// Brush panel: label(9) + gap(2) + NUM_BRUSH_SIZES rows × OPTS_BRUSH_CELL_H each.
#define OPTS_BRUSH_CELL_H      12
#define TOOL_OPTIONS_PANEL_H   (9 + 2 + NUM_BRUSH_SIZES * OPTS_BRUSH_CELL_H)
#define TOOL_OPTIONS_WIN_W     PALETTE_WIN_W
#define TOOL_OPTIONS_WIN_H     (TITLEBAR_HEIGHT + TOOL_OPTIONS_PANEL_H)
#define TOOL_OPTIONS_WIN_X     PALETTE_WIN_X
#define TOOL_OPTIONS_WIN_Y     (PALETTE_WIN_Y + TOOL_WIN_H + 4)

// Brush size selector: 5 MacPaint-style sizes (radii: 0, 1, 2, 3, 4).
#define NUM_BRUSH_SIZES   5
extern const int kBrushSizes[NUM_BRUSH_SIZES];

#define SWATCH_ROW_H    16
#define RIGHT_PANE_WIN_W 160
#define COLOR_SWATCH_COLS (RIGHT_PANE_WIN_W / SWATCH_ROW_H)
#define COLOR_SWATCH_ROWS ((NUM_COLORS + COLOR_SWATCH_COLS - 1) / COLOR_SWATCH_COLS)
// COLOR_WIN_Y: window top sits 4px below the menu bar (frame.y = window top).
#define COLOR_WIN_H   (TITLEBAR_HEIGHT + COLOR_SWATCH_ROWS * SWATCH_ROW_H)
#define COLOR_WIN_X   (SCREEN_W - COLOR_WIN_W - 4)
#define COLOR_WIN_W   RIGHT_PANE_WIN_W
#define COLOR_WIN_Y   (MENUBAR_HEIGHT + 4)

#define DOC_START_X   76
#define DOC_START_Y   60
#define DOC_CASCADE   20
#define DOC_WORKSPACE_MARGIN 40

#define NUM_COLORS 64
#define NUM_TOOLS  17
#define NUM_USER_COLORS  8

#define UNDO_MAX   20

// Menu item IDs
#define ID_FILE_NEW     1
#define ID_FILE_OPEN    2
#define ID_FILE_SAVE    3
#define ID_FILE_SAVEAS  4
#define ID_FILE_CLOSE   5
#define ID_FILE_QUIT    6

#define ID_EDIT_UNDO        10
#define ID_EDIT_REDO        11
#define ID_EDIT_CUT         12
#define ID_EDIT_COPY        13
#define ID_EDIT_PASTE       14
#define ID_EDIT_CLEAR_SEL   15
#define ID_EDIT_SELECT_ALL  16
#define ID_EDIT_DESELECT    17
#define ID_EDIT_CROP        18

#define ID_VIEW_ZOOM_IN   40
#define ID_VIEW_ZOOM_OUT  41
#define ID_VIEW_ZOOM_1X   42
#define ID_VIEW_ZOOM_2X   43
#define ID_VIEW_ZOOM_4X   44
#define ID_VIEW_ZOOM_6X   45
#define ID_VIEW_ZOOM_8X   46

#define ID_VIEW_ZOOM_FIT  55   // Fit on Screen (Ctrl+0, like Photoshop)

#define ID_VIEW_SHOW_GRID    47
#define ID_VIEW_SNAP_GRID    48
#define ID_VIEW_GRID_OPTIONS 49

// Supported zoom levels and their corresponding View menu IDs.
// These are the single source of truth used by win_canvas.c and win_menubar.c.
#define NUM_ZOOM_LEVELS 5
extern const int kZoomLevels[NUM_ZOOM_LEVELS];
extern const int kZoomMenuIDs[NUM_ZOOM_LEVELS];

#define ID_IMAGE_FLIP_H   50
#define ID_IMAGE_FLIP_V   51
#define ID_IMAGE_INVERT   52
#define ID_IMAGE_RESIZE   53
#define ID_COLOR_SWAP     54

#define ID_WINDOW_TOOLS    200
#define ID_WINDOW_COLORS   201
#define ID_WINDOW_LAYERS   202
#define ID_WINDOW_DOC_BASE 300  // IDs 300..315 reserved for open documents
#define WINDOW_MENU_MAX_DOCS 16

#define ID_HELP_ABOUT  100

// Layer menu command IDs
#define ID_LAYER_NEW          60
#define ID_LAYER_DUPLICATE    61
#define ID_LAYER_DELETE       62
#define ID_LAYER_MOVE_UP      63
#define ID_LAYER_MOVE_DOWN    64
#define ID_LAYER_MERGE_DOWN   65
#define ID_LAYER_FLATTEN      66
#define ID_LAYER_ADD_MASK     67
#define ID_LAYER_APPLY_MASK   68
#define ID_LAYER_REMOVE_MASK  69
#define ID_LAYER_EXTRACT_MASK 70
#define ID_LAYER_EDIT_MASK    71

// Tool command IDs – these are the authoritative tool identifiers used everywhere
#define ID_TOOL_PENCIL        20
#define ID_TOOL_BRUSH         21
#define ID_TOOL_ERASER        22
#define ID_TOOL_FILL          23
#define ID_TOOL_SELECT        24
#define ID_TOOL_HAND          25
#define ID_TOOL_ZOOM          26
#define ID_TOOL_LINE          27
#define ID_TOOL_RECT          28
#define ID_TOOL_ELLIPSE       29
#define ID_TOOL_ROUNDED_RECT  30
#define ID_TOOL_POLYGON       31
#define ID_TOOL_SPRAY         32
#define ID_TOOL_EYEDROPPER    33
#define ID_TOOL_MAGNIFIER     34
#define ID_TOOL_TEXT          35
#define ID_TOOL_CROP          36

// ============================================================
// Color helpers
// ============================================================

// Pack r,g,b,a bytes into a single uint32_t (format: 0xAARRGGBB little-endian: LSB=R, MSB=A).
#define MAKE_COLOR(r,g,b,a) \
  (((uint32_t)(uint8_t)(a)<<24)|((uint32_t)(uint8_t)(b)<<16)|((uint32_t)(uint8_t)(g)<<8)|(uint32_t)(uint8_t)(r))
#define COLOR_R(c) ((uint8_t)((c) & 0xFF))
#define COLOR_G(c) ((uint8_t)(((c) >> 8) & 0xFF))
#define COLOR_B(c) ((uint8_t)(((c) >> 16) & 0xFF))
#define COLOR_A(c) ((uint8_t)(((c) >> 24) & 0xFF))

// ============================================================
// Types
// ============================================================

// A single layer within a canvas document.
typedef struct {
  uint8_t *pixels;      // RGBA pixel buffer (canvas_w * canvas_h * 4 bytes)
  char     name[64];
  bool     visible;
  uint8_t  opacity;     // 0 = transparent, 255 = fully opaque
} layer_t;

typedef struct canvas_doc_s {
  uint8_t *pixels;           // convenience alias → layers[active_layer]->pixels
  int      canvas_w;         // image width in pixels
  int      canvas_h;         // image height in pixels
  GLuint   canvas_tex;
  bool     canvas_dirty;
  bool     drawing;
  bool     close_prompt_open;
  point_t  last;
  bool     modified;
  char     filename[512];
  window_t *win;
  window_t *canvas_win;
  struct canvas_doc_s *next;
  // Layer stack
  layer_t **layers;          // heap array, index 0=bottom … layer_count-1=top
  int       layer_count;
  int       active_layer;    // index of the active layer
  bool      editing_mask;    // true → drawing ops paint the active layer's alpha
  uint8_t  *composite_buf;   // canvas_w * canvas_h * 4 scratch buffer for compositing
  // Undo/redo history (heap-allocated layer-stack snapshots)
  uint8_t *undo_states[UNDO_MAX];
  int      undo_count;
  uint8_t *redo_states[UNDO_MAX];
  int      redo_count;
  bool     sel_active;
  point_t  sel_start;
  point_t  sel_end;
  // Shape tool rubber-band preview state
  uint8_t *shape_snapshot;  // pixel backup taken when shape drag starts
  point_t  shape_start;     // canvas coords where the shape drag began
  // Polygon tool in-progress vertices
  point_t  poly_pts[256];
  int      poly_count;
  bool     poly_active;     // true while accumulating polygon vertices
  // Floating selection state (during move drag)
  bool     sel_moving;
  point_t  move_origin;    // canvas pixel where drag began
  point_t  float_pos;      // current top-left of floating selection
  int      float_w;
  int      float_h;
  uint8_t *float_pixels;   // RGBA data extracted from canvas
  GLuint   float_tex;      // cached GL texture for float_pixels (0 = none)
} canvas_doc_t;

typedef struct {
  canvas_doc_t *doc;
  float         scale;
  int           pan_x;      // horizontal pan offset in screen pixels
  int           pan_y;      // vertical pan offset in screen pixels
  bool          panning;    // true while hand-tool drag is in progress
  point_t       pan_start;  // screen-local coords where hand drag began
  point_t       hover;      // canvas pixel coords under the cursor
  bool          hover_valid; // true when hover is on the canvas (for magnifier overlay)
  GLuint        mag_tex;    // GL texture for magnifier loupe (created once, updated each paint)
  char          last_sb[48]; // last text sent to status bar — avoids redundant updates
} canvas_win_state_t;

typedef struct {
  canvas_doc_t  *active_doc;
  canvas_doc_t  *docs;
  window_t      *menubar_win;
  window_t      *tool_win;
  window_t      *tool_options_win;
  window_t      *color_win;
  window_t      *layers_win;
  hinstance_t    hinstance;  // owning app instance
  int            current_tool;
  uint32_t       palette[NUM_COLORS];
  uint32_t       fg_color;
  uint32_t       bg_color;
  int            next_x;
  int            next_y;
  accel_table_t *accel;
  uint32_t       user_palette[NUM_USER_COLORS];
  int            num_user_colors;
  int            brush_size;    // current brush radius (index into kBrushSizes)
  bool           shape_filled;  // true = shapes draw filled, false = outline only
  // Text tool persistent settings
  int            text_font_size;  // pixel height, default 16
  bool           text_antialias;  // default true
  // Clipboard (shared across documents)
  uint8_t       *clipboard;
  int            clipboard_w;
  int            clipboard_h;
  // Grid
  bool           grid_visible;
  bool           grid_snap;
  int            grid_spacing_x;   // horizontal grid cell size in canvas pixels
  int            grid_spacing_y;   // vertical grid cell size in canvas pixels
} app_state_t;

// ============================================================
// Global state
// ============================================================

extern app_state_t *g_app;

// Tool display names (in ID_TOOL_PENCIL..ID_TOOL_CROP order; index with tool - ID_TOOL_PENCIL)
extern const char *tool_names[NUM_TOOLS];

static inline const char *tool_id_name(int tool_id) {
  switch (tool_id) {
    case ID_TOOL_PENCIL:       return "PENCIL";
    case ID_TOOL_BRUSH:        return "BRUSH";
    case ID_TOOL_ERASER:       return "ERASER";
    case ID_TOOL_FILL:         return "FILL";
    case ID_TOOL_SELECT:       return "SELECT";
    case ID_TOOL_HAND:         return "HAND";
    case ID_TOOL_ZOOM:         return "ZOOM";
    case ID_TOOL_LINE:         return "LINE";
    case ID_TOOL_RECT:         return "RECT";
    case ID_TOOL_ELLIPSE:      return "ELLIPSE";
    case ID_TOOL_ROUNDED_RECT: return "ROUNDED_RECT";
    case ID_TOOL_POLYGON:      return "POLYGON";
    case ID_TOOL_SPRAY:        return "SPRAY";
    case ID_TOOL_EYEDROPPER:   return "EYEDROPPER";
    case ID_TOOL_MAGNIFIER:    return "MAGNIFIER";
    case ID_TOOL_TEXT:         return "TEXT";
    case ID_TOOL_CROP:         return "CROP";
    default:                   return "UNKNOWN";
  }
}

static inline void debug_log_doc_state(const char *tag, const canvas_doc_t *doc) {
  if (!doc) {
    IE_DEBUG("%s doc=NULL", tag);
    return;
  }
  IE_DEBUG("%s doc=%p tool=%s modified=%d drawing=%d sel_active=%d sel_moving=%d poly_active=%d close_prompt_open=%d",
           tag,
           (void *)doc,
           g_app ? tool_id_name(g_app->current_tool) : "<no-app>",
           doc->modified,
           doc->drawing,
           doc->sel_active,
           doc->sel_moving,
           doc->poly_active,
           doc->close_prompt_open);
}

// ============================================================
// Canvas helpers (used across files)
// ============================================================

static inline bool canvas_in_bounds(const canvas_doc_t *doc, int x, int y) {
  return x >= 0 && x < doc->canvas_w && y >= 0 && y < doc->canvas_h;
}

static inline bool canvas_in_selection(const canvas_doc_t *doc, int x, int y) {
  if (!doc->sel_active) return true;
  int x0 = MIN(doc->sel_start.x, doc->sel_end.x);
  int y0 = MIN(doc->sel_start.y, doc->sel_end.y);
  int x1 = MAX(doc->sel_start.x, doc->sel_end.x);
  int y1 = MAX(doc->sel_start.y, doc->sel_end.y);
  return x >= x0 && x <= x1 && y >= y0 && y <= y1;
}

// Canvas pixel operations (new)
void canvas_flip_h(canvas_doc_t *doc);
void canvas_flip_v(canvas_doc_t *doc);
void canvas_invert_colors(canvas_doc_t *doc);
bool canvas_resize(canvas_doc_t *doc, int new_w, int new_h);

// Forward declarations for canvas operations
void canvas_set_pixel(canvas_doc_t *doc, int x, int y, uint32_t c);
uint32_t canvas_get_pixel(const canvas_doc_t *doc, int x, int y);
void canvas_clear(canvas_doc_t *doc);
void canvas_upload(canvas_doc_t *doc);
void canvas_draw_circle(canvas_doc_t *doc, int cx, int cy, int r, uint32_t c);
void canvas_draw_line(canvas_doc_t *doc, int x0, int y0, int x1, int y1, int radius, uint32_t c);
void canvas_flood_fill(canvas_doc_t *doc, int sx, int sy, uint32_t fill);
void canvas_spray(canvas_doc_t *doc, int cx, int cy, int radius, uint32_t c);
void canvas_draw_rect_outline(canvas_doc_t *doc, int x, int y, int w, int h, uint32_t c);
void canvas_draw_rect_filled(canvas_doc_t *doc, int x, int y, int w, int h, uint32_t outline, uint32_t fill);
void canvas_draw_ellipse_outline(canvas_doc_t *doc, int cx, int cy, int rx, int ry, uint32_t c);
void canvas_draw_ellipse_filled(canvas_doc_t *doc, int cx, int cy, int rx, int ry, uint32_t outline, uint32_t fill);
void canvas_draw_rounded_rect_outline(canvas_doc_t *doc, int x, int y, int w, int h, int r, uint32_t c);
void canvas_draw_rounded_rect_filled(canvas_doc_t *doc, int x, int y, int w, int h, int r, uint32_t outline, uint32_t fill);
void canvas_draw_polygon_outline(canvas_doc_t *doc, const point_t *pts, int count, uint32_t c);
void canvas_draw_polygon_filled(canvas_doc_t *doc, const point_t *pts, int count, uint32_t outline, uint32_t fill);
bool canvas_is_shape_tool(int tool_id);
void canvas_shape_begin(canvas_doc_t *doc, int cx, int cy);
void canvas_shape_preview(canvas_doc_t *doc, int x0, int y0, int x1, int y1, int tool, bool filled, uint32_t fg, uint32_t bg, bool shift_held);
void canvas_shape_commit(canvas_doc_t *doc);

// Selection operations
void canvas_copy_selection(canvas_doc_t *doc);
void canvas_cut_selection(canvas_doc_t *doc, uint32_t fill);
void canvas_clear_selection(canvas_doc_t *doc, uint32_t fill);
void canvas_paste_clipboard(canvas_doc_t *doc);
void canvas_select_all(canvas_doc_t *doc);
void canvas_deselect(canvas_doc_t *doc);
void canvas_crop_to_selection(canvas_doc_t *doc);
// Crop or expand the canvas to the active selection.
// If the selection extends outside the canvas the canvas grows (new areas filled
// with opaque white); if it is smaller the canvas shrinks.
// Returns true on success, false if the operation could not be performed
// (invalid state, oversized selection, or allocation failure — canvas unchanged).
bool canvas_crop_or_expand_to_selection(canvas_doc_t *doc);

// Move-selection helpers (called from win_canvas.c)
void canvas_begin_move(canvas_doc_t *doc, uint32_t bg);
void canvas_commit_move(canvas_doc_t *doc);

// Undo/redo
void doc_push_undo(canvas_doc_t *doc);
bool doc_undo(canvas_doc_t *doc);
bool doc_redo(canvas_doc_t *doc);
void doc_free_undo(canvas_doc_t *doc);
void doc_discard_undo(canvas_doc_t *doc);

// PNG I/O
bool png_save(const char *path, const canvas_doc_t *doc);

// Native file picker wrapper used by the menu commands.
bool show_file_picker(window_t *parent, bool save_mode, char *out_path, size_t out_sz);

// Forward declarations for document management
canvas_doc_t *create_document(const char *filename, int w, int h);
void close_document(canvas_doc_t *doc);
void doc_update_title(canvas_doc_t *doc);
// Show a "Unsaved Changes" dialog when doc->modified is set.
// If the user confirms, calls close_document().
// Returns true if the document was closed (or had no unsaved changes),
// false if the user pressed Cancel.
bool doc_confirm_close(canvas_doc_t *doc, window_t *parent_win);

// Window procedures
result_t editor_menubar_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_canvas_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_tool_palette_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_tool_options_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_color_palette_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

// Zoom support
void canvas_win_set_zoom(window_t *canvas_win, int new_scale);
void canvas_win_set_scale(window_t *canvas_win, float new_scale);

// Fit the canvas to the viewport at the largest integer zoom that shows the
// whole image — equivalent to Photoshop's "Fit on Screen" (Ctrl+0).
void canvas_win_fit_zoom(window_t *canvas_win);
float imageeditor_fit_scale_for_viewport(int content_w, int content_h,
                                         int viewport_w, int viewport_h,
                                         bool allow_zoom_in);
void imageeditor_format_zoom(char *buf, size_t buf_sz, float scale);
rect_t imageeditor_document_workspace_rect(void);
void imageeditor_max_document_frame_size(int *out_w, int *out_h);
void imageeditor_max_canvas_viewport_size(int *out_w, int *out_h);
void imageeditor_document_frame_for_viewport(int viewport_w, int viewport_h,
                                             int *out_w, int *out_h);

// Sync canvas scrollbars after content size changes (e.g. after canvas_resize)
void canvas_win_sync_scrollbars(window_t *canvas_win);

// Color picker dialog
bool show_color_picker(window_t *parent, uint32_t initial, uint32_t *out);

// Text dialog – show text insertion options; returns true if accepted.
typedef struct {
  char     text[256];    // text to render (NUL-terminated)
  int      font_size;    // pixel height
  uint32_t color;        // text color
  bool     antialias;    // true = antialiased rendering
} text_options_t;
bool show_text_dialog(window_t *parent, text_options_t *opts);

// Render a UTF-8 string into the canvas at (x, y) using stb_truetype.
// Returns true if at least one pixel was written.
bool canvas_draw_text_stb(canvas_doc_t *doc, int x, int y,
                           const text_options_t *opts);

// About dialog
void show_about_dialog(window_t *parent);

// Menu definitions
extern menu_def_t kMenus[];  // non-const: Window menu is rebuilt dynamically
extern const int  kNumMenus;

// Rebuild the Window menu (call after create/close document).
void window_menu_rebuild(void);

// Dispatch a menu command.  Called by editor_menubar_proc (standalone) or
// by the shell (gem mode) when the user selects a menu item.
void handle_menu_command(uint16_t id);

// Open an image file path and create a new document from it.
// Returns true on success, false on load/create failure.
bool imageeditor_open_file_path(const char *path);

// Palette window factory helpers — create, show, register and return the window.
// Also used by handle_menu_command to recreate closed palette windows.
window_t *create_tool_palette_window(void);
window_t *create_tool_options_window(void);
window_t *create_color_palette_window(void);

// New Layer dialog – lets the user pick a fill type (transparent/white/fg/bg).
// Returns true and writes the chosen fill color into *out_color if accepted.
bool show_new_layer_dialog(window_t *parent, uint32_t *out_color);

// Add Mask dialog – lets the user choose how the layer alpha should be filled.
// Returns true and writes the chosen fill mode into *out_fill_mode if accepted.
bool show_add_mask_dialog(window_t *parent, int *out_fill_mode);

// New Image / Canvas Size dialog
bool show_size_dialog(window_t *parent, const char *title, int *out_w, int *out_h);

// Grid Options dialog – returns true if accepted.
bool show_grid_options_dialog(window_t *parent, int *out_x, int *out_y);

// Layers palette window geometry.
// Positioned on the right side of the screen, below the color palette.
#define LAYERS_WIN_W          RIGHT_PANE_WIN_W
#define LAYERS_ROW_H           18   // height of each layer row in the panel
#define LAYERS_MAX_VIS_ROWS     5   // visible rows before scrolling
#define LAYERS_LIST_H         (LAYERS_ROW_H * LAYERS_MAX_VIS_ROWS)
// Toolbar band height: button size + top/bottom bevel + top/bottom padding.
#define LAYERS_TOOLBAR_H      (TB_SPACING + 2*(TOOLBAR_PADDING + TOOLBAR_BEVEL_WIDTH))
#define LAYERS_WIN_H          (TITLEBAR_HEIGHT + LAYERS_TOOLBAR_H + LAYERS_LIST_H)
#define LAYERS_WIN_X          (SCREEN_W - LAYERS_WIN_W - 4)
#define LAYERS_WIN_Y          (COLOR_WIN_Y + COLOR_WIN_H + 4)

// Hit-test zones within a layer row (x offsets)
#define LAYERS_EYE_W           14   // eye-icon click area width
#define LAYERS_CHIP_W          14   // alpha-edit icon click area width
#define LAYERS_NAME_X          (1 + LAYERS_EYE_W + 2 + LAYERS_CHIP_W + 3)  // start of name text

// ============================================================
// Layer management (canvas.c)
// ============================================================

// Add a new empty layer above the current active layer (filled with opaque white).
bool doc_add_layer(canvas_doc_t *doc);

// Add a new layer filled with the given RGBA color (0x00000000 = transparent,
// 0xFFFFFFFF = opaque white, etc.).  Convenience variant used by the New Layer
// dialog so the caller picks the fill without a second memset pass.
bool doc_add_layer_filled(canvas_doc_t *doc, uint32_t fill_color);

// Delete the active layer (minimum 1 layer always kept).
bool doc_delete_layer(canvas_doc_t *doc);

// Duplicate the active layer and insert the copy above it.
bool doc_duplicate_layer(canvas_doc_t *doc);

// Change the active layer, updating doc->pixels and resetting editing_mask.
void doc_set_active_layer(canvas_doc_t *doc, int idx);

// Move the active layer up (towards the top of the stack).
void doc_move_layer_up(canvas_doc_t *doc);

// Move the active layer down (towards the bottom of the stack).
void doc_move_layer_down(canvas_doc_t *doc);

// Flatten the active layer onto the one below it.
void doc_merge_down(canvas_doc_t *doc);

// Flatten all layers into a single background layer.
void doc_flatten(canvas_doc_t *doc);

// Free all layers (called by close_document).
void doc_free_layers(canvas_doc_t *doc);

// ============================================================
// Alpha editing / mask operations (canvas.c)
// ============================================================

typedef enum {
  MASK_EXTRACT_GRAYSCALE = 0,
  MASK_EXTRACT_WHITE     = 1,
  MASK_EXTRACT_BACKGROUND = 2,
  MASK_EXTRACT_FOREGROUND = 3,
} mask_extract_fill_t;

// Initialize the layer alpha at index idx. The default wrapper fills it white.
bool layer_add_mask_ex(canvas_doc_t *doc, int idx, int fill_mode);
bool layer_add_mask(canvas_doc_t *doc, int idx);

// Commit the currently edited alpha channel and exit mask-edit mode.
void layer_apply_mask(canvas_doc_t *doc, int idx);

// Discard the alpha edits by restoring the layer to opaque.
void layer_remove_mask(canvas_doc_t *doc, int idx);

// Open the active layer's alpha channel as a new document.
canvas_doc_t *canvas_extract_mask(canvas_doc_t *doc);

// ============================================================
// Layers palette (win_layers.c)
// ============================================================

result_t win_layers_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
window_t *create_layers_window(void);

// Refresh the Layers palette after layer changes.
void layers_win_refresh(void);

// Swap the active foreground/background colors.
void swap_foreground_background_colors(void);

#endif // __IMAGEEDITOR_H__
