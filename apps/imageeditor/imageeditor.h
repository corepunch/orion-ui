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

#include <orion/ui.h>
#include <orion/user/icons.h>
#include "image-editor.h"

#ifndef IMAGEEDITOR_DEBUG
#define IMAGEEDITOR_DEBUG 1
#endif

#ifndef IMAGEEDITOR_INDEXED
#define IMAGEEDITOR_INDEXED 0
#endif

#ifndef IMAGEEDITOR_BW
#define IMAGEEDITOR_BW 0
#endif

// BW mode is just indexed mode with a 2-color palette.
#if IMAGEEDITOR_BW
#undef IMAGEEDITOR_INDEXED
#define IMAGEEDITOR_INDEXED 1
#endif

#if IMAGEEDITOR_INDEXED
// Which palette index is treated as transparent (the "eraser" target).
#define IMAGEEDITOR_TRANSPARENT_INDEX 0
#endif

// Retina pixel buffer scale for BW mode (pencil test).  Set from axGetScaling()
// at startup when IMAGEEDITOR_BW_RETINA is defined; otherwise compile-time 1.
#ifdef IMAGEEDITOR_BW_RETINA
extern int g_bw_retina_scale;
#else
#define g_bw_retina_scale 1
#endif

// Bytes per pixel in the layer pixel buffer.
// 32-bit RGBA mode = 4; indexed mode = 1 (palette index); BW mode = 1 (0 or 1).
#if IMAGEEDITOR_INDEXED || IMAGEEDITOR_BW
#define DOC_BPP 1
#else
#define DOC_BPP 4
#endif

// Frame format used for animation timeline compress/expand.
#if IMAGEEDITOR_INDEXED
#define IE_FRAME_FORMAT FRAME_FORMAT_INDEXED
#else
#define IE_FRAME_FORMAT FRAME_FORMAT_RGBA
#endif

#ifndef IMAGEEDITOR_SHOW_SELECTION_BOUNDS
#define IMAGEEDITOR_SHOW_SELECTION_BOUNDS 0
#endif

#define IE_TRACE(...) do { fprintf(stderr, "[imageeditor] " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); } while (0)

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

#define CANVAS_W      640
#define CANVAS_H      480

// Maximum number of layers per document.
#define LAYER_MAX     32

#if IMAGEEDITOR_BW
#define SCREEN_W      1080
#define SCREEN_H      810
#elif UI_WINDOW_SCALE > 1
#define SCREEN_W      512
#define SCREEN_H      342
#else
#define SCREEN_W      1024
#define SCREEN_H      768
#endif

#define APP_TOOLBAR_Y   (MENUBAR_HEIGHT + 4)
// App toolbar band height: button size + top/bottom bevel + top/bottom padding.
#define APP_TOOLBAR_H   TOOLBAR_BAND_HEIGHT

#define PALETTE_WIN_X   4
#define IMAGEEDITOR_TOOL_ICON_PX 24
#define TOOL_PALETTE_BTN_SIZE    TB_SPACING
#define PALETTE_WIN_W   (TOOL_PALETTE_BTN_SIZE + 2 * (TOOLBAR_PADDING + TOOLBAR_BEVEL_WIDTH))
#define TOOL_ICON_W     IMAGEEDITOR_TOOL_ICON_PX
#define TOOL_ICON_H     IMAGEEDITOR_TOOL_ICON_PX
#define TOOL_TOOLBAR_H  ((NUM_TOOLS + 1) * TOOL_PALETTE_BTN_SIZE + NUM_TOOLS * TOOLBAR_SPACING + 2 * (TOOLBAR_PADDING + TOOLBAR_BEVEL_WIDTH))

// PALETTE_WIN_Y is the window top (title bar top) of the tool palette.
// It sits below the app toolbar, with a 4px gap.
#define PALETTE_WIN_Y  (APP_TOOLBAR_Y + APP_TOOLBAR_H + 4)

#define TOOL_WIN_H    (TITLEBAR_HEIGHT + TOOL_TOOLBAR_H)

// Floating options toolbar, initially beside the left tool strip.
// Content height accommodates brush, shape, and magic-wand option panels.
#define TOOL_OPTIONS_PANEL_H   TOOLBAR_BAND_HEIGHT
#define TOOL_OPTIONS_WIN_W     PALETTE_WIN_W
#define TOOL_OPTIONS_WIN_H     TOOL_OPTIONS_PANEL_H
#define TOOL_OPTIONS_WIN_X     (PALETTE_WIN_X + PALETTE_WIN_W + 8)
#define TOOL_OPTIONS_WIN_Y     (PALETTE_WIN_Y + 4 * TOOL_PALETTE_BTN_SIZE)

// Brush slider steps (radii: 0, 1, 2, 3, 4).
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
#define COLOR_WIN_Y   (APP_TOOLBAR_Y + APP_TOOLBAR_H + 4)

