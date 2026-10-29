#ifndef __ORION_SCRIPT_APPLICATION_H__
#define __ORION_SCRIPT_APPLICATION_H__
#include "script.h"
typedef struct script_application_s script_application_t;
script_application_t *script_application_open(const char *manifest, const char *data_directory, char *error, size_t size);
void script_application_close(script_application_t *app);
script_page_t *script_application_main(script_application_t *app);
script_runtime_t *script_application_runtime(script_application_t *app);
void script_application_quit_on_close(script_application_t *app, bool quit);
#endif
