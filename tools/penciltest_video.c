#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#define _DARWIN_C_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <ctype.h>
#include <limits.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <png.h>
#include "../apps/imageeditor/io/flc_codec.h"
#include "../apps/imageeditor/palette.h"
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#define PV_LIMIT (512u * 1024u * 1024u)
#define PV_BINS 32768
#define PV_META_SIZE 1084

typedef enum { PV_PENCIL, PV_BLACK, PV_PALETTE } pv_mode_t;
typedef enum { PV_DEFAULT_PALETTE, PV_GENERATED_PALETTE, PV_EXISTING_PALETTE } pv_palette_source_t;
typedef struct { int x, y, w, h; } pv_region_t;
typedef struct {
  const char *input, *output, *frames, *ffmpeg, *python, *filter, *background;
  pv_mode_t mode;
  pv_palette_source_t palette_source;
  const char *palette_path;
  int colors, threshold, outline_window, outline_threshold, width, height, max_frames, region_count;
  double fps, min_width, min_length;
  bool fixed_protection, temporal, sequence, outlines;
  pv_region_t regions[32];
} pv_options_t;
typedef struct { double pts, duration; uint16_t delay; } pv_time_t;
typedef struct { uint64_t count, sum[3]; } pv_bin_t;
typedef struct { int start, end, axis, range; uint64_t count; } pv_box_t;
typedef struct {
  FILE *pixels, *source_pixels;
  int width, height, count, fps, colors, black_index;
  pv_time_t *times;
  pv_bin_t *histogram;
  char (*names)[32];
  uint8_t *background;
  uint8_t palette[256][4], playback_palette[256][4], lookup[PV_BINS];
} pv_video_t;

static bool pv_error(const char *message) {
  fprintf(stderr, "penciltest-video: %s\n", message);
  return false;
}

static bool pv_system_error(const char *path) {
  fprintf(stderr, "penciltest-video: %s: %s\n", path, strerror(errno));
  return false;
}

static void pv_put16(uint8_t *p, uint16_t n) { p[0] = n; p[1] = n >> 8; }
static void pv_put32(uint8_t *p, uint32_t n) { pv_put16(p, n); pv_put16(p + 2, n >> 16); }
static bool pv_write(FILE *fp, const void *p, size_t n) { return fwrite(p, 1, n, fp) == n; }
static uint8_t pv_gray(const uint8_t *rgb) { return (77u * rgb[0] + 150u * rgb[1] + 29u * rgb[2] + 128u) >> 8; }
static int pv_bin_index(const uint8_t *rgb) { return (rgb[0] >> 3) * 1024 + (rgb[1] >> 3) * 32 + (rgb[2] >> 3); }

static bool pv_number(const char *s, double *value) {
  char *end;
  errno = 0;
  *value = strtod(s, &end);
  return s[0] && !*end && !errno && isfinite(*value);
}

static void pv_usage(FILE *fp) {
  fputs("Usage: penciltest-video [options] INPUT.mp4\n"
        "  -o, --output FILE.ptf   Native project (optional when --frames is used)\n"
        "  --mode pencil          Lossless 8-bit luminance -> Pencil coverage (default)\n"
        "  --mode black           Clean black ink -> Color; source grayscale -> visible Pencil\n"
        "  --mode palette         Quantize RGB video -> Color\n"
        "  --palette SOURCE       default (32 colors), generated, or an existing RIFF .pal\n"
        "  --colors N             Generated budget: 32 (default), 64, 128 or 256\n"
        "  --threshold N          Black when luminance < N, 0..255 (default 170)\n"
        "  --outlines             Extract outlines from filled colors; hide source Pencil\n"
        "  --outline-threshold N  Darkness below local mean, 0..255 (default 10); higher = less ink\n"
        "  --outline-window N     Local Gaussian window, odd 3..255 pixels (default 15)\n"
        "  --min-width N          Estimated stroke width, 1..63 pixels (default 3.5)\n"
        "  --min-length N         Remove short isolated strokes, 0..1000000 (default 20)\n"
        "  --protect-region RECT  Preserve source ink in X,Y,W,H; repeat for multiple regions\n"
        "  --fixed-protection     Keep protection rectangles fixed instead of following motion\n"
        "  --temporal             Motion-aligned stroke selection hysteresis\n"
        "  --python PATH          Python with NumPy/OpenCV (default bundled local venv or python3)\n"
        "  --stroke-filter PATH   Override the packaged Python filter\n"
        "  --frames DIRECTORY     Export grayscale / indexed PNGs and timing.tsv\n"
        "  --sequence             INPUT is a TSV: frame name<TAB>PNG path, relative to TSV\n"
        "  --background FILE.png  Shared guide image for pencil projects (same dimensions)\n"
        "  --size WIDTHxHEIGHT    Explicit resize; otherwise preserve source dimensions\n"
        "  --fps RATE             Explicit resampling, 1..1000; otherwise source timing\n"
        "  --max-frames N         Convert only the first N decoded/output frames\n"
        "  --ffmpeg PATH          FFmpeg executable (default $FFMPEG or ffmpeg)\n"
        "  -h, --help             Show help\n"
        "Outputs must not already exist. Audio is omitted. Native timing is in ms.\n", fp);
}

