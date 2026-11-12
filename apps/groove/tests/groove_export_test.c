#include "test_framework.h"
#include "test_env.h"
#include "apps/groove/groove.h"
#include "apps/groove/minimp3.h"
#include <unistd.h>

static bool export_test_cancel(void *userdata, float value) {
  (void)userdata; (void)value; return false;
}

static result_t export_media_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

static void test_export_formats(void) {
  TEST("export choices match the compiled encoders and sharing defaults to AAC");
  test_env_init();
  const char *audio[] = { "WAV (44.1 kHz, 16-bit)",
#ifdef GROOVE_USE_SHINE
    "MP3 (192 kbps)",
#endif
    "M4A (AAC)" };
  for (int kind = 0; kind < 3; kind++) {
    window_t *dlg = create_window_from_form(&groove_export_media_form, 0, 0, NULL, export_media_proc, 0, &kind);
    ASSERT_NOT_NULL(dlg);
    window_t *format = get_window_item(dlg, ID_EXPORT_MEDIA_FORMAT);
    ASSERT_NOT_NULL(format);
    ASSERT_STR_EQUAL(format->title, kind == 2 ? "MP4 (H.264 + AAC)" : kind == 1 ? "M4A (AAC)" : audio[0]);
    if (kind != 2) for (int i = 0; i < ARRAY_LEN(audio); i++) {
      char text[256];
      ASSERT(send_message(format, cbGetListBoxText, (uint32_t)i, text), "format exists");
      ASSERT_STR_EQUAL(text, audio[i]);
    }
    if (kind == 2) {
      window_t *size = get_window_item(dlg, ID_EXPORT_MEDIA_ASPECT);
      window_t *branding = get_window_item(dlg, ID_EXPORT_MEDIA_BRANDING);
      char text[256];
      ASSERT(send_message(size, cbGetListBoxText, 2, text), "portrait size exists");
      ASSERT_STR_EQUAL(text, "9:16 (1080 x 1920)");
      ASSERT_FALSE(window_is_enabled(branding));
      send_message(size, cbSetCurrentSelection, 2, NULL);
      send_message(dlg, evCommand, MAKEDWORD(ID_EXPORT_MEDIA_ASPECT, cbSelectionChange), size);
      ASSERT_TRUE(window_is_enabled(branding));
      ASSERT_EQUAL(send_message(branding, btnGetCheck, 0, NULL), btnStateChecked);
    }
    destroy_window(dlg);
  }
  test_env_shutdown(); PASS();
}

