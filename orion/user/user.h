#ifndef __UI_USER_H__
#define __UI_USER_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "messages.h"
#include "text.h"
#include <orion/kernel/kernel.h>

// Forward declarations
typedef struct window_s window_t;
struct menu_item_s;
typedef struct irect16_s irect16_t;
typedef struct database_s database_t;
typedef uint32_t flags_t;
typedef intptr_t result_t;

// Application instance handle (analogous to WinAPI HINSTANCE).
// Each gem/app process receives a unique hinstance_t when loaded.
// Windows tagged with the same non-zero hinstance belong to the same app.
// hinstance == 0 means system/unowned (shell, framework, tests).
typedef uint32_t hinstance_t;

typedef enum {
  WINDOW_ROLE_NORMAL = 0,
  WINDOW_ROLE_HOST,
  WINDOW_ROLE_PAGE,
} window_role_t;

#define DEFAULT_WINDOW_CASCADE_X 10
#define DEFAULT_WINDOW_CASCADE_Y 20

// Window procedure callback type
typedef result_t (*winproc_t)(window_t *, uint32_t, uint32_t, void *);

typedef struct {
  window_t *child;       // child about to receive the event
  uint32_t  child_msg;   // original event message
  uint32_t  child_wparam;
  void     *child_lparam;
} parent_notify_t;

// Window hook callback type
typedef void (*winhook_func_t)(window_t *win, uint32_t msg, uint32_t wparam, void *lparam, void *userdata);

// Point structure
typedef struct {
  int16_t x, y;
} ipoint16_t;

// Size structure
typedef struct {
  int16_t w, h;
} isize16_t;

// Float rectangle structure (used for normalized UVs and other float-space rects).
typedef struct {
  float x, y, w, h;
} frect_t;

// Rectangle structure
struct irect16_s {
  int16_t x, y, w, h;
};

// Auto-layout helpers.  Measures and arrangements follow WPF-style semantics:
// the parent passes an available size, the child reports its desired size, and
// the final arrange rect is usually the parent's full client area.
typedef struct {
  int avail_w;
  int avail_h;
  int desired_w;
  int desired_h;
} layout_measure_t;

typedef struct {
  irect16_t rect;
  uint8_t   h_align;  // LAYOUT_ALIGN_*; 0 = stretch
  uint8_t   v_align;  // LAYOUT_ALIGN_*; 0 = stretch
} layout_arrange_t;

typedef struct layout_s {
  uint8_t   h_align;        // horizontal alignment; 0 = stretch
  uint8_t   v_align;        // vertical alignment; 0 = stretch
  uint8_t   layout_spacing;  // spacing between direct children; 0 = default
  int16_t   layout_fixed_w;  // declarative width hint; grid columns: -1 content, 0 stretch
  int16_t   layout_fixed_h;  // declarative height hint used by auto-layout
  irect16_t layout_padding;  // inner padding for auto-layout containers
  irect16_t layout_margin;   // outer margin when nested inside a layout container
} layout_t;

// A fixed-size-tile bitmap strip, analogous to WinAPI HIMAGELIST / TB_ADDBITMAP.
// Icons are indexed 0..N left-to-right then top-to-bottom.
// Used with btnSetImage and tbSetStrip.
typedef struct bitmap_strip_s {
  uint32_t tex;     // OpenGL texture ID of the strip texture
  int      icon_w;  // logical pixel width of each icon tile
  int      icon_h;  // logical pixel height of each icon tile
  int      cols;    // number of tile columns in the strip (strip_w / icon_w)
  int      sheet_w; // logical sheet width (for UV ratios; texture may be denser)
  int      sheet_h; // logical sheet height (for UV ratios; texture may be denser)
} bitmap_strip_t;

typedef struct {
  irect16_t rect;             // item-local bounds; drawing origin is already set
  uint32_t state;             // CTRL_* flags
  int index;
} toolbar_draw_item_t;

typedef struct {
  uint32_t from_ident, to_ident;
} toolbar_drop_item_t;

typedef struct toolbar_state_s {
  // Owner-draw item list — buttons/separators/spacers/labels/dropdowns drawn inline.
  toolbar_item_t *items;          // owned copy of the item descriptors (malloc'd)
  int             item_count;     // number of items in items[]
  irect16_t      *item_rects;     // computed band-relative rect per item (malloc'd)
  char          (*item_tooltips)[256]; // owned tooltip string copies (parallel to items)
  char          (*item_icons)[64];     // owned icon name copies for dynamically-built items
  int             hot_item;       // index of hovered item; -1 = none
  int             pressed_item;   // index of currently pressed item; -1 = none
  bool            pressed_in_arrow; // true when the press was in the dropdown arrow zone
  // Embedded control child windows (COMBOBOX / TEXTEDIT / SLIDER).
  // These are real window_t children with toolbar-band-relative frames.
  window_t       *children;
  // Strip for icon rendering (set via tbSetStrip / tbLoadStrip)
  bitmap_strip_t  strip;
  uint32_t        strip_tex;    // GL texture owned here; freed on toolbar destroy
  int             btn_size;     // 0 = TB_SPACING default; >0 = custom square size in px
  toolbar_orientation_t orientation;
  uint32_t        style;        // TOOLBAR_STYLE_* flags
} toolbar_state_t;