static bool pv_parse(int argc, char **argv, pv_options_t *o) {
  *o = (pv_options_t){.colors = 32, .threshold = 170, .outline_window = 15, .outline_threshold = 10,
                      .min_width = 3.5, .min_length = 20,
                      .ffmpeg = getenv("FFMPEG"), .python = getenv("PENCILTEST_PYTHON")};
  if (!o->ffmpeg || !*o->ffmpeg) o->ffmpeg = "ffmpeg";
  bool colors_set = false, palette_set = false, ink_set = false, outline_set = false, positional = false;
  for (int i = 1; i < argc; i++) {
    const char *arg = argv[i], *v;
    double n;
    if (!positional && !strcmp(arg, "--")) { positional = true; continue; }
    if (!positional && !strcmp(arg, "--sequence")) { o->sequence = true; continue; }
    if (positional || arg[0] != '-') {
      if (o->input) return pv_error("only one input video is supported");
      o->input = arg; continue;
    }
    if (!strcmp(arg, "--fixed-protection")) { o->fixed_protection = true; ink_set = true; continue; }
    if (!strcmp(arg, "--temporal")) { o->temporal = true; ink_set = true; continue; }
    if (!strcmp(arg, "--outlines")) { o->outlines = true; ink_set = true; continue; }
    if (i + 1 == argc) return pv_error("missing option value");
    v = argv[++i];
    if      (!strcmp(arg, "-o") || !strcmp(arg, "--output")) o->output = v;
    else if (!strcmp(arg, "--frames")) o->frames = v;
    else if (!strcmp(arg, "--background")) o->background = v;
    else if (!strcmp(arg, "--ffmpeg")) o->ffmpeg = v;
    else if (!strcmp(arg, "--python")) { o->python = v; ink_set = true; }
    else if (!strcmp(arg, "--stroke-filter")) { o->filter = v; ink_set = true; }
    else if (!strcmp(arg, "--mode")) {
      if      (!strcmp(v, "pencil"))  o->mode = PV_PENCIL;
      else if (!strcmp(v, "black"))   o->mode = PV_BLACK;
      else if (!strcmp(v, "palette")) o->mode = PV_PALETTE;
      else return pv_error("--mode must be pencil, black or palette");
    } else if (!strcmp(arg, "--palette")) {
      palette_set = true;
      if (!strcmp(v, "default")) o->palette_source = PV_DEFAULT_PALETTE;
      else if (!strcmp(v, "generated")) o->palette_source = PV_GENERATED_PALETTE;
      else { o->palette_source = PV_EXISTING_PALETTE; o->palette_path = v; }
    } else if (!strcmp(arg, "--size")) {
      char *end;
      errno = 0;
      long width = strtol(v, &end, 10);
      if (errno || end == v || *end != 'x' || width < 1 || width > 16384)
        return pv_error("--size must be WIDTHxHEIGHT, each 1..16384");
      const char *start = end + 1;
      long height = strtol(start, &end, 10);
      if (errno || end == start || *end || height < 1 || height > 16384)
        return pv_error("--size must be WIDTHxHEIGHT, each 1..16384");
      o->width = (int)width; o->height = (int)height;
    } else if (!strcmp(arg, "--colors")) {
      if (!pv_number(v, &n) || (n != 32 && n != 64 && n != 128 && n != 256))
        return pv_error("--colors must be 32, 64, 128 or 256");
      o->colors = (int)n; colors_set = true;
    } else if (!strcmp(arg, "--threshold")) {
      if (!pv_number(v, &n) || n < 0 || n > 255 || n != floor(n))
        return pv_error("--threshold must be an integer in 0..255");
      o->threshold = (int)n; ink_set = true;
    } else if (!strcmp(arg, "--outline-threshold")) {
      if (!pv_number(v, &n) || n < 0 || n > 255 || n != floor(n))
        return pv_error("--outline-threshold must be an integer in 0..255");
      o->outline_threshold = (int)n; outline_set = true;
    } else if (!strcmp(arg, "--outline-window")) {
      if (!pv_number(v, &n) || n < 3 || n > 255 || n != floor(n) || !((int)n & 1))
        return pv_error("--outline-window must be an odd integer in 3..255");
      o->outline_window = (int)n; outline_set = true;
    } else if (!strcmp(arg, "--min-width")) {
      if (!pv_number(v, &o->min_width) || o->min_width < 1 || o->min_width > 63)
        return pv_error("--min-width must be in 1..63");
      ink_set = true;
    } else if (!strcmp(arg, "--min-length")) {
      if (!pv_number(v, &o->min_length) || o->min_length < 0 || o->min_length > 1000000)
        return pv_error("--min-length must be in 0..1000000");
      ink_set = true;
    } else if (!strcmp(arg, "--protect-region")) {
      if (o->region_count == 32) return pv_error("at most 32 protected regions are supported");
      int values[4];
      const char *p = v;
      for (int k = 0; k < 4; k++) {
        char *end;
        errno = 0;
        long value = strtol(p, &end, 10);
        if (errno || end == p || value < (k < 2 ? 0 : 1) || value > 16384 || *end != (k == 3 ? 0 : ','))
          return pv_error("region must be X,Y,W,H within 0..16384 with positive size");
        values[k] = (int)value; p = end + (k != 3);
      }
      o->regions[o->region_count++] = (pv_region_t){values[0], values[1], values[2], values[3]};
      ink_set = true;
    } else if (!strcmp(arg, "--fps")) {
      if (!pv_number(v, &o->fps) || o->fps < 1 || o->fps > 1000)
        return pv_error("--fps must be in 1..1000");
    } else if (!strcmp(arg, "--max-frames")) {
      if (!pv_number(v, &n) || n < 1 || n > INT_MAX || n != floor(n))
        return pv_error("--max-frames must be a positive integer");
      o->max_frames = (int)n;
    } else return pv_error("unknown option (use --help)");
  }
  if (!o->input || (!o->output && !o->frames)) return pv_error("input and --output and/or --frames are required");
  if (colors_set && !palette_set) o->palette_source = PV_GENERATED_PALETTE;
  if (colors_set && o->palette_source != PV_GENERATED_PALETTE)
    return pv_error("--colors applies only to --palette generated");
  if (ink_set && o->mode != PV_BLACK) return pv_error("ink cleanup options apply only to --mode black");
  if (outline_set && !o->outlines) return pv_error("outline options require --outlines");
  if (o->sequence && o->width) return pv_error("--size is not supported for PNG sequences; prepare equal-sized images");
  if (o->background && (!o->output || o->mode != PV_PENCIL)) return pv_error("--background requires a native pencil project");
  if (o->output && o->frames && !strcmp(o->output, o->frames)) return pv_error("project and frames must have different paths");
  return true;
}

static bool pv_absent(const char *path) {
  struct stat st;
  if (!lstat(path, &st)) { errno = EEXIST; return pv_system_error(path); }
  return errno == ENOENT || pv_system_error(path);
}

static bool pv_token(FILE *fp, char *out, size_t size) {
  int c;
  do {
    c = fgetc(fp);
    if (c == '#') while (c != '\n' && c != EOF) c = fgetc(fp);
  } while (c != EOF && isspace(c));
  size_t n = 0;
  while (c != EOF && !isspace(c)) {
    if (n + 1 == size) return false;
    out[n++] = (char)c; c = fgetc(fp);
  }
  out[n] = 0;
  return n && c != EOF;
}

