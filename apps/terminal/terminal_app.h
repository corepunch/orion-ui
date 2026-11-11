#ifndef __TERMINAL_APP_H__
#define __TERMINAL_APP_H__

// The Terminal app around TerminalView (components/, the terminal_components plugin).
#include "components/vgat.h"
#include "build/generated/apps/terminal/terminal.h"

result_t terminal_workspace_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
bool terminal_workspace_command(window_t *page, uint32_t command);
window_t *terminal_workspace_terminal(window_t *page, int pane);

#endif