// Window definition structure (for declarative window creation)
typedef struct {
  const char *class_name;
  const char *text;
  uint32_t id;
  int w, h;
  flags_t flags;
  uint8_t layout_spacing;
} windef_t;

// ── Dialog Data Exchange (DDX) ──────────────────────────────────────────────
// Analogous to MFC DDX / WinAPI dialog-data routines.
// Describe each control-to-field mapping in a static ctrl_binding_t array,
// then call dialog_push() on create and dialog_pull() on accept.

// Returns the number of elements in a statically-sized array.
#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

// Returns sizeof(((type *)0)->field) — the byte size of a struct field.
#define sizeof_field(type, field) ((size_t)(sizeof(((type *)0)->field)))

typedef struct ctrl_binding_s ctrl_binding_t;
typedef void (*ddx_bind_push_fn)(window_t *dlg, const ctrl_binding_t *binding,
                                 const void *state);
typedef void (*ddx_bind_pull_fn)(window_t *dlg, const ctrl_binding_t *binding,
                                 void *state);

typedef struct ctrl_binding_s {
  uint32_t    ctrl_id; // numeric child control ID
  uint16_t    command; // evCommand notification to listen for (HIWORD(wparam)); 0 = any
  uint32_t    getter;  // control getter message for message-based bindings (edGetText, cbGetCurrentSelection, etc.)
  size_t      offset;  // offsetof(state_t, field)
  size_t      wparam;  // getter message wparam (edGetText: buffer size; cbGetCurrentSelection: default index when selection < 0)
  ddx_bind_push_fn push; // optional push callback (state -> control)
  ddx_bind_pull_fn pull; // optional pull callback (control -> state)
} ctrl_binding_t;

// Built-in DDX callbacks for common scalar/text fields.
void ddx_push_int(window_t *dlg, const ctrl_binding_t *b, const void *state);
void ddx_pull_int(window_t *dlg, const ctrl_binding_t *b, void *state);
void ddx_push_float(window_t *dlg, const ctrl_binding_t *b, const void *state);
void ddx_pull_float(window_t *dlg, const ctrl_binding_t *b, void *state);
void ddx_push_u8(window_t *dlg, const ctrl_binding_t *b, const void *state);
void ddx_pull_u8(window_t *dlg, const ctrl_binding_t *b, void *state);
void ddx_push_text(window_t *dlg, const ctrl_binding_t *b, const void *state);
void ddx_pull_text(window_t *dlg, const ctrl_binding_t *b, void *state);
void ddx_push_combo(window_t *dlg, const ctrl_binding_t *b, const void *state);
void ddx_pull_combo(window_t *dlg, const ctrl_binding_t *b, void *state);
void ddx_push_check(window_t *dlg, const ctrl_binding_t *b, const void *state);
void ddx_pull_check(window_t *dlg, const ctrl_binding_t *b, void *state);

// DDX_TEXT — binds a textedit control; _Generic dispatches push/pull by field type.
//   int field          -> ddx_push_int   / ddx_pull_int
//   float field        -> ddx_push_float / ddx_pull_float
//   unsigned char field-> ddx_push_u8    / ddx_pull_u8
//   char[] / other     -> ddx_push_text  / ddx_pull_text
#define DDX_TEXT(id_, state_type, field) \
  (ctrl_binding_t){ \
    .ctrl_id = (id_), \
    .command = edUpdate, \
    .getter  = 0, \
    .offset  = offsetof(state_type, field), \
    .wparam  = sizeof_field(state_type, field), \
    .push = _Generic((((state_type *)0)->field), \
      int: ddx_push_int, \
      float: ddx_push_float, \
      unsigned char: ddx_push_u8, \
      default: ddx_push_text), \
    .pull = _Generic((((state_type *)0)->field), \
      int: ddx_pull_int, \
      float: ddx_pull_float, \
      unsigned char: ddx_pull_u8, \
      default: ddx_pull_text), \
  }

