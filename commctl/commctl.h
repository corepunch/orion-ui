#ifndef __UI_COMMCTL_H__
#define __UI_COMMCTL_H__

#include "../user/user.h"
#include "columnview.h"
#include "menubar.h"
#include "filelist.h"
#include "filepicker.h"
#include "msgbox.h"

// bitmap_strip_t is defined in user/user.h and available via the include above.
// Kept here as a comment for documentation purposes:
// A fixed-size-tile bitmap strip used with kButtonMessageSetImage (wparam=index, lparam=bitmap_strip_t*).

// Scrollbar info structure (WinAPI SCROLLINFO analogue).
// Used with kScrollBarMessageSetInfo.
typedef struct {
  int min_val; // minimum scroll position
  int max_val; // maximum scroll position (content size)
  int page;    // visible page size (viewport dimension)
  int pos;     // current scroll position
} scrollbar_info_t;

// Common control window procedures
result_t win_button(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_toolbar_button(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_checkbox(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_columnview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_combobox(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_textedit(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_multiedit(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_label(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_image(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_list(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_console(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_space(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_columnview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_filelist(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_terminal(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_menubar(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_scrollbar(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

// Terminal API functions
const char* terminal_get_buffer(window_t *win);

// Console API functions
void init_console(void);
void conprintf(const char* format, ...);
void draw_console(void);
void shutdown_console(void);
void toggle_console(void);

#endif
