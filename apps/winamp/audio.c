// AUDIO: streams an MP3 through minimp3, resamples to the device rate and
// applies balance, volume and Winamp's ten-band equalizer. engine_render runs
// on the audio thread; the UI changes engine state under axAudioLock.

#include "winamp.h"

#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "apps/groove/minimp3_ex.h"

static const float kBandHz[WA_BANDS] = { 60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000, 16000 };

void engine_close(wa_engine_t *e) {
  if (e->dec) { mp3dec_ex_close(e->dec); free(e->dec); e->dec = NULL; }
  free(e->file);
  e->file = NULL; e->file_size = 0;
  e->frames = e->pos = 0;
  e->buf_len = e->buf_at = 0;
  e->ended = false;
}

bool engine_open(wa_engine_t *e, const char *path) {
  engine_close(e);
  FILE *fp = fopen(path, "rb");
  if (!fp) { fprintf(stderr, "[wa] open failed path=%s\n", path); fflush(stderr); return false; }
  long n = fseek(fp, 0, SEEK_END) ? -1 : ftell(fp);
  if (n <= 0 || fseek(fp, 0, SEEK_SET) || !(e->file = malloc((size_t)n)) || fread(e->file, 1, (size_t)n, fp) != (size_t)n) {
    fprintf(stderr, "[wa] read failed path=%s bytes=%ld\n", path, n);
    fflush(stderr);
    fclose(fp);
    engine_close(e);
    return false;
  }
  fclose(fp);
  e->file_size = (size_t)n;
  mp3dec_ex_t *dec = calloc(1, sizeof(*dec));
  int err = dec ? mp3dec_ex_open_buf(dec, e->file, e->file_size, MP3D_SEEK_TO_SAMPLE) : MP3D_E_MEMORY;
  if (err || !dec->info.hz || dec->info.channels < 1 || dec->info.channels > 2) {
    fprintf(stderr, "[wa] mp3 open failed path=%s error=%d hz=%d channels=%d\n", path, err,
            dec ? dec->info.hz : 0, dec ? dec->info.channels : 0);
    fflush(stderr);
    if (dec && !err) mp3dec_ex_close(dec);
    free(dec);
    engine_close(e);
    return false;
  }
  e->dec = dec;
  e->hz = dec->info.hz;
  e->channels = dec->info.channels;
  e->frames = dec->samples / (uint64_t)e->channels;
  e->phase = 1.0f;   // fetch two frames before the first output
  e->cur[0] = e->cur[1] = e->nxt[0] = e->nxt[1] = 0;
  memset(e->eq_z, 0, sizeof(e->eq_z));
  return true;
}

void engine_seek(wa_engine_t *e, uint64_t frame) {
  if (!e->dec) return;
  if (frame > e->frames) frame = e->frames;
  if (mp3dec_ex_seek(e->dec, frame * (uint64_t)e->channels)) {
    fprintf(stderr, "[wa] seek failed frame=%llu frames=%llu\n", (unsigned long long)frame, (unsigned long long)e->frames);
    fflush(stderr);
    return;
  }
  e->pos = frame;
  e->buf_len = e->buf_at = 0;
  e->phase = 1.0f;
  e->ended = false;
}

static bool next_frame(wa_engine_t *e, float out[2]) {
  if (e->buf_at >= e->buf_len) {
    size_t want = (sizeof(e->buf) / sizeof(e->buf[0])) / (size_t)e->channels * (size_t)e->channels;
    e->buf_len = (int)mp3dec_ex_read(e->dec, e->buf, want);
    e->buf_at = 0;
    if (e->buf_len <= 0) return false;
  }
  float l = e->buf[e->buf_at] / 32768.0f;
  float r = e->channels == 2 ? e->buf[e->buf_at + 1] / 32768.0f : l;
  e->buf_at += e->channels;
  e->pos++;
  out[0] = l; out[1] = r;
  return true;
}

static float biquad(const wa_biquad_t *q, float z[2], float x) {
  float y = q->b0 * x + z[0];
  z[0] = q->b1 * x - q->a1 * y + z[1];
  z[1] = q->b2 * x - q->a2 * y;
  return y;
}

void engine_render(wa_engine_t *e, int16_t *out, int frames) {
  bool live = e->dec && e->state == WA_PLAYING && !e->ended;
  float step = live ? (float)e->hz / WA_RATE : 0;
  float lg = e->volume * (e->balance > 0 ? 1 - e->balance : 1);
  float rg = e->volume * (e->balance < 0 ? 1 + e->balance : 1);
  for (int i = 0; i < frames; i++) {
    float s[2] = { 0, 0 };
    if (live) {
      while (e->phase >= 1.0f) {
        e->phase -= 1.0f;
        e->cur[0] = e->nxt[0]; e->cur[1] = e->nxt[1];
        if (!next_frame(e, e->nxt)) { e->ended = true; live = false; break; }
      }
      if (live) {
        for (int ch = 0; ch < 2; ch++) s[ch] = e->cur[ch] + (e->nxt[ch] - e->cur[ch]) * e->phase;
        e->phase += step;
      }
    }
    if (e->eq_on) {
      for (int ch = 0; ch < 2; ch++) {
        s[ch] *= e->preamp_gain;
        for (int b = 0; b < WA_BANDS; b++) s[ch] = biquad(&e->eq[b], e->eq_z[b][ch], s[ch]);
      }
    }
    e->vis_ring[e->vis_at] = (s[0] + s[1]) * 0.5f;
    e->vis_at = (e->vis_at + 1) % WA_VIS_N;
    float l = s[0] * lg, r = s[1] * rg;
    l = l > 1 ? 1 : l < -1 ? -1 : l;
    r = r > 1 ? 1 : r < -1 ? -1 : r;
    out[i * 2] = (int16_t)(l * 32767);
    out[i * 2 + 1] = (int16_t)(r * 32767);
  }
}

// RBJ peaking filters at Winamp's band centres.
void engine_set_eq(wa_engine_t *e, const float db[WA_BANDS], float preamp_db, bool on) {
  for (int b = 0; b < WA_BANDS; b++) {
    float a = powf(10.0f, db[b] / 40.0f);
    float w = 2.0f * (float)M_PI * MIN(kBandHz[b], WA_RATE * 0.45f) / WA_RATE;
    float alpha = sinf(w) / (2.0f * 1.2f), cw = cosf(w);
    float a0 = 1 + alpha / a;
    e->eq[b] = (wa_biquad_t){ (1 + alpha * a) / a0, -2 * cw / a0, (1 - alpha * a) / a0, -2 * cw / a0, (1 - alpha / a) / a0 };
  }
  e->preamp_gain = powf(10.0f, preamp_db / 20.0f);
  if (on && !e->eq_on) memset(e->eq_z, 0, sizeof(e->eq_z));
  e->eq_on = on;
}
