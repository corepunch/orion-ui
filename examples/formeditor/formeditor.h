#ifndef __FORMEDITOR_H__
#define __FORMEDITOR_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../../ui.h"

// ============================================================
// Layout constants
// ============================================================

#define SCREEN_W          640
#define SCREEN_H          480

// Default form dimensions
#define FORM_DEFAULT_W    320
#define FORM_DEFAULT_H    200

// Tool palette — formeditor uses 21px icons inside 26px square buttons.
// toolbox.png: 63x299 pixels = 3 cols x N rows of 21x21 px icons
// (3 icon columns in the sprite sheet; display layout is always 2 button columns).
#define FE_TOOLBOX_ICON_W   21   // icon tile size in toolbox.png
#define FE_TOOLBOX_BTN_SIZE 26   // square button size used for the formeditor

// Palette window dimensions — 2 button columns, rows computed from NUM_TOOLS.
#define PALETTE_WIN_X     4
#define PALETTE_WIN_W     (TOOLBOX_COLS * FE_TOOLBOX_BTN_SIZE)
#define PALETTE_GRID_ROWS ((NUM_TOOLS + TOOLBOX_COLS - 1) / TOOLBOX_COLS)
#define PALETTE_WIN_H     (TITLEBAR_HEIGHT + PALETTE_GRID_ROWS * FE_TOOLBOX_BTN_SIZE + 4)

// Property browser window.  This is intentionally a reportview-backed
// inspector: close to VB1's simple property sheet, without inline editing yet.
#define PROPBROWSER_WIN_X (SCREEN_W - 184)
#define PROPBROWSER_WIN_Y (MENUBAR_HEIGHT + 4)
#define PROPBROWSER_WIN_W 180
#define PROPBROWSER_WIN_H 180

// Document window initial position
// frame.y is the window top; place it 8px below the menu bar.
#define DOC_START_X       (PALETTE_WIN_X + PALETTE_WIN_W + 10)
#define DOC_START_Y       (MENUBAR_HEIGHT + 8)

// ============================================================
// Control types  (VB3 toolbox order, skipping unsupported)
// Aliased to the framework's FORM_CTRL_* enum so that form_element_t.type
// values are directly usable with create_window_from_form().
// ============================================================

#define CTRL_BUTTON    FORM_CTRL_BUTTON
#define CTRL_CHECKBOX  FORM_CTRL_CHECKBOX
#define CTRL_LABEL     FORM_CTRL_LABEL
#define CTRL_TEXTEDIT  FORM_CTRL_TEXTEDIT
#define CTRL_LIST      FORM_CTRL_LIST
#define CTRL_COMBOBOX  FORM_CTRL_COMBOBOX
#define CTRL_TYPE_COUNT FORM_CTRL_COUNT

// ============================================================
// Number of tools
// ============================================================

#define NUM_TOOLS  7

// ============================================================
// Menu item IDs
// ============================================================

#define ID_FILE_NEW     1
#define ID_FILE_OPEN    2
#define ID_FILE_SAVE    3
#define ID_FILE_SAVEAS  4
#define ID_FILE_QUIT    5

#define ID_EDIT_DELETE  10
#define ID_EDIT_PROPS   11

#define ID_VIEW_GRID    20

#define ID_HELP_ABOUT   100

// Tool command IDs (VB3 toolbox slot numbers map to strip indices)
// Strip order: 0=Pointer, 1=Picture(skip), 2=Label, 3=TextBox,
//              4=Frame(skip), 5=CommandButton, 6=CheckBox, 7=Option(skip),
//              8=ComboBox, 9=ListBox, ...
#define ID_TOOL_SELECT    200
#define ID_TOOL_LABEL     202
#define ID_TOOL_TEXTEDIT  203
#define ID_TOOL_BUTTON    205
#define ID_TOOL_CHECKBOX  206
#define ID_TOOL_COMBOBOX  208
#define ID_TOOL_LIST      209

// ============================================================
// Limits
// ============================================================