// DDX_COMBO — binds a combobox control; field must be int (compile error otherwise).
// default_idx is used when combobox has no valid current selection.
#define DDX_COMBO(id_, state_type, field, default_idx) \
  (ctrl_binding_t){ \
    .ctrl_id = (id_), \
    .command = cbSelectionChange, \
    .getter  = 0, \
    .offset  = offsetof(state_type, field), \
    .wparam  = (default_idx), \
    .push = _Generic((((state_type *)0)->field), int: ddx_push_combo), \
    .pull = _Generic((((state_type *)0)->field), int: ddx_pull_combo), \
  }

// DDX_CHECK — binds a checkbox control; field must be bool or int (compile error otherwise).
#define DDX_CHECK(id_, state_type, field) \
  (ctrl_binding_t){ \
    .ctrl_id = (id_), \
    .command = btnClicked, \
    .getter  = 0, \
    .offset  = offsetof(state_type, field), \
    .wparam  = 0, \
    .push = _Generic((((state_type *)0)->field), bool: ddx_push_check, int: ddx_push_check), \
    .pull = _Generic((((state_type *)0)->field), bool: ddx_pull_check, int: ddx_pull_check), \
  }

// Describes one child control in a form definition (analogous to DLGITEMTEMPLATE).
// Controls may themselves contain nested child definitions so that layout
// containers such as stack/grid can be expressed as explicit components.
typedef struct form_ctrl_def_s {
  const char       *class_name; // control class name (e.g. "button")
  uint32_t          id;     // numeric control ID
  isize16_t         size;   // fixed size hint for auto-layout ({w, h}); 0 = measured
  flags_t           flags;  // style flags passed to create_window
  const char       *text;   // initial caption / label text
  const char       *name;   // identifier name (informational)
  uint8_t           h_align; // horizontal alignment; 0 = stretch
  uint8_t           v_align; // vertical alignment; 0 = stretch
  const struct form_ctrl_def_s *children; // nested child controls
  int               child_count; // number of entries in children[]
  uint8_t           layout_spacing; // spacing between direct children; 0 = default
  irect16_t         padding; // inner padding for layout containers
  irect16_t         margin;  // outer margin when this control is laid out by a parent
  uint32_t          parent;  // parent control ID; 0 = form root
  uint8_t           font;    // label font; FONT_SMALL by default
  bool              font_set; // font attribute explicitly set
  uint8_t           color;   // label color palette index; 0 = transparent
  bool              color_set; // color attribute explicitly set
  const void       *lparam;  // custom control creation parameter (e.g. tableview_params_t*)
  const struct menu_item_s *context_menu; // generated declarative menu; not owned
  int               context_menu_count;
} form_ctrl_def_t;

// Describes a complete form (window + children) as a serializable definition
// (analogous to DLGTEMPLATE).  Pass to create_window_from_form() to instantiate.
//
// DDX fields (bindings, binding_count, ok_id, cancel_id) are optional.
// When provided and show_ddx_dialog() is used instead of show_dialog_from_form(),
// no custom window proc is required: controls are populated on create, and the
// OK / Cancel buttons end the dialog automatically.
typedef struct {
  const char             *name;        // window title
  int                     width, height; // client area dimensions
  flags_t                 flags;       // window flags
  window_role_t           role;        // normal, page host, or hosted page
  uint8_t                 layout_spacing; // spacing between direct children; 0 = default
  irect16_t               padding;      // inner padding for auto-layout content
  irect16_t               margin;       // outer margin for this form when nested
  const form_ctrl_def_t  *children;    // array of child control definitions (may be NULL)
  int                     child_count; // number of entries in children[]
  // ── Toolbar fields ───────────────────────────────────────────────────────
  const void             *toolbar_items;  // toolbar_item_t array (may be NULL)
  int                     toolbar_count;  // number of toolbar items
  // ── DDX (Dialog Data Exchange) fields ───────────────────────────────────
  const ctrl_binding_t   *bindings;      // data-exchange table (may be NULL)
  int                     binding_count;   // number of entries in bindings[]
  uint32_t                ok_id;           // child ID of the Accept / OK button
  uint32_t                cancel_id;       // child ID of the Cancel button (0 = none)
  // ── Database binding fields ──────────────────────────────────────────────
  const char             *db_name;       // database instance name (e.g., "db")
  const char             *db_table;      // database table name (e.g., "posts")
  int                     db_table_id;   // TABLE_* enum value
  const void             *db_fields;     // db_field_meta_t array for this table
  int                     db_field_count; // number of fields in db_fields[]
} form_def_t;

// Declarative database API metadata emitted from .orion documents.
// This metadata is model/view-agnostic and can be consumed by applications to
// drive fetch actions and view bindings without hard-coded column setup.
typedef struct {
  const char *name;   // source name, e.g. "feed_posts"
  const char *model;  // logical model name, e.g. "post"
} db_source_def_t;

