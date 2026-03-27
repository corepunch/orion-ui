#ifndef __UI_COMMCTL_H__
#define __UI_COMMCTL_H__

#include "../user/user.h"
#include "columnview.h"
#include "menubar.h"
#include "filelist.h"

// A fixed-size-tile bitmap strip, analogous to WinAPI HIMAGELIST / TB_ADDBITMAP.
// Icons are indexed 0..N left-to-right then top-to-bottom.
// Used with kButtonMessageSetImage: wparam = icon index (iBitmap); lparam = bitmap_strip_t*.
typedef struct {
  uint32_t tex;     // OpenGL texture ID of the strip texture
  int      icon_w;  // pixel width of each icon tile
  int      icon_h;  // pixel height of each icon tile
  int      cols;    // number of tile columns in the strip (strip_w / icon_w)
  int      sheet_w; // total texture width in pixels (for UV calculation)
  int      sheet_h; // total texture height in pixels (for UV calculation)
} bitmap_strip_t;

// Common control window procedures
result_t win_button(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_toolbar_button(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_checkbox(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_combobox(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_textedit(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_label(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_list(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_console(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_space(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_columnview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_filelist(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_terminal(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_menubar(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

// Terminal API functions
const char* terminal_get_buffer(window_t *win);

// Console API functions
void init_console(void);
void conprintf(const char* format, ...);
void draw_console(void);
void shutdown_console(void);
void toggle_console(void);

#endif
