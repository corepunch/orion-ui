#define IMAGEEDITOR_BW 1
#define IMAGEEDITOR_BW_RETINA
#define PENCILTEST_VIDEO_NO_MAIN
#include "tools/penciltest_video.c"
#include "test_framework.h"
#include "test_env.h"
#include "apps/imageeditor/imageeditor.h"
#include "anim_test_helpers.h"

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
  o.threshold = 170; o.min_width = 1;
  pv_convert(&v, &o, rgb, image, pencil);
  for (int i = 0; i < 256; i++) ASSERT_EQUAL(image[i], i < 170 ? 0 : 255);
  PASS();
}

static void video_ink_cleanup(const char *directory) {
  TEST("stroke worker preserves complete frames and rejects failures, truncation and invalid regions");
  char python[PATH_MAX]; snprintf(python, sizeof(python), "%s/fake ' python", directory);
  FILE *script = fopen(python, "w"); ASSERT_NOT_NULL(script);
  fputs("#!/bin/sh\ncat\n", script); fclose(script); ASSERT_EQUAL(chmod(python, 0700), 0);
  pv_options_t o = {.mode = PV_BLACK, .python = python, .filter = "filter ' path.py", .threshold = 170, .min_width = 3.5, .min_length = 20};
  pv_video_t v = {.width = 3, .height = 1, .count = 1};
  uint8_t rgb[] = {0,0,0, 127,127,127, 255,255,255}, actual[9];
  v.pixels = tmpfile(); ASSERT_NOT_NULL(v.pixels);
  ASSERT_TRUE(pv_write(v.pixels, rgb, sizeof(rgb))); ASSERT_TRUE(pv_filter_ink(&o, &v));
  ASSERT_EQUAL(fread(actual, 1, 9, v.pixels), 9); ASSERT_EQUAL(memcmp(rgb, actual, 9), 0);
  script = fopen(python, "w"); ASSERT_NOT_NULL(script); fputs("#!/bin/sh\nprintf x\n", script); fclose(script);
  ASSERT_FALSE(pv_filter_ink(&o, &v));
  script = fopen(python, "w"); ASSERT_NOT_NULL(script); fputs("#!/bin/sh\nexit 1\n", script); fclose(script);
  ASSERT_FALSE(pv_filter_ink(&o, &v));
  o.region_count = 1; o.regions[0] = (pv_region_t){2,0,2,1};
  ASSERT_FALSE(pv_filter_ink(&o, &v));
  fclose(v.pixels); unlink(python);
  PASS();
}