typedef struct {
  const char *field;  // model field key, e.g. "title"
  const char *title;  // view column title
  int         width;  // preferred column width; <=0 means auto/flex
} db_binding_column_t;

typedef struct {
  const char               *name;   // binding identifier
  const char               *source; // source name this binding reads from
  const char               *view;   // target view/control name
  const db_binding_column_t *columns;
  int                       column_count;
} db_view_binding_t;

typedef enum {
  DB_ACTION_FETCH = 1,
  DB_ACTION_INSERT,
  DB_ACTION_UPDATE,
  DB_ACTION_DELETE,
  DB_ACTION_CUSTOM,
} db_action_kind_t;

typedef struct {
  const char       *name;   // action identifier
  db_action_kind_t  kind;   // fetch/insert/update/delete/custom
  const char       *source; // source name this action targets
  const char       *target; // target view/control or route name
} db_action_def_t;

typedef struct {
  const char *name;   // outlet identifier
  const char *type;   // expected object/control type, if known
  const char *target; // connected view/control name, if known
} db_outlet_def_t;

typedef struct {
  const db_source_def_t  *sources;
  int                     source_count;
  const db_view_binding_t *bindings;
  int                     binding_count;
  const db_action_def_t  *actions;
  int                     action_count;
  const db_outlet_def_t  *outlets;
  int                     outlet_count;
} db_api_def_t;

typedef result_t (*db_object_proc_t)(const void *object, uint32_t msg,
                                     uint32_t wparam, void *lparam);

// Action verbs for database object handlers (Action-Message DDX pattern).
// msg carries the verb; wparam carries packed payload metadata.
typedef enum {
  dbObjGetFieldText = 1,
  dbObjSetFieldText,
} db_object_action_t;

// Field-to-column lookup used by db_object_get_field_text().
// The mapped column_id is packed into LOWORD(wparam).
typedef struct {
  const char *field;
  uint16_t    column_id;
} db_field_msg_binding_t;

const db_source_def_t  *db_api_find_source(const db_api_def_t *api, const char *name);
const db_view_binding_t *db_api_find_binding(const db_api_def_t *api, const char *name);
const db_view_binding_t *db_api_find_binding_for_view(const db_api_def_t *api, const char *view);
const db_action_def_t  *db_api_find_action(const db_api_def_t *api, const char *name);
const db_outlet_def_t  *db_api_find_outlet(const db_api_def_t *api, const char *name);
bool db_object_get_field_text(const db_field_msg_binding_t *bindings, int binding_count,
                              db_object_proc_t proc, const void *object,
                              const char *field, char *buf, size_t buf_sz);

typedef struct {
  uint32_t color_index;   // palette index for label text color; 0 = transparent
  ui_font_t font;         // prepared font role for labels
  bool      color_set;    // whether color_index is explicitly set
} label_create_params_t;

static inline uint32_t label_pack_userdata(uint32_t color_index, ui_font_t font, bool color_set) {
  return (uint32_t)(color_index & 0xffu) |
         ((uint32_t)(font & 0xffu) << 8) |
         (color_set ? (1u << 16) : 0u);
}

// FormEditor component registry metadata/API.
// Runtime window classes and design-time components are registered through this
// API and can be provided by loadable plugins.
#define FE_MAX_COMPONENTS 128

#define FE_COMPONENT_PLACEABLE      0x0001u
#define FE_COMPONENT_SHOW_TOOLBAR   0x0002u

typedef struct {
  const char *class_name;     // stable runtime class key (e.g. "Button")
  const char *name_prefix;    // identifier prefix (e.g. "IDC_BTN")
  const char *toolbar_icon;   // SVG icon name resolved via sysicon_resolve() (NULL = none)
  isize16_t   default_size;   // default size when click-placing
  uint32_t    capabilities;   // FE_COMPONENT_* flags
  winproc_t   proc;           // runtime window proc backing this component
  
  // Window class defaults (used by auto-layout measurement system)
  isize16_t   default_layout_size; // layout defaults: -1 stretch, 0 measure, >0 fixed
  flags_t     default_flags;  // WINDOW_FLEXSPACE, WINDOW_VSCROLL, etc.
  uint8_t     default_h_align;// LAYOUT_ALIGN_STRETCH, etc.
  uint8_t     default_v_align;// LAYOUT_ALIGN_STRETCH, etc.
} fe_component_desc_t;

