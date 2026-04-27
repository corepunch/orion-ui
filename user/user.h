#ifndef __UI_USER_H__
#define __UI_USER_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "messages.h"
#include "../kernel/kernel.h" 

// Forward declarations
typedef struct window_s window_t;
typedef struct rect_s rect_t;
typedef uint32_t flags_t;
typedef uint32_t result_t;

// Application instance handle (analogous to WinAPI HINSTANCE).
// Each gem/app process receives a unique hinstance_t when loaded.
// Windows tagged with the same non-zero hinstance belong to the same app.
// hinstance == 0 means system/unowned (shell, framework, tests).
typedef uint32_t hinstance_t;

// Window procedure callback type
typedef result_t (*winproc_t)(window_t *, uint32_t, uint32_t, void *);

// Window hook callback type
typedef void (*winhook_func_t)(window_t *win, uint32_t msg, uint32_t wparam, void *lparam, void *userdata);

// Point structure
typedef struct {
  int x, y;
} point_t;

// Float rectangle structure (used for normalized UVs and other float-space rects).
typedef struct {
  float x, y, w, h;
} frect_t;

// Rectangle structure
struct rect_s {
  int x, y, w, h;
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
  winproc_t proc;
  const char *text;
  uint32_t id;
  int w, h;
  flags_t flags;
} windef_t;

// Control type codes used in form_ctrl_def_t (analogous to WinAPI dialog-template atom IDs).
typedef enum {
  FORM_CTRL_BUTTON    = 0,
  FORM_CTRL_CHECKBOX  = 1,
  FORM_CTRL_LABEL     = 2,
  FORM_CTRL_TEXTEDIT  = 3,
  FORM_CTRL_LIST      = 4,
  FORM_CTRL_COMBOBOX  = 5,
  FORM_CTRL_MULTIEDIT = 6,  // multi-line text edit (win_multiedit)
  FORM_CTRL_COUNT     = 7,
} form_ctrl_type_t;

// Describes one child control in a form definition (analogous to DLGITEMTEMPLATE).
typedef struct {
  form_ctrl_type_t  type;   // control class (FORM_CTRL_*)
  uint32_t          id;     // numeric control ID
  rect_t            frame;  // position and dimensions in parent client coordinates
  flags_t           flags;  // style flags passed to create_window
  const char       *text;   // initial caption / label text
  const char       *name;   // identifier name (informational)
} form_ctrl_def_t;

// Describes a complete form (window + children) as a serializable definition
// (analogous to DLGTEMPLATE).  Pass to create_window_from_form() to instantiate.
typedef struct {
  const char             *name;        // window title
  int                     width, height; // client area dimensions
  flags_t                 flags;       // window flags
  const form_ctrl_def_t  *children;    // array of child control definitions (may be NULL)
  int                     child_count; // number of entries in children[]
} form_def_t;

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
  rect_t frame;
  uint32_t id;
  uint16_t scroll[2];
  uint32_t flags;
  hinstance_t hinstance;  // owning app instance (0 = system/unowned)
  winproc_t proc;
  uint32_t child_id;
  bool hovered;
  bool editing;
  bool notabstop;
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
window_t *create_window(char const *title, flags_t flags, const rect_t* frame, 
                        window_t *parent, winproc_t proc, hinstance_t hinstance,
                        void *param);
window_t *create_window2(windef_t const *def, rect_t const *r, window_t *parent);
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
rect_t center_window_rect(rect_t frame_rect, window_t const *owner);

// Returns the client area of win in client coordinates {0, 0, client_w, client_h}.
// The client area excludes the title bar, toolbar, status bar, and any visible
// built-in scrollbar strips.  Analogous to WinAPI GetClientRect.
rect_t get_client_rect(window_t const *win);

// Adjusts *r (initially a desired client rect) to include the non-client area
// (title bar, toolbar, status bar, and scrollbar strips) for a window with the
// given flags.  Analogous to WinAPI AdjustWindowRectEx.
// After the call, r->x/r->y are the window-top-left offsets (r->y is negative
// when there is a title bar) and r->w/r->h are the total window dimensions.
// WINDOW_HSCROLL adds SCROLLBAR_WIDTH to height unless merged with WINDOW_STATUSBAR.
// WINDOW_VSCROLL adds SCROLLBAR_WIDTH to width.
// Usage:
//   rect_t r = {0, 0, client_w, client_h};
//   adjust_window_rect(&r, flags);
//   create_window(title, flags, MAKERECT(win_x + r.x, win_y + r.y, r.w, r.h), ...);
void adjust_window_rect(rect_t *r, flags_t flags);

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

// Theme functions (analogous to WinAPI SetSysColors / GetSysColor)
void set_sys_colors(int count, const int *indices, const uint32_t *colors);

// Drawing functions
void draw_button(rect_t const *r, int dx, int dy, bool pressed);

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

// ── Dialog Data Exchange (DDX) ──────────────────────────────────────────────
// Analogous to MFC DDX / WinAPI dialog-data routines.
// Describe each control-to-field mapping in a static ctrl_binding_t array,
// then call dialog_push() on create and dialog_pull() on accept.

// Returns the number of elements in a statically-sized array.
#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

// Returns sizeof(((type *)0)->field) — the byte size of a struct field.
#define sizeof_field(type, field) ((size_t)(sizeof(((type *)0)->field)))

typedef enum {
  BIND_STRING,    // char[] field: text-edit text ↔ char array (size = sizeof field)
  BIND_INT_COMBO, // int   field: combo-box selection index ↔ int  (size = default index)
  BIND_INT_EDIT,  // int   field: text-edit decimal text    ↔ int  (size = unused)
  BIND_MLSTRING,  // char[] field: multi-line text edit ↔ char array (size = sizeof field)
} bind_type_t;

typedef struct {
  uint32_t    ctrl_id; // numeric child control ID
  bind_type_t type;    // BIND_* transfer type
  size_t      offset;  // offsetof(state_t, field)
  size_t      size;    // BIND_STRING: sizeof char[] field;
                       // BIND_INT_COMBO: default index (used when pull returns < 0)
} ctrl_binding_t;

// dialog_push: write state fields → controls (call from evCreate).
void dialog_push(window_t *win, const void *state,
                 const ctrl_binding_t *b, int n);

// dialog_pull: read controls → state fields (call in OK handler before accept).
void dialog_pull(window_t *win, void *state,
                 const ctrl_binding_t *b, int n);

#endif
