#ifndef __FLC_CODEC_H__
#define __FLC_CODEC_H__

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// FLI byte packets: BRUN repeats are positive; LC repeats are negative.
static inline uint16_t flc_codec_u16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static inline uint32_t flc_codec_u32(const uint8_t *p) { return flc_codec_u16(p) | (uint32_t)flc_codec_u16(p + 2) << 16; }
static inline void flc_codec_put16(uint8_t *p, uint16_t n) { p[0] = n; p[1] = n >> 8; }
static inline void flc_codec_put32(uint8_t *p, uint32_t n) { flc_codec_put16(p, n); flc_codec_put16(p + 2, n >> 16); }

static inline int flc_codec_run(const uint8_t *p, int available, int limit) {
  int n = 1;
  while (n < available && n < limit && p[n] == *p) n++;
  return n;
}

static inline size_t flc_codec_brun(uint8_t *out, size_t capacity, const uint8_t *pixels, int w, int h) {
  size_t pos = 0;
  for (int y = 0; y < h; y++) {
    const uint8_t *row = pixels + (size_t)y * w;
    if (pos == capacity) return 0;
    size_t count_pos = pos++;
    int packets = 0;
    for (int x = 0; x < w;) {
      int run = flc_codec_run(row + x, w - x, 127);
      if (run >= 3) {
        if (capacity - pos < 2) return 0;
        out[pos++] = run; out[pos++] = row[x]; x += run;
      } else {
        int start = x++;
        while (x < w && x - start < 128 && flc_codec_run(row + x, w - x, 127) < 3) x++;
        int length = x - start;
        if (capacity - pos < (size_t)length + 1) return 0;
        out[pos++] = (uint8_t)-length; memcpy(out + pos, row + start, length); pos += length;
      }
      packets++;
    }
    out[count_pos] = packets > 255 ? 0 : packets;
  }
  return pos;
}

static inline size_t flc_codec_lc(uint8_t *out, size_t capacity, const uint8_t *pixels,
                                  const uint8_t *previous, int w, int h) {
  if (capacity < 4) return 0;
  int first = 0, end = h;
  while (first < h && !memcmp(pixels + (size_t)first * w, previous + (size_t)first * w, w)) first++;
  while (end > first && !memcmp(pixels + (size_t)(end - 1) * w, previous + (size_t)(end - 1) * w, w)) end--;
  flc_codec_put16(out, first); flc_codec_put16(out + 2, end - first);
  size_t pos = 4;
  for (int y = first; y < end; y++) {
    const uint8_t *row = pixels + (size_t)y * w, *old = previous + (size_t)y * w;
    if (pos == capacity) return 0;
    size_t count_pos = pos++;
    int packets = 0, x = 0, last = w;
    while (last && row[last - 1] == old[last - 1]) last--;
    while (x < last) {
      int skip = 0;
      while (x + skip < last && row[x + skip] == old[x + skip]) skip++;
      while (skip > 255) {
        if (packets == 255 || capacity - pos < 2) return 0;
        out[pos++] = 255; out[pos++] = 0; packets++; x += 255; skip -= 255;
      }
      x += skip;
      int run = flc_codec_run(row + x, last - x, 128);
      if (packets == 255 || capacity - pos < 3) return 0;
      out[pos++] = skip;
      if (run >= 3) {
        out[pos++] = (uint8_t)-run; out[pos++] = row[x]; x += run;
      } else {
        int start = x++;
        while (x < last && x - start < 127 && flc_codec_run(row + x, last - x, 128) < 3 &&
               !(row[x] == old[x] && x + 1 < last && row[x + 1] == old[x + 1])) x++;
        int length = x - start;
        if (capacity - pos < (size_t)length + 1) return 0;
        out[pos++] = length; memcpy(out + pos, row + start, length); pos += length;
      }
      packets++;
    }
    out[count_pos] = packets;
  }
  return pos;
}

// Returns a complete, word-aligned FLIC pixel subchunk. COPY bounds expansion.
static inline uint8_t *flc_codec_encode(const uint8_t *pixels, const uint8_t *previous,
                                        int w, int h, size_t *size) {
  if (!pixels || w < 1 || h < 1 || w > 16384 || h > 16384) return NULL;
  size_t n = (size_t)w * h, payload = n;
  bool zero = true;
  for (size_t i = 0; i < n; i++) if (pixels[i]) { zero = false; break; }
  uint8_t *chunk = malloc(zero ? 6 : n + (n & 1) + 6);
  if (!chunk) return NULL;
  uint16_t type = zero ? 13 : 16;
  if (zero) payload = 0;
  else {
    memcpy(chunk + 6, pixels, n);
    uint8_t *scratch = malloc(n);
    if (!scratch) { free(chunk); return NULL; }
    size_t length = flc_codec_brun(scratch, payload, pixels, w, h);
    if (length && length < payload) { payload = length; type = 15; memcpy(chunk + 6, scratch, length); }
    if (previous) {
      length = flc_codec_lc(scratch, payload, pixels, previous, w, h);
      if (length && length < payload) { payload = length; type = 12; memcpy(chunk + 6, scratch, length); }
    }
    free(scratch);
  }
  if (payload & 1) chunk[6 + payload] = 0;
  *size = 6 + payload + (payload & 1);
  flc_codec_put32(chunk, *size); flc_codec_put16(chunk + 4, type);
  uint8_t *packed = realloc(chunk, *size);
  if (!packed) free(chunk);
  return packed;
}

static inline bool flc_codec_decode(const uint8_t *chunk, size_t size, uint8_t *pixels,
                                     const uint8_t *previous, int w, int h) {
  if (!chunk || !pixels || w < 1 || h < 1 || w > 16384 || h > 16384 || size < 6 ||
      flc_codec_u32(chunk) != size || (size & 1)) return false;
  const uint8_t *p = chunk + 6, *end = chunk + size;
  uint16_t type = flc_codec_u16(chunk + 4);
  size_t n = (size_t)w * h;
  if (type == 13) memset(pixels, 0, n);
  else if (type == 16) {
    if ((size_t)(end - p) < n) return false;
    memcpy(pixels, p, n); p += n;
  } else if (type == 15 || type == 12) {
    int y = 0, lines = h;
    if (type == 12) {
      if (!previous || end - p < 4) return false;
      if (pixels != previous) memcpy(pixels, previous, n);
      y = flc_codec_u16(p); lines = flc_codec_u16(p + 2); p += 4;
      if (y > h || lines > h - y) return false;
    }
    while (lines--) {
      if (p == end) return false;
      int packets = *p++, x = 0;
      while (type == 15 ? x < w : packets-- > 0) {
        if (type == 12) { if (p == end) return false; x += *p++; }
        if (p == end) return false;
        int count = (int8_t)*p++, length = abs(count);
        bool repeat = type == 15 ? count > 0 : count < 0;
        if ((type == 15 && !length) || x > w || length > w - x || (size_t)(end - p) < (repeat ? 1u : (size_t)length)) return false;
        uint8_t *dst = pixels + (size_t)y * w + x;
        if (repeat) { memset(dst, *p++, length); }
        else { memcpy(dst, p, length); p += length; }
        x += length;
      }
      y++;
    }
  } else return false;
  return p == end || (end - p == 1 && !*p);
}

static inline bool flc_codec_write(FILE *fp, const uint8_t *pixels, const uint8_t *previous, int w, int h) {
  size_t size;
  uint8_t *chunk = flc_codec_encode(pixels, previous, w, h, &size);
  if (!chunk) return false;
  bool ok = fwrite(chunk, 1, size, fp) == size;
  free(chunk);
  return ok;
}

#endif