// Plugin export function pointer types — 3ds Max-style pull model.
// Plugins export these four functions; the loader queries them to register
// descriptors rather than the plugin pushing through a callback API.
typedef int                        (*fe_plugin_class_count_fn)(void);
typedef const fe_component_desc_t *(*fe_plugin_class_desc_fn)(int i);
typedef const char                *(*fe_plugin_description_fn)(void);
typedef uint32_t                   (*fe_plugin_version_fn)(void);
typedef bool                       (*fe_plugin_init_fn)(void);
typedef void                       (*fe_plugin_shutdown_fn)(void);

#define FE_PLUGIN_VERSION 1u

// On Windows, DLL symbols must be explicitly exported; other platforms export
// all global symbols from shared libraries by default.
#if defined(_WIN32)
#  define GEM_EXPORT __declspec(dllexport)
#else
#  define GEM_EXPORT
#endif

// Declares the standard FormEditor plugin exports from a static descriptor
// array, human-readable description, and ABI version value.
#define GEM_CLASSES(ARRAY, NAME, VERSION) \
  GEM_EXPORT int fe_plugin_class_count(void) { \
    return (int)ARRAY_LEN(ARRAY); \
  } \
  GEM_EXPORT const fe_component_desc_t *fe_plugin_class_desc(int i) { \
    if (i < 0 || i >= (int)ARRAY_LEN(ARRAY)) return NULL; \
    return &(ARRAY)[i]; \
  } \
  GEM_EXPORT const char *fe_plugin_description(void) { \
    return (NAME); \
  } \
  GEM_EXPORT uint32_t fe_plugin_version(void) { \
    return (uint32_t)(VERSION); \
  }

int fe_component_count(void);
const fe_component_desc_t *fe_component_at(int index);
const fe_component_desc_t *fe_component_by_id(int id);
int fe_component_id_of(const fe_component_desc_t *desc);
const fe_component_desc_t *fe_component_by_class_name(const char *class_name);
bool fe_component_rejects_parent(const fe_component_desc_t *desc, window_t *target);

bool fe_load_component_plugin(const char *path);
void fe_unload_component_plugins(void);

// Internal state for one built-in scrollbar (horizontal or vertical).
// Two of these live inside window_t when WINDOW_HSCROLL / WINDOW_VSCROLL is set.
typedef struct {
  int  min_val, max_val;   // content range
  int  page, pos;          // viewport size and current scroll position
  bool visible;            // bar is currently drawn (auto show/hide via set_scroll_info)
  bool enabled;            // bar accepts mouse interaction (enable_scroll_bar)
  int8_t visible_mode;     // SB_VIS_AUTO / SB_VIS_HIDE / SB_VIS_SHOW (see user/messages.h)
  bool dragging;           // thumb drag in progress
  int  drag_start_mouse;   // axis coord (window-local) when drag began
  int  drag_mouse;         // accumulated axis coord while dragging
  int  drag_start_pos;     // pos value when drag began
  // Modern overlay-scrollbar state.  Ignored when scrollbar_overlay == false.
  bool     overlay_visible;  // thumb is currently revealed (fading in/visible)
  uint32_t hide_timer_id;    // axSetTimer handle for auto-hide delay; 0 = none
} win_sb_t;

// Window structure
typedef struct {
  bool enabled, free_pan;
  int width, height;
  float pixel_ratio;
  view_matrix_t matrix;
  ipoint16_t pointer, drag_pointer;
} window_view_t;

struct window_s {
  irect16_t frame;
  irect16_t restore_frame;
  irect16_t workspace;
  uint32_t restore_decorations;
  bool workspace_valid;
  bool maximized;
  bool maximizable; // app exposes a restore command when title bar is hidden
  uint32_t id;
  uint64_t editor_id;    // optional design-time stable identity; 0 outside editors
  window_role_t role;
  // Runtime style/state flags share one 32-bit word.
  // WINDOW_*/BUTTON_* use low bits; WINDOW_STATE_* uses high bits.
  uint32_t flags;
  hinstance_t hinstance;  // owning app instance (0 = system/unowned)
  winproc_t proc;
  uint32_t value;
  char title[512];
  char statusbar_text[64];
  uint32_t cursor_pos;
  layout_t layout;
  void *userdata;
  void *userdata2;
  win_sb_t hscroll;   // built-in horizontal scrollbar state (WINDOW_HSCROLL)
  win_sb_t vscroll;   // built-in vertical scrollbar state (WINDOW_VSCROLL)
  window_view_t view; // Transforms this window's content; child frames remain in viewport space.
  struct window_s *next;
  struct window_s *children;
  struct window_s *parent;
  struct window_s *toolbar; // toolbar host window (win_toolbar); state lives in toolbar->userdata
  uint8_t toolbar_dock; // 0 = ordinary child, 1 = top toolbar, 2 = left toolbar
  struct window_s *active_page; // selected page projected by a WINDOW_ROLE_HOST
  struct window_s *page_host; // WINDOW_ROLE_HOST currently projecting this page
  uint32_t surface_fbo; // Offscreen render target for per-window composition.
  uint32_t surface_tex; // Backing texture for the render target.
  int surface_w, surface_h; // Render target size in physical pixels.
  const toolbar_item_t *page_toolbar_items; // declarative page contribution; not owned
  int page_toolbar_count;
  const struct menu_item_s *context_menu; // generated declarative menu; not owned
  int                       context_menu_count;
};