static void video_arguments(void) {
  TEST("converter rejects invalid modes, palette sizes, thresholds and ignored options");
  pv_options_t o;
  char *good[] = {"converter", "--mode", "palette", "--colors", "64", "--frames", "out", "input.mp4"};
  ASSERT_TRUE(pv_parse(ARRAY_LEN(good), good, &o)); ASSERT_EQUAL(o.colors, 64);
  good[4] = "63"; ASSERT_FALSE(pv_parse(ARRAY_LEN(good), good, &o));
  good[4] = "64"; good[2] = "pencil"; ASSERT_TRUE(pv_parse(ARRAY_LEN(good), good, &o));
  char *bad[] = {"converter", "--mode", "black", "--threshold", "256", "-o", "out.ptf", "input.mp4"};
  ASSERT_FALSE(pv_parse(ARRAY_LEN(bad), bad, &o));
  bad[4] = "128.5"; ASSERT_FALSE(pv_parse(ARRAY_LEN(bad), bad, &o));
  bad[4] = "128"; ASSERT_TRUE(pv_parse(ARRAY_LEN(bad), bad, &o));
  ASSERT_TRUE(o.min_width == 3.5); ASSERT_EQUAL(o.min_length, 20);
  ASSERT_EQUAL(o.threshold, 128);
  bad[3] = "--min-width"; bad[4] = "1"; ASSERT_TRUE(pv_parse(ARRAY_LEN(bad), bad, &o));
  bad[4] = "0"; ASSERT_FALSE(pv_parse(ARRAY_LEN(bad), bad, &o));
  bad[4] = "64"; ASSERT_FALSE(pv_parse(ARRAY_LEN(bad), bad, &o));
  bad[4] = "1.5"; ASSERT_TRUE(pv_parse(ARRAY_LEN(bad), bad, &o));
  bad[4] = "2"; bad[2] = "pencil"; ASSERT_FALSE(pv_parse(ARRAY_LEN(bad), bad, &o));
  char *pal[] = {"converter", "--palette", "default", "--colors", "32", "--frames", "out", "input.mp4"};
  ASSERT_FALSE(pv_parse(ARRAY_LEN(pal), pal, &o));
  pal[2] = "generated"; ASSERT_TRUE(pv_parse(ARRAY_LEN(pal), pal, &o));
  ASSERT_EQUAL(o.palette_source, PV_GENERATED_PALETTE);
  char *existing[] = {"converter", "--palette", "colors.pal", "--frames", "out", "input.mp4"};
  ASSERT_TRUE(pv_parse(ARRAY_LEN(existing), existing, &o));
  ASSERT_EQUAL(o.palette_source, PV_EXISTING_PALETTE); ASSERT_STR_EQUAL(o.palette_path, "colors.pal");
  char *sequence[] = {"converter", "--sequence", "--size", "10x10", "-o", "out.ptf", "frames.tsv"};
  ASSERT_FALSE(pv_parse(ARRAY_LEN(sequence), sequence, &o));
  sequence[2] = "--background"; sequence[3] = "guides.png";
  ASSERT_TRUE(pv_parse(ARRAY_LEN(sequence), sequence, &o)); ASSERT_TRUE(o.sequence);
  sequence[4] = "--frames"; ASSERT_FALSE(pv_parse(ARRAY_LEN(sequence), sequence, &o));
  char *outlines[] = {"converter", "--mode", "black", "--outlines", "--outline-threshold", "10",
                      "--outline-window", "15", "-o", "out.ptf", "input.mp4"};
  ASSERT_TRUE(pv_parse(ARRAY_LEN(outlines), outlines, &o)); ASSERT_TRUE(o.outlines);
  ASSERT_EQUAL(o.outline_threshold, 10); ASSERT_EQUAL(o.outline_window, 15);
  outlines[5] = "256"; ASSERT_FALSE(pv_parse(ARRAY_LEN(outlines), outlines, &o));
  outlines[5] = "10.5"; ASSERT_FALSE(pv_parse(ARRAY_LEN(outlines), outlines, &o));
  outlines[5] = "10"; outlines[7] = "14"; ASSERT_FALSE(pv_parse(ARRAY_LEN(outlines), outlines, &o));
  outlines[7] = "15"; outlines[2] = "pencil"; ASSERT_FALSE(pv_parse(ARRAY_LEN(outlines), outlines, &o));
  outlines[2] = "black"; outlines[3] = "--temporal"; ASSERT_FALSE(pv_parse(ARRAY_LEN(outlines), outlines, &o));
  PASS();
}

