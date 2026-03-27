#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "image.h"

uint8_t *load_image(const char *path, int *out_w, int *out_h) {
  int channels;
  return stbi_load(path, out_w, out_h, &channels, 4);
}

bool save_image_png(const char *path, const uint8_t *pixels, int w, int h) {
  return stbi_write_png(path, w, h, 4, pixels, w * 4) != 0;
}
