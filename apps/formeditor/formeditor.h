#ifndef __FORMEDITOR_H__
#define __FORMEDITOR_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <libxml/tree.h>

#include <orion/ui.h>

typedef database_t db_t;

// ============================================================
// Layout constants
// ============================================================

#define SCREEN_W          800
#define SCREEN_H          600

// Default form dimensions
#define FORM_DEFAULT_W    320
#define FORM_DEFAULT_H    240

// Editing mode:
//   - FE_EDIT_MODE_VB_STYLE     = fixed layout with absolute x/y placement
//   - FE_EDIT_MODE_AUTO_LAYOUT  = layout-managed forms with drop targeting
//
// Override FE_DEFAULT_EDIT_MODE at build time to switch the editor's default
// behaviour for new documents.
#define FE_EDIT_MODE_VB_STYLE     0
#define FE_EDIT_MODE_AUTO_LAYOUT   1
typedef uint8_t fe_edit_mode_t;

#ifndef FE_DEFAULT_EDIT_MODE
#define FE_DEFAULT_EDIT_MODE FE_EDIT_MODE_AUTO_LAYOUT
#endif

static inline bool fe_default_auto_layout_enabled(void) {
  return FE_DEFAULT_EDIT_MODE == FE_EDIT_MODE_AUTO_LAYOUT;
}

// Tool palettes.
#define FE_TOOLBAR_BTN_SIZE (get_theme()->toolbar_button_size)
#define FE_COMPONENTS_GRID_COLS 3   // default palette fits three items per row
#define FE_COMPONENTS_BTN_SIZE 56   // large-icon grid cell width/height
#define FE_COMPONENTS_MIN_ROWS 5    // default palette height shows a little over 4.5 icons

// Palette window dimensions.
#define PALETTE_WIN_X     4
#define PALETTE_WIN_W     (184 + get_theme()->scrollbar_width)

// Property browser window.  This is intentionally a reportview-backed
// inspector: close to VB1's simple property sheet, without inline editing yet.
#define PROPBROWSER_WIN_X (SCREEN_W - 184)
#define PROPBROWSER_WIN_W 180
#define PROPBROWSER_WIN_H 180

// Project forms browser.
#define FORMS_WIN_X       PROPBROWSER_WIN_X
#define FORMS_WIN_Y       (get_theme()->menubar_height + 4)
#define FORMS_WIN_W       PROPBROWSER_WIN_W
#define FORMS_WIN_H       180

#define PROPBROWSER_WIN_Y (FORMS_WIN_Y + FORMS_WIN_H + 4)

// Project plugins browser.
#define PLUGINS_WIN_X     PROPBROWSER_WIN_X
#define PLUGINS_WIN_Y     (PROPBROWSER_WIN_Y + PROPBROWSER_WIN_H + 4)
#define PLUGINS_WIN_W     PROPBROWSER_WIN_W
#define PLUGINS_WIN_H     120

// Document window initial position
// frame.y is the window top; place it 8px below the menu bar.
#define DOC_START_X       (PALETTE_WIN_X + PALETTE_WIN_W + 10)
#define DOC_START_Y       (get_theme()->menubar_height + 8)

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

// Tool command IDs (VB3 tool slot numbers map to strip indices)
// Strip order: 0=Pointer, 1=Picture(skip), 2=Label, 3=TextBox,
//              4=Frame(skip), 5=Button, 6=CheckBox, 7=Option(skip),
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
#define FE_MAX_TABLE_COLUMNS 16

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
// Project and App State Types
// ============================================================

typedef struct {
  char database[64];
  char table[64];
  char field[64];
} fe_database_field_ref_t;

typedef struct form_doc_state_t {
  bool   modified;
  bool   drag_overlay_active;
  irect16_t drag_overlay_rect;
  window_t *selected_window;
} form_doc_state_t;

static inline form_doc_state_t *fe_doc_state(window_t *doc) {
  return doc ? (form_doc_state_t *)doc->userdata : NULL;
}

static inline const form_doc_state_t *fe_doc_state_const(const window_t *doc) {
  return doc ? (const form_doc_state_t *)doc->userdata : NULL;
}

typedef struct {
  char name[64];
} form_plugin_ref_t;

#define FE_MAX_PROJECT_PLUGINS 32
#define FE_MAX_PROJECT_DATABASES 8

typedef struct {
  char filename[512];
  char name[64];
  char title[128];
  char root[256];
  void *xml_root;
  char menus_xml[16384];
  form_plugin_ref_t plugins[FE_MAX_PROJECT_PLUGINS];
  int plugin_count;
  db_t *databases[FE_MAX_PROJECT_DATABASES];
  int database_count;
  bool loaded;
  bool modified;
} form_project_t;

typedef enum {
  FE_WIN_MENUBAR = 0,
  FE_WIN_TOOL,
  FE_WIN_PROP,
  FE_WIN_FORMS,
  FE_WIN_PLUGINS,
  FE_NUM_WINDOWS
} fe_window_idx_t;

