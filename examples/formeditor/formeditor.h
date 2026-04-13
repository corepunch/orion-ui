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

// Padding around the form inside the canvas window
#define CANVAS_PADDING    10

// Tool palette
// toolbox.png: 69x299 = 3 cols x 13 rows at 23x23 pixels
#define TOOLBOX_ICON_W    23
#define TOOLBOX_ICON_H    23
#define TOOLBOX_COLS      3    // 69 / 23
// Button size: icon 23x23 inside a 27x27 square gives 2px bevel border on each side.
// (btn_fill_w = bsz-2 = 25, icon offset bx=+2, so 25-2-23 = 0 right gap; outer shadow adds 1px)
#define TOOLBOX_BTN_SIZE  27

// Palette window — 2-button-wide column
#define PALETTE_WIN_X     4
#define PALETTE_WIN_W     (TOOLBOX_BTN_SIZE * 2)
#define PALETTE_WIN_H     4     // minimal client area

// Document window initial position
#define DOC_START_X       (PALETTE_WIN_X + PALETTE_WIN_W + 10)
#define DOC_START_Y       (MENUBAR_HEIGHT + 4)
#define DOC_WIN_W         (SCREEN_W - DOC_START_X - 4)
#define DOC_WIN_H         (SCREEN_H - DOC_START_Y - 4)

// ============================================================
// Control types  (VB3 toolbox order, skipping unsupported)
// ============================================================

#define CTRL_BUTTON    0
#define CTRL_CHECKBOX  1
#define CTRL_LABEL     2
#define CTRL_TEXTEDIT  3
#define CTRL_LIST      4
#define CTRL_COMBOBOX  5
#define CTRL_TYPE_COUNT 6

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
  int      x, y, w, h;  // position and size in form coordinates
  uint32_t flags;        // reserved for future style flags
  char     text[64];     // control caption / label text
  char     name[32];     // identifier name (e.g. "IDC_BUTTON1")
} form_element_t;

typedef struct {
  form_element_t elements[MAX_ELEMENTS];
  int    element_count;
  int    form_w, form_h;
  bool   modified;
  char   filename[512];
  int    next_id;                      // next numeric control ID
  int    type_counters[CTRL_TYPE_COUNT]; // per-type name counter
  window_t *canvas_win;
  window_t *doc_win;
} form_doc_t;

typedef struct {
  form_doc_t  *doc;
  window_t    *menubar_win;
  window_t    *tool_win;
  int          current_tool;
  accel_table_t *accel;
} app_state_t;

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