static bool pv_read_ppm(FILE *fp, pv_video_t *v, uint8_t **rgb) {
  char token[32];
  double w, h;
  if (!pv_token(fp, token, sizeof(token)) || strcmp(token, "P6") ||
      !pv_token(fp, token, sizeof(token)) || !pv_number(token, &w) ||
      !pv_token(fp, token, sizeof(token)) || !pv_number(token, &h) ||
      !pv_token(fp, token, sizeof(token)) || strcmp(token, "255") ||
      w < 1 || h < 1 || w > 16384 || h > 16384 || w != floor(w) || h != floor(h))
    return pv_error("invalid decoded PPM frame");
  if (v->count && (w != v->width || h != v->height)) return pv_error("video dimensions changed during decoding");
  if (!*rgb) {
    v->width = (int)w; v->height = (int)h;
    *rgb = malloc((size_t)v->width * v->height * 3);
    if (!*rgb) return pv_error("cannot allocate decoded frame");
  }
  size_t bytes = (size_t)v->width * v->height * 3;
  return fread(*rgb, 1, bytes, fp) == bytes || pv_error("truncated decoded frame");
}

static bool pv_timing(FILE *log, pv_video_t *v, const pv_options_t *o) {
  v->times = calloc(v->count, sizeof(*v->times));
  if (!v->times) return pv_error("cannot allocate frame timing");
  char line[8192];
  int found = 0;
  double rate = 0, time_base = 0;
  rewind(log);
  while (fgets(line, sizeof(line), log)) {
    if (!strstr(line, "Parsed_showinfo_")) continue;
    char *p = strstr(line, "config in time_base:"), *q;
    int num, den;
    if (p && sscanf(p, "config in time_base: %d/%d", &num, &den) == 2 && num > 0 && den > 0)
      time_base = (double)num / den;
    if (p && (q = strstr(p, "frame_rate:")) && sscanf(q, "frame_rate: %d/%d", &num, &den) == 2 && num > 0 && den > 0)
      rate = (double)num / den;
    p = strstr(line, " n:");
    int index;
    if (!p || sscanf(p, " n: %d", &index) != 1 || index != found) continue;
    char *stamp = strstr(p, " pts_time:");
    if (!stamp) continue;
    if (found == v->count) break; // A limited decode can log one extra filtered frame.
    double pts, duration = 0;
    if (sscanf(stamp, " pts_time:%lf", &pts) != 1 || !isfinite(pts)) return pv_error("invalid source timestamp");
    int64_t ticks;
    q = strstr(p, " pts:");
    if (time_base && q && sscanf(q, " pts: %" SCNd64, &ticks) == 1) pts = ticks * time_base;
    q = strstr(stamp, " duration_time:");
    if (q) sscanf(q, " duration_time:%lf", &duration);
    q = strstr(stamp, " duration:");
    if (time_base && q && sscanf(q, " duration: %" SCNd64, &ticks) == 1 && ticks > 0) duration = ticks * time_base;
    v->times[found++] = (pv_time_t){.pts = pts, .duration = duration};
  }
  if (found != v->count) return pv_error("FFmpeg did not report timing for every decoded frame");
  if (o->fps) rate = o->fps;
  if (!(rate > 0) && v->count > 1) rate = (v->count - 1) / (v->times[v->count - 1].pts - v->times[0].pts);
  if (!(rate > 0) || !isfinite(rate)) return pv_error("unknown source frame rate; specify --fps");
  v->fps = (int)fmax(1, fmin(1000, round(rate)));
  double elapsed_ms = 0;
  int64_t rounded_ms = 0;
  for (int i = 0; i < v->count; i++) {
    double duration = i + 1 < v->count ? v->times[i + 1].pts - v->times[i].pts : v->times[i].duration;
    if (i + 1 == v->count && (!(duration > 0) || !isfinite(duration))) duration = 1 / rate;
    if (!(duration > 0) || !isfinite(duration) || duration > 65.535)
      return pv_error("frame duration cannot be represented by Penciltest (1..65535 ms)");
    elapsed_ms += duration * 1000;
    int64_t end_ms = llround(elapsed_ms), delay = end_ms - rounded_ms;
    if (delay < 1 || delay > 65535) return pv_error("frame duration cannot be represented by Penciltest (1..65535 ms)");
    v->times[i].delay = (uint16_t)delay;
    rounded_ms = end_ms;
  }
  return true;
}

static uint8_t *pv_read_png(const char *path, int *w, int *h) {
  png_image png = {.version = PNG_IMAGE_VERSION};
  if (!png_image_begin_read_from_file(&png, path)) { pv_error(png.message); png_image_free(&png); return NULL; }
  if (!png.width || !png.height || png.width > 16384 || png.height > 16384 ||
      (uint64_t)png.width * png.height * 4 > PV_LIMIT) {
    png_image_free(&png); pv_error("PNG dimensions exceed the working-layer limit"); return NULL;
  }
  png.format = PNG_FORMAT_RGBA;
  size_t n = (size_t)png.width * png.height;
  uint8_t *pixels = malloc(n * 4);
  if (!pixels || !png_image_finish_read(&png, NULL, pixels, 0, NULL)) {
    pv_error(pixels ? png.message : "cannot allocate PNG pixels"); free(pixels); png_image_free(&png); return NULL;
  }
  *w = png.width; *h = png.height;
  for (size_t p = 0; p < n; p++) {
    uint8_t rgba[4]; memcpy(rgba, pixels + p * 4, 4);
    for (int ch = 0; ch < 3; ch++) pixels[p * 3 + ch] = (rgba[ch] * rgba[3] + 255u * (255 - rgba[3]) + 127) / 255;
  }
  png_image_free(&png);
  return pixels;
}

