#define IMAGEEDITOR_BW 1
#define IMAGEEDITOR_BW_RETINA
#define PENCILTEST_VIDEO_NO_MAIN
#include "tools/penciltest_video.c"
#include "test_framework.h"
#include "test_env.h"
#include "apps/imageeditor/imageeditor.h"

app_state_t *g_app;
int g_bw_retina_scale = 1;

static void video_grayscale(void) {
  TEST("all 256 grayscale levels transfer exactly to coverage and PNG samples");
  pv_video_t v = {.width = 256, .height = 1};
  pv_options_t o = {.mode = PV_PENCIL};
  uint8_t rgb[256 * 3], image[256], pencil[256];
  for (int i = 0; i < 256; i++) memset(rgb + i * 3, i, 3);
  pv_convert(&v, &o, rgb, image, pencil);
  for (int i = 0; i < 256; i++) {
    ASSERT_EQUAL(image[i], i); ASSERT_EQUAL(pencil[i], 255 - i);
  }
  o.mode = PV_BLACK; o.threshold = 128;
  pv_convert(&v, &o, rgb, image, pencil);
  for (int i = 0; i < 256; i++) {
    ASSERT_EQUAL(image[i], i < 128 ? 0 : 255); ASSERT_EQUAL(pencil[i], 0);
  }
  PASS();
}

static void video_arguments(void) {
  TEST("converter rejects invalid modes, palette sizes, thresholds and ignored options");
  pv_options_t o;
  char *good[] = {"converter", "--mode", "palette", "--colors", "64", "--frames", "out", "input.mp4"};
  ASSERT_TRUE(pv_parse(ARRAY_LEN(good), good, &o)); ASSERT_EQUAL(o.colors, 64);
  good[4] = "63"; ASSERT_FALSE(pv_parse(ARRAY_LEN(good), good, &o));
  good[4] = "64"; good[2] = "pencil"; ASSERT_FALSE(pv_parse(ARRAY_LEN(good), good, &o));
  char *bad[] = {"converter", "--mode", "black", "--threshold", "256", "-o", "out.ptf", "input.mp4"};
  ASSERT_FALSE(pv_parse(ARRAY_LEN(bad), bad, &o));
  bad[4] = "128.5"; ASSERT_FALSE(pv_parse(ARRAY_LEN(bad), bad, &o));
  bad[4] = "128"; ASSERT_TRUE(pv_parse(ARRAY_LEN(bad), bad, &o));
  PASS();
}

static void video_global_palette(void) {
  TEST("global palettes respect every budget and keep transparency at index 255");
  const int budgets[] = {32,64,128,256};
  for (int b = 0; b < ARRAY_LEN(budgets); b++) {
    pv_video_t v = {0};
    pv_options_t o = {.mode = PV_PALETTE, .colors = budgets[b]};
    v.histogram = calloc(PV_BINS, sizeof(*v.histogram)); ASSERT_NOT_NULL(v.histogram);
    for (int i = 0; i < PV_BINS; i++) {
      v.histogram[i].count = 1;
      for (int ch = 0; ch < 3; ch++) v.histogram[i].sum[ch] = ((i >> ((2 - ch) * 5)) & 31) * 8;
    }
    pv_palette(&v, &o);
    ASSERT_EQUAL(v.colors, budgets[b] == 256 ? 255 : budgets[b]);
    ASSERT_EQUAL(v.palette[255][3], 0);
    for (int i = 0; i < v.colors; i++) ASSERT_EQUAL(v.palette[i][3], 255);
    for (int i = 0; i < PV_BINS; i++) ASSERT_TRUE(v.lookup[i] < v.colors);
    pv_free(&v);
  }
  PASS();
}

