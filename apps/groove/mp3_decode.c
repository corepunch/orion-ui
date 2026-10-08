#include "groove.h"

#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
#include "minimp3_ex.h"

static bool read_mp3(const char *filename, uint8_t **data, int *size) {
  char path[1024];
  const char *exe = ui_get_exe_dir();
  int n = snprintf(path, sizeof(path), "%s/../share/groove/vocals/%s", exe, filename);
  FILE *fp = n > 0 && (size_t)n < sizeof(path) ? fopen(path, "rb") : NULL;
  if (!fp) {
    n = snprintf(path, sizeof(path), "apps/groove/share/vocals/%s", filename);
    fp = n > 0 && (size_t)n < sizeof(path) ? fopen(path, "rb") : NULL;
  }
  if (!fp) {
    fprintf(stderr, "[gr] MP3 open failed file=%s path=%s\n", filename, path);
    fflush(stderr);
    return false;
  }
  if (fseek(fp, 0, SEEK_END) || (*size = (int)ftell(fp)) <= 0 || fseek(fp, 0, SEEK_SET)) {
    fprintf(stderr, "[gr] MP3 size/read failed file=%s\n", filename);
    fflush(stderr);
    fclose(fp);
    return false;
  }
  *data = malloc((size_t)*size);
  if (!*data) { fprintf(stderr, "[gr] MP3 allocation failed file=%s bytes=%d\n", filename, *size); fflush(stderr); fclose(fp); return false; }
  bool ok = fread(*data, 1, (size_t)*size, fp) == (size_t)*size;
  fclose(fp);
  if (!ok) { fprintf(stderr, "[gr] MP3 read failed file=%s bytes=%d\n", filename, *size); fflush(stderr); free(*data); *data = NULL; }
  return ok;
}

bool groove_mp3_load(const char *filename, int frames, float **pcm) {
  if (!filename || !*filename || frames <= 0 || !pcm) {
    fprintf(stderr, "[gr] MP3 load rejected file=%s frames=%d pcm=%p\n", filename ? filename : "(null)", frames, (void *)pcm);
    fflush(stderr);
    return false;
  }
  *pcm = NULL;
  uint8_t *data = NULL;
  int bytes = 0;
  if (!read_mp3(filename, &data, &bytes)) return false;
  mp3dec_ex_t dec;
  int err = mp3dec_ex_open_buf(&dec, data, (size_t)bytes, MP3D_SEEK_TO_SAMPLE);
  if (err) {
    fprintf(stderr, "[gr] MP3 open/decode failed file=%s error=%d\n", filename, err);
    fflush(stderr);
    free(data);
    return false;
  }
  size_t total_samples = (size_t)dec.samples;
  int channels = dec.info.channels, hz = dec.info.hz;
  mp3d_sample_t *decoded = malloc(total_samples * sizeof(*decoded));
  if (!decoded) { fprintf(stderr, "[gr] MP3 decode allocation failed file=%s samples=%zu\n", filename, total_samples); fflush(stderr); mp3dec_ex_close(&dec); free(data); return false; }
  size_t used = mp3dec_ex_read(&dec, decoded, total_samples);
  int decode_error = dec.last_error;
  mp3dec_ex_close(&dec);
  free(data);
  if (decode_error || channels < 1 || channels > 2 || hz != GR_SAMPLE_RATE || used != total_samples || total_samples < (size_t)channels * 2) {
    fprintf(stderr, "[gr] MP3 decode failed file=%s error=%d channels=%d hz=%d samples=%zu/%zu\n", filename, decode_error, channels, hz, used, total_samples);
    fflush(stderr);
    free(decoded);
    return false;
  }
  size_t source_frames = total_samples / (size_t)channels;
  float *out = malloc((size_t)frames * sizeof(*out));
  if (!out) { fprintf(stderr, "[gr] MP3 output allocation failed file=%s frames=%d\n", filename, frames); fflush(stderr); free(decoded); return false; }
  for (int i = 0; i < frames; i++) {
    double at = (double)i * (source_frames - 1) / MAX(1, frames - 1);
    size_t a = (size_t)at, b = MIN(a + 1, source_frames - 1);
    float t = (float)(at - a);
    int32_t sa = 0, sb = 0;
    for (int ch = 0; ch < channels; ch++) {
      sa += decoded[a * channels + ch];
      sb += decoded[b * channels + ch];
    }
    out[i] = ((float)(sa / channels) * (1.0f - t) + (float)(sb / channels) * t) / 32768.0f;
  }
  free(decoded);
  *pcm = out;
  return true;
}
