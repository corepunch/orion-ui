// PLAYLIST: the track list, folder scanning and a cheap MP3 probe for the
// title (ID3v2) and length (Xing/Info frame count, else constant bitrate).

#include "winamp.h"
#include <dirent.h>
#include <ctype.h>
#include <strings.h>

void playlist_clear(wa_playlist_t *pl) {
  for (int i = 0; i < pl->count; i++) { free(pl->items[i].path); free(pl->items[i].title); }
  free(pl->items);
  *pl = (wa_playlist_t){ .current = -1, .selected = -1 };
}

bool playlist_add(wa_playlist_t *pl, const char *path) {
  for (int i = 0; i < pl->count; i++) if (!strcmp(pl->items[i].path, path)) return false;
  if (pl->count == pl->cap) {
    int cap = pl->cap ? pl->cap * 2 : 32;
    wa_track_t *items = realloc(pl->items, (size_t)cap * sizeof(*items));
    if (!items) { fprintf(stderr, "[wa] playlist growth failed count=%d\n", pl->count); fflush(stderr); return false; }
    pl->items = items; pl->cap = cap;
  }
  wa_track_t *t = &pl->items[pl->count];
  *t = (wa_track_t){ strdup(path), NULL, -1 };
  if (!t->path) return false;
  mp3_probe(path, &t->title, &t->seconds);
  pl->count++;
  return true;
}

void playlist_remove(wa_playlist_t *pl, int index) {
  if (index < 0 || index >= pl->count) {
    fprintf(stderr, "[wa] playlist remove rejected index=%d count=%d\n", index, pl->count);
    fflush(stderr);
    return;
  }
  free(pl->items[index].path);
  free(pl->items[index].title);
  memmove(&pl->items[index], &pl->items[index + 1], (size_t)(pl->count - index - 1) * sizeof(*pl->items));
  pl->count--;
  if (pl->current == index) pl->current = -1; else if (pl->current > index) pl->current--;
  if (pl->selected >= pl->count) pl->selected = pl->count - 1;
}

// Moves one track, keeping the current and selected tracks pointing at the same items.
void playlist_move(wa_playlist_t *pl, int from, int to) {
  if (from < 0 || from >= pl->count || to < 0 || to >= pl->count) {
    fprintf(stderr, "[wa] playlist move rejected from=%d to=%d count=%d\n", from, to, pl->count);
    fflush(stderr);
    return;
  }
  if (from == to) return;
  wa_track_t t = pl->items[from];
  if (from < to) memmove(&pl->items[from], &pl->items[from + 1], (size_t)(to - from) * sizeof(t));
  else memmove(&pl->items[to + 1], &pl->items[to], (size_t)(from - to) * sizeof(t));
  pl->items[to] = t;
  int *marks[] = { &pl->current, &pl->selected };
  for (int i = 0; i < 2; i++) {
    int *m = marks[i];
    if (*m == from) *m = to;
    else if (from < to && *m > from && *m <= to) (*m)--;
    else if (from > to && *m >= to && *m < from) (*m)++;
  }
}

static int compare_names(const void *a, const void *b) { return strcasecmp(*(char *const *)a, *(char *const *)b); }

static bool has_mp3_ext(const char *name) {
  size_t n = strlen(name);
  return n > 4 && !strcasecmp(name + n - 4, ".mp3");
}

int playlist_scan(wa_playlist_t *pl, const char *dir) {
  DIR *d = opendir(dir);
  if (!d) return 0;
  char **names = NULL;
  int count = 0, cap = 0;
  struct dirent *ent;
  while ((ent = readdir(d))) {
    if (ent->d_name[0] == '.' || !has_mp3_ext(ent->d_name)) continue;
    if (count == cap) { cap = cap ? cap * 2 : 32; char **n = realloc(names, (size_t)cap * sizeof(*n)); if (!n) break; names = n; }
    names[count++] = strdup(ent->d_name);
  }
  closedir(d);
  qsort(names, (size_t)count, sizeof(*names), compare_names);
  int added = 0;
  for (int i = 0; i < count; i++) {
    char path[1024];
    if (names[i] && snprintf(path, sizeof(path), "%s/%s", dir, names[i]) < (int)sizeof(path) && playlist_add(pl, path)) added++;
    free(names[i]);
  }
  free(names);
  return added;
}

// ── Probe ──────────────────────────────────────────────────────────────────

