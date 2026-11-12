#include "groove.h"
#include <errno.h>
#include <orion/kernel/renderer.h>
#include <orion/user/image.h>

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

// A full-song timeline shared with saved-song previews and the video strip.
// Only columns in [visible0, visible1) are drawn, so a wide strip renders in tiles.
static void overview_draw(const song_t *song, irect16_t r, int visible0, int visible1) {
  int span = MAX(1, song_length_ticks(song));
  bool solo = false;
  for (int t = 0; t < GR_TRACKS; t++) solo |= song->solo[t];
  for (int t = 0; t < GR_TRACKS; t++) {
    int y0 = t * r.h / GR_TRACKS, y1 = (t + 1) * r.h / GR_TRACKS;
    fill_rect(get_sys_color(t % 2 ? brColumnViewBg : brWindowDarkBg), R(r.x, r.y + y0, r.w, y1 - y0));
  }
  for (int bar = 0; bar * GR_TICKS_BAR < span; bar++) {
    int x = r.x + (int)((int64_t)bar * GR_TICKS_BAR * r.w / span);
    if (x >= visible0 && x < visible1) fill_rect(color_with_alpha(get_sys_color(brLightEdge), bar % 4 ? 0x10 : 0x30), R(x, r.y, 1, r.h));
  }
  for (int i = 0; i < song->nclips; i++) {
    const clip_t *c = &song->clips[i];
    const block_t *b = block_get(c->block);
    int x0 = (int)((int64_t)c->position * r.w / span), x1 = (int)((int64_t)song_clip_end(song, i) * r.w / span);
    int y0 = c->track * r.h / GR_TRACKS, y1 = (c->track + 1) * r.h / GR_TRACKS;
    bool audible = solo ? song->solo[c->track] : !song->mute[c->track];
    if (x1 <= x0 || r.x + x1 < visible0 || r.x + x0 >= visible1) continue;
    irect16_t clip = R(r.x + x0, r.y + y0 + 2, MAX(1, x1 - x0 - 1), MAX(1, y1 - y0 - 4));
    uint32_t color = audible ? category_color(b ? b->cat : CAT_VOX) : get_sys_color(brTextSecondary);
    fill_rect(color_with_alpha(color, audible ? 0xc0 : 0x50), clip);
    if (b && clip.h >= 36 && clip.w > 26) {
      draw_text_ellipsized("Text", b->display_name ? b->display_name : b->name,
        clip.x + 4, clip.y + 2, MAX(0, clip.w - 8), get_sys_color(brTextNormal));
      if (b->audio.npeaks > 0) for (int x = MAX(2, visible0 - clip.x); x < MIN(clip.w - 2, visible1 - clip.x); x++) {
        int source_tick = (c->position + (int)((int64_t)x * span / r.w)) - c->position;
        int peak = (int)((int64_t)(source_tick % (b->bars * GR_TICKS_BAR)) * b->audio.npeaks / (b->bars * GR_TICKS_BAR));
        int h = MAX(1, b->audio.peaks[peak] * (clip.h - 26) / 255);
        fill_rect(color_with_alpha(get_sys_color(brTextNormal), 0x80), R(clip.x + x, clip.y + 23 + (clip.h - 26 - h) / 2, 1, h));
      }
    }
  }
}

void song_draw_overview(const song_t *song, irect16_t r) { overview_draw(song, r, r.x, r.x + r.w); }

#define EXPORT_RULER_H     22
#define EXPORT_EQ_BASELINE 0.7f
#define EXPORT_FPS         60
#define VIDEO_BANDS        48
#define VIDEO_FFT          2048

typedef struct {
  const song_t *song;
  const char *title;
  irect16_t view, eq;     // timeline viewport (ruler + tracks) and spectrum, in canvas units
  int strip_w, tile_x;    // full-song strip width and the tile being painted
  bool portrait, branding, strip;
  uint32_t logo;
  ipoint16_t logo_size;
} export_canvas_t;

static void export_canvas_load_logo(export_canvas_t *canvas) {
  char path[1024];
  int n = snprintf(path, sizeof(path), "%s/../share/groove/branding/groove.png", ui_get_exe_dir());
  if (n < 0 || n >= (int)sizeof(path)) return;
  int width = 0, height = 0;
  uint8_t *pixels = load_image(path, &width, &height);
  if (!pixels) return;
  canvas->logo = R_CreateTextureSRGBA8(width, height, pixels, R_FILTER_LINEAR, R_WRAP_CLAMP);
  canvas->logo_size = (ipoint16_t){ width, height };
  image_free(pixels);
}

