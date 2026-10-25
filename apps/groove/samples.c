// Owned microphone recordings. Files and embedded assets use the same bounded WAV representation.
#include "groove.h"
#include <stdatomic.h>

typedef struct { sample_t source; block_t block; char key[32]; } recorded_block_t;
static recorded_block_t recorded[GR_MAX_BLOCKS];
static atomic_int recorded_count;

static bool sample_equal(const sample_t *a, const sample_t *b) {
  return a->frames == b->frames && a->bpm == b->bpm && a->bars == b->bars && !strcmp(a->name, b->name) &&
    !memcmp(a->pcm, b->pcm, (size_t)a->frames * sizeof(int16_t));
}

static bool sample_valid(const sample_t *s) {
  return s && s->pcm && s->name[0] && memchr(s->name, 0, sizeof(s->name)) &&
    s->bpm >= GR_BPM_MIN && s->bpm <= GR_BPM_MAX && (s->bars == 1 || s->bars == 2 || s->bars == 4) &&
    s->frames == s->bars * bar_frames_for_bpm(s->bpm);
}

static bool sample_u32_write(FILE *f, uint32_t value) {
  uint8_t b[] = { value, value >> 8, value >> 16, value >> 24 };
  return fwrite(b, 1, sizeof(b), f) == sizeof(b);
}
static bool sample_u32_read(FILE *f, uint32_t *value) {
  uint8_t b[4];
  if (fread(b, 1, sizeof(b), f) != sizeof(b)) return false;
  *value = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
  return true;
}

bool sample_write(FILE *f, const sample_t *s) {
  if (!f || !sample_valid(s)) return false;
  uint32_t bytes = (uint32_t)s->frames * 2;
  const uint8_t fmt[] = { 1, 0, 1, 0, 0x44, 0xac, 0, 0, 0x88, 0x58, 1, 0, 2, 0, 16, 0 };
  bool ok = fwrite("RIFF", 1, 4, f) == 4 && sample_u32_write(f, 116 + bytes) &&
    fwrite("WAVEfmt ", 1, 8, f) == 8 && sample_u32_write(f, sizeof(fmt)) && fwrite(fmt, 1, sizeof(fmt), f) == sizeof(fmt) &&
    fwrite("gruv", 1, 4, f) == 4 && sample_u32_write(f, 72) && sample_u32_write(f, s->bpm) && sample_u32_write(f, s->bars) &&
    fwrite(s->name, 1, sizeof(s->name), f) == sizeof(s->name) && fwrite("data", 1, 4, f) == 4 && sample_u32_write(f, bytes);
  for (int i = 0; ok && i < s->frames; i++) {
    uint16_t v = (uint16_t)s->pcm[i]; uint8_t b[] = { v, v >> 8 };
    ok = fwrite(b, 1, 2, f) == 2;
  }
  return ok;
}

bool sample_read(FILE *f, sample_t *out) {
  if (!f || !out) return false;
  sample_t s = {0};
  uint32_t size, fmt_size, meta_size, bpm, bars, bytes;
  uint8_t riff[4], wave[8], fmt[16], meta[4], data[4];
  const uint8_t expected[] = { 1, 0, 1, 0, 0x44, 0xac, 0, 0, 0x88, 0x58, 1, 0, 2, 0, 16, 0 };
  if (fread(riff, 1, 4, f) != 4 || memcmp(riff, "RIFF", 4) || !sample_u32_read(f, &size) ||
      fread(wave, 1, 8, f) != 8 || memcmp(wave, "WAVEfmt ", 8) || !sample_u32_read(f, &fmt_size) || fmt_size != 16 ||
      fread(fmt, 1, 16, f) != 16 || memcmp(fmt, expected, 16) || fread(meta, 1, 4, f) != 4 || memcmp(meta, "gruv", 4) ||
      !sample_u32_read(f, &meta_size) || meta_size != 72 || !sample_u32_read(f, &bpm) || !sample_u32_read(f, &bars) ||
      fread(s.name, 1, sizeof(s.name), f) != sizeof(s.name) || !s.name[0] || !memchr(s.name, 0, sizeof(s.name)) ||
      fread(data, 1, 4, f) != 4 || memcmp(data, "data", 4) || !sample_u32_read(f, &bytes) ||
      bpm < GR_BPM_MIN || bpm > GR_BPM_MAX || (bars != 1 && bars != 2 && bars != 4) ||
      bytes != bars * (uint32_t)bar_frames_for_bpm((int)bpm) * 2 || size != 116 + bytes) return false;
  s.bpm = bpm; s.bars = bars; s.frames = bytes / 2;
  if (!(s.pcm = malloc(bytes))) return false;
  for (int i = 0; i < s.frames; i++) {
    uint8_t b[2];
    if (fread(b, 1, 2, f) != 2) { free(s.pcm); return false; }
    s.pcm[i] = (int16_t)((uint16_t)b[0] | (uint16_t)b[1] << 8);
  }
  *out = s;
  return true;
}