typedef struct {
  window_t       *active_form;
  window_t       *forms[MAX_ELEMENTS];
  int             form_count;
  int             grid_size;
  bool            show_grid;
  bool            snap_to_grid;
  window_t       *windows[FE_NUM_WINDOWS];
  hinstance_t  hinstance;  // owning app instance
  int          current_tool;
  accel_table_t *accel;
  form_project_t project;
} app_state_t;

// ============================================================
// Notifications
// ============================================================

typedef enum {
  FE_EVENT_DOCUMENT_CREATED,
  FE_EVENT_DOCUMENT_CLOSED,
  FE_EVENT_DOCUMENT_ACTIVATED,
  FE_EVENT_DOCUMENT_MODIFIED,
  FE_EVENT_SELECTION_CHANGED,
  FE_EVENT_ELEMENT_ADDED,
  FE_EVENT_ELEMENT_DELETED,
  FE_EVENT_ELEMENT_MODIFIED,
  FE_EVENT_PROJECT_MODIFIED,
  FE_EVENT_COMPONENT_REGISTRY_CHANGED,
} fe_event_type_t;

typedef void (*fe_observer_fn_t)(fe_event_type_t event, window_t *doc, void *ctx);

// ============================================================
// Globals
// ============================================================

extern app_state_t *g_app;

// ============================================================
// Window procedures
// ============================================================

lresult_t editor_menubar_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
lresult_t win_components_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
lresult_t win_tool_palette_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
lresult_t win_property_browser_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
lresult_t win_forms_browser_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
lresult_t win_plugins_browser_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
lresult_t win_canvas_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
lresult_t win_canvas_runtime_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
void canvas_rebuild_live_controls(window_t *doc);
void canvas_set_component_drag_hover(window_t *doc, bool active, window_t *target);
window_t *canvas_find_component_drop_target(window_t *doc, int type, int canvas_x, int canvas_y);
bool canvas_drop_component_to_target(window_t *doc, int type, window_t *target, int screen_x, int screen_y);
bool canvas_bind_database_field(window_t *doc, const fe_database_field_ref_t *field,
                                int screen_x, int screen_y);
void formeditor_rebuild_tool_palette(void);
window_t *formeditor_create_components_palette(hinstance_t hinstance);
window_t *formeditor_create_tool_toolbar(hinstance_t hinstance);
window_t *property_browser_create(hinstance_t hinstance);
void property_browser_refresh(window_t *doc);
window_t *forms_browser_create(hinstance_t hinstance);
void forms_browser_refresh(void);
window_t *plugins_browser_create(hinstance_t hinstance);
void plugins_browser_refresh(void);
void formeditor_show_database_object_window(int db_index);
void formeditor_close_database_object_window(void);

// ============================================================
// Document model helpers
// ============================================================

void fe_doc_mark_modified(window_t *doc);
bool fe_doc_drop_create_component(int component_id, window_t *parent_target);

void fe_error_set(char *error, size_t error_sz, const char *message);
bool fe_resolve_table_column_database_field(xmlNodePtr table_node,
    const fe_database_field_ref_t *field, char *field_expr, size_t field_expr_sz,
    char *title, size_t title_sz, char *error, size_t error_sz);
xmlNodePtr fe_project_table_node_for_window(window_t *doc, window_t *target);
bool fe_project_update_table_column_binding(window_t *doc, window_t *table,
    int column, const char *field_expr, const char *title);
bool fe_project_append_component_node(window_t *doc, window_t *parent_target, window_t *child, const char *class_name);

// ============================================================
// Layout / project / runtime form APIs
// ============================================================

window_t *create_form_doc(int w, int h);
void        close_form_doc(window_t *doc);
void form_doc_update_title(window_t *doc);
void form_doc_activate(window_t *doc);
void form_doc_show_only(window_t *doc);
irect16_t form_doc_frame_for_size(int form_w, int form_h, uint32_t form_flags);

bool fe_project_load(const char *path);
bool fe_project_save(const char *path);
void fe_project_clear_xml(void);
void fe_project_clear_doc_xml(window_t *doc);

window_t *fe_create_runtime_form_window(window_t *doc, window_t *parent, winproc_t proc);

// ============================================================
// Notifications API
// ============================================================

int fe_subscribe(fe_observer_fn_t callback, void *ctx);
void fe_unsubscribe(int subscription_id);
void fe_notify(fe_event_type_t event, window_t *doc);

// ============================================================
// Menu dispatch
// ============================================================

void handle_menu_command(uint16_t id);

extern menu_def_t  kMenus[];
extern const int   kNumMenus;

// ============================================================
// Dialogs
// ============================================================

void show_about_dialog(window_t *parent);
void show_grid_settings_dialog(window_t *parent, window_t *doc);
bool show_form_props_dialog(window_t *parent, window_t *doc);

#endif // __FORMEDITOR_H__