// Canvas transparency checkerboard. 16 px squares are a good middle ground
// between readable and subtle in the image editor.
#define CANVAS_CHECKER_SQUARE_PX 16

#define DOC_PALETTE_GAP 8
#define APP_TOOLS_W   (g_app && g_app->tool_win ? g_app->tool_win->frame.w : PALETTE_WIN_W)
#define DOC_START_X   (APP_TOOLS_W + DOC_PALETTE_GAP)
#define DOC_START_Y   (APP_TOOLBAR_Y + APP_TOOLBAR_H + DOC_PALETTE_GAP)
#define DOC_WORKSPACE_MARGIN 16

#define NUM_COLORS 64
#define NUM_TOOLS  19
#define NUM_USER_COLORS  8

#define UNDO_MAX   20
#define MAX_POLY_POINTS 256
#define MAX_FILENAME 512

// Menu command IDs and static menu resources are generated from imageeditor.orion.

// Supported zoom levels and their corresponding View menu IDs.
// These are the single source of truth used by win_canvas.c and win_menubar.c.
#define NUM_ZOOM_LEVELS 5
extern const int kZoomLevels[NUM_ZOOM_LEVELS];
extern const int kZoomMenuIDs[NUM_ZOOM_LEVELS];

#define ID_FILTER_BASE   1000
#define IMAGEEDITOR_MAX_FILTERS 64


#define ID_WINDOW_DOC_BASE 300  // IDs 300..315 reserved for open documents
#define WINDOW_MENU_MAX_DOCS 16

#include "lv_cmpn.h"
#include "fg_preview.h"
#include "render_effects.h"
#include "build/generated/apps/imageeditor/imageeditor.h"

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
typedef enum {
  LAYER_BLEND_NORMAL = 0,
  LAYER_BLEND_MULTIPLY,
  LAYER_BLEND_SCREEN,
  LAYER_BLEND_ADD,
  LAYER_BLEND_COUNT
} layer_blend_mode_t;

typedef enum {
  IMAGE_RESIZE_NEAREST = 0,
  IMAGE_RESIZE_BILINEAR,
  IMAGE_RESIZE_FILTER_COUNT
} image_resize_filter_t;

typedef struct {
  uint8_t *pixels;      // pixel buffer: RGBA (canvas_w * canvas_h * 4 bytes)
                        // or indexed (canvas_w * canvas_h * 1 byte) depending on DOC_BPP
  GLuint   tex;         // GPU texture for realtime compositing
  irect16_t dirty_rect; // pixel damage since the last successful upload
  char     name[64];
  bool     visible;
  uint8_t  opacity;     // 0 = transparent, 255 = fully opaque
  uint8_t  blend_mode;  // layer_blend_mode_t
  // Live-preview effect applied while an adjustment dialog is open.
  struct {
    bool                      active;
    imageeditor_render_effect_t effect;
    ui_render_effect_params_t params;
  } preview;
} layer_t;

// Forward-declare anim_timeline_t so canvas_doc_t can hold a pointer.
typedef struct anim_timeline_s anim_timeline_t;

// Undo/redo stack (heap-allocated layer-stack snapshots)
typedef struct {
  uint8_t *states[UNDO_MAX];
  int      count;
} undo_t;

