#ifndef __ORION_PROCESS_H__
#define __ORION_PROCESS_H__

#include <stdbool.h>

typedef struct ui_process ui_process_t;

// Launch an executable with UTF-8 arguments. No shell is involved.
ui_process_t *ui_process_start(const char *const *argv);
// True when the child has exited. A launch or wait failure is not exit code 0.
bool ui_process_poll(ui_process_t *process, int *exit_code);
// Release the handle, terminating and reaping the child if it is still running.
void ui_process_close(ui_process_t *process);

#endif
