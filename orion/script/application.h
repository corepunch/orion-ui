#ifndef __ORION_SCRIPT_APPLICATION_H__
#define __ORION_SCRIPT_APPLICATION_H__
#include "script.h"
typedef struct script_application_s script_application_t;
script_application_t *script_application_open(const char *manifest, const char *data_directory, char *error, size_t size);
script_application_t *script_application_open_headless(const char *manifest, const char *data_directory, char *error, size_t size);
script_application_t *script_application_open_recovery(const char *manifest, const char *data_directory, char *error, size_t size);
void script_application_close(script_application_t *app);
script_page_t *script_application_main(script_application_t *app);
// The latest open page built from `form`, hosted or in its own window, or NULL.
script_page_t *script_application_page(script_application_t *app, const char *form);
script_runtime_t *script_application_runtime(script_application_t *app);
void script_application_quit_on_close(script_application_t *app, bool quit);
// A standalone application shows the <ribbon> its manifest declares; a gem's
// shell owns the menu row, so the launcher allows it only outside a shell.
// Call before opening. The ribbon window and its shortcuts are what the event
// loop passes to translate_accelerator().
void script_application_allow_ribbon(bool allowed);
window_t *script_application_menu_window(script_application_t *app);
accel_table_t *script_application_accelerators(script_application_t *app);
#endif