typedef struct canvas_doc_s {
  uint8_t *pixels;           // convenience alias → layer.stack[layer.active]->pixels
  int      canvas_w;         // image width in pixels
  int      canvas_h;         // image height in pixels
  struct {
    uint32_t color;          // document background color used by preview/display paths
    bool     show;           // true = paint the document background behind pixels
  } background;
  bool     canvas_dirty; // full upload required; overrides layer damage
  bool     drawing;
  bool     close_prompt_open;
  ipoint16_t  last;
  bool     modified;
  char     filename[MAX_FILENAME];
  window_t *win;
  window_t *canvas_win;
  struct canvas_doc_s *next;
  // Layer stack
  struct {
    layer_t **stack;           // heap array, index 0=bottom … count-1=top
    int       count;
    int       active;          // index of the active layer
    bool      editing_mask;    // true → drawing ops paint the active layer's alpha
    bool      mask_only_view;  // true → canvas shows the active layer alpha only
    uint8_t  *composite_buf;   // canvas_w * canvas_h * 4 scratch buffer for compositing
  } layer;
  // Undo/redo history (heap-allocated layer-stack snapshots)
  undo_t undo;
  undo_t redo;
  // Selection state
  struct {
    bool        active;
    ipoint16_t  start;
    ipoint16_t  end;
    bool        add_mode;    // true while Shift-dragging to add to selection
    // Selection mask (canvas_w x canvas_h: 0=selected/editable, 255=protected/unselected)
    struct {
      uint8_t    *data;      // NULL means the whole canvas is editable
      GLuint      tex;       // GL_RED texture cache for protected-area overlay
      bool        dirty;
      ipoint16_t  offset;    // transient drag offset, committed on mouse-up
    } mask;
    // Move/drag state
    struct {
      bool        active;       // selection is being moved
      bool        mask_moving;  // mask offset is being adjusted
      ipoint16_t  origin;       // canvas pixel where drag began
    } move;
    struct {
      irect16_t   rect;          // position and size of floating selection
      uint8_t    *pixels;        // RGBA data extracted from canvas
      uint8_t    *mask;          // rect.w x rect.h edit mask, same semantics as sel.mask.data
      GLuint      tex;           // cached GL texture for pixels (0 = none)
    } floating;
  } sel;
  // Shape tool rubber-band preview state
  struct {
    uint8_t    *snapshot;  // pixel backup taken when shape drag starts
    ipoint16_t  start;     // canvas coords where the shape drag began
  } shape;
  struct {
    bool active, soft;
    int radius;
    uint32_t color;
    ipoint16_t sample, stamp;
    float x, y;  // Last curve endpoint, retaining half-pixel midpoints.
  } stroke;
  // Polygon tool in-progress vertices
  struct {
    ipoint16_t  pts[MAX_POLY_POINTS];
    int         count;
    bool        active;     // true while accumulating polygon vertices
  } poly;
  anim_timeline_t *anim;   // animation timeline; non-NULL after successful create_document(),
                           // but may be NULL on allocation failure (OOM guard)
#if IMAGEEDITOR_INDEXED
  // Indexed-color (256-color palette) mode.
  // doc->pixels stores 1-byte palette indices rather than 4-byte RGBA values.
  struct {
    uint32_t entries[256]; // RGBA value for each palette entry (index → color)
    int      count;        // number of used entries (up to 256)
    int      transparent;  // palette index that means "transparent" (default: IMAGEEDITOR_TRANSPARENT_INDEX)
  } ipal;
#endif
} canvas_doc_t;

typedef struct {
  canvas_doc_t *doc;
  bool          gesture_active;
  bool          stroke_modified;
  bool          stroke_undo;
  struct {
    bool active;     // true while hand-tool drag is in progress
  } pan;
  ipoint16_t    hover;       // canvas pixel coords under the cursor
  bool          hover_valid; // true when hover is on the canvas (for magnifier overlay)
  GLuint        mag_tex;     // GL texture for magnifier loupe (created once, updated each paint)
  GLuint        onion_tex;    // GL texture for animation trace overlay
  int           onion_tex_w;  // cached canvas width for onion_tex
  int           onion_tex_h;  // cached canvas height for onion_tex
  char          last_sb[64]; // last text sent to status bar — avoids redundant updates
} canvas_win_state_t;

typedef struct {
  char     name[64];
  uint32_t program;
} image_filter_t;

#define ONION_SKIN_MAX_STEPS 4

typedef struct {
  canvas_doc_t  *active_doc;
  canvas_doc_t  *docs;
  window_t      *chrome_win;
  window_t      *menubar_win;
  window_t      *main_toolbar_win;
  window_t      *tool_win;
  window_t      *tool_options_win;
  window_t      *color_win;
  window_t      *layers_win;
  window_t      *timeline_win;
  uint32_t       anim_timer_id; // axSetTimer handle for playback; 0 = stopped
  hinstance_t    hinstance;  // owning app instance
  int            current_tool;
  bool           anim_trace_enabled; // onion-skin overlay toggle for animation frames
  int            anim_trace_frames;  // active onion-skin step span (max non-zero prev/next opacity step)
  float          anim_trace_prev_opacity[ONION_SKIN_MAX_STEPS];
  float          anim_trace_next_opacity[ONION_SKIN_MAX_STEPS];
  uint32_t       palette[NUM_COLORS];
  uint32_t       fg_color;
  uint32_t       bg_color;
#if IMAGEEDITOR_INDEXED
  int            fg_palette_idx; // active palette index (indexed mode only)
#endif
  accel_table_t *accel;
  uint32_t       user_palette[NUM_USER_COLORS];
  int            num_user_colors;
  int            brush_tool;
  int            shape_tool;
  int            brush_size;    // current brush radius (index into kBrushSizes)
  bool           shape_filled;  // true = shapes draw filled, false = outline only
  // Text tool persistent settings (font size, antialias flag).
  struct {
    int  font_size;  // pixel height, default 16
    bool antialias;  // default true
  } text_tool;
  // Magic-wand tool settings.
  struct {
    bool     antialias;
    int      spread;         // RGB tolerance, 0..255
    uint32_t overlay_color;
  } wand;
  // Fill tool gap detection (Gangnet/Van Thong-style invisible stitching).
  // Maximum gap to stitch closed before the fill runs, in canvas pixels.
  // 0 = off; otherwise one of IE_FILL_GAP_SMALL/MEDIUM/LARGE.
  struct {
    int gap;
  } fill;
  // Instagram-style filter presets loaded from share/filters.
  image_filter_t filters[IMAGEEDITOR_MAX_FILTERS];
  int            filter_count;
  // Clipboard (shared across documents)
  uint8_t       *clipboard;
  isize16_t      clipboard_size;
  // Grid overlay settings.
  struct {
    bool       visible;
    bool       snap;
    ipoint16_t spacing;  // grid cell size in canvas pixels
  } grid;
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
    case ID_TOOL_MAGIC_WAND:   return "MAGIC_WAND";
    case ID_TOOL_MOVE:         return "MOVE";
    default:                   return "UNKNOWN";
  }
}