static void test_audio_export(void) {
  TEST("finite lossless mixdown matches playback, excludes audition, preserves transport and supports the enabled encoders");
  test_env_init();
  g_app = app_init(); ASSERT_NOT_NULL(g_app);
  song_t song; song_init(&song);
  ASSERT(app_block_audio(0), "source ready");
  ASSERT(song_add_clip(&song, 0, 0, 0) >= 0, "clip placed");
  song.clips[0].length = GR_TICKS_BAR;
  song.clips[0].gain_db = -6;
  song.loop = true; song.playing = true; song.pos = 527; song.preview_block = 0; song.preview_pos = 123;
  song_t before = song;
  char directory[] = "/tmp/orion-export-XXXXXX"; ASSERT_NOT_NULL(mkdtemp(directory));
  char wav[512], mp3[512], cancelled[512];
  snprintf(wav, sizeof(wav), "%s/mix with 'quotes'.wav", directory);
  snprintf(mp3, sizeof(mp3), "%s/mix.mp3", directory);
  snprintf(cancelled, sizeof(cancelled), "%s/cancelled.wav", directory);
  ASSERT(song_export_wav(&song, wav, NULL, NULL), "WAV exported");
  ASSERT_EQUAL(memcmp(&song, &before, sizeof(song)), 0);
  int frames = bar_frames_for_bpm(song.bpm);
  FILE *f = fopen(wav, "rb"); ASSERT_NOT_NULL(f);
  ASSERT_EQUAL(fseek(f, 0, SEEK_END), 0); ASSERT_EQUAL(ftell(f), 44 + frames * 4);
  rewind(f); uint8_t header[44]; ASSERT_EQUAL(fread(header, 1, 44, f), 44);
  ASSERT_EQUAL(memcmp(header, "RIFF", 4), 0); ASSERT_EQUAL(memcmp(header + 8, "WAVEfmt ", 8), 0);
  ASSERT_EQUAL(header[22], 2); ASSERT_EQUAL(header[34], 16);
  song_t render = song; render.pos = 0; render.loop = false; render.preview_block = -1;
  float lr[2048]; int16_t expected[2048]; uint8_t bytes[4096];
  for (int at = 0; at < frames;) {
    int n = MIN(1024, frames - at);
    song_render(&render, lr, n); groove_master_pcm(lr, expected, n * 2);
    ASSERT_EQUAL(fread(bytes, 4, (size_t)n, f), (size_t)n);
    for (int i = 0; i < n * 2; i++) ASSERT_EQUAL((int16_t)((unsigned)bytes[i * 2] | (unsigned)bytes[i * 2 + 1] << 8), expected[i]);
    at += n;
  }
  fclose(f);
  block_pcm_t *snapshot = calloc((size_t)blocks_count(), sizeof(*snapshot)); ASSERT_NOT_NULL(snapshot);
  snapshot[0] = block_get(0)->audio;
  snapshot[0].pcm = malloc((size_t)snapshot[0].frames * sizeof(float)); ASSERT_NOT_NULL(snapshot[0].pcm);
  memcpy(snapshot[0].pcm, block_get(0)->audio.pcm, (size_t)snapshot[0].frames * sizeof(float));
  memset(block_get(0)->audio.pcm, 0, (size_t)snapshot[0].frames * sizeof(float));
  ASSERT(song_export_wav_with_audio(&song, snapshot, cancelled, NULL, NULL), "a private export survives changes to live source audio");
  FILE *left = fopen(wav, "rb"), *right = fopen(cancelled, "rb"); ASSERT_NOT_NULL(left); ASSERT_NOT_NULL(right);
  uint8_t a[4096], b[4096]; size_t count;
  while ((count = fread(a, 1, sizeof(a), left)) > 0) {
    ASSERT_EQUAL(fread(b, 1, count, right), count); ASSERT_EQUAL(memcmp(a, b, count), 0);
  }
  fclose(left); fclose(right); remove(cancelled);
  memcpy(block_get(0)->audio.pcm, snapshot[0].pcm, (size_t)snapshot[0].frames * sizeof(float));
  free(snapshot[0].pcm); free(snapshot);
  ASSERT_FALSE(song_export_wav(&song, cancelled, export_test_cancel, NULL));
  ASSERT(access(cancelled, F_OK) != 0, "cancel removes partial WAV");
#ifdef GROOVE_USE_SHINE
  ASSERT(groove_encode_mp3(wav, mp3, NULL, NULL), "portable MP3 encoded");
  f = fopen(mp3, "rb"); ASSERT_NOT_NULL(f);
  fseek(f, 0, SEEK_END); long size = ftell(f); rewind(f);
  uint8_t *encoded = malloc((size_t)size); ASSERT_NOT_NULL(encoded);
  ASSERT_EQUAL(fread(encoded, 1, (size_t)size, f), (size_t)size); fclose(f);
  mp3dec_t decoder; mp3dec_init(&decoder); mp3dec_frame_info_t info;
  int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME]; int samples = 0; float energy = 0;
  for (int at = 0; at < size;) {
    int n = mp3dec_decode_frame(&decoder, encoded + at, (int)size - at, pcm, &info);
    if (!info.frame_bytes) break;
    if (n) { ASSERT_EQUAL(info.hz, GR_SAMPLE_RATE); ASSERT_EQUAL(info.channels, 2); samples += n;
      for (int i = 0; i < n * 2; i++) energy += fabsf(pcm[i] / 32768.0f); }
    at += info.frame_bytes;
  }
  ASSERT(samples >= frames && samples < frames + 2304, "MP3 padding is less than two codec frames");
  ASSERT(energy > 1, "MP3 decodes to audible PCM"); free(encoded);
#endif
  const char *out = getenv("GROOVE_MEDIA_TEST_OUTPUT");
  if (out) {
    axMkDir(out); char path[1024];
    snprintf(path, sizeof(path), "%s/audio.m4a", out);
    ASSERT(ui_media_encode(wav, path, UI_MEDIA_M4A, NULL, NULL, NULL), "AAC encoded using the platform backend");
    const struct { int width, height; const char *suffix; } sizes[] = {
      {1280, 720, "16-9"}, {1680, 720, "21-9"}, {1080, 1920, "9-16"},
    };
    for (size_t i = 0; i < ARRAY_LEN(sizes); i++) {
      int width = sizes[i].width, height = sizes[i].height;
      uint8_t *rgba = calloc((size_t)width * height, 4); ASSERT_NOT_NULL(rgba);
      for (int pixel = 0; pixel < width * height; pixel++) { rgba[pixel * 4] = 30; rgba[pixel * 4 + 3] = 255; }
      ui_media_video_t video = {.rgba=rgba, .width=width, .height=height, .x=64, .y=92, .w=width-96, .h=596,
        .frames=frames, .rate=GR_SAMPLE_RATE, .fps=30, .color=0xffffffff};
      snprintf(path, sizeof(path), "%s/timeline-%s.mp4", out, sizes[i].suffix);
      ASSERT(ui_media_encode(wav, path, UI_MEDIA_MP4, &video, NULL, NULL), "MP4 with audio encoded");
      free(rgba);
    }
    char path_wav[1024], path_mp3[1024]; snprintf(path_wav, sizeof(path_wav), "%s/mix.wav", out); snprintf(path_mp3, sizeof(path_mp3), "%s/mix.mp3", out);
    ASSERT_EQUAL(rename(wav, path_wav), 0);
#ifdef GROOVE_USE_SHINE
    ASSERT_EQUAL(rename(mp3, path_mp3), 0);
#endif
  }
  remove(wav); remove(mp3); rmdir(directory);
  app_shutdown(g_app); test_env_shutdown(); PASS();
}

