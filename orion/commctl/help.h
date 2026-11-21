#ifndef __ORION_HELP_H__
#define __ORION_HELP_H__

#include <orion/user/user.h>

// Open an offline .ohlp book in an owned, modeless window. A NULL topic opens
// the first topic, or raises the existing viewer and preserves its place.
bool help_open(window_t *owner, const char *path, const char *topic_id);

#endif