static inline void debug_log_doc_state(const char *tag, const canvas_doc_t *doc) {
  if (!doc) {
    IE_DEBUG("%s doc=NULL", tag);
    return;
  }
  IE_DEBUG("%s doc=%p tool=%s modified=%d drawing=%d sel.active=%d sel.move.active=%d poly.active=%d close_prompt_open=%d",
           tag,
           (void *)doc,
           g_app ? tool_id_name(g_app->current_tool) : "<no-app>",
           doc->modified,
           doc->drawing,
           doc->sel.active,
           doc->sel.move.active,
           doc->poly.active,
           doc->close_prompt_open);
}

// ============================================================
// Canvas helpers (used across files)
// ============================================================

static inline bool canvas_in_bounds(const canvas_doc_t *doc, int x, int y) {
  return x >= 0 && x < doc->canvas_w && y >= 0 && y < doc->canvas_h;
}

static inline bool canvas_in_selection(const canvas_doc_t *doc, int x, int y) {
  if (!doc->sel.active) return true;
  if (doc->sel.mask.data) {
    int sx = x - doc->sel.mask.offset.x;
    int sy = y - doc->sel.mask.offset.y;
    if (!canvas_in_bounds(doc, sx, sy)) return false;
    return doc->sel.mask.data[(size_t)sy * doc->canvas_w + sx] == 0;
  }
  int x0 = MIN(doc->sel.start.x, doc->sel.end.x);
  int y0 = MIN(doc->sel.start.y, doc->sel.end.y);
  int x1 = MAX(doc->sel.start.x, doc->sel.end.x);
  int y1 = MAX(doc->sel.start.y, doc->sel.end.y);
  return x >= x0 && x <= x1 && y >= y0 && y <= y1;
}

// Canvas pixel operations (new)
void canvas_flip_h(canvas_doc_t *doc);
void canvas_flip_v(canvas_doc_t *doc);
void canvas_invert_colors(canvas_doc_t *doc);
bool canvas_resize(canvas_doc_t *doc, int new_w, int new_h);
bool canvas_resize_image(canvas_doc_t *doc, int new_w, int new_h,
                         image_resize_filter_t filter);

// Composite all visible layers into dst (4-byte RGBA per pixel).
// dst must point to canvas_w * canvas_h * 4 bytes.
void canvas_composite(const canvas_doc_t *doc, uint8_t *dst);
// Apply a checkerboard background to the composited RGBA buffer in-place.
void canvas_composite_over_bg(const canvas_doc_t *doc, uint8_t *rgba);