static void export_portrait_cover(const export_canvas_t *canvas, irect16_t r) {
  irect16_t safe = rect_inset_edges(r, R(34, 144, 62, 155));
  uint32_t primary = get_sys_color(brTextNormal), secondary = get_sys_color(brTextSecondary);
  if (canvas->branding) {
    irect16_t mark = rect_split_top(safe, 88);
    if (canvas->logo) {
      int width = MIN(174, mark.w);
      int height = width * canvas->logo_size.y / canvas->logo_size.x;
      draw_rect_ex(canvas->logo, rect_center(mark, width, height), 0, 1.0f);
    } else draw_text_clipped("Heading", ui_tr("Made with Groove"), &mark, secondary, TEXT_ALIGN_CENTER);
  }
  irect16_t title_area = rect_split_top(rect_trim_top(safe, 98), 54);
  char title[256];
  text_ellipsize("Display", canvas->title, title_area.w, title, sizeof(title));
  draw_text_clipped("Display", title, &title_area, primary, TEXT_ALIGN_CENTER);
  irect16_t tempo_area = rect_split_top(rect_trim_top(safe, 156), 26);
  char number[24], tempo[40];
  ui_format_number(canvas->song->bpm, 0, number, sizeof(number));
  snprintf(tempo, sizeof(tempo), ui_tr("%s BPM"), number);
  draw_text_clipped("Heading", tempo, &tempo_area, secondary, TEXT_ALIGN_CENTER);
}

static void export_paint_backdrop(const export_canvas_t *canvas, irect16_t r) {
  uint32_t secondary = get_sys_color(brTextSecondary);
  fill_rect(get_sys_color(brControlBg), r);
  if (canvas->portrait) export_portrait_cover(canvas, r);
  else {
    char title[80]; snprintf(title, sizeof(title), "Orion Groove  /  %d BPM", canvas->song->bpm);
    draw_text("Text", title, 32, 24, get_sys_color(brTextNormal));
  }
  irect16_t view = canvas->view;
  int track_h = (view.h - EXPORT_RULER_H) / GR_TRACKS;
  for (int track = 0; track < GR_TRACKS; track++) {
    char number[8]; snprintf(number, sizeof(number), "%d", track + 1);
    draw_text("Text", number, view.x - 32, view.y + EXPORT_RULER_H + track * track_h + (track_h - text_char_height("Text")) / 2, secondary);
  }
  irect16_t eq = canvas->eq;
  fill_rect(color_with_alpha(get_sys_color(brLightEdge), 0x30), R(eq.x, eq.y + (int)(eq.h * EXPORT_EQ_BASELINE), eq.w, 1));
}

// One viewport-wide slice of the zoomed full-song strip: beat ruler above the tracks.
static void export_paint_strip_tile(const export_canvas_t *canvas, irect16_t r) {
  int span = MAX(1, song_length_ticks(canvas->song));
  uint32_t secondary = get_sys_color(brTextSecondary), edge = get_sys_color(brLightEdge);
  fill_rect(get_sys_color(brControlBg), R(0, 0, r.w, EXPORT_RULER_H));
  for (int beat = 0; beat * GR_SNAP_TICKS <= span; beat++) {
    int x = (int)((int64_t)beat * GR_SNAP_TICKS * canvas->strip_w / span) - canvas->tile_x;
    bool bar = beat % GR_BEATS_BAR == 0;
    if (x < -48 || x >= r.w) continue;
    fill_rect(color_with_alpha(edge, bar ? 0x80 : 0x38), R(x, bar ? 4 : EXPORT_RULER_H - 6, 1, bar ? EXPORT_RULER_H - 4 : 6));
    if (bar) { char text[16]; snprintf(text, sizeof(text), "%d", beat / GR_BEATS_BAR + 1); draw_text("Text", text, x + 5, 4, secondary); }
  }
  overview_draw(canvas->song, R(-canvas->tile_x, EXPORT_RULER_H, canvas->strip_w, r.h - EXPORT_RULER_H), 0, r.w);
}

