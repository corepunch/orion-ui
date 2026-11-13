#ifndef __UI_COMMCTL_H__
#define __UI_COMMCTL_H__

#include <orion/user/user.h>
#include <orion/user/gdi.h>
#include <orion/user/toolbar.h>
#include <time.h>

// ---------------------------------------------------------------------------
// columnview.h
// ---------------------------------------------------------------------------

// Layout constants shared by the columnview-style controls.
// FONT_SIZE       — chrome font, from kernel/kernel.h.
// FONT_SIZE_SMALL — content font, from kernel/kernel.h.
#define COLUMNVIEW_ENTRY_HEIGHT  (FONT_SIZE_SMALL + 5)  // data rows
#define REPORTVIEW_TWO_LINE_ENTRY_HEIGHT (FONT_SIZE_SMALL * 2 + 5)
#define COLUMNVIEW_HEADER_HEIGHT (FONT_SIZE + 6)        // the header
#define COLUMNVIEW_WIN_PADDING   4
#define REPORTVIEW_MAX_SUBITEMS  (MAX_REPORTVIEW_COLUMNS - 1)  // every column after the first

// ReportView messages (WinAPI-style report/list view naming).
enum {
  RVM_ADDITEM = evUser + 100,
  RVM_DELETEITEM,
  RVM_GETITEMCOUNT,
  RVM_GETSELECTION,
  RVM_SETSELECTION,
  RVM_CLEAR,
  RVM_SETCOLUMNWIDTH,
  RVM_GETCOLUMNWIDTH,
  RVM_GETITEMDATA,
  RVM_SETITEMDATA,
  RVM_SETVIEWMODE,
  RVM_ADDCOLUMN,
  RVM_CLEARCOLUMNS,
  RVM_GETCOLUMNCOUNT,
  RVM_SETREPORTCOLUMNWIDTH, // wparam = col_index; lparam = (void*)(uintptr_t)new_width (0 = auto)
  RVM_GETREPORTCOLUMNWIDTH, // wparam = col_index; returns current report column width
  RVM_SETREDRAW,            // wparam = 0 suspend redraw; non-zero resume and repaint if dirty
  RVM_SETICONSTRIP,         // lparam = bitmap_strip_t* (NULL to clear); strip owned by caller
  RVM_SETICONSIZE,          // wparam = icon pixel size for RVM_VIEW_LARGE_ICON mode
  RVM_SETLARGEICONCOLS,     // wparam = fixed column count for RVM_VIEW_LARGE_ICON (0 = auto)
  RVM_SETCOLUMNTITLESVISIBLE, // wparam = 0 hide report headers; non-zero show
  RVM_GETCOLUMNTITLESVISIBLE,
  RVM_SETPRESERVEICONCOLORS, // wparam = 0 tint icons with row color; non-zero draw strip colors
  RVM_SETICONTEXTGAP,      // wparam = pixels between icon slot and item text in icon-list mode
  RVM_HITTEST,             // wparam = MAKEDWORD(content_x, content_y); returns item index or -1
  RVM_SETEXTENDEDSTYLE,    // wparam = style mask (0 = all); lparam = new RVS_EX_* bits
  RVM_GETEXTENDEDSTYLE,
  RVM_SETITEMSTATE,        // wparam = item index (-1 = all); lparam = reportview_item_state_t*
  RVM_GETITEMSTATE,        // wparam = item index; lparam = state mask
  RVM_SETCELLSTYLE,        // wparam = reportview_cell_style_t
  RVM_GETCELLSTYLE,
  RVM_SETFOOTER,           // wparam = text count; lparam = const char *const * per column (NULL = no footer row)
};