// Forward declarations for canvas operations
void canvas_set_pixel(canvas_doc_t *doc, int x, int y, uint32_t c);
void canvas_draw_pen(canvas_doc_t *doc, int x, int y, uint32_t c);
void canvas_draw_pen_line(canvas_doc_t *doc, int x0, int y0, int x1, int y1, uint32_t c);
uint32_t canvas_get_pixel(const canvas_doc_t *doc, int x, int y);
void canvas_clear(canvas_doc_t *doc);
void canvas_upload(canvas_doc_t *doc);
void canvas_mark_dirty_pixel(canvas_doc_t *doc, int x, int y);
#if IMAGEEDITOR_INDEXED
// Find the palette entry nearest to the given RGBA color.
// Returns the palette index (0–255), or ipal.transparent if the palette is empty.
int canvas_nearest_palette_index(const canvas_doc_t *doc, uint32_t color);
// Resolve the RGBA color for a pixel at (x, y) by mapping index → ipal.entries.
uint32_t canvas_get_pixel_rgba(const canvas_doc_t *doc, int x, int y);
// The eraser color in indexed mode: transparent palette index expanded to RGBA.
static inline uint32_t canvas_eraser_color(const canvas_doc_t *doc) {
  (void)doc;
  return MAKE_COLOR(0x00, 0x00, 0x00, 0x00);  // sentinel; translated to transparent index in set_pixel
}
#endif
void canvas_draw_circle(canvas_doc_t *doc, int cx, int cy, int r, uint32_t c);
void canvas_draw_line(canvas_doc_t *doc, int x0, int y0, int x1, int y1, int radius, uint32_t c);
void canvas_draw_soft_circle(canvas_doc_t *doc, int cx, int cy, int r, uint32_t c);
void canvas_draw_soft_line(canvas_doc_t *doc, int x0, int y0, int x1, int y1, int radius, uint32_t c);
void canvas_stroke_begin(canvas_doc_t *doc, ipoint16_t point, int radius, uint32_t color, bool soft);
void canvas_stroke_drag(canvas_doc_t *doc, ipoint16_t point);
void canvas_stroke_end(canvas_doc_t *doc, ipoint16_t point);
void canvas_stroke_cancel(canvas_doc_t *doc);
void canvas_draw_scaled_circle(canvas_doc_t *doc, int cx, int cy, int logical_radius, uint32_t c);
void canvas_draw_scaled_line(canvas_doc_t *doc, int x0, int y0, int x1, int y1, int logical_radius, uint32_t c);
void canvas_draw_scaled_soft_circle(canvas_doc_t *doc, int cx, int cy, int logical_radius, uint32_t c);
void canvas_draw_scaled_soft_line(canvas_doc_t *doc, int x0, int y0, int x1, int y1, int logical_radius, uint32_t c);
void canvas_flood_fill(canvas_doc_t *doc, int sx, int sy, uint32_t fill);
// Gap-closing flood fill: stitches gaps up to gap_px wide with invisible
// barrier segments before filling, then merges stitch-adjacent slivers.
// Returns the number of stitch segments drawn (0 when gap_px <= 0).
// See canvas/canvas_fill_gap.c for the Gangnet/Van Thong pipeline.
// Settings and the limit are logical pixels; gap_px is in backing pixels.
#define IE_FILL_GAP_OFF    0
#define IE_FILL_GAP_SMALL  2
#define IE_FILL_GAP_MEDIUM 5
#define IE_FILL_GAP_LARGE  10
#define IE_FILL_GAP_MAX    32
int canvas_flood_fill_with_gap(canvas_doc_t *doc, int sx, int sy,
                               uint32_t fill, int gap_px);
// Gap candidate detectors. Ink = in-selection pixels != target.
// Returns the total number found (only the first max_out are stored).
int canvas_gap_detect_endpoints(const canvas_doc_t *doc, uint32_t target,
                                ipoint16_t *out, int max_out);
int canvas_gap_detect_corners(const canvas_doc_t *doc, uint32_t target,
                              ipoint16_t *out, int max_out);
bool canvas_magic_wand_select(canvas_doc_t *doc, int sx, int sy,
                              int spread, bool antialias);
bool canvas_magic_wand_select_add(canvas_doc_t *doc, int sx, int sy,
                                  int spread, bool antialias);
void canvas_spray(canvas_doc_t *doc, int cx, int cy, int radius, uint32_t c);
void canvas_draw_rect_outline(canvas_doc_t *doc, int x, int y, int w, int h, uint32_t c);
void canvas_draw_rect_filled(canvas_doc_t *doc, int x, int y, int w, int h, uint32_t outline, uint32_t fill);
void canvas_draw_ellipse_outline(canvas_doc_t *doc, int cx, int cy, int rx, int ry, uint32_t c);
void canvas_draw_ellipse_filled(canvas_doc_t *doc, int cx, int cy, int rx, int ry, uint32_t outline, uint32_t fill);
void canvas_draw_rounded_rect_outline(canvas_doc_t *doc, int x, int y, int w, int h, int r, uint32_t c);
void canvas_draw_rounded_rect_filled(canvas_doc_t *doc, int x, int y, int w, int h, int r, uint32_t outline, uint32_t fill);
void canvas_draw_polygon_outline(canvas_doc_t *doc, const ipoint16_t *pts, int count, uint32_t c);
void canvas_draw_polygon_filled(canvas_doc_t *doc, const ipoint16_t *pts, int count, uint32_t outline, uint32_t fill);
void canvas_draw_rect_scaled(canvas_doc_t *doc, int x, int y, int w, int h, bool filled, uint32_t outline, uint32_t fill);
void canvas_draw_ellipse_scaled(canvas_doc_t *doc, int cx, int cy, int rx, int ry, bool filled, uint32_t outline, uint32_t fill);
void canvas_draw_rounded_rect_scaled(canvas_doc_t *doc, int x, int y, int w, int h, int r, bool filled, uint32_t outline, uint32_t fill);
void canvas_draw_polygon_scaled(canvas_doc_t *doc, const ipoint16_t *pts, int count, bool filled, uint32_t outline, uint32_t fill);
bool canvas_is_shape_tool(int tool_id);
void canvas_constrain_tool_drag(int tool_id, uint32_t mods,
                                int x0, int y0, int *x1, int *y1);