#define MAX_ELEMENTS  256
#define CTRL_ID_BASE  1001

// ============================================================
// Types
// ============================================================

typedef struct {
  int      type;        // CTRL_* constant
  int      id;          // numeric control ID (e.g. 1001)
  irect16_t frame;      // position and size in form coordinates
  uint32_t flags;        // reserved for future style flags
  char     text[64];     // control caption / label text
  char     name[32];     // identifier name (e.g. "IDC_BUTTON1")
  window_t *live_win;    // design-time live control hosted on the canvas
} form_element_t;

typedef struct {
  form_element_t elements[MAX_ELEMENTS];
  int    element_count;
  isize16_t form_size;
  bool   modified;
  char   filename[512];
  int    next_id;                      // next numeric control ID
  int    type_counters[CTRL_TYPE_COUNT]; // per-type name counter
  window_t *canvas_win;
  window_t *doc_win;
  // Grid settings
  int    grid_size;       // dot spacing in form pixels (default 8)
  bool   show_grid;       // paint grid dots on the form surface
  bool   snap_to_grid;    // snap moves/resizes to grid
} form_doc_t;

typedef struct {
  form_doc_t  *doc;
  window_t    *menubar_win;
  window_t    *tool_win;
  window_t    *prop_win;
  hinstance_t  hinstance;  // owning app instance
  int          current_tool;
  accel_table_t *accel;
} app_state_t;

// ============================================================
// Drag mode for the canvas window
// ============================================================

typedef enum {
  DRAG_NONE,
  DRAG_MOVE,
  DRAG_RESIZE,
  DRAG_RUBBERBND,
} drag_mode_t;

typedef struct {
  int16_t x, y;
} canvas_pt_t;

typedef struct {
  int16_t x, y;
} form_pt_t;

typedef struct {
  drag_mode_t mode;
  union {
    struct {
      canvas_pt_t start;
      irect16_t   frame;
    } move;
    struct {
      canvas_pt_t start;
      irect16_t   frame;
      int         handle;
    } resize;
    struct {
      canvas_pt_t start;
      irect16_t   band;       // rubber-band in form coords
      int         ctrl_type;  // CTRL_* being placed
    } place;
  };
} drag_state_t;

// ============================================================
// Canvas window state (stored in canvas_win->userdata)
// ============================================================

typedef struct {
  form_doc_t *doc;
  window_t   *preview_win;
  int         preview_type;
  ipoint16_t  pan;
  int         selected_idx;   // -1 = no selection
  drag_state_t drag;
} canvas_state_t;

// ============================================================
// Globals
// ============================================================

extern app_state_t *g_app;

// ============================================================
// Window procedures
// ============================================================

result_t editor_menubar_proc(window_t *win, uint32_t msg,
                              uint32_t wparam, void *lparam);
result_t win_canvas_proc(window_t *win, uint32_t msg,
                          uint32_t wparam, void *lparam);
result_t win_tool_palette_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam);
result_t win_property_browser_proc(window_t *win, uint32_t msg,
                                    uint32_t wparam, void *lparam);
void canvas_rebuild_live_controls(form_doc_t *doc);
void canvas_sync_live_controls(form_doc_t *doc);
window_t *property_browser_create(hinstance_t hinstance);
void property_browser_refresh(form_doc_t *doc);

// ============================================================
// Document helpers
// ============================================================

form_doc_t *create_form_doc(int w, int h);
void        close_form_doc(form_doc_t *doc);
void        form_doc_update_title(form_doc_t *doc);

// ============================================================
// Form I/O
// ============================================================

bool form_save(form_doc_t *doc, const char *path);
bool form_load(form_doc_t *doc, const char *path);

// ============================================================
// Menu dispatch
// ============================================================

extern menu_def_t  kMenus[];
extern const int   kNumMenus;
void handle_menu_command(uint16_t id);

// ============================================================
// Dialogs
// ============================================================

void show_about_dialog(window_t *parent);
bool show_props_dialog(window_t *parent, form_element_t *el);

#endif // __FORMEDITOR_H__
