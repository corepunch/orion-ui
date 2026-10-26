#include "groove.h"
#include <errno.h>

void groove_master_pcm(const float *in, int16_t *out, int samples) {
  for (int i = 0; i < samples; i++) out[i] = (int16_t)(tanhf(in[i]) * 32000.0f);
}

static void export_u32(uint8_t *p, uint32_t n) {
  for (int i = 0; i < 4; i++) p[i] = (uint8_t)(n >> (i * 8));
}

bool song_export_wav_with_audio(const song_t *source, const block_pcm_t *audio, const char *path, ui_media_progress_fn progress, void *userdata) {
  if (!source || !path || source->bpm < GR_BPM_MIN || source->bpm > GR_BPM_MAX) return false;
  int64_t total = position_frames_for_bpm(song_length_ticks(source), source->bpm);
  if (total <= 0 || total > (UINT32_MAX - 36) / 4) return false;
  song_t song = *source;
  song.pos = 0; song.playing = true; song.loop = false; song.preview_block = -1; song.preview_pos = 0;
  uint8_t header[44] = {0};
  memcpy(header, "RIFF", 4); export_u32(header + 4, (uint32_t)(36 + total * 4));
  memcpy(header + 8, "WAVEfmt ", 8); export_u32(header + 16, 16);
  header[20] = 1; header[22] = 2; export_u32(header + 24, GR_SAMPLE_RATE);
  export_u32(header + 28, GR_SAMPLE_RATE * 4); header[32] = 4; header[34] = 16;
  memcpy(header + 36, "data", 4); export_u32(header + 40, (uint32_t)(total * 4));
  FILE *f = fopen(path, "wb");
  bool ok = f && fwrite(header, 1, sizeof(header), f) == sizeof(header);
  float lr[2048]; int16_t pcm[2048]; uint8_t le[4096];
  for (int64_t at = 0; ok && at < total;) {
    if (progress && !progress(userdata, (float)((double)at / total))) { ok = false; break; }
    int n = (int)MIN(total - at, 1024);
    song_render_with_audio(&song, lr, n, audio); groove_master_pcm(lr, pcm, n * 2);
    for (int i = 0; i < n * 2; i++) { le[i * 2] = (uint8_t)pcm[i]; le[i * 2 + 1] = (uint8_t)((uint16_t)pcm[i] >> 8); }
    ok = fwrite(le, 4, (size_t)n, f) == (size_t)n;
    at += n;
  }
  if (f && fclose(f)) ok = false;
  if (!ok) { fprintf(stderr, "[groove-export] WAV failed path=%s: %s\n", path, strerror(errno)); fflush(stderr); remove(path); }
  return ok;
}

// A full-song timeline shared with saved-song previews. The export canvas is
// painted once through Orion, then the media backend adds the moving playhead.
void song_draw_overview(const song_t *song, irect16_t r) {
  int span = MAX(1, song_length_ticks(song));
  bool solo = false;
  for (int t = 0; t < GR_TRACKS; t++) solo |= song->solo[t];
  for (int t = 0; t < GR_TRACKS; t++) {
    int y0 = t * r.h / GR_TRACKS, y1 = (t + 1) * r.h / GR_TRACKS;
    fill_rect(get_sys_color(t % 2 ? brColumnViewBg : brWindowDarkBg), R(r.x, r.y + y0, r.w, y1 - y0));
  }
  for (int bar = 0; bar * GR_TICKS_BAR < span; bar++)
    fill_rect(color_with_alpha(get_sys_color(brLightEdge), bar % 4 ? 0x10 : 0x30),
      R(r.x + (int)((int64_t)bar * GR_TICKS_BAR * r.w / span), r.y, 1, r.h));
  for (int i = 0; i < song->nclips; i++) {
    const clip_t *c = &song->clips[i];
    const block_t *b = block_get(c->block);
    int x0 = (int)((int64_t)c->position * r.w / span), x1 = (int)((int64_t)song_clip_end(song, i) * r.w / span);
    int y0 = c->track * r.h / GR_TRACKS, y1 = (c->track + 1) * r.h / GR_TRACKS;
    bool audible = solo ? song->solo[c->track] : !song->mute[c->track];
    if (!b || x1 <= x0) continue;
    irect16_t clip = R(r.x + x0, r.y + y0 + 2, MAX(1, x1 - x0 - 1), MAX(1, y1 - y0 - 4));
    uint32_t color = audible ? category_color(b->cat) : get_sys_color(brTextSecondary);
    fill_rect(color_with_alpha(color, audible ? 0xc0 : 0x50), clip);
    if (clip.h >= 36 && clip.w > 26) {
      draw_text_ellipsized(FONT_SMALL, b->display_name ? b->display_name : b->name,
        clip.x + 4, clip.y + 2, MAX(0, clip.w - 8), get_sys_color(brTextNormal));
      if (b->audio.npeaks > 0) for (int x = 2; x < clip.w - 2; x++) {
        int source_tick = (c->position + (int)((int64_t)x * span / r.w)) - c->position;
        int peak = (int)((int64_t)(source_tick % (b->bars * GR_TICKS_BAR)) * b->audio.npeaks / (b->bars * GR_TICKS_BAR));
        int h = MAX(1, b->audio.peaks[peak] * (clip.h - 26) / 255);
        fill_rect(color_with_alpha(get_sys_color(brTextNormal), 0x80), R(clip.x + x, clip.y + 23 + (clip.h - 26 - h) / 2, 1, h));
      }
    }
  }
}

