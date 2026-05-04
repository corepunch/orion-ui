// embed_icon_strip.c - pack PNG icons into an embedded RGBA sprite strip.

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../user/stb_image.h"

typedef struct {
  const char *header_path;
  const char *source_path;
  const char *symbol;
  int tile;
  int cols;
  bool mono_white;
  int first_icon_arg;
} options_t;

static void usage(const char *argv0) {
  fprintf(stderr,
          "usage: %s --tile N --cols N --symbol name --header out.h --source out.c [--mono-white] icon.png...\n",
          argv0);
}

static bool parse_int(const char *s, int *out) {
  if (!s || !*s || !out) return false;
  char *end = NULL;
  errno = 0;
  long v = strtol(s, &end, 10);
  if (errno || !end || *end || v <= 0 || v > 4096) return false;
  *out = (int)v;
  return true;
}

static bool valid_symbol(const char *s) {
  if (!s || !*s) return false;
  if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
  for (const char *p = s + 1; *p; p++) {
    if (!(isalnum((unsigned char)*p) || *p == '_')) return false;
  }
  return true;
}

static bool parse_args(int argc, char **argv, options_t *opt) {
  memset(opt, 0, sizeof(*opt));
  opt->cols = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--tile") == 0 && i + 1 < argc) {
      if (!parse_int(argv[++i], &opt->tile)) return false;
    } else if (strcmp(argv[i], "--cols") == 0 && i + 1 < argc) {
      if (!parse_int(argv[++i], &opt->cols)) return false;
    } else if (strcmp(argv[i], "--symbol") == 0 && i + 1 < argc) {
      opt->symbol = argv[++i];
    } else if (strcmp(argv[i], "--header") == 0 && i + 1 < argc) {
      opt->header_path = argv[++i];
    } else if (strcmp(argv[i], "--source") == 0 && i + 1 < argc) {
      opt->source_path = argv[++i];
    } else if (strcmp(argv[i], "--mono-white") == 0) {
      opt->mono_white = true;
    } else {
      opt->first_icon_arg = i;
      break;
    }
  }
  if (opt->first_icon_arg <= 0 || opt->first_icon_arg >= argc) return false;
  if (opt->tile <= 0 || opt->cols <= 0) return false;
  if (!opt->header_path || !opt->source_path || !valid_symbol(opt->symbol)) return false;
  return true;
}

static void downsample_icon(uint8_t *dst, int tile,
                            const uint8_t *src, int sw, int sh,
                            bool mono_white) {
  for (int y = 0; y < tile; y++) {
    int sy0 = (y * sh) / tile;
    int sy1 = ((y + 1) * sh) / tile;
    if (sy1 <= sy0) sy1 = sy0 + 1;
    for (int x = 0; x < tile; x++) {
      int sx0 = (x * sw) / tile;
      int sx1 = ((x + 1) * sw) / tile;
      if (sx1 <= sx0) sx1 = sx0 + 1;

      uint64_t ar = 0, ag = 0, ab = 0, aa = 0;
      int count = 0;
      for (int sy = sy0; sy < sy1 && sy < sh; sy++) {
        for (int sx = sx0; sx < sx1 && sx < sw; sx++) {
          const uint8_t *p = src + ((size_t)sy * sw + sx) * 4;
          uint32_t a = p[3];
          ar += (uint32_t)p[0] * a;
          ag += (uint32_t)p[1] * a;
          ab += (uint32_t)p[2] * a;
          aa += a;
          count++;
        }
      }

      uint8_t *d = dst + ((size_t)y * tile + x) * 4;
      uint32_t a = count > 0 ? (uint32_t)((aa + (uint64_t)count / 2) / (uint64_t)count) : 0;
      if (mono_white) {
        d[0] = 255;
        d[1] = 255;
        d[2] = 255;
      } else if (aa > 0) {
        d[0] = (uint8_t)((ar + aa / 2) / aa);
        d[1] = (uint8_t)((ag + aa / 2) / aa);
        d[2] = (uint8_t)((ab + aa / 2) / aa);
      } else {
        d[0] = d[1] = d[2] = 0;
      }
      d[3] = (uint8_t)a;
    }
  }
}