static void video_source_pencil(const char *directory) {
  TEST("black projects retain source Pencil coverage and timing; outline projects hide the reference");
  char path[PATH_MAX], filtered_path[PATH_MAX], python[PATH_MAX], failed_path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/source-pencil.ptf", directory);
  snprintf(filtered_path, sizeof(filtered_path), "%s/filtered.rgb", directory);
  snprintf(python, sizeof(python), "%s/source-python", directory);
  snprintf(failed_path, sizeof(failed_path), "%s/truncated-pencil.ptf", directory);
  uint8_t source[2 * 257 * 3], filtered[sizeof(source)];
  for (int frame = 0; frame < 2; frame++) for (int p = 0; p < 257; p++) {
    int index = (frame * 257 + p) * 3;
    memset(source + index, frame ? 255 - (p & 255) : (p & 255), 3);
    memset(filtered + index, p % 2 ? 255 : 0, 3);
  }
  source[256 * 3] = 255; source[256 * 3 + 1] = source[256 * 3 + 2] = 0;
  source[(257 + 256) * 3] = source[(257 + 256) * 3 + 1] = 0; source[(257 + 256) * 3 + 2] = 255;
  FILE *fp = fopen(filtered_path, "wb"); ASSERT_NOT_NULL(fp);
  ASSERT_TRUE(pv_write(fp, filtered, sizeof(filtered))); fclose(fp);
  fp = fopen(python, "w"); ASSERT_NOT_NULL(fp);
  fprintf(fp, "#!/bin/sh\ncat '%s'\n", filtered_path); fclose(fp); ASSERT_EQUAL(chmod(python, 0700), 0);
  pv_options_t o = {.output = path, .mode = PV_BLACK, .python = python, .threshold = 170,
                    .min_width = 3.5, .min_length = 20, .temporal = true};
  pv_video_t v = {.width = 257, .height = 1, .count = 2, .fps = 24};
  v.pixels = tmpfile(); v.times = calloc(2, sizeof(*v.times));
  ASSERT_NOT_NULL(v.pixels); ASSERT_NOT_NULL(v.times);
  v.times[0].delay = 42; v.times[1].delay = 83;
  ASSERT_TRUE(pv_write(v.pixels, source, sizeof(source)));
  ASSERT_TRUE(pv_filter_ink(&o, &v)); ASSERT_NOT_NULL(v.source_pixels); ASSERT_TRUE(v.pixels != v.source_pixels);
  ASSERT_TRUE(pv_palette(&v, &o));
  for (int outlines = 0; outlines < 2; outlines++) {
    o.outlines = outlines;
    ASSERT_TRUE(pv_output(&o, &v));
    int w, h; uint32_t palette[256], bg; bool show;
    pencil_file_layers_t layers;
    anim_timeline_t *tl = flc_load_layers(path, &w, &h, palette, &bg, &show, &layers);
    ASSERT_NOT_NULL(tl); ASSERT_EQUAL(w, 257); ASSERT_EQUAL(h, 1); ASSERT_EQUAL(tl->frame_count, 2);
    ASSERT_EQUAL(layers.active, IE_LAYER_COLOR); ASSERT_EQUAL(layers.visible, outlines ? 11 : 15);
    for (int frame = 0; frame < 2; frame++) {
      uint8_t pencil[257], ink[257];
      for (int p = 0; p < 257; p++) { pencil[p] = frame ? p : 255 - p; ink[p] = p % 2 ? 255 : v.black_index; }
      pencil[256] = frame ? 226 : 178;
      ASSERT_TRUE(test_anim_cel_matches(tl->frames[frame], 0, ink, w, h));
      ASSERT_TRUE(test_anim_cel_matches(tl->frames[frame], 1, pencil, w, h));
      ASSERT_EQUAL(tl->frames[frame]->delay_ms, frame ? 83 : 42);
    }
    free(layers.background); anim_timeline_free(tl); unlink(path);
  }
  o.output = failed_path;
  ASSERT_EQUAL(ftruncate(fileno(v.source_pixels), sizeof(source) - 1), 0);
  ASSERT_FALSE(pv_output(&o, &v)); ASSERT_TRUE(access(failed_path, F_OK) != 0);
  pv_free(&v);
  unlink(path); unlink(filtered_path); unlink(python);
  PASS();
}