static bool pv_decode_sequence(const pv_options_t *o, pv_video_t *v) {
  FILE *manifest = fopen(o->input, "r");
  if (!manifest) return pv_system_error(o->input);
  v->pixels = tmpfile();
  bool histogram = o->mode == PV_PALETTE || o->palette_source == PV_GENERATED_PALETTE;
  if (histogram) v->histogram = calloc(PV_BINS, sizeof(*v->histogram));
  bool ok = v->pixels && (!histogram || v->histogram);
  double rate = o->fps ? o->fps : 1;
  v->fps = (int)round(rate);
  char *line = NULL; size_t capacity = 0; ssize_t length;
  while (ok && (length = getline(&line, &capacity, manifest)) >= 0) {
    while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) line[--length] = 0;
    if (!length) continue;
    char *path = strchr(line, '\t');
    if (!path || path == line || path - line > 31 || !path[1] || strchr(path + 1, '\t')) {
      ok = pv_error("sequence rows must contain a 1..31 byte name and PNG path separated by one tab"); break;
    }
    *path++ = 0;
    char resolved[PATH_MAX];
    const char *slash = strrchr(o->input, '/');
    int prefix = slash && path[0] != '/' ? (int)(slash - o->input + 1) : 0;
    if (snprintf(resolved, sizeof(resolved), "%.*s%s", prefix, o->input, path) >= (int)sizeof(resolved)) {
      ok = pv_error("sequence PNG path is too long"); break;
    }
    int w, h; uint8_t *rgb = pv_read_png(resolved, &w, &h);
    if (!rgb) { ok = false; break; }
    size_t n = (size_t)w * h;
    if ((v->count && (w != v->width || h != v->height)) || v->count == 65535 || n * 3 > PV_LIMIT) {
      free(rgb); ok = pv_error("sequence dimensions differ or native project limits are exceeded"); break;
    }
    pv_time_t *times = realloc(v->times, (v->count + 1u) * sizeof(*times));
    if (times) v->times = times;
    char (*names)[32] = realloc(v->names, (v->count + 1u) * sizeof(*names));
    if (names) v->names = names;
    int64_t delay = llround((v->count + 1) * 1000.0 / rate) - llround(v->count * 1000.0 / rate);
    if (!times || !names || delay < 1 || delay > 65535) { free(rgb); ok = pv_error("cannot allocate sequence metadata or represent timing"); break; }
    if (v->histogram) for (size_t p = 0; p < n; p++) {
      pv_bin_t *bin = &v->histogram[pv_bin_index(rgb + p * 3)]; bin->count++;
      for (int ch = 0; ch < 3; ch++) bin->sum[ch] += rgb[p * 3 + ch];
    }
    v->width = w; v->height = h;
    v->times[v->count] = (pv_time_t){.pts = v->count / rate, .duration = 1 / rate, .delay = delay};
    memset(v->names[v->count], 0, 32); snprintf(v->names[v->count], 32, "%s", line);
    ok = pv_write(v->pixels, rgb, n * 3); free(rgb);
    if (ok) v->count++;
    if (o->max_frames && v->count == o->max_frames) break;
  }
  if (ferror(manifest)) ok = pv_system_error(o->input);
  free(line); fclose(manifest);
  return (ok && v->count) || pv_error("PNG sequence decoding failed or sequence is empty");
}

static bool pv_decode(const pv_options_t *o, pv_video_t *v) {
  if (o->sequence) return pv_decode_sequence(o, v);
  FILE *log = tmpfile();
  v->pixels = tmpfile();
  bool histogram = o->mode == PV_PALETTE || o->palette_source == PV_GENERATED_PALETTE;
  if (histogram) v->histogram = calloc(PV_BINS, sizeof(*v->histogram));
  if (!log || !v->pixels || (histogram && !v->histogram)) {
    if (log) fclose(log);
    return pv_error("cannot allocate decode spool");
  }
  char filter[256] = "", size[80] = "", fps[80] = "", limit[32];
  if (o->width) snprintf(size, sizeof(size), "scale=%d:%d:flags=lanczos,", o->width, o->height);
  if (o->fps) snprintf(fps, sizeof(fps), "fps=%.9g,", o->fps);
  snprintf(filter, sizeof(filter), "%s%sformat=rgb24,showinfo", size, fps);
  snprintf(limit, sizeof(limit), "%d", o->max_frames);
  char *args[40]; int ac = 0;
#define PV_ARG(s) args[ac++] = (char *)(s)
  PV_ARG(o->ffmpeg); PV_ARG("-hide_banner"); PV_ARG("-nostdin"); PV_ARG("-nostats");
  PV_ARG("-loglevel"); PV_ARG("info"); PV_ARG("-xerror"); PV_ARG("-i"); PV_ARG(o->input);
  PV_ARG("-map"); PV_ARG("0:V:0"); PV_ARG("-an"); PV_ARG("-sn"); PV_ARG("-dn");
  PV_ARG("-vf"); PV_ARG(filter); PV_ARG("-vsync"); PV_ARG("0");
  if (o->max_frames) { PV_ARG("-frames:v"); PV_ARG(limit); }
  PV_ARG("-c:v"); PV_ARG("ppm"); PV_ARG("-f"); PV_ARG("image2pipe"); PV_ARG("pipe:1");
  args[ac] = NULL;
#undef PV_ARG
  int pipefd[2];
  if (pipe(pipefd)) { fclose(log); return pv_system_error("decoder pipe"); }
  pid_t child = fork();
  if (child < 0) { close(pipefd[0]); close(pipefd[1]); fclose(log); return pv_system_error("decoder process"); }
  if (!child) {
    if (dup2(pipefd[1], STDOUT_FILENO) < 0 || dup2(fileno(log), STDERR_FILENO) < 0) _exit(126);
    close(pipefd[0]); close(pipefd[1]);
    execvp(o->ffmpeg, args);
    fprintf(stderr, "Cannot execute FFmpeg '%s': %s\n", o->ffmpeg, strerror(errno));
    _exit(127);
  }
  close(pipefd[1]);
  FILE *stream = fdopen(pipefd[0], "rb");
  uint8_t *rgb = NULL;
  bool ok = stream != NULL;
  if (!stream) close(pipefd[0]);
  while (ok) {
    int c = fgetc(stream);
    if (c == EOF) { if (ferror(stream)) ok = false; break; }
    ungetc(c, stream);
    if (!(ok = pv_read_ppm(stream, v, &rgb))) break;
    uint64_t n = (uint64_t)v->width * v->height;
    if (v->count == INT_MAX || (o->output && (v->count == 65535 || n * 3 > PV_LIMIT))) {
      ok = pv_error("native project exceeds Penciltest's frame-count or working-layer limit; use --size, --fps, --max-frames, or export only --frames");
      break;
    }
    if (v->histogram) for (size_t p = 0; p < n; p++) {
      const uint8_t *pixel = rgb + p * 3;
      pv_bin_t *bin = &v->histogram[pv_bin_index(pixel)];
      bin->count++;
      for (int ch = 0; ch < 3; ch++) bin->sum[ch] += pixel[ch];
    }
    if (!(ok = pv_write(v->pixels, rgb, (size_t)n * 3))) pv_system_error("decoded pixel spool");
    if (ok) v->count++;
  }
  if (stream) fclose(stream);
  if (!ok) kill(child, SIGTERM);
  int status = 0;
  pid_t waited;
  do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
  if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status)) {
    if (ok) {
      pv_error("FFmpeg decoding failed:");
      rewind(log);
      char line[8192];
      while (fgets(line, sizeof(line), log))
        if (!strstr(line, "showinfo") && !strstr(line, "comment         :")) fputs(line, stderr);
    }
    ok = false;
  }
  free(rgb);
  if (ok && !v->count) ok = pv_error("video contains no decoded frames");
  if (ok) ok = pv_timing(log, v, o);
  fclose(log);
  return ok;
}

