#ifndef __UI_USER_H__
#define __UI_USER_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "messages.h"
#include "../kernel/kernel.h" 

// Forward declarations
typedef struct window_s window_t;
typedef struct irect16_s irect16_t;
typedef uint32_t flags_t;
typedef uint32_t result_t;

// Application instance handle (analogous to WinAPI HINSTANCE).
// Each gem/app process receives a unique hinstance_t when loaded.
// Windows tagged with the same non-zero hinstance belong to the same app.
// hinstance == 0 means system/unowned (shell, framework, tests).
typedef uint32_t hinstance_t;

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

// A fixed-size-tile bitmap strip, analogous to WinAPI HIMAGELIST / TB_ADDBITMAP.
// Icons are indexed 0..N left-to-right then top-to-bottom.
// Used with btnSetImage and tbSetStrip.
typedef struct bitmap_strip_s {
  uint32_t tex;     // OpenGL texture ID of the strip texture
  int      icon_w;  // pixel width of each icon tile
  int      icon_h;  // pixel height of each icon tile
  int      cols;    // number of tile columns in the strip (strip_w / icon_w)
  int      sheet_w; // total texture width in pixels (for UV calculation)
  int      sheet_h; // total texture height in pixels (for UV calculation)
} bitmap_strip_t;

// Window definition structure (for declarative window creation)
typedef struct {
  const char *class_name;
  const char *text;
  uint32_t id;
  int w, h;
  flags_t flags;
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
typedef struct {
  const char       *class_name; // control class name (e.g. "button")
  uint32_t          id;     // numeric control ID
  irect16_t         frame;  // position and dimensions in parent client coordinates
  flags_t           flags;  // style flags passed to create_window
  const char       *text;   // initial caption / label text
  const char       *name;   // identifier name (informational)
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
  const form_ctrl_def_t  *children;    // array of child control definitions (may be NULL)
  int                     child_count; // number of entries in children[]
  // ── DDX (Dialog Data Exchange) fields ───────────────────────────────────
  const ctrl_binding_t   *bindings;      // data-exchange table (may be NULL)
  int                     binding_count;   // number of entries in bindings[]
  uint32_t                ok_id;           // child ID of the Accept / OK button
  uint32_t                cancel_id;       // child ID of the Cancel button (0 = none)
} form_def_t;

// FormEditor component registry metadata/API.
// Runtime window classes and design-time components are registered through this
// API and can be provided by loadable plugins.
#define FE_MAX_COMPONENTS 128

#define FE_COMPONENT_PLACEABLE      0x0001u
#define FE_COMPONENT_SHOW_TOOLBOX   0x0002u

#include "silk.h"

typedef struct {
  const char *class_name;     // stable runtime class key (e.g. "button")
  const char *display_name;   // UI/display caption base (e.g. "Button")
  const char *token;          // stable serialization token (e.g. "button")
  const char *name_prefix;    // identifier prefix (e.g. "IDC_BTN")
  int         toolbox_ident;  // command ID sent by toolbox host
  IconId        toolbox_icon;   // icon index in the Fugue strip
  isize16_t   default_size;   // default size when click-placing
  uint32_t    capabilities;   // FE_COMPONENT_* flags
  winproc_t   proc;           // runtime window proc backing this component
} fe_component_desc_t;

// Plugin export function pointer types — 3ds Max-style pull model.
// Plugins export these four functions; the loader queries them to register
// descriptors rather than the plugin pushing through a callback API.
typedef int                        (*fe_plugin_class_count_fn)(void);
typedef const fe_component_desc_t *(*fe_plugin_class_desc_fn)(int i);
typedef const char                *(*fe_plugin_description_fn)(void);
typedef uint32_t                   (*fe_plugin_version_fn)(void);

#define FE_PLUGIN_VERSION 1u

// Declares the standard FormEditor plugin exports from a static descriptor
// array, human-readable description, and ABI version value.
#define GEM_CLASSES(ARRAY, NAME, VERSION) \
  int fe_plugin_class_count(void) { \
    return (int)ARRAY_LEN(ARRAY); \
  } \
  const fe_component_desc_t *fe_plugin_class_desc(int i) { \
    if (i < 0 || i >= (int)ARRAY_LEN(ARRAY)) return NULL; \
    return &(ARRAY)[i]; \
  } \
  const char *fe_plugin_description(void) { \
    return (NAME); \
  } \
  uint32_t fe_plugin_version(void) { \
    return (uint32_t)(VERSION); \
  }

bool fe_register_component(const fe_component_desc_t *desc);
int fe_component_count(void);
const fe_component_desc_t *fe_component_at(int index);
const fe_component_desc_t *fe_component_by_id(int id);
const fe_component_desc_t *fe_component_by_tool_ident(int ident);
const fe_component_desc_t *fe_component_by_token(const char *token);

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
  int  drag_start_pos;     // pos value when drag began
} win_sb_t;