void canvas_shape_begin(canvas_doc_t *doc, int cx, int cy);
void canvas_shape_preview(canvas_doc_t *doc, int x0, int y0, int x1, int y1, int tool, bool filled, uint32_t fg, uint32_t bg, bool shift_held);
void canvas_shape_commit(canvas_doc_t *doc);

// Selection operations
void canvas_copy_selection(canvas_doc_t *doc);
void canvas_cut_selection(canvas_doc_t *doc, uint32_t fill);
void canvas_clear_selection(canvas_doc_t *doc, uint32_t fill);
void canvas_paste_clipboard(canvas_doc_t *doc);
bool canvas_select_rect(canvas_doc_t *doc, int x0, int y0, int x1, int y1);
bool canvas_select_rect_add(canvas_doc_t *doc, int x0, int y0, int x1, int y1);
void canvas_select_all(canvas_doc_t *doc);
void canvas_deselect(canvas_doc_t *doc);
void canvas_clear_selection_mask(canvas_doc_t *doc);
bool canvas_expand_selection(canvas_doc_t *doc, int amount);
bool canvas_contract_selection(canvas_doc_t *doc, int amount);
void canvas_crop_to_selection(canvas_doc_t *doc);
// Crop or expand the canvas to the active selection.
// If the selection extends outside the canvas the canvas grows (new areas filled
// with transparent pixels); if it is smaller the canvas shrinks.
// Returns true on success, false if the operation could not be performed
// (invalid state, oversized selection, or allocation failure — canvas unchanged).
bool canvas_crop_or_expand_to_selection(canvas_doc_t *doc);
void canvas_set_selection_mask_offset(canvas_doc_t *doc, int dx, int dy);
bool canvas_commit_selection_mask_offset(canvas_doc_t *doc);

// Move-selection helpers (called from win_canvas.c)
void canvas_begin_move(canvas_doc_t *doc, uint32_t bg);
void canvas_commit_move(canvas_doc_t *doc);
void canvas_discard_move(canvas_doc_t *doc);
bool canvas_translate_selection_mask(canvas_doc_t *doc, int dx, int dy);

// Undo/redo
void doc_push_undo(canvas_doc_t *doc);
bool doc_undo(canvas_doc_t *doc);
bool doc_cancel_undo(canvas_doc_t *doc);
bool doc_redo(canvas_doc_t *doc);
void doc_free_undo(canvas_doc_t *doc);
void doc_discard_undo(canvas_doc_t *doc);

// Unified image I/O (image_io.c).
// Indexed mode: PCX and BMP 8-bit indexed.
// 32-bit mode:  PNG save; any format load via user/image.h.
//
// image_io_load: returns a malloc'd pixel buffer (1 bpp in indexed mode,
//   4 bpp RGBA in 32-bit mode).  Caller must free() the result.
//   In 32-bit mode the underlying stb buffer is copied internally, so
//   free() is always safe regardless of stb's allocator hooks.
//   out_pal[256] and *out_pal_count are only set in indexed mode.
uint8_t *image_io_load(const char *path, int *out_w, int *out_h,
                       uint32_t out_pal[256], int *out_pal_count);
bool     image_io_save(const char *path, const canvas_doc_t *doc);

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
enum { IE_OPT_AA = 9001, IE_OPT_SPREAD, IE_OPT_COLOR, IE_OPT_SIZE, IE_OPT_FILLED, IE_OPT_GAP };
void imageeditor_sync_tool_options(void);
int imageeditor_tool_group(int tool);
result_t win_tool_options_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_color_palette_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

// Zoom support
void canvas_win_set_zoom(window_t *canvas_win, int new_scale);
void canvas_win_set_scale(window_t *canvas_win, float new_scale);

// Fit the canvas to the viewport at the largest integer zoom that shows the
// whole image — equivalent to Photoshop's "Fit on Screen" (Ctrl+0).
void canvas_win_fit_zoom(window_t *canvas_win);
void imageeditor_format_zoom(char *buf, size_t buf_sz, float scale);
float imageeditor_fit_scale_for_viewport(int content_w, int content_h,
                                         int viewport_w, int viewport_h,
                                         bool allow_zoom_in);
irect16_t imageeditor_document_workspace_rect(void);
void imageeditor_max_document_frame_size(int *out_w, int *out_h);
void imageeditor_max_canvas_viewport_size(int *out_w, int *out_h);
void imageeditor_document_frame_for_viewport(int viewport_w, int viewport_h,
                                             int *out_w, int *out_h);
