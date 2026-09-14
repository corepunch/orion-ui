#ifndef __UI_COMMCTL_H__
#define __UI_COMMCTL_H__

#include <orion/user/user.h>
#include "columnview.h"
#include "menubar.h"
#include "appchrome.h"
#include "filelist.h"

// Forward declarations for types from other subsystems
typedef struct database_s database_t;

// Register all common controls with the window system.
void register_commctl_classes(void);

// Intrinsic sizing shared by button-like controls.  Height is framework-owned;
// callers choose a CONTROL_SIZE_* style instead of supplying pixel heights.
int  control_predefined_height(flags_t flags);
void control_apply_predefined_height(window_t *win, const char *module);
bool control_arrange_predefined_height(window_t *win, const layout_arrange_t *arrange);

// Built-in commctl class list APIs used by FormEditor/apps to register
// classes from this library.
int get_num_classes(void);
const fe_component_desc_t *get_class_at_index(int index);

// bitmap_strip_t is defined in user/user.h and available via the include above.
// Kept here as a comment for documentation purposes:
// A fixed-size-tile bitmap strip used with btnSetImage (wparam=index, lparam=bitmap_strip_t*).

// Scrollbar info structure (WinAPI SCROLLINFO analogue).
// Used with sbSetInfo.
typedef struct {
  int min_val; // minimum scroll position
  int max_val; // maximum scroll position (content size)
  int page;    // visible page size (viewport dimension)
  int pos;     // current scroll position
} scrollbar_info_t;

// Slider range structure (WinAPI trackbar style min/max range).
typedef struct {
  int min_val;
  int max_val;
} slider_range_t;

