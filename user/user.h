#ifndef __UI_USER_H__
#define __UI_USER_H__

#include <stdint.h>
#include <stdbool.h>

#include "messages.h"
#include "../kernel/kernel.h" 

// Forward declarations
typedef struct window_s window_t;
typedef struct rect_s rect_t;
typedef uint32_t flags_t;
typedef uint32_t result_t;

// Window procedure callback type
typedef result_t (*winproc_t)(window_t *, uint32_t, uint32_t, void *);

// Window hook callback type
typedef void (*winhook_func_t)(window_t *win, uint32_t msg, uint32_t wparam, void *lparam, void *userdata);

// Point structure
typedef struct {
  int x, y;
} point_t;

// Rectangle structure
struct rect_s {
  int x, y, w, h;
};

// A fixed-size-tile bitmap strip, analogous to WinAPI HIMAGELIST / TB_ADDBITMAP.
// Icons are indexed 0..N left-to-right then top-to-bottom.
// Used with kButtonMessageSetImage and kToolBarMessageSetStrip.
typedef struct {
  uint32_t tex;     // OpenGL texture ID of the strip texture
  int      icon_w;  // pixel width of each icon tile
  int      icon_h;  // pixel height of each icon tile
  int      cols;    // number of tile columns in the strip (strip_w / icon_w)
  int      sheet_w; // total texture width in pixels (for UV calculation)
  int      sheet_h; // total texture height in pixels (for UV calculation)
} bitmap_strip_t;

// Toolbar button structure
typedef struct toolbar_button_s {
  int icon;
  int ident;
  bool active;
} toolbar_button_t;

// Window definition structure (for declarative window creation)
typedef struct {
  winproc_t proc;
  const char *text;
  uint32_t id;
  int w, h;
  flags_t flags;
} windef_t;

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
  winproc_t proc;
  uint32_t child_id;
  bool hovered;
  bool editing;
  bool notabstop;
  bool pressed;
  bool value;
  bool visible;
  bool disabled;
  char title[64];
  char statusbar_text[64];
  uint32_t cursor_pos;
  uint32_t num_toolbar_buttons;
  toolbar_button_t *toolbar_buttons;
  bitmap_strip_t toolbar_strip;
  void *userdata;
  void *userdata2;
  win_sb_t hscroll;   // built-in horizontal scrollbar state (WINDOW_HSCROLL)
  win_sb_t vscroll;   // built-in vertical scrollbar state (WINDOW_VSCROLL)
  struct window_s *next;
  struct window_s *children;
  struct window_s *parent;
};

// Window management functions
window_t *create_window(char const *title, flags_t flags, const rect_t* frame, 
                        window_t *parent, winproc_t proc, void *param);
window_t *create_window2(windef_t const *def, rect_t const *r, window_t *parent);
void *allocate_window_data(window_t *win, size_t size);
void show_window(window_t *win, bool visible);
void destroy_window(window_t *win);
void clear_window_children(window_t *win);
void move_window(window_t *win, int x, int y);
void resize_window(window_t *win, int new_w, int new_h);

// Window message functions
int send_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
void post_message(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
void invalidate_window(window_t *win);

// Window query functions
window_t *get_window_item(window_t const *win, uint32_t id);
bool is_window(window_t *win);
int window_title_bar_y(window_t const *win);
window_t *get_root_window(window_t *window);
window_t *find_window(int x, int y);
window_t *find_default_button(window_t *win);

// Global window focus/tracking state
extern window_t *_focused;
extern window_t *_tracked;
extern window_t *_captured;

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

// Dialog functions
void end_dialog(window_t *win, uint32_t code);
uint32_t show_dialog(char const *title, const rect_t* frame, window_t *parent, 
                     winproc_t proc, void *param);

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

// Global window list
extern window_t *windows;
extern window_t *g_inspector;

#endif