enum { WINDOW_PAINT_CONTENT = 0, WINDOW_PAINT_OVERLAY = 1 };
void window_view_init(window_t *win, int width, int height, float pixel_ratio, bool free_pan);
void window_view_set_size(window_t *win, int width, int height);
float window_view_zoom(const window_t *win);
void window_view_set_zoom(window_t *win, float zoom, const ipoint16_t *content_anchor);
void window_view_center(window_t *win);
void window_view_pan(window_t *win, ipoint16_t delta);
void window_view_begin_drag(window_t *win);
void window_view_drag(window_t *win);
void window_view_set_scroll(window_t *win, int axis, int pos);
int window_view_scroll(const window_t *win, int axis);
frect_t window_view_bounds(const window_t *win);
irect16_t window_view_visible_rect(const window_t *win);
void window_view_apply_gesture(window_t *win, const ax_gesture_t *gesture);
ipoint16_t window_content_to_client(const window_t *win, ipoint16_t point);
ipoint16_t window_client_to_content(const window_t *win, ipoint16_t point);
// Platform/router entry: client coordinates in, content coordinates delivered to the proc.
result_t send_pointer_message(window_t *win, uint32_t msg, uint32_t point, void *lparam);

static inline bool window_has_state(const window_t *win, uint32_t state_flag) {
  return win && ((win->flags & state_flag) != 0u);
}

static inline void window_set_state(window_t *win, uint32_t state_flag, bool enabled) {
  if (!win) return;
  if (enabled)
    win->flags |= state_flag;
  else
    win->flags &= ~state_flag;
}

static inline toolbar_state_t *window_toolbar_state(window_t *win) {
  if (!win || !win->toolbar) return NULL;
  return (toolbar_state_t *)win->toolbar->userdata;
}

// Returns the combined height of the non-client title bar and (if WINDOW_TOOLBAR
// is set) the toolbar band.  Used by event routing and layout.
int titlebar_height(window_t const *win);
int window_caption_height(window_t const *win);
int statusbar_height(window_t const *win);
int window_screen_x(window_t const *win);
int window_screen_y(window_t const *win);

// Window management functions
// Class-based API (preferred): create by registered class name.
window_t *create_window_class(char const *title, flags_t flags, const irect16_t* frame,
                              window_t *parent, const char *class_name,
                              hinstance_t hinstance, void *param);
// Raw proc path: used by dialog/form internals and compatibility migration.
window_t *create_window_proc(char const *title, flags_t flags, const irect16_t* frame,
                             window_t *parent, winproc_t proc,
                             hinstance_t hinstance, void *param);

// Window class registry.
bool register_window_class(const fe_component_desc_t *desc);
int get_num_window_classes(void);
const fe_component_desc_t *get_window_class_at_index(int index);
const fe_component_desc_t *find_window_class_desc_by_proc(winproc_t proc);
bool window_is_class(const window_t *win, const char *class_name);
winproc_t find_window_class_proc(const char *class_name);
const fe_component_desc_t *find_window_class_desc(const char *class_name);
void register_builtin_window_classes(void);

// Query window class defaults.
isize16_t get_class_default_size(const char *class_name);
flags_t  get_class_default_flags(const char *class_name);
uint8_t  get_class_default_h_align(const char *class_name);
uint8_t  get_class_default_v_align(const char *class_name);

#define UI_WNDCLASS(name_sym, proc_sym) \
  ((fe_component_desc_t){ .class_name = (name_sym), .proc = (proc_sym) })

