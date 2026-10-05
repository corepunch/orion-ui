// Offline rasterizer: the complete atlas is parsed and rendered in one pass.
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <orion/user/stb_image_write.h>

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "Usage: svg_atlas_render atlas.svg atlas.png\n");
    return 1;
  }
  NSVGimage *image = nsvgParseFromFile(argv[1], "px", 96);
  if (!image || !image->shapes || image->width < 1 || image->height < 1 ||
      image->width > 8192 || image->height > 8192) {
    fprintf(stderr, "[atlas] invalid SVG: %s (expected nonempty, at most 8192x8192)\n", argv[1]);
    nsvgDelete(image);
    return 1;
  }
  int w = (int)ceilf(image->width), h = (int)ceilf(image->height);
  NSVGrasterizer *rasterizer = nsvgCreateRasterizer();
  unsigned char *pixels = calloc((size_t)w * h, 4);
  if (!pixels || !rasterizer) {
    fprintf(stderr, "[atlas] allocation failed size=%dx%d\n", w, h);
    free(pixels);
    nsvgDeleteRasterizer(rasterizer);
    nsvgDelete(image);
    return 1;
  }
  nsvgRasterize(rasterizer, image, 0, 0, 1, pixels, w, h, w * 4);
  int ok = stbi_write_png(argv[2], w, h, 4, pixels, w * 4);
  if (!ok) fprintf(stderr, "[atlas] PNG write failed: %s\n", argv[2]);
  free(pixels);
  nsvgDeleteRasterizer(rasterizer);
  nsvgDelete(image);
  return ok ? 0 : 1;
}