static result_t win_export_canvas(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)wparam;
  if (msg == evCreate) { win->userdata = lparam; return true; }
  if (msg == evDestroy) { win->userdata = NULL; return true; }
  if (msg != evPaint) return false;
  const export_canvas_t *canvas = win->userdata;
  if (canvas->strip) export_paint_strip_tile(canvas, get_client_rect(win));
  else export_paint_backdrop(canvas, get_client_rect(win));
  return true;
}

// Paints a canvas window and resamples its surface (physical pixels) to the output size.
static uint8_t *export_capture(window_t *win, int width, int height) {
  send_message(win, evPaint, 0, NULL);
  uint8_t *pixels = NULL;
  int w = 0, h = 0;
  if (!window_capture(win, &pixels, &w, &h)) return NULL;
  uint8_t *out = malloc((size_t)width * height * 4);
  if (!out) {
    fprintf(stderr, "[groove-export] capture allocation failed size=%dx%d\n", width, height); fflush(stderr);
    free(pixels); return NULL;
  }
  for (int y = 0; y < height; y++) {
    float sy = MAX(0.0f, (y + 0.5f) * h / height - 0.5f), fy;
    int y0 = MIN((int)sy, h - 1), y1 = MIN(y0 + 1, h - 1); fy = sy - y0;
    for (int x = 0; x < width; x++) {
      float sx = MAX(0.0f, (x + 0.5f) * w / width - 0.5f), fx;
      int x0 = MIN((int)sx, w - 1), x1 = MIN(x0 + 1, w - 1); fx = sx - x0;
      const uint8_t *a = pixels + ((size_t)y0 * w + x0) * 4, *b = pixels + ((size_t)y0 * w + x1) * 4;
      const uint8_t *c = pixels + ((size_t)y1 * w + x0) * 4, *d = pixels + ((size_t)y1 * w + x1) * 4;
      uint8_t *o = out + ((size_t)y * width + x) * 4;
      for (int k = 0; k < 3; k++)
        o[k] = (uint8_t)((a[k] * (1 - fx) + b[k] * fx) * (1 - fy) + (c[k] * (1 - fx) + d[k] * fx) * fy + 0.5f);
      o[3] = 255;
    }
  }
  free(pixels);
  return out;
}

// ── Motion video ─────────────────────────────────────────────────────────
// Orion paints a static backdrop and a zoomed full-song strip once. The encoder
// worker then composites each frame in software: the strip scrolls with
// sub-pixel precision under an audio-clocked playhead, and a spectrum analyser
// follows the rendered mix.

typedef struct {
  int width, height, unit;
  uint8_t *background, *strip;
  int strip_w;                    // output pixels; strip height is view.h
  irect16_t view, eq;             // output pixels
  int64_t frames, count;          // audio frames and video frames
  int rate, fps;
  double beat_frames;
  uint8_t accent[3], band_rgb[VIDEO_BANDS][3];
  float *levels, *caps, *pulse;   // per video frame, written by song_export_video_analyze
} song_video_t;

static float video_smoothstep(float e0, float e1, float x) {
  float t = (x - e0) / (e1 - e0);
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return t * t * (3 - 2 * t);
}

static void video_rgb(uint32_t color, uint8_t rgb[3]) {
  rgb[0] = (uint8_t)color; rgb[1] = (uint8_t)(color >> 8); rgb[2] = (uint8_t)(color >> 16);
}

static void video_free(song_video_t *v) {
  if (!v) return;
  free(v->background); free(v->strip); free(v->levels); free(v->caps); free(v->pulse); free(v);
}

// Spectrum colours run through the song's own block families, track by track.
static void video_palette(song_video_t *v, const song_t *s) {
  uint32_t palette[GR_TRACKS + 1];
  int n = 0;
  for (int track = 0; track < GR_TRACKS; track++) {
    int first = -1;
    for (int i = 0; i < s->nclips; i++)
      if (s->clips[i].track == track && (first < 0 || s->clips[i].position < s->clips[first].position)) first = i;
    const block_t *b = first >= 0 ? block_get(s->clips[first].block) : NULL;
    if (!b) continue;
    uint32_t color = category_color(b->cat);
    bool seen = false;
    for (int k = 0; k < n; k++) seen |= (palette[k] & 0xffffff) == (color & 0xffffff);
    if (!seen) palette[n++] = color;
  }
  if (n < 2) palette[n++] = get_sys_color(brAccent);
  for (int band = 0; band < VIDEO_BANDS; band++) {
    float at = (float)band / (VIDEO_BANDS - 1) * (n - 1);
    int k = MIN((int)at, n - 2);
    float t = video_smoothstep(0, 1, at - k);
    uint8_t a[3], b[3]; video_rgb(palette[k], a); video_rgb(palette[k + 1], b);
    for (int c = 0; c < 3; c++) v->band_rgb[band][c] = (uint8_t)(a[c] + (b[c] - a[c]) * t);
  }
}