static void video_native_roundtrip(const char *directory, pv_mode_t mode) {
  TEST("converted odd-size two-frame project reopens with native layer data, palette and timing");
  char path[PATH_MAX], frames[PATH_MAX], png_path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/mode-%d.ptf", directory, mode);
  snprintf(frames, sizeof(frames), "%s/mode-%d-frames", directory, mode);
  pv_options_t o = {.output = path, .frames = frames, .mode = mode, .colors = 32, .threshold = 128};
  pv_time_t times[] = {{.delay = 42}, {.pts = 0.042, .delay = 83}};
  uint8_t rgb[] = {0,0,0, 127,127,127, 255,255,255, 255,0,0, 0,255,0, 0,0,255};
  pv_video_t v = {.width = 3, .height = 1, .count = 2, .fps = 24, .times = times};
  v.pixels = tmpfile(); ASSERT_NOT_NULL(v.pixels);
  ASSERT_TRUE(pv_write(v.pixels, rgb, sizeof(rgb)));
  if (mode == PV_PALETTE) {
    v.histogram = calloc(PV_BINS, sizeof(*v.histogram)); ASSERT_NOT_NULL(v.histogram);
    for (int p = 0; p < 6; p++) {
      pv_bin_t *bin = &v.histogram[pv_bin_index(rgb + p * 3)]; bin->count++;
      for (int ch = 0; ch < 3; ch++) bin->sum[ch] += rgb[p * 3 + ch];
    }
  }
  pv_palette(&v, &o);
  ASSERT_TRUE(pv_output(&o, &v));
  int w, h; uint32_t palette[256], bg; bool show;
  pencil_file_layers_t layers;
  anim_timeline_t *tl = flc_load_layers(path, &w, &h, palette, &bg, &show, &layers);
  ASSERT_NOT_NULL(tl); ASSERT_EQUAL(w, 3); ASSERT_EQUAL(h, 1);
  ASSERT_EQUAL(tl->frame_count, 2); ASSERT_EQUAL(tl->fps, 24);
  ASSERT_EQUAL(tl->frames[0]->delay_ms, 42); ASSERT_EQUAL(tl->frames[1]->delay_ms, 83);
  ASSERT_EQUAL(layers.active, mode == PV_PENCIL ? IE_LAYER_PENCIL : IE_LAYER_COLOR);
  ASSERT_EQUAL(layers.visible, 15); ASSERT_TRUE(show); ASSERT_EQUAL(bg, MAKE_COLOR(255,255,255,255));
  ASSERT_EQUAL(memcmp(palette, v.palette, sizeof(palette)), 0);
  ASSERT_EQUAL(layers.background[0], 255);
  for (int i = 0; i < 2; i++) {
    uint8_t expected[3], pencil[3];
    pv_convert(&v, &o, rgb + i * 9, expected, pencil);
    ASSERT_EQUAL(memcmp(tl->frames[i]->data, expected, 3), 0);
    ASSERT_EQUAL(memcmp(tl->frames[i]->cels + 3, pencil, 3), 0);
    ASSERT_EQUAL(tl->frames[i]->cels[6], 255);
    if (mode == PV_PENCIL) { ASSERT_EQUAL(tl->frames[i]->cels[0], 255); }
    else { ASSERT_EQUAL(memcmp(tl->frames[i]->cels, expected, 3), 0); }
    snprintf(png_path, sizeof(png_path), "%s/frame-%06d.png", frames, i + 1);
    FILE *png_file = fopen(png_path, "rb"); ASSERT_NOT_NULL(png_file);
    uint8_t header[26]; ASSERT_EQUAL(fread(header, 1, sizeof(header), png_file), sizeof(header)); fclose(png_file);
    ASSERT_EQUAL(header[24], 8); ASSERT_EQUAL(header[25], mode == PV_PENCIL ? PNG_COLOR_TYPE_GRAY : PNG_COLOR_TYPE_PALETTE);
    png_image png = {.version = PNG_IMAGE_VERSION};
    ASSERT_TRUE(png_image_begin_read_from_file(&png, png_path));
    png.format = mode == PV_PENCIL ? PNG_FORMAT_GRAY : PNG_FORMAT_RGBA;
    uint8_t decoded[12]; ASSERT_TRUE(png_image_finish_read(&png, NULL, decoded, 0, NULL));
    if (mode == PV_PENCIL) { ASSERT_EQUAL(memcmp(decoded, expected, 3), 0); }
    else for (int p = 0; p < 3; p++) {
      ASSERT_EQUAL(decoded[p * 4 + 3], v.palette[expected[p]][3]);
      if (expected[p] != 255) ASSERT_EQUAL(memcmp(decoded + p * 4, v.palette[expected[p]], 4), 0);
    }
    png_image_free(&png); unlink(png_path);
  }
  // A published project must never be overwritten, including on a race at publication.
  o.frames = NULL;
  ASSERT_FALSE(pv_output(&o, &v));
  anim_timeline_t *again = flc_load(path, &w, &h, palette, &bg, &show);
  ASSERT_NOT_NULL(again); anim_timeline_free(again);
  snprintf(png_path, sizeof(png_path), "%s/timing.tsv", frames); unlink(png_path); rmdir(frames);
  free(layers.background); anim_timeline_free(tl);
  fclose(v.pixels); free(v.histogram); unlink(path);
  PASS();
}