bool imageeditor_handle_zoom_command(canvas_doc_t *doc, uint32_t id);
void canvas_win_update_status(window_t *win, int px, int py, bool hover_valid);
void imageeditor_sync_filter_menu(void);
bool imageeditor_load_filters(void);
void imageeditor_free_filters(void);
bool imageeditor_apply_filter(canvas_doc_t *doc, int filter_idx);
bool imageeditor_apply_builtin_filter(canvas_doc_t *doc, uint16_t id);
bool imageeditor_apply_builtin_blur(canvas_doc_t *doc, int radius);
bool show_filter_gallery_dialog(window_t *parent);

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
window_t *create_main_toolbar_window(void);
result_t main_toolbar_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
void imageeditor_sync_main_toolbar(void);
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
bool show_image_resize_dialog(window_t *parent, int *out_w, int *out_h,
                              image_resize_filter_t *out_filter);

// Grid Options dialog – returns true if accepted.
bool show_grid_options_dialog(window_t *parent, int *out_x, int *out_y);
bool show_onion_skin_dialog(window_t *parent);

// Selection Modify dialog – returns true if accepted.
bool show_selection_modify_dialog(window_t *parent, const char *title, int *out_amount);
bool show_blur_dialog(window_t *parent, int *out_amount);

// Layers palette window geometry.
// Positioned on the right side of the screen, below the color palette.
#define LAYERS_WIN_W          RIGHT_PANE_WIN_W
#define LAYERS_ROW_H           24   // height of each layer row in the panel
#define LAYERS_MAX_VIS_ROWS     5   // visible rows before scrolling
#define LAYERS_LIST_H         (LAYERS_ROW_H * LAYERS_MAX_VIS_ROWS)
// Toolbar band height: button size + top/bottom bevel + top/bottom padding.
#define LAYERS_TOOLBAR_H      (TB_SPACING + 2*(TOOLBAR_PADDING + TOOLBAR_BEVEL_WIDTH))
#define LAYERS_WIN_H          (TITLEBAR_HEIGHT + LAYERS_TOOLBAR_H + LAYERS_LIST_H)
#define LAYERS_WIN_X          (SCREEN_W - LAYERS_WIN_W - 4)
#define LAYERS_WIN_Y          (COLOR_WIN_Y + COLOR_WIN_H + 4)

// Hit-test zones within a layer row (x offsets)
#define LAYERS_EYE_W           16   // eye-icon click area width
#define LAYERS_CHIP_W          16   // alpha-edit icon click area width
#define LAYERS_NAME_X          (1 + LAYERS_EYE_W + 2 + LAYERS_CHIP_W + 3)  // start of name text
#define LAYERS_NAME_W          (LAYERS_WIN_W - LAYERS_NAME_X - 4)

#define ID_LAYER_BLEND_BASE     80
#define ID_LAYER_BLEND_COMBO    72

// ============================================================
// Layer management (canvas.c)
// ============================================================

// Add a new empty layer above the current active layer (filled with transparent pixels).
bool doc_add_layer(canvas_doc_t *doc);

// Add a new layer filled with the given RGBA color (0x00000000 = transparent,
// 0xFFFFFFFF = opaque white, etc.).  Convenience variant used by the New Layer
// dialog so the caller picks the fill without a second memset pass.
bool doc_add_layer_filled(canvas_doc_t *doc, uint32_t fill_color);

// Fill the active layer with the given RGBA color, ignoring selection state.
bool canvas_fill_active_layer(canvas_doc_t *doc, uint32_t fill_color);

// Delete the active layer (minimum 1 layer always kept).
bool doc_delete_layer(canvas_doc_t *doc);

// Duplicate the active layer and insert the copy above it.
bool doc_duplicate_layer(canvas_doc_t *doc);

// Change the active layer, updating doc->pixels and resetting editing_mask.
void doc_set_active_layer(canvas_doc_t *doc, int idx);
void doc_set_mask_only_view(canvas_doc_t *doc, bool enabled);
void doc_set_layer_blend_mode(canvas_doc_t *doc, int idx, layer_blend_mode_t mode);
void layer_clear_preview_effect(canvas_doc_t *doc, int idx);
bool layer_set_preview_effect(canvas_doc_t *doc, int idx,
                              imageeditor_render_effect_t effect,
                              const ui_render_effect_params_t *params);
bool layer_commit_preview_effect(canvas_doc_t *doc, int idx);
const char *layer_blend_mode_name(layer_blend_mode_t mode);

// Move the active layer up (towards the top of the stack).
void doc_move_layer_up(canvas_doc_t *doc);

// Move the active layer down (towards the bottom of the stack).
void doc_move_layer_down(canvas_doc_t *doc);

// Flatten the active layer onto the one below it.
void doc_merge_down(canvas_doc_t *doc);

// Flatten all layers into a single layer.
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
bool show_levels_dialog(window_t *parent);

// Swap the active foreground/background colors.
void swap_foreground_background_colors(void);

// ============================================================
// Animation support (anim.c / win_timeline.c)
// Animation is always enabled: a single-frame timeline is the default
// canvas state; additional frames enable sprite/animation workflows.
// ============================================================

