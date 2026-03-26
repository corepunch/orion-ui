#ifndef __UI_COMMCTL_H__
#define __UI_COMMCTL_H__

#include "../user/user.h"
#include "columnview.h"
#include "menubar.h"

// Common control window procedures
result_t win_button(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_checkbox(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_combobox(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_textedit(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_label(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_list(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_console(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_space(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_columnview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
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