static int pv_sort_axis;
static int pv_compare_bins(const void *a, const void *b) {
  int x = *(const int *)a, y = *(const int *)b, shift = (2 - pv_sort_axis) * 5;
  int difference = ((x >> shift) & 31) - ((y >> shift) & 31);
  return difference ? difference : x - y;
}

static void pv_measure_box(pv_box_t *box, const int *bins, const pv_bin_t *histogram) {
  int lo[3] = {31,31,31}, hi[3] = {0,0,0};
  box->count = 0;
  for (int i = box->start; i < box->end; i++) {
    int bin = bins[i];
    box->count += histogram[bin].count;
    for (int ch = 0; ch < 3; ch++) {
      int value = (bin >> ((2 - ch) * 5)) & 31;
      if (value < lo[ch]) lo[ch] = value;
      if (value > hi[ch]) hi[ch] = value;
    }
  }
  box->axis = 0; box->range = hi[0] - lo[0];
  for (int ch = 1; ch < 3; ch++) if (hi[ch] - lo[ch] > box->range) {
    box->axis = ch; box->range = hi[ch] - lo[ch];
  }
}

static const char *pv_exe_dir(void) {
  static char path[PATH_MAX];
#ifdef __APPLE__
  uint32_t size = sizeof(path);
  if (_NSGetExecutablePath(path, &size)) return NULL;
#else
  ssize_t size = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (size < 0 || (size_t)size >= sizeof(path) - 1) return NULL;
  path[size] = 0;
#endif
  char *slash = strrchr(path, '/');
  if (!slash) return NULL;
  *slash = 0;
  return path;
}

static bool pv_filter_ink(const pv_options_t *o, pv_video_t *v) {
  if (o->mode != PV_BLACK) return true;
  const char *dir = pv_exe_dir();
  char script[PATH_MAX], python[PATH_MAX], numbers[7][32], rectangles[32][64];
  if (!dir) return pv_error("cannot locate packaged stroke filter");
  if (snprintf(script, sizeof(script), "%s/simplify_lineart.py", dir) >= (int)sizeof(script) ||
      snprintf(python, sizeof(python), "%s/../lineart-python/bin/python3", dir) >= (int)sizeof(python))
    return pv_error("stroke filter path is too long");
  const char *interpreter = o->python && *o->python ? o->python : !access(python, X_OK) ? python : "python3";
  char *args[180]; int count = 0;
  args[count++] = (char *)interpreter; args[count++] = (char *)(o->filter ? o->filter : script);
  args[count++] = "--video-worker";
  snprintf(numbers[0], sizeof(numbers[0]), "%d", v->width);
  snprintf(numbers[1], sizeof(numbers[1]), "%d", v->height);
  snprintf(numbers[2], sizeof(numbers[2]), "%d", o->threshold);
  snprintf(numbers[3], sizeof(numbers[3]), "%.9g", o->min_width);
  snprintf(numbers[4], sizeof(numbers[4]), "%.9g", o->min_length);
  for (int i = 0; i < 5; i++) args[count++] = numbers[i];
  if (o->outlines) {
    snprintf(numbers[5], sizeof(numbers[5]), "%d", o->outline_threshold);
    snprintf(numbers[6], sizeof(numbers[6]), "%d", o->outline_window);
    args[count++] = "--outlines";
    args[count++] = "--outline-threshold"; args[count++] = numbers[5];
    args[count++] = "--outline-window"; args[count++] = numbers[6];
  }
  for (int i = 0; i < o->region_count; i++) {
    pv_region_t r = o->regions[i];
    if (r.x + r.w > v->width || r.y + r.h > v->height) return pv_error("region extends outside decoded video dimensions");
    snprintf(rectangles[i], sizeof(rectangles[i]), "%d,%d,%d,%d", r.x, r.y, r.w, r.h);
    args[count++] = "--protect-region"; args[count++] = rectangles[i];
  }
  if (o->fixed_protection) args[count++] = "--fixed-protection";
  if (o->temporal) args[count++] = "--temporal";
  args[count] = NULL;
  FILE *filtered = tmpfile();
  if (!filtered) return pv_error("cannot allocate stroke-filter spool");
  rewind(v->pixels);
  pid_t child = fork();
  if (child < 0) { fclose(filtered); return pv_system_error("stroke filter fork"); }
  if (!child) {
    if (dup2(fileno(v->pixels), STDIN_FILENO) < 0 || dup2(fileno(filtered), STDOUT_FILENO) < 0) _exit(126);
    close(fileno(v->pixels)); close(fileno(filtered));
    execvp(interpreter, args);
    fprintf(stderr, "penciltest-video: cannot execute Python %s: %s\n", interpreter, strerror(errno));
    _exit(127);
  }
  int status;
  pid_t waited;
  do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
  struct stat st;
  bool ok = waited == child && WIFEXITED(status) && !WEXITSTATUS(status) && !fstat(fileno(filtered), &st) &&
            (uint64_t)st.st_size == (uint64_t)v->width * v->height * v->count * 3;
  if (!ok) { fclose(filtered); return pv_error("stroke filter failed or returned incomplete frames; no outputs published"); }
  if (o->output) v->source_pixels = v->pixels; else fclose(v->pixels);
  v->pixels = filtered; rewind(v->pixels);
  return true;
}