static void video_timestamps(void) {
  TEST("fractional and variable source timestamps round cumulatively without drift");
  FILE *log = tmpfile(); ASSERT_NOT_NULL(log);
  fputs("[Parsed_showinfo_0] config in time_base: 1/24000, frame_rate: 24000/1001\n", log);
  for (int i = 0; i < 24; i++) fprintf(log, "[Parsed_showinfo_0] n: %d pts_time:%.9f duration_time:%.9f\n", i, i * 1001.0 / 24000, 1001.0 / 24000);
  pv_video_t v = {.count = 24}; pv_options_t o = {0};
  ASSERT_TRUE(pv_timing(log, &v, &o));
  int sum = 0;
  for (int i = 0; i < 24; i++) sum += v.times[i].delay;
  ASSERT_EQUAL(sum, 1001); free(v.times); fclose(log);
  log = tmpfile(); ASSERT_NOT_NULL(log);
  fputs("[Parsed_showinfo_0] config in time_base: 1/1000, frame_rate: 24/1\n"
        "[Parsed_showinfo_0] n: 0 pts_time:2 duration_time:0.040\n"
        "[Parsed_showinfo_0] n: 1 pts_time:2.040 duration_time:0.110\n"
        "[Parsed_showinfo_0] n: 2 pts_time:2.150 duration_time:0.080\n", log);
  v = (pv_video_t){.count = 3};
  ASSERT_TRUE(pv_timing(log, &v, &o));
  ASSERT_EQUAL(v.times[0].delay, 40); ASSERT_EQUAL(v.times[1].delay, 110); ASSERT_EQUAL(v.times[2].delay, 80);
  free(v.times); fclose(log);
  PASS();
}