// WinAPI list-view compatible state-image model. State image indices are
// one-based: 1 = unchecked, 2 = checked when RVS_EX_CHECKBOXES is enabled.
#define RVS_EX_CHECKBOXES          0x00000004u
#define RVS_EX_FRAME               0x00000008u  // themed rounded frame (theme_t.table); rows clip to it
#define RVS_EX_FITROWS             0x00000010u  // measures to all its rows, for lists in popovers
#define RVS_EX_ONECLICK            0x00000020u  // rows are links: a click activates (RVN_DBLCLK); the selection shows only with focus
#define RVIS_STATEIMAGEMASK        0x0000f000u
#define RV_INDEXTOSTATEIMAGEMASK(i) ((uint32_t)(i) << 12)
#define RV_STATEIMAGEINDEX(state)   (((uint32_t)(state) & RVIS_STATEIMAGEMASK) >> 12)

enum {
  RVM_VIEW_ICON = 0,        // Small-icon list (icon-left, label-right, one row per item)
  RVM_VIEW_REPORT = 1,      // Multi-column report with sortable header
  RVM_VIEW_LARGE_ICON = 2,  // Large-thumbnail grid (icon-above, label-below) — WinAPI LVS_ICON
  RVM_VIEW_THUMBNAIL = 3,   // Photo grid: unlabeled square tiles that fill the width (RVM_SETICONSIZE = minimum tile)
};

typedef enum {
  REPORTVIEW_CELL_COLUMNS = 0, // One line per item, rendered as report columns.
  REPORTVIEW_CELL_TWO_LINE = 1, // First column is title; second is subtitle.
  REPORTVIEW_CELL_STACKED = 2,  // First column over the second, in secondary text; the others keep their columns.
} reportview_cell_style_t;
#define TABLEVIEW_CELL_COLUMNS  REPORTVIEW_CELL_COLUMNS
#define TABLEVIEW_CELL_TWO_LINE REPORTVIEW_CELL_TWO_LINE

typedef struct {
  const char *title;
  uint32_t width;
  uint32_t min_width;  // 0 = use default (20px), >0 = per-column minimum
  uint32_t align;      // 0 = left, TEXT_ALIGN_RIGHT (numbers), TEXT_ALIGN_CENTER
  int color;           // sys colour role of the column's text, 0 = the row's colour
  bool badge;          // cells are status badges toned by their row's tone
} reportview_column_t;

typedef struct {
  const char *text;
  int         icon;       // strip tile index; icon_name overrides it outside icon grid
  const char *icon_name;  // SVG icon for report/icon lists; icon grid always uses strip index
  uint32_t    icon_color; // report rows: tint of icon_name (0 = text colour)
  uint32_t    color;
  uint32_t    userdata;
  uint32_t    state;
  const char *subitems[REPORTVIEW_MAX_SUBITEMS];
  uint32_t    subitem_count;
  const char *section;    // report rows: a caption drawn above this row, starting a group (NULL = none)
  int         tone;       // sys colour role of the row's badge cells (0 = secondary)
} reportview_item_t;

typedef struct {
  uint32_t state;
  uint32_t state_mask;
} reportview_item_state_t;

#define ReportView_SetCheckState(win, item, checked) do {                    \
  reportview_item_state_t _state = {                                         \
    RV_INDEXTOSTATEIMAGEMASK((checked) ? 2 : 1), RVIS_STATEIMAGEMASK         \
  };                                                                          \
  send_message((win), RVM_SETITEMSTATE, (uint32_t)(item), &_state);           \
} while (0)
#define ReportView_GetCheckState(win, item)                                  \
  (RV_STATEIMAGEINDEX(send_message((win), RVM_GETITEMSTATE,                  \
                                    (uint32_t)(item),                         \
                                    (void *)(uintptr_t)RVIS_STATEIMAGEMASK)) == 2)

// ReportView notifications
enum {
  RVN_SELCHANGE = 200,
  RVN_DBLCLK,
  RVN_DELETE,
  RVN_ITEMCHECK,
  RVN_ICONCLICK,   // the row's icon (item.icon_name, a TableView RowIcon) was clicked; it is also selected
};

