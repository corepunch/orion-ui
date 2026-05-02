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

// Palette window dimensions.
#define PALETTE_WIN_X     4
#define PALETTE_WIN_W     (TOOLBOX_COLS * FE_TOOLBOX_BTN_SIZE)

// Property browser window.  This is intentionally a reportview-backed
// inspector: close to VB1's simple property sheet, without inline editing yet.
#define PROPBROWSER_WIN_X (SCREEN_W - 184)
#define PROPBROWSER_WIN_Y (MENUBAR_HEIGHT + 4)
#define PROPBROWSER_WIN_W 180
#define PROPBROWSER_WIN_H 180

// Project forms browser.
#define FORMS_WIN_X       PALETTE_WIN_X
#define FORMS_WIN_Y       (MENUBAR_HEIGHT + 126)
#define FORMS_WIN_W       150
#define FORMS_WIN_H       210

// Document window initial position
// frame.y is the window top; place it 8px below the menu bar.
#define DOC_START_X       (PALETTE_WIN_X + PALETTE_WIN_W + 10)
#define DOC_START_Y       (MENUBAR_HEIGHT + 8)

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

// Built-in component indices as registered by formeditor_components.
// Kept as compatibility aliases for tests and older form editor code; project
// files should use component tokens/class names instead.
#define CTRL_BUTTON    0
#define CTRL_CHECKBOX  1
#define CTRL_LABEL     2
#define CTRL_TEXTEDIT  3
#define CTRL_LIST      4
#define CTRL_COMBOBOX  5

// ============================================================
// Types
// ============================================================

typedef struct {
  int      type;        // registered component ID
  int      id;          // numeric control ID (e.g. 1001)
  char     id_expr[32]; // original ID expression from project XML, if any
  irect16_t frame;      // position and size in form coordinates
  uint32_t flags;        // reserved for future style flags
  char     flags_expr[128]; // original flags expression from project XML, if any
  char     text[64];     // control caption / label text
  char     name[32];     // identifier name (e.g. "IDC_BUTTON1")
  window_t *live_win;    // design-time live control hosted on the canvas
} form_element_t;

typedef struct form_doc_t {
  form_element_t elements[MAX_ELEMENTS];
  int    element_count;
  isize16_t form_size;
  uint32_t flags;       // form/window flags exported in form_def_t
  bool   modified;
  char   form_id[64];
  char   form_title[128];
  char   owner[256];
  char   required_plugin[64];
  int    next_id;                      // next numeric control ID
  int    type_counters[FE_MAX_COMPONENTS]; // per-component name counter
  window_t *canvas_win;
  window_t *doc_win;
  struct form_doc_t *next;
  // Grid settings
  int    grid_size;       // dot spacing in form pixels (default 8)
  bool   show_grid;       // paint grid dots on the form surface
  bool   snap_to_grid;    // snap moves/resizes to grid
} form_doc_t;

typedef struct {
  char name[64];
} form_plugin_ref_t;

#define FE_MAX_PROJECT_PLUGINS 32

typedef struct {
  char filename[512];
  char name[64];
  char title[128];
  char root[256];
  form_plugin_ref_t plugins[FE_MAX_PROJECT_PLUGINS];
  int plugin_count;
  bool loaded;
  bool modified;
} form_project_t;

typedef struct {
  form_doc_t  *doc;
  form_doc_t  *docs;
  window_t    *menubar_win;
  window_t    *tool_win;
  window_t    *prop_win;
  window_t    *forms_win;
  hinstance_t  hinstance;  // owning app instance
  int          current_tool;
  accel_table_t *accel;
  form_project_t project;
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
result_t win_forms_browser_proc(window_t *win, uint32_t msg,
                                uint32_t wparam, void *lparam);
void canvas_rebuild_live_controls(form_doc_t *doc);
void canvas_sync_live_controls(form_doc_t *doc);
window_t *property_browser_create(hinstance_t hinstance);
void property_browser_refresh(form_doc_t *doc);
window_t *forms_browser_create(hinstance_t hinstance);
void forms_browser_refresh(void);

// ============================================================
// Document helpers
// ============================================================

form_doc_t *create_form_doc(int w, int h);
void        close_form_doc(form_doc_t *doc);
void        form_doc_update_title(form_doc_t *doc);
void        form_doc_activate(form_doc_t *doc);

// ============================================================
// Project I/O
// ============================================================

bool form_project_load(const char *path);
bool form_project_save(const char *path);

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
