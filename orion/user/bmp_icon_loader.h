#ifndef __BMP_ICON_LOADER_H__
#define __BMP_ICON_LOADER_H__

#include <stdbool.h>
#include <stdio.h>

#include "svg_icon_loader.h"

bool bmp_build_strip(const char *icons_dir, const char **bmp_names, int count,
                     int icon_size, int cols, bitmap_strip_t *out,
                     FILE *missing);

#endif