static result_t win_export_canvas(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)wparam; (void)lparam;
  const song_t *s = win->userdata;
  if (msg == evCreate) { win->userdata = lparam; return true; }
  if (msg == evDestroy) { win->userdata = NULL; return true; }
  if (msg != evPaint) return false;
  irect16_t r = get_client_rect(win);
  fill_rect(get_sys_color(brControlBg), r);
  char title[80]; snprintf(title, sizeof(title), "Orion Groove  /  %d BPM", s->bpm);
  draw_text(FONT_SYSTEM, title, 32, 24, get_sys_color(brTextNormal));
  int span = MAX(1, song_length_ticks(s)), step = MAX(1, (span / GR_TICKS_BAR + 15) / 16);
  irect16_t timeline = R(64, 92, r.w - 96, r.h - 124);
  for (int bar = 0; bar * GR_TICKS_BAR < span; bar += step) {
    char text[20]; snprintf(text, sizeof(text), "%d", bar + 1);
    draw_text(FONT_SMALL, text, timeline.x + (int)((int64_t)bar * GR_TICKS_BAR * timeline.w / span), 68, get_sys_color(brTextSecondary));
  }
  for (int track = 0; track < GR_TRACKS; track++) {
    char number[8]; snprintf(number, sizeof(number), "%d", track + 1);
    draw_text(FONT_SYSTEM, number, 32, timeline.y + track * timeline.h / GR_TRACKS + 12, get_sys_color(brTextSecondary));
  }
  song_draw_overview(s, timeline);
  return true;
}

bool song_export_canvas(const song_t *s, int width, int height, ui_media_video_t *video) {
  window_t *canvas = create_window("", WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOACTIVATE | WINDOW_NOTABSTOP,
    MAKERECT(0, 0, width, height), NULL, win_export_canvas, 0, (void *)s);
  if (!canvas) return false;
  send_message(canvas, evPaint, 0, NULL);
  uint8_t *pixels = NULL, *scaled = NULL;
  int w = 0, h = 0;
  bool ok = window_capture(canvas, &pixels, &w, &h);
  if (ok && (w != width || h != height)) {
    scaled = malloc((size_t)width * height * 4);
    ok = scaled != NULL;
    if (ok) for (int y = 0; y < height; y++) for (int x = 0; x < width; x++)
      memcpy(scaled + ((size_t)y * width + x) * 4, pixels + ((size_t)(y * h / height) * w + x * w / width) * 4, 4);
    free(pixels); pixels = scaled;
  }
  destroy_window(canvas);
  if (!ok) { free(pixels); return false; }
  *video = (ui_media_video_t){ .rgba = pixels, .width = width, .height = height,
    .x = 64, .y = 92, .w = width - 96, .h = height - 124,
    .color = get_sys_color(brTextNormal), .frames = position_frames_for_bpm(song_length_ticks(s), s->bpm), .rate = GR_SAMPLE_RATE, .fps = 30 };
  return true;
}

bool song_export_wav(const song_t *song, const char *path, ui_media_progress_fn progress, void *userdata) {
  return song_export_wav_with_audio(song, NULL, path, progress, userdata);
}