static bool write_header(const options_t *opt, int w, int h, int count) {
  FILE *f = fopen(opt->header_path, "wb");
  if (!f) return false;
  fprintf(f, "#ifndef %s_H\n", opt->symbol);
  fprintf(f, "#define %s_H\n\n", opt->symbol);
  fprintf(f, "#include <stdint.h>\n\n");
  fprintf(f, "extern const int %s_width;\n", opt->symbol);
  fprintf(f, "extern const int %s_height;\n", opt->symbol);
  fprintf(f, "extern const int %s_tile;\n", opt->symbol);
  fprintf(f, "extern const int %s_count;\n", opt->symbol);
  fprintf(f, "extern const uint8_t %s_rgba[%d];\n\n", opt->symbol, w * h * 4);
  fprintf(f, "#endif\n");
  fclose(f);
  (void)count;
  return true;
}

static bool write_source(const options_t *opt, const uint8_t *pixels,
                         int w, int h, int count) {
  FILE *f = fopen(opt->source_path, "wb");
  if (!f) return false;
  const char *base = strrchr(opt->header_path, '/');
  base = base ? base + 1 : opt->header_path;
  fprintf(f, "#include \"%s\"\n\n", base);
  fprintf(f, "const int %s_width = %d;\n", opt->symbol, w);
  fprintf(f, "const int %s_height = %d;\n", opt->symbol, h);
  fprintf(f, "const int %s_tile = %d;\n", opt->symbol, opt->tile);
  fprintf(f, "const int %s_count = %d;\n\n", opt->symbol, count);
  fprintf(f, "const uint8_t %s_rgba[%d] = {", opt->symbol, w * h * 4);
  for (int i = 0; i < w * h * 4; i++) {
    if ((i % 12) == 0) fprintf(f, "\n ");
    fprintf(f, " 0x%02x,", pixels[i]);
  }
  fprintf(f, "\n};\n");
  fclose(f);
  return true;
}

int main(int argc, char **argv) {
  options_t opt;
  if (!parse_args(argc, argv, &opt)) {
    usage(argv[0]);
    return 2;
  }

  int count = argc - opt.first_icon_arg;
  int rows = (count + opt.cols - 1) / opt.cols;
  int sheet_w = opt.cols * opt.tile;
  int sheet_h = rows * opt.tile;
  uint8_t *sheet = calloc((size_t)sheet_w * sheet_h * 4, 1);
  if (!sheet) return 1;

  for (int i = 0; i < count; i++) {
    const char *path = argv[opt.first_icon_arg + i];
    int sw = 0, sh = 0, sc = 0;
    uint8_t *src = stbi_load(path, &sw, &sh, &sc, 4);
    if (!src) {
      fprintf(stderr, "embed_icon_strip: failed to read %s\n", path);
      free(sheet);
      return 1;
    }
    uint8_t *tmp = malloc((size_t)opt.tile * opt.tile * 4);
    if (!tmp) {
      stbi_image_free(src);
      free(sheet);
      return 1;
    }
    downsample_icon(tmp, opt.tile, src, sw, sh, opt.mono_white);
    stbi_image_free(src);

    int dx = (i % opt.cols) * opt.tile;
    int dy = (i / opt.cols) * opt.tile;
    for (int y = 0; y < opt.tile; y++) {
      memcpy(sheet + ((size_t)(dy + y) * sheet_w + dx) * 4,
             tmp + (size_t)y * opt.tile * 4,
             (size_t)opt.tile * 4);
    }
    free(tmp);
  }

  bool ok = write_header(&opt, sheet_w, sheet_h, count) &&
            write_source(&opt, sheet, sheet_w, sheet_h, count);
  free(sheet);
  return ok ? 0 : 1;
}