#define UI_CLASS(proc_sym) \
  register_window_class(&(fe_component_desc_t){ .class_name = #proc_sym, .proc = (proc_sym) })

// Migration bridge: `create_window` accepts either a class name string or a
// winproc symbol and dispatches to the appropriate creation function.
#define create_window(title, flags, frame, parent, class_or_proc, hinstance, param) \
  _Generic((class_or_proc), \
    const char *: create_window_class, \
    char *:       create_window_class, \
    default:      create_window_proc \
  )((title), (flags), (frame), (parent), (class_or_proc), (hinstance), (param))

window_t *create_window2(windef_t const *def, irect16_t const *r, window_t *parent);
window_t *create_window_from_form(form_def_t const *def, int x, int y,
                                  window_t *parent, winproc_t proc,
                                  hinstance_t hinstance, void *lparam);
bool set_host_page(window_t *host, window_t *page);
void *allocate_window_data(window_t *win, size_t size);
void show_window(window_t *win, bool visible);
void destroy_window(window_t *win);
void clear_window_children(window_t *win);
void clear_toolbar_children(window_t *win);
bool maximize_window(window_t *win);
bool restore_window(window_t *win);
void update_maximized_window(window_t *win);
void set_application_workspace(window_t *owner, const irect16_t *area);
void move_window(window_t *win, int x, int y);
void resize_window(window_t *win, int new_w, int new_h);
void layout_measure_window(window_t *win, layout_measure_t *m);
void layout_arrange_window(window_t *win, const irect16_t *rect);
void window_layout_sync(window_t *win);
void set_default_window_position(int x, int y);

// Window message functions
intptr_t send_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
void post_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
void invalidate_window(window_t *win);

// Window query functions
window_t *get_window_item(window_t const *win, uint32_t id);
bool is_window(window_t *win);
bool window_in_drag_area(window_t const *win, int sy);
bool window_in_drag_area_at(window_t const *win, int sx, int sy);
window_t *get_root_window(window_t *window);
// Framework-owned desktop root created by UI_INIT_DESKTOP, or NULL when the
// current runtime has no desktop. Desktop icon controls should parent here.
window_t *get_desktop_window(void);
void enable_desktop_window(bool enabled);
void sync_desktop_window(void);
window_t *find_window(int x, int y);
window_t *find_default_button(window_t *win);
bool window_has_focus(const window_t *win);

// Returns a copy of frame_rect centered within the owner window's root window
// frame, or centered on screen when owner is NULL. The input rect is treated as
// a full window-frame rect (top-left of title bar, full width/height), not a
// client rect. The result is clamped to the visible screen bounds.
irect16_t center_window_rect(irect16_t frame_rect, window_t const *owner);

// Returns the client area of win in client coordinates {0, 0, client_w, client_h}.
// The client area excludes the title bar, toolbar, status bar, and any visible
// built-in scrollbar strips.  Analogous to WinAPI GetClientRect.
irect16_t get_client_rect(window_t const *win);

// Adjusts *r (initially a desired client rect) to include the non-client area
// (title bar, toolbar, status bar, and scrollbar strips) for a window with the
// given flags.  Analogous to WinAPI AdjustWindowRectEx.
// After the call, r->x/r->y are the window-top-left offsets (r->y is negative
// when there is a title bar) and r->w/r->h are the total window dimensions.
// WINDOW_HSCROLL adds SCROLLBAR_WIDTH to height unless merged with WINDOW_STATUSBAR.
// WINDOW_VSCROLL adds SCROLLBAR_WIDTH to width.
// Usage:
//   irect16_t r = {0, 0, client_w, client_h};
//   adjust_window_rect(&r, flags);
//   create_window(title, flags, MAKERECT(win_x + r.x, win_y + r.y, r.w, r.h), ...);
void adjust_window_rect(irect16_t *r, flags_t flags);

// Global runtime state shared across UI subsystems.
typedef struct {
  bool      running;
  window_t *windows;
  window_t *focused;
  window_t *tracked;
  window_t *captured;
  window_t *dragging;
  window_t *resizing;
  window_t *toolbar_down_win;
  window_t *modal_overlay_parent;
  int       default_window_x;
  int       default_window_y;
  int       last_mouse_sx;    // last known pointer position in scaled screen pixels
  int       last_mouse_sy;    // (updated on every platform mouse event; used for hover resync)
  window_t *tracked_toolbar;  // toolbar host that last received evMouseMove (for evMouseLeave delivery)
} ui_runtime_state_t;

extern ui_runtime_state_t g_ui_runtime;

// Window utility functions
void set_window_item_text(window_t *win, uint32_t id, const char *fmt, ...);
void load_window_children(window_t *win, windef_t const *def);
void enable_window(window_t *win, bool enable);
void set_focus(window_t* win);
void set_capture(window_t *win);
void track_mouse(window_t *win);
void move_to_top(window_t* win);

// Window hook registration
void register_window_hook(uint32_t msg, winhook_func_t func, void *userdata);
void deregister_window_hook(uint32_t msg, winhook_func_t func, void *userdata);
void remove_from_global_hooks(window_t *win);
void cleanup_all_hooks(void);
void reset_message_queue(void);

// Dialog functions
void end_dialog(window_t *win, uint32_t code);
uint32_t show_dialog_ex(char const *title, int width, int height,
                       window_t *parent, uint32_t flags,
                       winproc_t proc, void *param);
uint32_t show_dialog(char const *title, int width, int height,
                     window_t *parent, winproc_t proc, void *param);
uint32_t show_dialog_from_form_ex(form_def_t const *def, char const *title,
                                  window_t *parent, uint32_t flags,
                                  winproc_t proc, void *param);
uint32_t show_dialog_from_form(form_def_t const *def, char const *title,
                               window_t *parent, winproc_t proc, void *param);

// Show a modal dialog driven entirely by form DDX bindings.
// No custom proc is needed: evCreate pushes state → controls,
// the ok_id button pulls controls → state and ends with code 1,
// the cancel_id button (if set) ends with code 0.
// Pressing Enter in any edit box is equivalent to clicking the OK button.
// Returns the dialog end code (1 = accepted, 0 = cancelled).
uint32_t show_ddx_dialog(form_def_t const *def, const char *title,
                         window_t *parent, void *state);

// Show a modal database-driven dialog (DDX + database integration).
// Fetches a record from the database table on open, pushes fields to controls,
// pulls control values back on OK, and updates/inserts the record.
// If record_id == 0, creates a new record (INSERT); otherwise updates existing (UPDATE).
// The database is looked up automatically from def->db_name via the registry.
// Returns the dialog end code (1 = accepted/saved, 0 = cancelled).
uint32_t show_db_dialog(form_def_t const *def, const char *title,
                        window_t *parent, int record_id);

// Extended version with FK parent ID support (e.g., post_id for comments)
// fk_field: name of FK field to populate (e.g., "post_id")
// fk_value: FK value to set (e.g., 42 for post #42)
uint32_t show_db_dialog_ex(form_def_t const *def, const char *title,
                           window_t *parent, int record_id,
                           const char *fk_field, int fk_value);

// Database registry (NeXTSTEP-style singleton pattern)
// Applications register their database at startup, framework retrieves automatically.
void ui_set_database(database_t *db);
database_t *ui_get_database(void);

// Theme functions (analogous to WinAPI SetSysColors / GetSysColor)
void set_sys_colors(int count, const int *indices, const uint32_t *colors);

// Drawing functions
void draw_button(irect16_t r, int dx, int dy, bool pressed);

// Built-in scrollbar API (analogous to WinAPI SetScrollInfo / GetScrollInfo).
// These operate on the WINDOW_HSCROLL / WINDOW_VSCROLL built-in bars, not on
// win_scrollbar child windows.  bar = SB_HORZ, SB_VERT, or SB_BOTH.
void set_scroll_info(window_t *win, int bar, scroll_info_t const *info, bool redraw);
void get_scroll_info(window_t *win, int bar, scroll_info_t *info);
int  get_scroll_pos(window_t *win, int bar);
void enable_scroll_bar(window_t *win, int bar, bool enable);
// Publish pixel content extent and position; resolve both gutters and relayout.
void set_scroll_content(window_t *win, int width, int height, int x, int y);
void show_scroll_bar(window_t *win, int bar, bool show);
void reset_scroll_bar_auto(window_t *win, int bar);

extern window_t *g_inspector;

// dialog_push: write state fields → controls (call from evCreate).
void dialog_push(window_t *win, const void *state,
                 const ctrl_binding_t *b, int n);

// dialog_pull: read controls → state fields (call in OK handler before accept).
void dialog_pull(window_t *win, void *state,
                 const ctrl_binding_t *b, int n);

// dialog_pull_command: read controls → state for bindings that listen to the
// specified evCommand notification (HIWORD(wparam)).
// Returns number of bindings applied.
int dialog_pull_command(window_t *win, void *state,
                        const ctrl_binding_t *b, int n,
                        uint16_t command);

// ── Tooltip API ───────────────────────────────────────────────────────────────
// Tooltips are shown after a short hover delay for toolbar buttons.
// The tooltip text follows the "Name (Hotkey)" convention used by WinAPI apps.
//
// tooltip_update() is called from event.c on every kEventMouseMoved; callers
// do not need to call it directly.
//
// tooltip_cancel() can be called by any code that needs to hide the tooltip
// immediately (e.g. on button click or window close).

// Update the tooltip for the currently hovered control.
// src_win — the window acting as source (NULL = no tooltip).
// text    — text to show; NULL or "" cancels any pending tooltip.
// sx, sy  — current cursor screen coordinates (used to position the popup).
void tooltip_update(window_t *src_win, const char *text, int sx, int sy);

// Immediately hide any visible tooltip and disarm the pending show-timer.
void tooltip_cancel(void);

#endif