int samples_count(void) { return recorded_count; }
const sample_t *sample_get(int id) {
  int i = id - blocks_builtin_count();
  return i >= 0 && i < recorded_count ? &recorded[i].source : NULL;
}
const block_t *sample_block_get(int id) {
  int i = id - blocks_builtin_count();
  return i >= 0 && i < recorded_count ? &recorded[i].block : NULL;
}

int sample_add(const sample_t *s) {
  if (!sample_valid(s)) return -1;
  size_t bytes = (size_t)s->frames * sizeof(int16_t);
  for (int i = 0; i < recorded_count; i++) {
    const sample_t *old = &recorded[i].source;
    if (sample_equal(old, s)) return blocks_builtin_count() + i;
    bytes += (size_t)old->frames * sizeof(int16_t);
  }
  if (blocks_builtin_count() + recorded_count >= GR_MAX_BLOCKS || bytes > 64u * 1024u * 1024u) return -1;
  recorded_block_t *r = &recorded[recorded_count];
  int16_t *copy = malloc((size_t)s->frames * 2);
  if (!copy) return -1;
  memcpy(copy, s->pcm, (size_t)s->frames * 2);
  r->source = *s; r->source.pcm = copy;
  snprintf(r->key, sizeof(r->key), "Recording:%d", recorded_count);
  r->block = (block_t){ .name = r->key, .display_name = r->source.name, .cat = CAT_VOX,
    .family = FAMILY_SAMPLES, .genres = GENRE_ANY, .bars = s->bars };
  return blocks_builtin_count() + recorded_count++;
}

void samples_truncate(int count) {
  while (recorded_count > count) {
    recorded_block_t *r = &recorded[--recorded_count];
    free(r->source.pcm); free(r->block.audio.pcm);
    memset(r, 0, sizeof(*r));
  }
}

bool sample_render(int id, int bpm, block_pcm_t *out) {
  const sample_t *s = sample_get(id);
  if (!s || !out) return false;
  *out = (block_pcm_t){ .frames = s->bars * bar_frames_for_bpm(bpm), .npeaks = s->bars * GR_PEAKS_BAR };
  out->pcm = calloc((size_t)out->frames, sizeof(float));
  if (!out->pcm) return false;
  // Keep recorded voices at their original pitch/speed. A changed tempo crops or pads the block.
  for (int i = 0; i < MIN(s->frames, out->frames); i++) out->pcm[i] = s->pcm[i] / 32768.0f;
  for (int k = 0; k < out->npeaks; k++) {
    float peak = 0;
    for (int j = (int)((int64_t)out->frames * k / out->npeaks); j < (int)((int64_t)out->frames * (k + 1) / out->npeaks); j++)
      peak = fmaxf(peak, fabsf(out->pcm[j]));
    out->peaks[k] = (uint8_t)(peak * 255);
  }
  return true;
}

static bool_t sample_scan(const AXdirent *entry, void *userdata) {
  const char *dir = userdata;
  size_t n = strlen(entry->name);
  if (entry->is_directory || entry->is_hidden || n < 4 || strcasecmp(entry->name + n - 4, ".wav")) return true;
  char path[1024];
  int len = snprintf(path, sizeof(path), "%s/%s", dir, entry->name);
  FILE *f = len > 0 && (size_t)len < sizeof(path) ? fopen(path, "rb") : NULL;
  sample_t sample = {0};
  if (f) {
    if (sample_read(f, &sample) && fgetc(f) == EOF) sample_add(&sample);
    free(sample.pcm); fclose(f);
  }
  return true;
}

void samples_load(const char *dir) { if (dir && dir[0]) axListDir(dir, sample_scan, (void *)dir); }

bool sample_save(const char *dir, const sample_t *s) {
  if (!dir || !dir[0] || !sample_valid(s)) return false;
  // Never use a user-supplied title as a path, and never overwrite an existing recording.
  char path[1024], temp[1030];
  unsigned int serial = 1;
  do {
    int n = snprintf(path, sizeof(path), "%s/recording-%u.wav", dir, serial++);
    if (n <= 0 || (size_t)n >= sizeof(path)) return false;
    if (axPathExists(path)) {
      FILE *existing = fopen(path, "rb");
      sample_t old = {0};
      bool match = existing && sample_read(existing, &old) && fgetc(existing) == EOF && sample_equal(&old, s);
      if (existing) fclose(existing);
      free(old.pcm);
      if (match) return true;
    }
  } while (axPathExists(path) && serial);
  snprintf(temp, sizeof(temp), "%s.tmp", path);
  FILE *f = fopen(temp, "wb");
  if (!f) return false;
  bool ok = sample_write(f, s);
  if (fclose(f)) ok = false;
  if (ok && rename(temp, path)) ok = false;
  if (!ok) remove(temp);
  return ok;
}