static void video_palette_sources(const char *directory) {
  TEST("default/existing palettes are embedded independently of pencil coverage; black uses its actual index");
  char path[PATH_MAX]; snprintf(path, sizeof(path), "%s/custom.pal", directory);
  const uint8_t fixture[] = {'R','I','F','F', 28,0,0,0, 'P','A','L',' ', 'd','a','t','a', 16,0,0,0,
    0,3,3,0, 255,0,0,0, 0,0,0,0, 0,0,255,0};
  FILE *fp = fopen(path, "wb"); ASSERT_NOT_NULL(fp);
  ASSERT_EQUAL(fwrite(fixture, 1, sizeof(fixture), fp), sizeof(fixture)); fclose(fp);
  pv_video_t v = {.width = 1, .height = 1};
  pv_options_t o = {.mode = PV_PENCIL};
  ASSERT_TRUE(pv_palette(&v, &o)); ASSERT_EQUAL(v.colors, 32);
  ASSERT_EQUAL(memcmp(v.palette, pencil_default_colors(), sizeof(v.palette)), 0);
  ASSERT_EQUAL(v.playback_palette[128][0], 128); ASSERT_EQUAL(v.palette[128][3], 0);
  o.palette_source = PV_EXISTING_PALETTE; o.palette_path = path;
  ASSERT_TRUE(pv_palette(&v, &o)); ASSERT_EQUAL(v.colors, 3);
  uint8_t rgb[] = {127,127,127}, image, coverage;
  pv_convert(&v, &o, rgb, &image, &coverage); ASSERT_EQUAL(image, 127); ASSERT_EQUAL(coverage, 128);
  o.mode = PV_BLACK; o.threshold = 128;
  ASSERT_TRUE(pv_palette(&v, &o)); ASSERT_EQUAL(v.black_index, 1);
  pv_convert(&v, &o, rgb, &image, &coverage); ASSERT_EQUAL(image, 1);
  o.palette_path = "/nonexistent-palette.pal"; ASSERT_FALSE(pv_palette(&v, &o));
  PASS();
}

static void video_global_palette(void) {
  TEST("global palettes respect every budget and keep transparency at index 255");
  const int budgets[] = {32,64,128,256};
  for (int b = 0; b < ARRAY_LEN(budgets); b++) {
    pv_video_t v = {0};
    pv_options_t o = {.mode = PV_PALETTE, .palette_source = PV_GENERATED_PALETTE, .colors = budgets[b]};
    v.histogram = calloc(PV_BINS, sizeof(*v.histogram)); ASSERT_NOT_NULL(v.histogram);
    for (int i = 0; i < PV_BINS; i++) {
      v.histogram[i].count = 1;
      for (int ch = 0; ch < 3; ch++) v.histogram[i].sum[ch] = ((i >> ((2 - ch) * 5)) & 31) * 8;
    }
    ASSERT_TRUE(pv_palette(&v, &o));
    ASSERT_EQUAL(v.colors, budgets[b] == 256 ? 255 : budgets[b]);
    ASSERT_EQUAL(v.palette[255][3], 0);
    for (int i = 0; i < v.colors; i++) ASSERT_EQUAL(v.palette[i][3], 255);
    for (int i = 0; i < PV_BINS; i++) ASSERT_TRUE(v.lookup[i] < v.colors);
    pv_free(&v);
  }
  PASS();
}