// The camera holds the playhead at 40% of the view and eases in and out at
// the song edges with a C1 quadratic knee, so scrolling never jerks.
static double video_camera(const song_video_t *v, double head) {
  double hi = v->strip_w - v->view.w, x = head - v->view.w * 0.4;
  if (hi <= 0) return 0;
  double k = MIN(v->view.w * 0.3, hi / 2);
  if (x <= -k) return 0;
  if (x < k) return (x + k) * (x + k) / (4 * k);
  if (x >= hi + k) return hi;
  if (x > hi - k) return hi - (hi + k - x) * (hi + k - x) / (4 * k);
  return x;
}

static void video_timeline(const song_video_t *v, uint8_t *rgba, double camera, double screen, float glow) {
  int w = v->view.w, h = v->view.h;
  float u = (float)v->unit;
  int base = (int)floor(camera), frac = (int)((camera - base) * 256);
  uint16_t dim[4096], fade[4096];
  int16_t add[4096][3];
  for (int col = 0; col < w; col++) {
    float dx = col + 0.5f - (float)screen;
    float upcoming = video_smoothstep(-2 * u, 26 * u, dx);
    float edge = video_smoothstep(0, 28 * u, col + 0.5f) * video_smoothstep(0, 28 * u, w - col - 0.5f);
    float core = MAX(0.0f, 1 - fabsf(dx) / (1.6f * u));
    float halo = 0.45f * expf(-dx * dx / (2 * 81 * u * u));
    float trail = dx < 0 ? 0.16f * expf(dx / (70 * u)) : 0;
    dim[col] = (uint16_t)((1 - 0.42f * upcoming) * 256);
    fade[col] = (uint16_t)(edge * 256);
    for (int c = 0; c < 3; c++)
      add[col][c] = (int16_t)MIN(510.0f, (v->accent[c] * (halo + trail) + 235 * core) * glow * edge);
  }
  for (int y = 0; y < h; y++) {
    const uint8_t *src = v->strip + (size_t)y * v->strip_w * 4;
    uint8_t *dst = rgba + ((size_t)(v->view.y + y) * v->width + v->view.x) * 4;
    for (int col = 0; col < w; col++, dst += 4) {
      int i0 = MIN(base + col, v->strip_w - 1), i1 = MIN(i0 + 1, v->strip_w - 1);
      for (int c = 0; c < 3; c++) {
        int s = ((src[i0 * 4 + c] * (256 - frac) + src[i1 * 4 + c] * frac) >> 8) * dim[col] >> 8;
        int o = dst[c] + (((s - dst[c]) * fade[col]) >> 8) + add[col][c];
        dst[c] = (uint8_t)(o > 255 ? 255 : o);
      }
    }
  }
}

static void video_blend(uint8_t *p, const float rgb[3], float alpha) {
  for (int c = 0; c < 3; c++) p[c] = (uint8_t)(p[c] + (rgb[c] - p[c]) * alpha + 0.5f);
}

