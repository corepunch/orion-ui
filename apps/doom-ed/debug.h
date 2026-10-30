#ifndef __DOOM_ED_DEBUG_H__
#define __DOOM_ED_DEBUG_H__
#include <stdio.h>
#ifndef DOOM_ED_DEBUG
#define DOOM_ED_DEBUG 0
#endif
#if DOOM_ED_DEBUG
#define doom_ed_debug(...) fprintf(stderr, __VA_ARGS__)
#else
#define doom_ed_debug(...) ((void)0)
#endif

#endif