static void video_decoder(const char *directory) {
  TEST("decoder handles quoted paths, failures and truncated frames without publishing");
  char decoder[PATH_MAX], input[PATH_MAX];
  snprintf(decoder, sizeof(decoder), "%s/fake ' decoder", directory);
  snprintf(input, sizeof(input), "%s/input ' video.mp4", directory);
  FILE *script = fopen(decoder, "w"); ASSERT_NOT_NULL(script);
  fputs("#!/bin/sh\n"
        "printf 'P6\\n3 1\\n255\\n\\000\\000\\000\\177\\177\\177\\377\\377\\377'\n"
        "printf '[Parsed_showinfo_0] config in time_base: 1/24, frame_rate: 24/1\\n' >&2\n"
        "printf '[Parsed_showinfo_0] n: 0 pts: 0 pts_time:0 duration:1 duration_time:0.0416667\\n' >&2\n", script);
  fclose(script); ASSERT_EQUAL(chmod(decoder, 0700), 0);
  pv_options_t o = {.input = input, .ffmpeg = decoder};
  pv_video_t v = {0};
  ASSERT_TRUE(pv_decode(&o, &v));
  ASSERT_EQUAL(v.width, 3); ASSERT_EQUAL(v.count, 1); ASSERT_EQUAL(v.times[0].delay, 42);
  uint8_t rgb[9]; rewind(v.pixels); ASSERT_EQUAL(fread(rgb, 1, 9, v.pixels), 9);
  ASSERT_EQUAL(rgb[0], 0); ASSERT_EQUAL(rgb[3], 127); ASSERT_EQUAL(rgb[6], 255);
  pv_free(&v);
  script = fopen(decoder, "w"); ASSERT_NOT_NULL(script);
  fputs("#!/bin/sh\nprintf 'P6\\n3 1\\n255\\n\\000'\n", script); fclose(script);
  v = (pv_video_t){0}; ASSERT_FALSE(pv_decode(&o, &v)); pv_free(&v);
  script = fopen(decoder, "w"); ASSERT_NOT_NULL(script);
  fputs("#!/bin/sh\necho 'test decode error' >&2\nexit 1\n", script); fclose(script);
  v = (pv_video_t){0}; ASSERT_FALSE(pv_decode(&o, &v)); pv_free(&v);
  unlink(decoder);
  script = tmpfile(); ASSERT_NOT_NULL(script); pv_usage(script); fclose(script);
  PASS();
}

static void video_external_projects(const char *directory) {
  TEST("full example projects reopen with every frame matching its exported PNG");
  const char *names[] = {"pencil", "black", "palette-64"};
  for (int mode = 0; mode < 3; mode++) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s.ptf", directory, names[mode]);
    int w, h; uint32_t palette[256], bg; bool show;
    pencil_file_layers_t layers;
    anim_timeline_t *tl = flc_load_layers(path, &w, &h, palette, &bg, &show, &layers);
    ASSERT_NOT_NULL(tl);
    size_t n = (size_t)w * h;
    uint8_t *samples = malloc(n); ASSERT_NOT_NULL(samples);
    for (int i = 0; i < tl->frame_count; i++) {
      snprintf(path, sizeof(path), "%s/%s-frames/frame-%06d.png", directory, names[mode], i + 1);
      png_image png = {.version = PNG_IMAGE_VERSION};
      ASSERT_TRUE(png_image_begin_read_from_file(&png, path));
      uint8_t colormap[256 * 4];
      png.format = mode == PV_PENCIL ? PNG_FORMAT_GRAY : PNG_FORMAT_RGBA_COLORMAP;
      ASSERT_TRUE(png_image_finish_read(&png, NULL, samples, 0, colormap));
      png_image_free(&png);
      const uint8_t *cel = tl->frames[i]->cels + (mode == PV_PENCIL ? n : 0);
      if (mode == PV_PENCIL) {
        for (size_t p = 0; p < n; p++) ASSERT_EQUAL(cel[p], 255 - samples[p]);
      } else {
        for (size_t p = 0; p < n; p++) {
          uint8_t *color = colormap + samples[p] * 4;
          uint32_t expected = MAKE_COLOR(color[0], color[1], color[2], color[3]);
          if (!color[3]) { ASSERT_EQUAL(cel[p], 255); }
          else { ASSERT_EQUAL(palette[cel[p]], expected); }
        }
      }
    }
    free(samples); free(layers.background); anim_timeline_free(tl);
  }
  PASS();
}

int main(void) {
  TEST_START("Penciltest video conversion");
  char directory[] = "/tmp/orion-video-test-XXXXXX";
  if (!mkdtemp(directory)) { perror(directory); return 1; }
  video_grayscale(); video_arguments(); video_global_palette(); video_timestamps(); video_decoder(directory);
  video_native_roundtrip(directory, PV_PENCIL);
  video_native_roundtrip(directory, PV_BLACK);
  video_native_roundtrip(directory, PV_PALETTE);
  const char *examples = getenv("PENCILTEST_VIDEO_EXAMPLES");
  if (examples) video_external_projects(examples);
  rmdir(directory);
  TEST_END();
}