static void video_equalizer(const song_video_t *v, uint8_t *rgba, int64_t index, float flare) {
  irect16_t r = v->eq;
  float u = (float)v->unit, slot = r.w / (float)VIDEO_BANDS, gap = MAX(2.0f, slot * 0.28f);
  int baseline = r.y + (int)(r.h * EXPORT_EQ_BASELINE);
  float room = baseline - r.y - 6 * u, mirror = (float)(r.y + r.h - baseline);
  for (int band = 0; band < VIDEO_BANDS; band++) {
    float level = v->levels ? v->levels[index * VIDEO_BANDS + band] : 0;
    float cap = v->caps ? v->caps[index * VIDEO_BANDS + band] : 0;
    int x0 = r.x + (int)(band * slot + gap / 2), x1 = r.x + (int)((band + 1) * slot - gap / 2);
    const uint8_t *tone = v->band_rgb[band];
    float top = baseline - MAX(level * room, 1.5f * u);
    for (int y = (int)floorf(top); y < baseline; y++) {
      float coverage = MIN(1.0f, y + 1 - top), lift = (baseline - y) / room;
      float shine = 0.4f * lift * lift + 0.12f * flare * lift, rgb[3];
      for (int c = 0; c < 3; c++) rgb[c] = tone[c] + (255 - tone[c]) * shine;
      uint8_t *p = rgba + ((size_t)y * v->width + x0) * 4;
      for (int x = x0; x < x1; x++, p += 4) video_blend(p, rgb, 0.94f * coverage);
    }
    float depth = MIN(mirror - u, (baseline - top) * 0.5f), rgb[3];
    for (int c = 0; c < 3; c++) rgb[c] = tone[c];
    for (int y = baseline + (int)u + 1; y < baseline + depth; y++) {
      float alpha = 0.26f * (1 - (y - baseline) / depth);
      uint8_t *p = rgba + ((size_t)y * v->width + x0) * 4;
      for (int x = x0; x < x1; x++, p += 4) video_blend(p, rgb, alpha);
    }
    float cap_top = baseline - cap * room - 4 * u, cap_bottom = cap_top + 2 * u;
    for (int c = 0; c < 3; c++) rgb[c] = tone[c] + (255 - tone[c]) * 0.6f;
    for (int y = (int)floorf(cap_top); y < (int)ceilf(cap_bottom); y++) {
      float coverage = MIN(y + 1.0f, cap_bottom) - MAX((float)y, cap_top);
      if (coverage <= 0 || y < r.y) continue;
      uint8_t *p = rgba + ((size_t)y * v->width + x0) * 4;
      for (int x = x0; x < x1; x++, p += 4) video_blend(p, rgb, 0.95f * coverage);
    }
  }
}

static bool song_video_frame(void *userdata, int64_t index, uint8_t *rgba) {
  const song_video_t *v = userdata;
  memcpy(rgba, v->background, (size_t)v->width * v->height * 4);
  double audio = (double)index * v->rate / v->fps;
  double head = MIN(audio, (double)v->frames) * v->strip_w / v->frames;
  int64_t at = MIN(index, v->count - 1);
  float bass = v->pulse ? v->pulse[at] : 0;
  float flare = expf(-7.0f * (float)fmod(audio / v->beat_frames, 1.0));
  double camera = video_camera(v, head);
  video_timeline(v, rgba, camera, head - camera, 0.7f + 0.4f * flare + 0.9f * bass);
  video_equalizer(v, rgba, at, flare);
  return true;
}

static void video_fft(float *re, float *im, int n) {
  for (int i = 1, j = 0; i < n; i++) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
  }
  for (int len = 2; len <= n; len <<= 1) {
    double angle = -2 * 3.14159265358979323846 / len;
    float wr = (float)cos(angle), wi = (float)sin(angle);
    for (int i = 0; i < n; i += len) {
      float cr = 1, ci = 0;
      for (int k = 0; k < len / 2; k++) {
        float *ar = &re[i + k], *ai = &im[i + k], *br = &re[i + k + len / 2], *bi = &im[i + k + len / 2];
        float vr = *br * cr - *bi * ci, vi = *br * ci + *bi * cr;
        *br = *ar - vr; *bi = *ai - vi; *ar += vr; *ai += vi;
        float next = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = next;
      }
    }
  }
}

