#ifndef __PAGE_HISTORY_H__
#define __PAGE_HISTORY_H__

#include "../../gitclient.h"

result_t page_history_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
bool     page_history_handle(window_t *main_win, uint32_t msg, uint32_t wparam, void *lparam);
void     page_history_refresh_sidebar(void);

#endif
