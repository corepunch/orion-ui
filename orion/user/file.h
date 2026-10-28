#ifndef __FILE_REPLACE_H__
#define __FILE_REPLACE_H__
#include <stdbool.h>

// Publish a completed file, replacing an existing destination without deleting it first.
// Both paths are UTF-8. Source and destination must be on the same filesystem.
bool file_replace(const char *source, const char *destination);
#endif