// One FFT per video frame over the exported mix, mapped to log-spaced bands
// with a pink tilt, then smoothed: fast attack, slow release, falling caps.
bool song_export_video_analyze(ui_media_video_t *video, const char *wav, ui_media_progress_fn progress, void *userdata) {
  song_video_t *v = video && video->render == song_video_frame ? video->userdata : NULL;
  if (!v || !wav) { fprintf(stderr, "[groove-export] video analysis rejected video=%p wav=%p\n", (void *)v, (void *)wav); fflush(stderr); return false; }
  FILE *f = fopen(wav, "rb");
  float *levels = calloc((size_t)v->count * VIDEO_BANDS, sizeof(float));
  float *caps = calloc((size_t)v->count * VIDEO_BANDS, sizeof(float)), *pulse = calloc((size_t)v->count, sizeof(float));
  bool ok = f && levels && caps && pulse;
  if (!ok) fprintf(stderr, "[groove-export] video analysis unavailable wav=%s: %s\n", wav, strerror(errno));
  float window[VIDEO_FFT], re[VIDEO_FFT], im[VIDEO_FFT], lo[VIDEO_BANDS], hi[VIDEO_BANDS], tilt[VIDEO_BANDS];
  float level[VIDEO_BANDS] = {0}, cap[VIDEO_BANDS] = {0}, fall[VIDEO_BANDS] = {0}, bass = 0;
  int hold[VIDEO_BANDS] = {0};
  int16_t pcm[VIDEO_FFT * 2];
  for (int i = 0; i < VIDEO_FFT; i++) window[i] = 0.5f - 0.5f * cosf(2 * 3.14159265f * i / (VIDEO_FFT - 1));
  for (int band = 0; band < VIDEO_BANDS; band++) {
    float f0 = 35.0f * powf(16000.0f / 35.0f, (float)band / VIDEO_BANDS), f1 = 35.0f * powf(16000.0f / 35.0f, (float)(band + 1) / VIDEO_BANDS);
    lo[band] = f0 * VIDEO_FFT / v->rate; hi[band] = f1 * VIDEO_FFT / v->rate;
    tilt[band] = 3.0f * log2f(sqrtf(f0 * f1) / 1000.0f);
  }
  for (int64_t frame = 0; ok && frame < v->count; frame++) {
    if (progress && frame % 64 == 0 && !progress(userdata, (float)frame / v->count)) { ok = false; break; }
    int64_t start = frame * v->rate / v->fps - VIDEO_FFT / 2, first = MAX(0, start), last = MIN(v->frames, start + VIDEO_FFT);
    memset(pcm, 0, sizeof(pcm));
    if (last > first) {
      ok = fseek(f, 44 + (long)first * 4, SEEK_SET) == 0 &&
           fread(pcm + (first - start) * 2, 4, (size_t)(last - first), f) == (size_t)(last - first);
    }
    for (int i = 0; i < VIDEO_FFT; i++) { re[i] = (pcm[i * 2] + pcm[i * 2 + 1]) / 65536.0f * window[i]; im[i] = 0; }
    video_fft(re, im, VIDEO_FFT);
    float target[VIDEO_BANDS];
    for (int band = 0; band < VIDEO_BANDS; band++) {
      float mag = 0;
      if (hi[band] - lo[band] < 1) {
        float at = (lo[band] + hi[band]) / 2, t = at - floorf(at);
        int k = (int)at;
        mag = hypotf(re[k], im[k]) * (1 - t) + hypotf(re[k + 1], im[k + 1]) * t;
      } else for (int k = (int)ceilf(lo[band]); k <= (int)hi[band] && k < VIDEO_FFT / 2; k++) mag = MAX(mag, hypotf(re[k], im[k]));
      float db = 20 * log10f(mag * 4 / VIDEO_FFT + 1e-9f) + tilt[band];
      float t = (db + 64) / 58;
      target[band] = t <= 0 ? 0 : t >= 1 ? 1 : powf(t, 1.35f);
    }
    float bass_target = 0;
    for (int band = 0; band < VIDEO_BANDS; band++) {
      float s = 0.25f * target[MAX(0, band - 1)] + 0.5f * target[band] + 0.25f * target[MIN(VIDEO_BANDS - 1, band + 1)];
      level[band] += (s - level[band]) * (s > level[band] ? 0.55f : 0.14f);
      if (level[band] >= cap[band]) { cap[band] = level[band]; fall[band] = 0; hold[band] = 18; }
      else if (hold[band] > 0) hold[band]--;
      else { fall[band] += 0.0012f; cap[band] = MAX(level[band], cap[band] - fall[band]); }
      if (band < 6) bass_target += level[band] / 6;
    }
    bass += (bass_target - bass) * (bass_target > bass ? 0.6f : 0.12f);
    memcpy(levels + frame * VIDEO_BANDS, level, sizeof(level));
    memcpy(caps + frame * VIDEO_BANDS, cap, sizeof(cap));
    pulse[frame] = bass;
  }
  if (f) fclose(f);
  if (!ok) { free(levels); free(caps); free(pulse); fflush(stderr); return false; }
  free(v->levels); free(v->caps); free(v->pulse);
  v->levels = levels; v->caps = caps; v->pulse = pulse;
  return true;
}

