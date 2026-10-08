#ifndef __PAGE_CHANGES_H__
#define __PAGE_CHANGES_H__

#include "../../gitclient.h"

result_t page_changes_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
bool     page_changes_handle(window_t *main_win, uint32_t msg, uint32_t wparam, void *lparam);
void     page_changes_update_commit(void);

#endif