result_t win_reportview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_iconview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_icongrid(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

// ---------------------------------------------------------------------------
// menubar.c
// ---------------------------------------------------------------------------

// Height of the menu bar strip — matches the generous dropdown row rhythm.
// FONT_SIZE is a compile-time constant from kernel/kernel.h (included via user/user.h).
#ifdef AX_PLATFORM_IOS
#define MENUBAR_HEIGHT 29
#else
#define MENUBAR_HEIGHT (FONT_SIZE + 12)
#endif

// One item inside a dropdown menu.  label == NULL && id == 0 means separator.
// Submenus use id == 0 with a non-NULL label and submenu_items/submenu_count.
typedef struct menu_item_s {
  const char        *label;
  uint16_t           id;
  const struct menu_item_s *submenu_items;
  int                submenu_count;
  bool               disabled;
} menu_item_t;

// A top-level menu entry, e.g. "File" with its list of dropdown items.
typedef struct {
  const char        *label;
  const menu_item_t *items;
  int                item_count;
} menu_def_t;

// Send to a menu-bar window to configure its menus.
//   wparam = number of menu_def_t entries
//   lparam = const menu_def_t *  (caller owns; structs are shallow-copied)
#define kMenuBarMessageSetMenus (evUser + 200)

// Send to a menu-bar window to associate an accelerator table for hotkey hints.
// The menu bar stores a reference (not a copy); the caller must keep the table
// alive at least as long as the menu-bar window.
//   wparam = 0 (unused)
//   lparam = const accel_table_t *  (pass NULL to clear)
#define kMenuBarMessageSetAccelerators (evUser + 201)
#define kMenuBarMessageGetContentWidth (evUser + 202)

// Notification code placed in HIWORD(wparam) of evCommand that
// win_menubar sends to *itself* when the user selects an item.
//   LOWORD(wparam) = the selected item's id field
#define kMenuBarNotificationItemClick 300

result_t win_menubar(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

// Show a context-menu popup at screen coordinates.
// Items are shallow-copied; the caller must keep the menu_item_t array alive
// only until this function returns (the array is copied into the popup).
// The popup sends evCommand to notify_win with:
//   HIWORD(wparam) = kMenuBarNotificationItemClick
//   LOWORD(wparam) = the selected item's id
// Returns true on success.
bool show_popup_menu(window_t *notify_win, const menu_item_t *items,
                     int item_count, int screen_x, int screen_y);

// ---------------------------------------------------------------------------
// appchrome.c
// ---------------------------------------------------------------------------

// Application chrome owns menu and docked toolbar bands. Empty space passes input through.
// A NULL menubar_proc leaves room for the shell menu.
// A NULL toolbar_proc creates no toolbar band.
window_t *create_app_chrome(const char *title, winproc_t menubar_proc,
                            const menu_def_t *menus, int menu_count,
                            winproc_t toolbar_proc, hinstance_t hinstance);
window_t *app_chrome_menubar(window_t *chrome);
window_t *create_application_chrome(const char *title, winproc_t menubar_proc,
                                    const menu_def_t *menus, int menu_count,
                                    winproc_t toolbar_proc, const application_toolbar_t *toolbar,
                                    hinstance_t hinstance);
window_t *app_chrome_toolbar(window_t *chrome);
window_t *app_chrome_add_toolbar(window_t *chrome, toolbar_dock_t dock, winproc_t proc);

// ---------------------------------------------------------------------------
// filelist.c
// ---------------------------------------------------------------------------

// commctl/filelist.h - A file-browser specific control
//
// win_filelist extends win_reportview: it provides directory listing with
// sorting, optional extension filtering, path navigation, and file metadata
// (size, modification time).  All directory management is internal; callers
// receive FLN_* notifications and drive the control through FLM_* messages.

// ---------------------------------------------------------------------------
// File-item descriptor.  path is heap-allocated (owned by win_filelist).
// Callers must never free it; the pointer is valid only for the duration of a
// synchronous notification callback.
// ---------------------------------------------------------------------------
typedef struct {
  char    *path;         // full absolute path
  int      icon;         // icon index passed to draw_icon8()
  bool     is_directory;
  bool     is_hidden;    // entry is a hidden file (name starts with '.')
  size_t   size;         // file size in bytes (0 for directories)
  time_t   modified;     // last-modification timestamp
} fileitem_t;

// ---------------------------------------------------------------------------
// Messages sent TO win_filelist
// ---------------------------------------------------------------------------
enum {
  // Navigate to a directory.
  // wparam: unused   lparam: const char* absolute path
  FLM_SETPATH = evUser + 200,

  // Copy the current directory path into a caller buffer.
  // wparam: buffer size (incl. NUL)   lparam: char* destination
  FLM_GETPATH,

  // Reload the current directory from disk.
  // wparam: unused   lparam: unused
  FLM_REFRESH,

  // Copy the full path of the selected item into a caller buffer.
  // Returns an empty string when nothing is selected or when the selected
  // item is a directory (use FLM_GETPATH to obtain the current directory).
  // wparam: buffer size (incl. NUL)   lparam: char* destination
  FLM_GETSELECTEDPATH,

  // Set a file-extension filter.  Only files whose names end with the given
  // extension (case-insensitive) are shown; directories are always shown.
  // Pass NULL or "" to remove the filter.
  // wparam: unused   lparam: const char* extension (e.g. ".png", "png")
  FLM_SETFILTER,
};

// ---------------------------------------------------------------------------
// Notification codes emitted by win_filelist via evCommand to
// get_root_window(win).  HIWORD(wparam) = code, LOWORD(wparam) = item index.
// ---------------------------------------------------------------------------
enum {
  // Selection changed (single click).
  // lparam: const fileitem_t* of the selected item (file or directory),
  //         or NULL when the selection is cleared.
  FLN_SELCHANGE = 300,

  // A non-directory file was activated (double-click / Enter).
  // Directory navigation is handled internally before this fires.
  // lparam: const fileitem_t* of the activated file.
  FLN_FILEOPEN,

  // The current directory changed after navigation.
  // lparam: const char* new absolute directory path.
  FLN_NAVDIR,
};

result_t win_filelist(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

// ---------------------------------------------------------------------------
// (original commctl.h body)
// ---------------------------------------------------------------------------

// Forward declarations for types from other subsystems
typedef struct database_s database_t;

// Register all common controls with the window system.
#define imageSetPath (evUser + 211)
result_t win_imageview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
void register_commctl_classes(void);

// Intrinsic sizing shared by button-like controls.  Height is framework-owned;
// callers choose a CONTROL_SIZE_* style instead of supplying pixel heights.
int  control_predefined_height(flags_t flags);
int  control_text_padding_y(flags_t flags);
int  edit_padding_x(const window_t *win);   // TextEdit padding-left: the text clears the field's ends
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

// Bitmap-skinned button. Sprites are rects in `bm`, stretched to the control's frame, so the same
// skin serves any scale. `on`/`on_down` (w == 0: fall back to up/down) show while checked.
typedef struct {
  const bitmap_t *bm;
  irect16_t up, down, on, on_down;
  bool toggle;        // reflects btnGetCheck with the on sprites
  bool auto_check;    // a click flips the checked state; otherwise the owner drives it with btnSetCheck
} sprite_button_t;

// Bitmap-skinned slider. All coordinates are in "native" skin pixels (the control's size at 1:1) and are
// scaled to the real frame. Frame i of the track is `track` moved by (i % columns, i / columns) * frame_step;
// the frame follows the value, or its distance from the centre when by_magnitude (balance).
typedef struct {
  const bitmap_t *bm;
  isize16_t native;
  irect16_t track;
  ipoint16_t frame_step;
  int frames, columns;
  irect16_t thumb, thumb_down;
  irect16_t travel;   // x, y: thumb origin at the minimum; w, h: span along the axis (0 on the other)
  bool vertical, by_magnitude;
} sprite_slider_t;

// Common control window procedures
result_t win_spritebutton(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_spriteslider(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_button(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_toolbar_button(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
typedef struct {
  const toolbar_item_t *items;
  int count;
  uint32_t style;  // TOOLBAR_STYLE_* added to the bar's own (TOOLBAR_STYLE_SHOW_LABELS: captions under icons)
} activitybar_params_t;
result_t win_activitybar(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_checkbox(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

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
result_t win_scrollbar(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_slider(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_segmented(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_gradient(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
typedef struct { uint32_t style; } tabview_params_t;
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
  // Record actions, like EODisplayGroup insert/delete over a detail data
  // source: the record form edits the record, and a new record takes the
  // view's filter (its master's key) as its foreign key. Each returns 1 when
  // the data changed; the view then notifies its window with tvnChanged.
  tvInsertRecord,           // lparam = "field=value;..." defaults or NULL
  tvEditRecord,             // the selected record
  tvDeleteRecord,           // the selected record, after confirmation; delete rules apply
  tvGetTable,               // lparam = int* table id; returns 1 when bound
};
// evCommand notification (HIWORD) sent by a TableView after a record action.
#define tvnChanged 0x7401

// <RowIcon field= value= icon= color=>: the first matching rule draws its icon
// before a row's first column. A rule without field always matches; without
// value it matches a set field (non-empty, non-zero).
typedef struct {
  const char *field, *value, *icon;
  int color;                   // sys colour role (brTextSuccess, ...), -1 = the text colour
} tableview_row_icon_t;

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
  reportview_cell_style_t cell_style; // REPORTVIEW_CELL_COLUMNS (default), TWO_LINE or STACKED (cell-style="stacked")
  bool hide_column_titles;
  const struct form_def_s *record_form; // database form editing one row (form=); double-click edits
  bool blank_zeros;            // zeros="blank": numeric zeros show empty, as accounting reports do
  const tableview_row_icon_t *row_icons; // status icons by record state (NULL = none)
  int row_icon_count;
  const bool *column_totals;   // total="sum" per column: a totals row under the rows (NULL = none)
  const char *totals_label;    // caption in the first column without a total (NULL = "Totals")
  const char *totals_skip;     // rows with this field set stay out of the totals (subaccounts repeating their parent)
  bool framed;                 // frame="true": a themed rounded frame that clips the rows (RVS_EX_FRAME)
  const char *group_field;     // group-field="section": consecutive rows sharing its value form a captioned group
  bool fit_rows;               // fit-rows="true": measures to all its rows (RVS_EX_FITROWS), for popovers
  const int *column_colors;    // Column color="accent": sys colour role per column, 0 = the row's (NULL = none)
  const int *column_badges;    // Column style="badge": 1 shows the cell as a status badge toned by the row's RowIcon colour (NULL = none)
  bool click_activates;        // activate="click": rows are links (RVS_EX_ONECLICK), nothing is selected at first
} tableview_params_t;

result_t win_tableview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
void tableview_handle_master_selection(window_t *root, window_t *master);
// A double-clicked TableView with a record form edits the row; returns true
// when it handled the activation.
bool tableview_handle_activate(window_t *view);

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
result_t win_tilegrid(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_card(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_speechbubble(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_badge(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
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
#define SPLITTER_HIGHLIGHT_WIDTH 2
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
// Declarative forms may supply splitview_params_t through form_ctrl_def_t.lparam.
// svSetPaneMargin supplies the shared leaf margin for the resize band; leaf
// margins themselves use ordinary layout.layout_margin metadata.
typedef struct {
  int orientation;
  double split_ratio;  // Fraction of available space given to the left/top pane.
} splitview_params_t;
result_t win_splitview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
window_t *splitview_get_left(window_t *win);
window_t *splitview_get_right(window_t *win);

typedef struct { window_t *page; ipoint16_t point; } tab_drag_t; // point in source TabView content space
typedef struct { window_t *target; int before; } tab_move_t;
typedef struct { window_t *pane, *source; uint32_t command; } tab_action_t;
typedef struct { const char *text; uintptr_t data; } tab_item_t; // tcSetItems
// before is an insertion slot (0..count), or -1 to append. Also supports same-pane reorder.
bool tabview_move_page(window_t *source, int index, window_t *target, int before);

// Console API functions
void init_console(void);
void conprintf(const char* format, ...);
void draw_console(void);
void shutdown_console(void);
void toggle_console(void);

#endif
