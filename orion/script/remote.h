#ifndef __SCRIPT_REMOTE_H__
#define __SCRIPT_REMOTE_H__
#include <stdbool.h>
#include <stddef.h>
// Native host only. URL and bearer token are read from the process environment.
bool script_remote_configure_from_environment(char *error, size_t size);
bool script_remote_manifest(const char *local, char *remote, size_t size);
#endif