static void video_native_roundtrip(const char *directory, pv_mode_t mode, pv_palette_source_t source) {
  TEST("converted odd-size two-frame project reopens with native layer data, palette and timing");
  char path[PATH_MAX], frames[PATH_MAX], png_path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/mode-%d.ptf", directory, mode);
  snprintf(frames, sizeof(frames), "%s/mode-%d-frames", directory, mode);
  pv_options_t o = {.output = path, .frames = frames, .mode = mode, .colors = 32, .threshold = 170, .min_width = 2};
  char palette_path[PATH_MAX]; snprintf(palette_path, sizeof(palette_path), "%s/custom.pal", directory);
  o.palette_source = source; o.palette_path = palette_path;
  pv_time_t times[] = {{.delay = 42}, {.pts = 0.042, .delay = 83}};
  uint8_t rgb[] = {0,0,0, 127,127,127, 255,255,255, 255,0,0, 0,255,0, 0,0,255};
  pv_video_t v = {.width = 3, .height = 1, .count = 2, .fps = 24, .times = times};
  v.pixels = tmpfile(); ASSERT_NOT_NULL(v.pixels);
  ASSERT_TRUE(pv_write(v.pixels, rgb, sizeof(rgb)));
  if (mode == PV_PALETTE || source == PV_GENERATED_PALETTE) {
    v.histogram = calloc(PV_BINS, sizeof(*v.histogram)); ASSERT_NOT_NULL(v.histogram);
    for (int p = 0; p < 6; p++) {
      pv_bin_t *bin = &v.histogram[pv_bin_index(rgb + p * 3)]; bin->count++;
      for (int ch = 0; ch < 3; ch++) bin->sum[ch] += rgb[p * 3 + ch];
    }
  }
  ASSERT_TRUE(pv_palette(&v, &o));
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
    ASSERT_NULL(tl->frames[i]->data);
    ASSERT_EQUAL(tl->frames[i]->format, FRAME_FORMAT_RLE_INDEXED);
    ASSERT_TRUE(test_anim_cel_matches(tl->frames[i], 1, pencil, 3, 1));
    ASSERT_EQUAL(test_anim_cel_pixel(tl->frames[i], 2, 0, 3, 1), 255);
    if (mode == PV_PENCIL) { ASSERT_EQUAL(test_anim_cel_pixel(tl->frames[i], 0, 0, 3, 1), 255); }
    else { ASSERT_TRUE(test_anim_cel_matches(tl->frames[i], 0, expected, 3, 1)); }
    snprintf(png_path, sizeof(png_path), "%s/frame-%06d.png", frames, i + 1);
    FILE *png_file = fopen(png_path, "rb"); ASSERT_NOT_NULL(png_file);
    uint8_t header[26]; ASSERT_EQUAL(fread(header, 1, sizeof(header), png_file), sizeof(header)); fclose(png_file);
    ASSERT_EQUAL(header[24], mode == PV_BLACK ? 1 : 8); ASSERT_EQUAL(header[25], mode == PV_PENCIL ? PNG_COLOR_TYPE_GRAY : PNG_COLOR_TYPE_PALETTE);
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

static void video_external_projects(const char *directory, bool ink_only) {
  TEST("full example projects reopen with every frame matching its exported PNG");
  const char *names[] = {"pencil", "black", "palette-32", "palette-default", "palette-existing"};
  for (int example = ink_only ? 1 : 0; example < (ink_only ? 2 : ARRAY_LEN(names)); example++) {
    int mode = example < 3 ? example : PV_PALETTE;
    char path[PATH_MAX];
    const char *project_override = ink_only ? getenv("PENCILTEST_VIDEO_INK_PROJECT") : NULL;
    const char *pencil_frames = ink_only ? getenv("PENCILTEST_VIDEO_PENCIL_FRAMES") : NULL;
    if (project_override) snprintf(path, sizeof(path), "%s", project_override);
    else snprintf(path, sizeof(path), "%s/%s.ptf", directory, names[example]);
    int w, h; uint32_t palette[256], bg; bool show;
    pencil_file_layers_t layers;
    anim_timeline_t *tl = flc_load_layers(path, &w, &h, palette, &bg, &show, &layers);
    ASSERT_NOT_NULL(tl);
    if (example != 2) ASSERT_EQUAL(memcmp(palette, pencil_default_colors(), sizeof(palette)), 0);
    ASSERT_EQUAL(palette[255], 0);
    if (pencil_frames) { ASSERT_EQUAL(layers.active, IE_LAYER_COLOR); ASSERT_EQUAL(layers.visible, 15); }
    size_t n = (size_t)w * h;
    uint8_t *samples = malloc(n); ASSERT_NOT_NULL(samples);
    uint8_t *cel = malloc(n); ASSERT_NOT_NULL(cel);
    size_t stored_bytes = 0;
    for (int i = 0; i < tl->frame_count; i++) {
      snprintf(path, sizeof(path), "%s/%s-frames/frame-%06d.png", directory, names[example], i + 1);
      png_image png = {.version = PNG_IMAGE_VERSION};
      ASSERT_TRUE(png_image_begin_read_from_file(&png, path));
      uint8_t colormap[256 * 4];
      png.format = mode == PV_PENCIL ? PNG_FORMAT_GRAY : PNG_FORMAT_RGBA_COLORMAP;
      ASSERT_TRUE(png_image_finish_read(&png, NULL, samples, 0, colormap));
      png_image_free(&png);
      ASSERT_NULL(tl->frames[i]->data);
      ASSERT_TRUE(anim_frame_expand_cel(tl->frames[i], mode == PV_PENCIL ? 1 : 0, cel, w, h));
      stored_bytes += tl->frames[i]->cels_size;
      if (mode == PV_PENCIL) {
        for (size_t p = 0; p < n; p++) ASSERT_EQUAL(cel[p], 255 - samples[p]);
      } else {
        for (size_t p = 0; p < n; p++) {
          uint8_t *color = colormap + samples[p] * 4;
          uint32_t expected = MAKE_COLOR(color[0], color[1], color[2], color[3]);
          if (!color[3]) { ASSERT_EQUAL(cel[p], 255); }
          else { ASSERT_EQUAL(palette[cel[p]], expected); }
          if (mode == PV_BLACK && color[3]) ASSERT_EQUAL(expected, MAKE_COLOR(0,0,0,255));
        }
      }
      if (pencil_frames) {
        snprintf(path, sizeof(path), "%s/frame-%06d.png", pencil_frames, i + 1);
        png_image source = {.version = PNG_IMAGE_VERSION};
        ASSERT_TRUE(png_image_begin_read_from_file(&source, path)); source.format = PNG_FORMAT_GRAY;
        ASSERT_TRUE(png_image_finish_read(&source, NULL, samples, 0, NULL)); png_image_free(&source);
        ASSERT_TRUE(anim_frame_expand_cel(tl->frames[i], 1, cel, w, h));
        for (size_t p = 0; p < n; p++) ASSERT_EQUAL(cel[p], 255 - samples[p]);
      }
    }
    printf("\n    %s: %d frames, retained RLE cels %.2f MiB (raw cels %.2f MiB)\n", names[example], tl->frame_count,
           stored_bytes / 1048576.0, 3.0 * n * tl->frame_count / 1048576.0);
    ASSERT_TRUE(stored_bytes < 3 * n * tl->frame_count);
    free(cel); free(samples); free(layers.background); anim_timeline_free(tl);
  }
  PASS();
}

static void video_sequence_verify(const char *path, pv_video_t *v) {
  int w, h; uint32_t palette[256], bg; bool show;
  pencil_file_layers_t layers;
  anim_timeline_t *tl = flc_load_layers(path, &w, &h, palette, &bg, &show, &layers);
  ASSERT_NOT_NULL(tl); ASSERT_EQUAL(w, v->width); ASSERT_EQUAL(h, v->height);
  ASSERT_EQUAL(tl->frame_count, v->count); ASSERT_EQUAL(layers.active, IE_LAYER_PENCIL);
  ASSERT_EQUAL(layers.visible, 15); ASSERT_TRUE(show);
  size_t n = (size_t)w * h;
  ASSERT_EQUAL(memcmp(layers.background, v->background, n), 0);
  ASSERT_EQUAL(memcmp(palette, v->palette, sizeof(palette)), 0);
  uint8_t *rgb = malloc(n * 3), *image = malloc(n), *pencil = malloc(n), *playback = calloc(1, n);
  ASSERT_NOT_NULL(rgb); ASSERT_NOT_NULL(image); ASSERT_NOT_NULL(pencil); ASSERT_NOT_NULL(playback);
  FILE *file = fopen(path, "rb"); ASSERT_NOT_NULL(file); ASSERT_EQUAL(fseek(file, 128, SEEK_SET), 0);
  pv_options_t o = {.mode = PV_PENCIL}; rewind(v->pixels);
  for (int i = 0; i < v->count; i++) {
    ASSERT_EQUAL(fread(rgb, 1, n * 3, v->pixels), n * 3);
    pv_convert(v, &o, rgb, image, pencil);
    ASSERT_TRUE(test_anim_cel_matches(tl->frames[i], 1, pencil, w, h));
    ASSERT_STR_EQUAL(tl->frames[i]->name, v->names[i]);
    ASSERT_EQUAL(tl->frames[i]->delay_ms, v->times[i].delay);
    for (int cel = 0; cel < 3; cel += 2) {
      memset(image, 255, n); ASSERT_TRUE(test_anim_cel_matches(tl->frames[i], cel, image, w, h));
    }
    uint8_t header[16]; ASSERT_EQUAL(fread(header, 1, 16, file), 16);
    ASSERT_EQUAL(flc_codec_u16(header + 4), 0xf1fa);
    for (int chunk = 0; chunk < flc_codec_u16(header + 6); chunk++) {
      uint8_t ch[6]; ASSERT_EQUAL(fread(ch, 1, 6, file), 6);
      size_t size = flc_codec_u32(ch); ASSERT_TRUE(size >= 6);
      uint8_t *data = malloc(size); ASSERT_NOT_NULL(data); memcpy(data, ch, 6);
      ASSERT_EQUAL(fread(data + 6, 1, size - 6, file), size - 6);
      if (flc_codec_u16(ch + 4) != 4) ASSERT_TRUE(flc_codec_decode(data, size, playback, playback, w, h));
      free(data);
    }
    pv_composite_background(v, image, pencil);
    ASSERT_EQUAL(memcmp(image, playback, n), 0);
  }
  fclose(file); free(rgb); free(image); free(pencil); free(playback); free(layers.background); anim_timeline_free(tl);
}

static void video_png_sequence(const char *directory) {
  TEST("named PNG sequence retains transparent pencil coverage and separate shared guides after native reload");
  char manifest[PATH_MAX], png_path[PATH_MAX], background[PATH_MAX], output[PATH_MAX];
  snprintf(manifest, sizeof(manifest), "%s/poses.tsv", directory);
  snprintf(png_path, sizeof(png_path), "%s/pose with spaces.png", directory);
  snprintf(background, sizeof(background), "%s/guides.png", directory);
  snprintf(output, sizeof(output), "%s/poses.ptf", directory);
  uint8_t rgba[] = {0,0,0,255, 0,0,0,128, 0,0,0,0};
  png_image png = {.version = PNG_IMAGE_VERSION, .width = 3, .height = 1, .format = PNG_FORMAT_RGBA};
  ASSERT_TRUE(png_image_write_to_file(&png, png_path, 0, rgba, 0, NULL));
  uint8_t guides[] = {128,128,128, 255,255,255, 0,0,0}; png.format = PNG_FORMAT_RGB;
  ASSERT_TRUE(png_image_write_to_file(&png, background, 0, guides, 0, NULL));
  FILE *fp = fopen(manifest, "w"); ASSERT_NOT_NULL(fp);
  fputs("Front\tpose with spaces.png\nBack 3/4\tpose with spaces.png\n", fp); fclose(fp);
  pv_options_t o = {.input = manifest, .sequence = true, .output = output, .background = background, .fps = 2};
  pv_video_t v = {0};
  ASSERT_TRUE(pv_decode(&o, &v)); ASSERT_EQUAL(v.count, 2); ASSERT_EQUAL(v.fps, 2);
  ASSERT_TRUE(pv_palette(&v, &o)); ASSERT_TRUE(pv_background(&o, &v));
  ASSERT_TRUE(pv_output(&o, &v)); video_sequence_verify(output, &v);
  uint8_t image[3] = {0}, pencil[] = {255,128,0};
  pv_composite_background(&v, image, pencil);
  ASSERT_EQUAL(image[0], 0); ASSERT_EQUAL(image[1], 127); ASSERT_EQUAL(image[2], 0);
  pv_free(&v); unlink(output);
  png.width = 1; ASSERT_TRUE(png_image_write_to_file(&png, background, 0, guides, 0, NULL));
  ASSERT_TRUE(pv_decode(&o, &v)); ASSERT_TRUE(pv_palette(&v, &o)); ASSERT_FALSE(pv_background(&o, &v)); pv_free(&v);
  fp = fopen(manifest, "w"); ASSERT_NOT_NULL(fp); fputs("Malformed row\n", fp); fclose(fp);
  v = (pv_video_t){0}; ASSERT_FALSE(pv_decode(&o, &v)); pv_free(&v);
  unlink(manifest); unlink(png_path); unlink(background);
  PASS();
}

static void video_sequence_example(const char *directory) {
  TEST("character study project reopens with every named pencil cel and shared guide pixel intact");
  char manifest[PATH_MAX], background[PATH_MAX], palette[PATH_MAX], output[PATH_MAX];
  snprintf(manifest, sizeof(manifest), "%s/frames.tsv", directory);
  snprintf(background, sizeof(background), "%s/background.png", directory);
  snprintf(palette, sizeof(palette), "%s/grayscale.pal", directory);
  snprintf(output, sizeof(output), "%s/character-blockouts.ptf", directory);
  pv_options_t o = {.input = manifest, .sequence = true, .output = output, .background = background,
                    .palette_source = PV_EXISTING_PALETTE, .palette_path = palette, .fps = 1};
  pv_video_t v = {0};
  ASSERT_TRUE(pv_decode(&o, &v)); ASSERT_TRUE(pv_palette(&v, &o)); ASSERT_TRUE(pv_background(&o, &v));
  ASSERT_EQUAL(v.count, 10); video_sequence_verify(output, &v); pv_free(&v);
  PASS();
}

int main(void) {
  TEST_START("Penciltest video conversion");
  char directory[] = "/tmp/orion-video-test-XXXXXX";
  if (!mkdtemp(directory)) { perror(directory); return 1; }
  video_grayscale(); video_ink_cleanup(directory); video_arguments(); video_global_palette(); video_timestamps(); video_decoder(directory);
  video_palette_sources(directory);
  video_source_pencil(directory);
  video_png_sequence(directory);
  video_native_roundtrip(directory, PV_PENCIL, PV_DEFAULT_PALETTE);
  video_native_roundtrip(directory, PV_BLACK, PV_DEFAULT_PALETTE);
  video_native_roundtrip(directory, PV_PALETTE, PV_DEFAULT_PALETTE);
  video_native_roundtrip(directory, PV_PALETTE, PV_EXISTING_PALETTE);
  video_native_roundtrip(directory, PV_PALETTE, PV_GENERATED_PALETTE);
  video_native_roundtrip(directory, PV_PENCIL, PV_GENERATED_PALETTE);
  video_native_roundtrip(directory, PV_BLACK, PV_GENERATED_PALETTE);
  const char *examples = getenv("PENCILTEST_VIDEO_EXAMPLES");
  if (examples) video_external_projects(examples, false);
  const char *ink_example = getenv("PENCILTEST_VIDEO_INK_EXAMPLE");
  if (ink_example) video_external_projects(ink_example, true);
  const char *sequence_example = getenv("PENCILTEST_SEQUENCE_EXAMPLE");
  if (sequence_example) video_sequence_example(sequence_example);
  char palette_path[PATH_MAX]; snprintf(palette_path, sizeof(palette_path), "%s/custom.pal", directory); unlink(palette_path);
  rmdir(directory);
  TEST_END();
}