void song_export_video_free(ui_media_video_t *video) {
  if (video && video->render == song_video_frame) video_free(video->userdata);
  if (video) *video = (ui_media_video_t){0};
}

bool song_export_canvas(const song_t *s, int width, int height, const char *title, bool branding, ui_media_video_t *video) {
  bool portrait = width < height;
  // Lay out the portrait artwork at 2x so its type and spacing read at phone size.
  int unit = portrait ? 2 : 1, canvas_w = width / unit, canvas_h = height / unit;
  int span = MAX(1, song_length_ticks(s));
  export_canvas_t art = { .song = s, .title = title ? title : "", .portrait = portrait, .branding = branding,
    .view = portrait ? R(58, 336, canvas_w - 120, EXPORT_RULER_H + 320) : R(64, 66, canvas_w - 96, EXPORT_RULER_H + 360),
    .eq = portrait ? R(58, 692, canvas_w - 120, 108) : R(64, 470, canvas_w - 96, 226) };
  // A fixed zoom keeps a few bars readable; a short song fills the view instead of scrolling.
  art.strip_w = MAX(art.view.w, (int)((int64_t)span * (portrait ? 104 : 144) / GR_TICKS_BAR));
  if (portrait && branding) export_canvas_load_logo(&art);
  flags_t flags = WINDOW_NOTITLE | WINDOW_NOFILL | WINDOW_NOACTIVATE | WINDOW_NOTABSTOP;
  song_video_t *v = calloc(1, sizeof(*v));
  window_t *backdrop = v ? create_window("", flags, MAKERECT(0, 0, canvas_w, canvas_h), NULL, win_export_canvas, 0, &art) : NULL;
  window_t *tile = backdrop ? create_window("", flags, MAKERECT(0, 0, art.view.w, art.view.h), NULL, win_export_canvas, 0, &art) : NULL;
  bool ok = tile != NULL;
  if (ok) {
    *v = (song_video_t){ .width = width, .height = height, .unit = unit, .strip_w = art.strip_w * unit,
      .view = rect_scale(art.view, unit), .eq = rect_scale(art.eq, unit),
      .frames = position_frames_for_bpm(span, s->bpm), .rate = GR_SAMPLE_RATE, .fps = EXPORT_FPS,
      .beat_frames = bar_frames_for_bpm(s->bpm) / (double)GR_BEATS_BAR };
    v->count = (v->frames * v->fps + v->rate - 1) / v->rate;
    video_rgb(get_sys_color(brAccent), v->accent);
    video_palette(v, s);
    v->background = export_capture(backdrop, width, height);
    v->strip = malloc((size_t)v->strip_w * v->view.h * 4);
    ok = v->background && v->strip && v->frames > 0;
  }
  art.strip = true;
  for (art.tile_x = 0; ok && art.tile_x < art.strip_w; art.tile_x += art.view.w) {
    uint8_t *pixels = export_capture(tile, v->view.w, v->view.h);
    int columns = MIN(art.view.w, art.strip_w - art.tile_x) * unit;
    ok = pixels != NULL;
    if (ok) for (int y = 0; y < v->view.h; y++)
      memcpy(v->strip + ((size_t)y * v->strip_w + (size_t)art.tile_x * unit) * 4, pixels + (size_t)y * v->view.w * 4, (size_t)columns * 4);
    free(pixels);
  }
  if (tile) destroy_window(tile);
  if (backdrop) destroy_window(backdrop);
  if (art.logo) R_DeleteTexture(art.logo);
  if (!ok) { video_free(v); return false; }
  *video = (ui_media_video_t){ .width = width, .height = height,
    .x = v->view.x, .y = v->view.y, .w = v->view.w, .h = v->view.h, .color = get_sys_color(brAccent),
    .frames = v->frames, .rate = v->rate, .fps = v->fps, .render = song_video_frame, .userdata = v };
  return true;
}

bool song_export_wav(const song_t *song, const char *path, ui_media_progress_fn progress, void *userdata) {
  return song_export_wav_with_audio(song, NULL, path, progress, userdata);
}