// Window structure
struct window_s {
  irect16_t frame;
  uint32_t id;
  uint16_t scroll[2];
  uint32_t flags;
  hinstance_t hinstance;  // owning app instance (0 = system/unowned)
  winproc_t proc;
  uint32_t child_id;
  bool hovered;
  bool editing;
  bool pressed;
  bool value;
  bool visible;
  bool disabled;
  char title[512];
  char statusbar_text[64];
  uint32_t cursor_pos;
  window_t *toolbar_children; // real child windows in the toolbar band (toolbar-band-relative frames)
  bitmap_strip_t toolbar_strip;
  uint32_t toolbar_strip_tex;  // GL texture owned by tbLoadStrip (freed on destroy)
  int    toolbar_btn_size;   // 0 = use TB_SPACING default; >0 = custom square button size in pixels
  window_t *sidebar_child;  // WINDOW_SIDEBAR: the single child that fills the left panel
  int       sidebar_width;  // WINDOW_SIDEBAR: width of the sidebar panel (0 = SIDEBAR_DEFAULT_WIDTH)
  void *userdata;
  void *userdata2;
  win_sb_t hscroll;   // built-in horizontal scrollbar state (WINDOW_HSCROLL)
  win_sb_t vscroll;   // built-in vertical scrollbar state (WINDOW_VSCROLL)
  struct window_s *next;
  struct window_s *children;
  struct window_s *parent;
};

// Returns the combined height of the non-client title bar and (if WINDOW_TOOLBAR
// is set) the single-row toolbar band.  Used by event routing and layout.
int titlebar_height(window_t const *win);
int statusbar_height(window_t const *win);

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
bool register_window_class_once(const fe_component_desc_t *desc);
winproc_t find_window_class_proc(const char *class_name);

#define UI_WNDCLASS(name_sym, proc_sym) \
  ((fe_component_desc_t){ .class_name = (name_sym), .proc = (proc_sym) })

#define UI_CLASS(proc_sym) \
  register_window_class_once(&(fe_component_desc_t){ .class_name = #proc_sym, .proc = (proc_sym) })

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
void *allocate_window_data(window_t *win, size_t size);
void show_window(window_t *win, bool visible);
void destroy_window(window_t *win);
void clear_window_children(window_t *win);
void clear_toolbar_children(window_t *win);
void move_window(window_t *win, int x, int y);
void resize_window(window_t *win, int new_w, int new_h);
void set_default_window_position(int x, int y);

// Window message functions
int send_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
void post_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
void invalidate_window(window_t *win);

// Window query functions
window_t *get_window_item(window_t const *win, uint32_t id);
bool is_window(window_t *win);
bool window_in_drag_area(window_t const *win, int sy);
window_t *get_root_window(window_t *window);
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
// Tooltips are shown after a short hover delay for toolbar and toolbox buttons.
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