static bool pv_palette(pv_video_t *v, const pv_options_t *o) {
  memset(v->palette, 0, sizeof(v->palette));
  if (o->palette_source == PV_DEFAULT_PALETTE) {
    const char *dir = pv_exe_dir();
    if (!dir || !pencil_palette_load_default(dir, v->palette, &v->colors))
      return pv_error("cannot load the shared default palette");
  } else if (o->palette_source == PV_EXISTING_PALETTE) {
    if (!riff_palette_load(o->palette_path, v->palette, &v->colors)) return false;
    if (v->colors > 255) return pv_error("Penciltest reserves index 255 for transparency; existing palettes must have 1..255 colors");
  } else {
    if (!v->histogram) return pv_error("missing video histogram for generated palette");
    int bins[PV_BINS], count = 0;
    for (int i = 0; i < PV_BINS; i++) if (v->histogram[i].count) bins[count++] = i;
    if (!count) return pv_error("video histogram has no colors");
    pv_box_t boxes[255] = {{.end = count}};
    pv_measure_box(boxes, bins, v->histogram);
    int target = o->colors == 256 ? 255 : o->colors;
    v->colors = 1;
    while (v->colors < target) {
      int best = -1;
      double score = -1;
      for (int i = 0; i < v->colors; i++) if (boxes[i].end - boxes[i].start > 1) {
        double s = (double)boxes[i].count * boxes[i].range;
        if (s > score) { best = i; score = s; }
      }
      if (best < 0) break;
      pv_box_t *box = &boxes[best];
      pv_sort_axis = box->axis;
      qsort(bins + box->start, box->end - box->start, sizeof(*bins), pv_compare_bins);
      uint64_t weight = 0;
      int split = box->start;
      do { weight += v->histogram[bins[split++]].count; }
      while (split < box->end - 1 && weight < box->count / 2);
      boxes[v->colors] = (pv_box_t){.start = split, .end = box->end};
      box->end = split;
      pv_measure_box(box, bins, v->histogram);
      pv_measure_box(&boxes[v->colors++], bins, v->histogram);
    }
    for (int i = 0; i < v->colors; i++) {
      uint64_t sums[3] = {0};
      for (int j = boxes[i].start; j < boxes[i].end; j++)
        for (int ch = 0; ch < 3; ch++) sums[ch] += v->histogram[bins[j]].sum[ch];
      for (int ch = 0; ch < 3; ch++) v->palette[i][ch] = (sums[ch] + boxes[i].count / 2) / boxes[i].count;
      v->palette[i][3] = 255;
    }
  }
  if (o->mode == PV_BLACK) {
    v->black_index = -1;
    for (int i = 0; i < v->colors; i++)
      if (!v->palette[i][0] && !v->palette[i][1] && !v->palette[i][2]) { v->black_index = i; break; }
    if (v->black_index < 0 && o->palette_source == PV_GENERATED_PALETTE) {
      v->black_index = v->colors < (o->colors < 255 ? o->colors : 255) ? v->colors++ : 0;
      memset(v->palette[v->black_index], 0, 3); v->palette[v->black_index][3] = 255;
    }
    if (v->black_index < 0) return pv_error("--mode black requires an RGB black entry in the chosen palette");
  }
  if (v->histogram) {
    for (int bin = 0; bin < PV_BINS; bin++) if (v->histogram[bin].count) {
      int rgb[3];
      for (int ch = 0; ch < 3; ch++) rgb[ch] = (v->histogram[bin].sum[ch] + v->histogram[bin].count / 2) / v->histogram[bin].count;
      int best = 0, distance = INT_MAX;
      for (int i = 0; i < v->colors; i++) {
        int d = 0;
        for (int ch = 0; ch < 3; ch++) { int diff = rgb[ch] - v->palette[i][ch]; d += diff * diff; }
        if (d < distance) { best = i; distance = d; }
      }
      v->lookup[bin] = best;
    }
  }
  memcpy(v->playback_palette, v->palette, sizeof(v->palette));
  if (o->mode == PV_PENCIL) for (int i = 0; i < 256; i++) {
    memset(v->playback_palette[i], i, 3); v->playback_palette[i][3] = 255;
  }
  return true;
}

static void pv_convert(const pv_video_t *v, const pv_options_t *o, const uint8_t *rgb, uint8_t *image, uint8_t *pencil) {
  size_t n = (size_t)v->width * v->height;
  memset(pencil, 0, n);
  for (size_t p = 0; p < n; p++) {
    uint8_t gray = pv_gray(rgb + p * 3);
    if      (o->mode == PV_PENCIL)  { image[p] = gray; pencil[p] = 255 - gray; }
    else if (o->mode == PV_BLACK)   image[p] = gray < o->threshold ? v->black_index : 255;
    else                          image[p] = v->lookup[pv_bin_index(rgb + p * 3)];
  }
}

static bool pv_background(const pv_options_t *o, pv_video_t *v) {
  if (!o->background) return true;
  int w, h; uint8_t *rgb = pv_read_png(o->background, &w, &h);
  if (!rgb) return false;
  if (w != v->width || h != v->height) { free(rgb); return pv_error("background must match frame dimensions"); }
  size_t n = (size_t)w * h;
  v->background = malloc(n);
  if (!v->background) { free(rgb); return pv_error("cannot allocate shared background"); }
  for (size_t p = 0; p < n; p++) {
    const uint8_t *pixel = rgb + p * 3;
    int best = 255, distance = 0;
    for (int ch = 0; ch < 3; ch++) { int d = pixel[ch] - 255; distance += d * d; }
    for (int i = 0; i < v->colors; i++) if (v->palette[i][3] == 255) {
      int d = 0;
      for (int ch = 0; ch < 3; ch++) { int diff = pixel[ch] - v->palette[i][ch]; d += diff * diff; }
      if (d < distance) { best = i; distance = d; }
    }
    v->background[p] = best;
  }
  free(rgb);
  return true;
}

static void pv_composite_background(const pv_video_t *v, uint8_t *image, const uint8_t *pencil) {
  if (!v->background) return;
  for (size_t p = 0, n = (size_t)v->width * v->height; p < n; p++) {
    int gray = v->background[p] == 255 ? 255 : pv_gray(v->palette[v->background[p]]);
    image[p] = (gray * (255 - pencil[p]) + 127) / 255;
  }
}