#include "anim/anim.h"

// Floating frame strip geometry.
#define TIMELINE_THUMB_W   TB_SPACING
#define TIMELINE_WIN_H     TOOLBAR_BAND_HEIGHT

// Factory helper: create, show, and register the timeline palette window.
window_t *create_timeline_window(void);

// Synchronize the timeline toolbar with the active playback state.
void timeline_toolbar_sync(void);

// Refresh the timeline palette after frame changes.
void timeline_win_refresh(void);

// Animation playback tick — called from evTimer to advance frames.
void anim_tick(canvas_doc_t *doc);
void anim_stop_playback(canvas_doc_t *doc);

// Export helpers
bool anim_export_gif(canvas_doc_t *doc, const char *path);
bool anim_export_apng(canvas_doc_t *doc, const char *path);
bool anim_export_spritesheet(canvas_doc_t *doc, const char *path);

// ============================================================
// Operation lifecycle API (canvas_ops.c)
// ============================================================

// Begin a document mutation operation — pushes undo snapshot.
bool ie_doc_begin_op(canvas_doc_t *doc, const char *op_name);

// Commit the operation — marks dirty, updates title, and refreshes all views.
// If success is false, discards the undo snapshot instead.
void ie_doc_commit_op(canvas_doc_t *doc, bool success);

// Dirty state and title management.
void ie_doc_mark_dirty(canvas_doc_t *doc);
void ie_doc_update_title(canvas_doc_t *doc);

// Invalidation helpers — refresh specific views after changes.
void ie_doc_invalidate_all(canvas_doc_t *doc);
void ie_doc_invalidate_canvas(canvas_doc_t *doc);
void ie_doc_invalidate_layers(canvas_doc_t *doc);
void ie_doc_invalidate_timeline(canvas_doc_t *doc);

// Targeted refresh hooks — use after specific mutation types.
void ie_doc_after_pixels_changed(canvas_doc_t *doc);
void ie_doc_after_layers_changed(canvas_doc_t *doc);
void ie_doc_after_selection_changed(canvas_doc_t *doc);

// ── Tool handler system (tools/tools.h)
// ============================================================

// Tool handler interface — replaces giant switch statements in win_canvas.c.
// See tools/tools.h for the handler structure definition.
typedef struct tool_handler_s tool_handler_t;

// Get the handler for a tool ID, returns NULL if no handler registered.
const tool_handler_t *get_tool_handler(int tool_id);

// Register all built-in tool handlers (called at startup).
void register_builtin_tools(void);

// ============================================================
// Command modules (commands/*.c)
// ============================================================

// Replaces handle_menu_command's giant switch with focused command modules.
// Each command uses Phase 1's ie_doc_begin_op/commit_op lifecycle wrappers.
// See commands/commands.h for all command declarations.

// Edit commands
void cmd_undo(canvas_doc_t *doc);
void cmd_redo(canvas_doc_t *doc);
void cmd_cut(canvas_doc_t *doc);
void cmd_copy(canvas_doc_t *doc);
void cmd_paste(canvas_doc_t *doc);

// Selection commands
void cmd_select_all(canvas_doc_t *doc);
void cmd_deselect(canvas_doc_t *doc);
void cmd_select_clear(canvas_doc_t *doc);
void cmd_select_expand(canvas_doc_t *doc, int amount);
void cmd_select_contract(canvas_doc_t *doc, int amount);
void cmd_crop_to_selection(canvas_doc_t *doc);

// Image commands
void cmd_flip_horizontal(canvas_doc_t *doc);
void cmd_flip_vertical(canvas_doc_t *doc);
void cmd_invert_colors(canvas_doc_t *doc);
void cmd_resize_image(canvas_doc_t *doc, int new_w, int new_h, image_resize_filter_t filter);
void cmd_resize_canvas(canvas_doc_t *doc, int new_w, int new_h);

// Layer commands
void cmd_layer_new(canvas_doc_t *doc, uint32_t fill_color);
void cmd_layer_delete(canvas_doc_t *doc);
void cmd_layer_duplicate(canvas_doc_t *doc);
void cmd_layer_move_up(canvas_doc_t *doc);
void cmd_layer_move_down(canvas_doc_t *doc);
void cmd_layer_merge_down(canvas_doc_t *doc);
void cmd_layer_flatten(canvas_doc_t *doc);
void cmd_layer_fill(canvas_doc_t *doc, uint32_t color);
void cmd_layer_add_mask(canvas_doc_t *doc, int fill_mode);
void cmd_layer_apply_mask(canvas_doc_t *doc);
void cmd_layer_remove_mask(canvas_doc_t *doc);
canvas_doc_t *cmd_layer_extract_mask(canvas_doc_t *doc);
void cmd_layer_edit_mask(canvas_doc_t *doc, bool enable);

#endif // __IMAGEEDITOR_H__