static void test_video_clock(void) {
  TEST("16:9 and 21:9 playheads follow the audio clock and preserve the timeline pixels");
  for (int aspect = 0; aspect < 2; aspect++) {
    int width = aspect ? 1680 : 1280;
    uint8_t *base = calloc((size_t)width * 720, 4), *frame = malloc((size_t)width * 720 * 4);
    ASSERT_NOT_NULL(base); ASSERT_NOT_NULL(frame);
    ui_media_video_t v = {.rgba=base, .width=width, .height=720, .x=64, .y=92, .w=width-96, .h=596,
      .frames=GR_SAMPLE_RATE * 2, .rate=GR_SAMPLE_RATE, .fps=30, .color=0xffabcdef};
    ASSERT(ui_media_video_frame(&v, 0, frame), "first frame");
    ASSERT_EQUAL(frame[(92 * width + 64) * 4], 0xef);
    ASSERT(ui_media_video_frame(&v, 30, frame), "middle frame");
    int middle = 64 + (v.w - 1) / 2;
    ASSERT_EQUAL(frame[(92 * width + middle) * 4], 0xef);
    ASSERT_EQUAL(frame[(92 * width + 64) * 4], 0);
    ASSERT_EQUAL(frame[0], 0); ASSERT_EQUAL(base[(92 * width + middle) * 4], 0);
    ASSERT_FALSE(ui_media_video_frame(&v, 60, frame));
    free(base); free(frame);
  }
  PASS();
}

static bool video_region_differs(const uint8_t *a, const uint8_t *b, int width, irect16_t r) {
  for (int y = r.y; y < r.y + r.h; y++)
    if (memcmp(a + ((size_t)y * width + r.x) * 4, b + ((size_t)y * width + r.x) * 4, (size_t)r.w * 4)) return true;
  return false;
}

static void test_motion_video(void) {
  TEST("motion video scrolls the zoomed timeline and drives the spectrum from the mix");
  test_env_init();
  g_app = app_init(); ASSERT_NOT_NULL(g_app);
  song_t song; song_init(&song);
  ASSERT(app_block_audio(0), "source ready");
  for (int bar = 0; bar < 24; bar += 2) ASSERT(song_add_clip(&song, 0, bar % 8, bar * GR_TICKS_BAR) >= 0, "clip placed");
  char directory[] = "/tmp/orion-motion-XXXXXX"; ASSERT_NOT_NULL(mkdtemp(directory));
  char wav[512]; snprintf(wav, sizeof(wav), "%s/mix.wav", directory);
  ASSERT(song_export_wav(&song, wav, NULL, NULL), "WAV exported");
  ui_media_video_t video = {0};
  ASSERT(song_export_canvas(&song, 1280, 720, "Motion", false, &video), "canvas rendered");
  ASSERT_NOT_NULL(video.render);
  ASSERT(song_export_video_analyze(&video, wav, NULL, NULL), "spectrum analysed");
  ASSERT_FALSE(song_export_video_analyze(&video, wav, export_test_cancel, NULL));
  int64_t count = (video.frames * video.fps + video.rate - 1) / video.rate;
  size_t bytes = (size_t)video.width * video.height * 4;
  uint8_t *first = malloc(bytes), *middle = malloc(bytes), *next = malloc(bytes);
  ASSERT_NOT_NULL(first); ASSERT_NOT_NULL(middle); ASSERT_NOT_NULL(next);
  ASSERT(ui_media_video_frame(&video, 0, first), "first frame");
  ASSERT(ui_media_video_frame(&video, count / 2, middle), "middle frame");
  ASSERT(ui_media_video_frame(&video, count / 2 + 1, next), "next frame");
  ASSERT_FALSE(ui_media_video_frame(&video, count, next));
  irect16_t view = {video.x, video.y, video.w, video.h}, title = {0, 0, video.width, 40};
  ASSERT(video_region_differs(middle, next, video.width, view), "consecutive frames move");
  ASSERT(video_region_differs(first, middle, video.width, R(0, 470, video.width, 226)), "spectrum responds to the mix");
  ASSERT_FALSE(video_region_differs(first, middle, video.width, title));
  const char *out = getenv("GROOVE_MEDIA_TEST_OUTPUT");
  if (out) {
    char path[1024]; axMkDir(out);
    snprintf(path, sizeof(path), "%s/motion.mp4", out); remove(path);
    ASSERT(ui_media_encode(wav, path, UI_MEDIA_MP4, &video, NULL, NULL), "motion MP4 encoded");
    snprintf(path, sizeof(path), "%s/motion-middle.rgba", out);
    FILE *f = fopen(path, "wb"); ASSERT_NOT_NULL(f); fwrite(middle, 1, bytes, f); fclose(f);
  }
  free(first); free(middle); free(next);
  song_export_video_free(&video);
  ASSERT(video.render == NULL, "video released");
  remove(wav); rmdir(directory);
  app_shutdown(g_app); test_env_shutdown(); PASS();
}

int main(void) {
  TEST_START("Groove export");
  test_export_formats();
  test_audio_export();
  test_video_clock();
  test_motion_video();
  TEST_END();
}