static uint32_t syncsafe(const uint8_t *p) { return (uint32_t)(p[0] & 127) << 21 | (p[1] & 127) << 14 | (p[2] & 127) << 7 | (p[3] & 127); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

// ID3 text frame to UTF-8 (UTF-16 keeps the BMP; enough for titles).
static char *id3_text(const uint8_t *p, uint32_t n) {
  if (n < 2) return NULL;
  int enc = p[0];
  p++; n--;
  char *out = malloc(n * 3 + 1);
  if (!out) return NULL;
  size_t o = 0;
  if (enc == 1 || enc == 2) {
    bool le = enc == 1 && n >= 2 && p[0] == 0xFF && p[1] == 0xFE;
    if (enc == 1 && n >= 2 && (p[0] == 0xFF || p[0] == 0xFE)) { p += 2; n -= 2; }
    for (uint32_t i = 0; i + 1 < n; i += 2) {
      unsigned c = le ? p[i] | p[i + 1] << 8 : p[i] << 8 | p[i + 1];
      if (!c) break;
      if (c < 0x80) out[o++] = (char)c;
      else if (c < 0x800) { out[o++] = (char)(0xC0 | c >> 6); out[o++] = (char)(0x80 | (c & 63)); }
      else { out[o++] = (char)(0xE0 | c >> 12); out[o++] = (char)(0x80 | (c >> 6 & 63)); out[o++] = (char)(0x80 | (c & 63)); }
    }
  } else {
    for (uint32_t i = 0; i < n && p[i]; i++) {
      unsigned c = p[i];
      if (enc == 3 || c < 0x80) out[o++] = (char)c;
      else { out[o++] = (char)(0xC0 | c >> 6); out[o++] = (char)(0x80 | (c & 63)); }
    }
  }
  while (o && out[o - 1] == ' ') o--;
  out[o] = 0;
  if (!o) { free(out); return NULL; }
  return out;
}

static size_t parse_id3(const uint8_t *buf, size_t n, char **artist, char **song) {
  if (n < 10 || memcmp(buf, "ID3", 3)) return 0;
  int ver = buf[3];
  size_t size = 10 + syncsafe(buf + 6) + (buf[5] & 0x10 ? 10 : 0);
  size_t at = 10, end = MIN(size, n);
  if (ver >= 3 && (buf[5] & 0x40) && at + 4 <= end) at += ver == 4 ? syncsafe(buf + at) : be32(buf + at) + 4;
  int head = ver == 2 ? 6 : 10;
  while (at + head <= end && buf[at]) {
    const uint8_t *f = buf + at;
    uint32_t len = ver == 2 ? (uint32_t)(f[3] << 16 | f[4] << 8 | f[5]) : ver == 4 ? syncsafe(f + 4) : be32(f + 4);
    if (at + head + len > end) break;
    bool is_title = ver == 2 ? !memcmp(f, "TT2", 3) : !memcmp(f, "TIT2", 4);
    bool is_artist = ver == 2 ? !memcmp(f, "TP1", 3) : !memcmp(f, "TPE1", 4);
    if (is_title && !*song) *song = id3_text(f + head, len);
    if (is_artist && !*artist) *artist = id3_text(f + head, len);
    at += head + len;
  }
  return size;
}

static const int kBitrates[2][16] = {
  { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 },   // MPEG-1 layer III
  { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 },        // MPEG-2/2.5 layer III
};
static const int kRates[3] = { 44100, 48000, 32000 };

static int probe_seconds(const uint8_t *buf, size_t n, size_t at, long file_size) {
  for (; at + 4 < n; at++) {
    const uint8_t *h = buf + at;
    if (h[0] != 0xFF || (h[1] & 0xE6) != 0xE2) continue;           // sync + layer III
    int version = h[1] >> 3 & 3, bri = h[2] >> 4, sri = h[2] >> 2 & 3;
    if (version == 1 || bri == 0 || bri == 15 || sri == 3) continue;
    bool mpeg1 = version == 3;
    int rate = kRates[sri] >> (version == 3 ? 0 : version == 2 ? 1 : 2);
    int kbps = kBitrates[mpeg1 ? 0 : 1][bri];
    int spf = mpeg1 ? 1152 : 576;
    bool mono = (h[3] >> 6) == 3;
    size_t side = mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17);
    const uint8_t *x = h + 4 + side;
    if (x + 12 < buf + n && (!memcmp(x, "Xing", 4) || !memcmp(x, "Info", 4)) && (be32(x + 4) & 1))
      return (int)((double)be32(x + 8) * spf / rate + 0.5);
    return (int)((file_size - (long)at) * 8 / (kbps * 1000L));
  }
  return -1;
}

static char *file_title(const char *path) {
  const char *name = strrchr(path, '/');
  name = name ? name + 1 : path;
  char *t = strdup(name);
  if (!t) return NULL;
  char *dot = strrchr(t, '.');
  if (dot && dot != t) *dot = 0;
  for (char *p = t; *p; p++) if (*p == '_') *p = ' ';
  return t;
}

bool mp3_probe(const char *path, char **title, int *seconds) {
  *title = NULL;
  *seconds = -1;
  FILE *fp = fopen(path, "rb");
  if (!fp) { fprintf(stderr, "[wa] probe open failed path=%s\n", path); fflush(stderr); *title = file_title(path); return false; }
  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  uint8_t head[10];
  size_t want = 64 * 1024;
  if (fread(head, 1, 10, fp) == 10 && !memcmp(head, "ID3", 3)) want += 10 + syncsafe(head + 6);
  want = MIN(want, (size_t)MAX(size, 0));
  uint8_t *buf = malloc(want ? want : 1);
  size_t n = 0;
  if (buf) { fseek(fp, 0, SEEK_SET); n = fread(buf, 1, want, fp); }
  fclose(fp);
  char *artist = NULL, *song = NULL;
  size_t audio_at = buf ? parse_id3(buf, n, &artist, &song) : 0;
  if (buf) *seconds = probe_seconds(buf, n, audio_at, size);
  free(buf);
  if (song) {
    size_t len = strlen(song) + (artist ? strlen(artist) + 3 : 0) + 1;
    if ((*title = malloc(len))) {
      if (artist) snprintf(*title, len, "%s - %s", artist, song); else snprintf(*title, len, "%s", song);
    }
  }
  free(artist);
  free(song);
  if (!*title) *title = file_title(path);
  return true;
}