// Common control window procedures
result_t win_button(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_toolbar_button(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_checkbox(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_reportview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_iconview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_icongrid(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

// Individual desktop-style icon. Each icon is a child window with its own
// winproc, selection/focus state, image, status image, badges, and opaque model pointer.
// The texture remains owned by the caller so multiple icons can share it.
#define ICON_MAX_BADGES 4
typedef enum { ICON_BADGE_TOP_LEFT, ICON_BADGE_TOP_RIGHT,
               ICON_BADGE_BOTTOM_LEFT, ICON_BADGE_BOTTOM_RIGHT,
               ICON_BADGE_TOP_CENTER } icon_badge_anchor_t;
typedef struct { uint32_t texture; int width, height; } icon_image_t;
typedef struct {
  const char *text;
  uint32_t background;
  uint32_t foreground;
  icon_badge_anchor_t anchor;
} icon_badge_t;
typedef struct {
  icon_image_t image;
  void *item_data; // reserved model/popup/drag source context; not owned
  bool draggable;  // allow direct repositioning inside the parent by dragging
  window_t *notify_window; // optional evCommand target; defaults to the parent
} icon_params_t;
result_t win_icon(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
// Combobox creation parameters — for database-driven dropdowns.
// When source/display/value attributes are present in .orion forms,
// orionc generates a combobox_params_t structure and passes it via lparam.
typedef struct {
  database_t *db;             // Database instance (NULL = populate manually)
  int         table_id;       // TABLE_* enum value for source table
  const char *display_field;  // Field name to show in dropdown (e.g. "name")
  const char *value_field;    // Field name for actual value (e.g. "id")
  const char *filter_field;   // Optional: field name to filter on (e.g. "is_remote")
  const char *filter_value;   // Optional: value to match as string (e.g. "1" or "0")
} combobox_params_t;

// Combobox internal state (shared with list control for dropdown)
#define MAX_COMBOBOX_STRINGS MAX_LIST_ITEMS
typedef char combobox_string_t[64];
typedef struct {
  combobox_params_t params;  // Copy of creation params
  combobox_string_t *texts;  // Display strings
  int *values;               // Value field data (e.g., IDs) for foreign keys
} combobox_state_t;

result_t win_combobox(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_textedit(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_multiedit(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_label(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_image(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_list(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_console(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_space(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_separator(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_filelist(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_menubar(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_scrollbar(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_slider(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_gradient(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_tabview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

// Column browser (NSBrowser-style multi-column hierarchical navigation).
// Data source callback pattern for populating columns dynamically.
typedef struct {
  int (*get_child_count)(void *ctx, int column, int parent_idx);
  const char *(*get_child_title)(void *ctx, int column, int parent_idx, int child_idx);
  bool (*is_leaf)(void *ctx, int column, int idx);
  void *userdata;
} column_browser_datasource_t;

enum {
  cbSetDataSource = evUser + 300,  // lparam = column_browser_datasource_t*
  cbRefresh,                        // Rebuild all columns from current path
  cbGetSelection,                   // wparam = column; returns selected index or -1
  cbSetPath,                        // lparam = int[] path array, wparam = length
  cbGetColumnCount,                 // Returns number of visible columns
};

result_t win_column_browser(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

// Database-backed table view — automatically populates from database API.
// See commctl/tableview.c for full documentation and usage examples.
enum {
  tvRefresh = evUser + 260,
  tvSetFilter,
  tvGetSelectedRecord,
  tvGetRecord,              // wparam = row index; returns bound record pointer
};

typedef struct {
  database_t *db;              // Database instance
  int table_id;                // TABLE_* enum value
  int filter_field;            // Field to filter by (0 = fetch all)
  intptr_t filter_value;       // Value to match
  const char **field_names;    // Column field names (NULL-terminated)
  const char **column_titles;  // Column display titles (NULL-terminated)
  const int *column_widths;    // Column widths (0 = flex, NULL = all 0)
  const int *column_min_widths; // Column minimum widths (0 = default 20px), NULL = all default
  const char *check_field;     // Boolean state-image binding (NULL = no checkboxes)
  uint32_t master_id;          // Parent TableView control ID (0 = unbound)
  int master_filter_field;     // Child FK field used by dbFetch (integer FK only)
  const char *master_key;      // Parent field referenced by the FK (must be integer)
  reportview_cell_style_t cell_style; // REPORTVIEW_CELL_COLUMNS (default) or TWO_LINE
} tableview_params_t;

result_t win_tableview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
void tableview_handle_master_selection(window_t *root, window_t *master);

// Auto-layout container windows.
typedef struct {
  flags_t orientation;       // WINDOW_STACK_HORIZONTAL bit flag; 0 = vertical
  uint8_t spacing;           // spacing between direct children (0 = default)
  irect16_t padding;         // inner padding for the container
  irect16_t margin;          // outer margin when nested in a parent layout
} layout_view_config_t;

result_t win_stack(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_grid(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_flow(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
// Backward-compatible aliases for legacy call sites.
result_t win_stackview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_gridview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_flowview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_column(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
void layout_stack_measure_window(window_t *win, layout_measure_t *m);
void layout_stack_arrange_window(window_t *win, const irect16_t *rect);
void layout_grid_measure_window(window_t *win, layout_measure_t *m);
void layout_grid_arrange_window(window_t *win, const irect16_t *rect);
void layout_flow_measure_window(window_t *win, layout_measure_t *m);
void layout_flow_arrange_window(window_t *win, const irect16_t *rect);
void layout_flow_horizontal(window_t *first, int start_x, int gap);

// Splitter — thin draggable divider bar between two sibling panels.
// Orientation is set via lparam at create time:
//   (void *)SPLIT_VERT  → vertical bar (narrow column, drag left/right)
//   (void *)SPLIT_HORZ  → horizontal bar (narrow row, drag up/down)
//
// On left-click the splitter sends evCommand(MAKEDWORD(id, spnDragStart)) to
// its parent.  lparam packs the hit point in parent-local coordinates
// as MAKEDWORD(uint16_t px, uint16_t py).  The parent should:
//   1. Call set_capture(parent_win) to receive subsequent mouse events.
//   2. Track evMouseMove to recompute the layout.
//   3. Call set_capture(NULL) + stop tracking on evLeftButtonUp.
// See apps/gitclient/view_main.c for the canonical usage pattern.
#define SPLIT_VERT 0
#define SPLIT_HORZ 1
result_t win_splitter(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
int      win_splitter_orientation(window_t *win);

// SplitView — two-pane container with a draggable divider.
// Orientation is set via lparam at create time:
//   (void *)SPLIT_VERT  → vertical divider (panes left/right)
//   (void *)SPLIT_HORZ  → horizontal divider (panes top/bottom)
// The first two children of the splitview become the left/top and right/bottom
// panes; a splitter bar is created automatically between them.
// The parent does NOT need to handle spnDragStart — the drag loop is internal.
result_t win_splitview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
window_t *splitview_get_left(window_t *win);
window_t *splitview_get_right(window_t *win);

// Console API functions
void init_console(void);
void conprintf(const char* format, ...);
void draw_console(void);
void shutdown_console(void);
void toggle_console(void);

#endif