static bool pv_png(const char *path, const pv_video_t *v, pv_mode_t mode, const uint8_t *pixels) {
  FILE *fp = fopen(path, "wb");
  if (!fp) return pv_system_error(path);
  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  png_infop info = png ? png_create_info_struct(png) : NULL;
  uint8_t *packed = mode == PV_BLACK ? malloc(((size_t)v->width + 7) / 8) : NULL;
  bool ok = false;
  if (png && info && (mode != PV_BLACK || packed) && !setjmp(png_jmpbuf(png))) {
    png_init_io(png, fp);
    png_set_IHDR(png, info, v->width, v->height, mode == PV_BLACK ? 1 : 8, mode == PV_PENCIL ? PNG_COLOR_TYPE_GRAY : PNG_COLOR_TYPE_PALETTE,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_set_compression_level(png, 9);
    png_set_sRGB(png, info, PNG_sRGB_INTENT_PERCEPTUAL);
    if (mode == PV_BLACK) {
      png_color colors[] = {{0,0,0}, {255,255,255}};
      uint8_t alpha[] = {255,0};
      png_set_PLTE(png, info, colors, 2); png_set_tRNS(png, info, alpha, 2, NULL);
    } else if (mode != PV_PENCIL) {
      png_color colors[256]; uint8_t alpha[256];
      for (int i = 0; i < 256; i++) {
        colors[i] = (png_color){v->palette[i][0], v->palette[i][1], v->palette[i][2]};
        alpha[i] = v->palette[i][3];
      }
      colors[255] = (png_color){255,255,255};
      png_set_PLTE(png, info, colors, 256); png_set_tRNS(png, info, alpha, 256, NULL);
    }
    png_write_info(png, info);
    for (int y = 0; y < v->height; y++) {
      const uint8_t *row = pixels + (size_t)y * v->width;
      if (packed) {
        memset(packed, 0, ((size_t)v->width + 7) / 8);
        for (int x = 0; x < v->width; x++) if (row[x] == 255) packed[x / 8] |= 0x80 >> (x % 8);
        row = packed;
      }
      png_write_row(png, row);
    }
    png_write_end(png, info); ok = true;
  }
  if (png) png_destroy_write_struct(&png, info ? &info : NULL);
  free(packed);
  if (fclose(fp)) ok = false;
  return ok || pv_error("PNG write failed");
}

static bool pv_flc_frame(FILE *fp, const pv_video_t *v, const uint8_t *image, int index, uint8_t *previous, bool first) {
  size_t n = (size_t)v->width * v->height;
  size_t size;
  uint8_t *chunk = flc_codec_encode(image, first ? NULL : previous, v->width, v->height, &size);
  if (!chunk) return false;
  uint8_t header[16] = {0}, palette[778] = {0};
  pv_put32(header, 16 + (first ? sizeof(palette) : 0) + size); pv_put16(header + 4, 0xf1fa);
  pv_put16(header + 6, first ? 2 : 1); pv_put16(header + 8, v->times[index].delay);
  pv_put32(palette, sizeof(palette)); pv_put16(palette + 4, 4); pv_put16(palette + 6, 1);
  for (int i = 0; i < 256; i++) for (int ch = 0; ch < 3; ch++)
    palette[10 + i * 3 + ch] = v->playback_palette[i][3] ? v->playback_palette[i][ch] : 255;
  bool ok = pv_write(fp, header, sizeof(header)) && (!first || pv_write(fp, palette, sizeof(palette))) && pv_write(fp, chunk, size);
  if (ok) memcpy(previous, image, n);
  free(chunk);
  return ok;
}

static bool pv_metadata(FILE *fp, const pv_video_t *v) {
  uint8_t header[16] = {0};
  pv_put32(header, 16u + (uint32_t)v->count * PV_META_SIZE);
  pv_put16(header + 4, 0x7074); memcpy(header + 8, "PTA1", 4);
  if (!pv_write(fp, header, sizeof(header))) return false;
  for (int i = 0; i < v->count; i++) {
    uint8_t m[PV_META_SIZE] = {0};
    memcpy(m, "PTF1", 4);
    if (v->names) memcpy(m + 4, v->names[i], 32);
    else snprintf((char *)m + 4, 32, "Frame %d", i + 1);
    pv_put32(m + 36, v->fps); pv_put32(m + 40, v->times[i].delay);
    m[44] = m[45] = 1; pv_put32(m + 48, 0xffffffff);
    memcpy(m + 60, v->palette, sizeof(v->palette));
    if (!pv_write(fp, m, sizeof(m))) return false;
  }
  return true;
}

static bool pv_output(const pv_options_t *o, pv_video_t *v) {
  size_t n = (size_t)v->width * v->height;
  uint8_t *rgb = malloc(n * 3), *image = malloc(n), *pencil = malloc(n), *blank = malloc(n);
  uint8_t *previous = o->output ? malloc(n) : NULL;
  uint8_t project_header[128] = {0};
  char *temp = NULL, *directory = NULL;
  FILE *project = NULL, *layers = NULL, *timing = NULL;
  int png_count = 0;
  bool temp_created = false;
  bool ok = rgb && image && pencil && blank && (!o->output || previous);
  if (!ok) pv_error("cannot allocate conversion frame");
  if (ok && o->output) {
    temp = malloc(strlen(o->output) + 12);
    if (temp) temp[0] = 0;
    layers = tmpfile();
    if (!temp || !layers) ok = pv_error("cannot allocate project spool");
    if (ok) {
      sprintf(temp, "%s.XXXXXX", o->output);
      int fd = mkstemp(temp);
      if (fd < 0) ok = pv_system_error(o->output);
      else {
        temp_created = true;
        if (!(project = fdopen(fd, "w+b"))) { close(fd); ok = pv_system_error(o->output); }
      }
    }
    if (ok) {
      pv_put16(project_header + 4, 0xaf12); pv_put16(project_header + 6, v->count);
      pv_put16(project_header + 8, v->width); pv_put16(project_header + 10, v->height);
      pv_put16(project_header + 12, 8); pv_put16(project_header + 14, 3); pv_put32(project_header + 16, 1000 / v->fps);
      pv_put16(project_header + 38, 1); pv_put16(project_header + 40, 1); pv_put32(project_header + 80, 128);
      memset(blank, 255, n);
      ok = pv_write(project, project_header, sizeof(project_header)) &&
           flc_codec_write(layers, v->background ? v->background : blank, NULL, v->width, v->height);
    }
  }
  if (ok && o->frames) {
    directory = malloc(strlen(o->frames) + 12);
    if (!directory) ok = pv_error("cannot allocate PNG directory path");
    else {
      sprintf(directory, "%s.XXXXXX", o->frames);
      if (!mkdtemp(directory)) { free(directory); directory = NULL; ok = pv_system_error(o->frames); }
    }
    if (ok) {
      char path[PATH_MAX];
      if (snprintf(path, sizeof(path), "%s/timing.tsv", directory) >= (int)sizeof(path)) ok = pv_error("PNG directory path is too long");
      else if (!(timing = fopen(path, "w"))) ok = pv_system_error(path);
      else if (fprintf(timing, "frame\tpts_seconds\tdelay_ms\n") < 0) ok = false;
    }
  }
  rewind(v->pixels);
  if (v->source_pixels) rewind(v->source_pixels);
  for (int i = 0; ok && i < v->count; i++) {
    ok = fread(rgb, 1, n * 3, v->pixels) == n * 3;
    if (!ok) break;
    pv_convert(v, o, rgb, image, pencil);
    pv_composite_background(v, image, pencil);
    if (project) {
      if (v->source_pixels) {
        if (fread(rgb, 1, n * 3, v->source_pixels) != n * 3) { ok = pv_error("truncated source pencil frame"); break; }
        for (size_t p = 0; p < n; p++) pencil[p] = 255 - pv_gray(rgb + p * 3);
      }
      if (i == 1) pv_put32(project_header + 84, ftell(project));
      ok = pv_flc_frame(project, v, image, i, previous, i == 0);
      memset(blank, 255, n);
      const uint8_t *planes[] = {o->mode == PV_PENCIL ? blank : image, pencil, blank};
      for (int layer = 0; ok && layer < 3; layer++) {
        ok = flc_codec_write(layers, planes[layer], NULL, v->width, v->height);
      }
      long stored = ftell(layers);
      if (stored < 0 || (uint64_t)stored + n > PV_LIMIT) ok = pv_error("encoded layer storage exceeds Penciltest's 512 MiB limit");
    }
    if (ok && directory) {
      char path[PATH_MAX];
      if (snprintf(path, sizeof(path), "%s/frame-%06d.png", directory, i + 1) >= (int)sizeof(path)) ok = pv_error("PNG path is too long");
      else { png_count = i + 1; ok = pv_png(path, v, o->mode, image); }
      if (ok) ok = fprintf(timing, "%d\t%.9f\t%u\n", i + 1, v->times[i].pts, v->times[i].delay) > 0;
    }
  }
  if (ok && project) {
    rewind(v->pixels);
    ok = fread(rgb, 1, n * 3, v->pixels) == n * 3;
    pv_convert(v, o, rgb, image, pencil);
    pv_composite_background(v, image, pencil);
    if (v->count == 1) pv_put32(project_header + 84, ftell(project));
    if (ok) ok = pv_flc_frame(project, v, image, 0, previous, false) && pv_metadata(project, v);
    uint8_t header[16] = {0};
    long layer_size = ftell(layers);
    if (layer_size < 0 || (uint64_t)layer_size > 2u * PV_LIMIT) ok = false;
    pv_put32(header, 16 + layer_size); pv_put16(header + 4, 0x7075); memcpy(header + 8, "PTL5", 4);
    header[12] = 4; header[13] = o->mode == PV_PENCIL ? 2 : 1;
    header[14] = o->outlines ? 11 : 15;
    if (ok) ok = pv_write(project, header, sizeof(header));
    if (fflush(layers)) ok = false;
    rewind(layers);
    size_t bytes;
    while (ok && (bytes = fread(rgb, 1, n * 3, layers))) ok = pv_write(project, rgb, bytes);
    long total = ftell(project);
    if (total < 128 || (uint64_t)total > 2u * PV_LIMIT) ok = false;
    pv_put32(project_header, total);
    if (ok) ok = !fseek(project, 0, SEEK_SET) && pv_write(project, project_header, sizeof(project_header));
    if (ferror(layers) || fflush(project) || fsync(fileno(project))) ok = false;
  }
  if (layers) fclose(layers);
  if (project && fclose(project)) ok = false;
  if (timing && fclose(timing)) ok = false;
  // Publish only completed outputs, without replacing an existing project.
  bool linked = false;
  if (ok && o->output) {
    if (link(temp, o->output)) ok = pv_system_error(o->output);
    else linked = true;
  }
  if (ok && directory) {
    ok = pv_absent(o->frames);
    if (ok && rename(directory, o->frames)) ok = pv_system_error(o->frames);
  }
  if (!ok) {
    pv_error("conversion failed; incomplete outputs removed");
    if (linked) unlink(o->output);
    if (directory) {
      char path[PATH_MAX];
      for (int i = 1; i <= png_count; i++) {
        snprintf(path, sizeof(path), "%s/frame-%06d.png", directory, i); unlink(path);
      }
      snprintf(path, sizeof(path), "%s/timing.tsv", directory); unlink(path); rmdir(directory);
    }
  }
  if (temp_created) unlink(temp);
  free(temp); free(directory); free(rgb); free(image); free(pencil); free(blank); free(previous);
  return ok;
}

static void pv_free(pv_video_t *v) {
  if (v->pixels) fclose(v->pixels);
  if (v->source_pixels) fclose(v->source_pixels);
  free(v->times); free(v->histogram); free(v->names); free(v->background);
  *v = (pv_video_t){0};
}

#ifndef PENCILTEST_VIDEO_NO_MAIN
int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { pv_usage(stdout); return 0; }
  pv_options_t o;
  if (!pv_parse(argc, argv, &o)) { pv_usage(stderr); return 2; }
  if ((o.output && !pv_absent(o.output)) || (o.frames && !pv_absent(o.frames))) return 1;
  pv_video_t v = {0};
  bool ok = pv_decode(&o, &v);
  if (ok) ok = pv_filter_ink(&o, &v);
  if (ok) {
    ok = pv_palette(&v, &o);
    if (ok) ok = pv_background(&o, &v);
    if (ok) ok = pv_output(&o, &v);
  }
  if (ok) printf("Converted %d frames, %dx%d, %s%s%s\n", v.count, v.width, v.height,
                 o.output ? o.output : "", o.output && o.frames ? "; " : "", o.frames ? o.frames : "");
  pv_free(&v);
  return ok ? 0 : 1;
}
#endif
