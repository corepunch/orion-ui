// Versioned project files for Groove arrangements.

#include "groove.h"
#include <inttypes.h>
#ifdef _WIN32
#include <windows.h>
#endif

static uint32_t track_flags(const bool flags[GR_TRACKS]) {
  uint32_t bits = 0;
  for (int i = 0; i < GR_TRACKS; i++) if (flags[i]) bits |= 1u << i;
  return bits;
}

static void flags_from_bits(bool flags[GR_TRACKS], uint32_t bits) {
  for (int i = 0; i < GR_TRACKS; i++) flags[i] = !!(bits & (1u << i));
}

static int append_extension(char *path, size_t cap) {
  const char *slash = strrchr(path, '/'), *backslash = strrchr(path, '\\');
  const char *base = MAX(slash ? slash + 1 : path, backslash ? backslash + 1 : path);
  const char *dot = strrchr(base, '.');
  if (dot && !strcasecmp(dot, ".groove")) return 1;
  size_t n = strlen(path);
  if (n + sizeof(".groove") > cap) return 0;
  memcpy(path + n, ".groove", sizeof(".groove"));
  return 1;
}

bool app_save_song(const char *path) {
  if (!g_app || !path || !path[0]) return false;
  char target[sizeof(g_app->filename)];
  if (snprintf(target, sizeof(target), "%s", path) >= (int)sizeof(target) || !append_extension(target, sizeof(target))) return false;
  char temp[sizeof(target) + 5];
  if (snprintf(temp, sizeof(temp), "%s.tmp", target) >= (int)sizeof(temp)) return false;
  song_t snapshot;
  app_lock();
  snapshot = g_app->song;
  app_unlock();
  song_t *s = &snapshot;
  int asset_ids[GR_MAX_CLIPS], nassets = 0;
  for (int i = 0; i < s->nclips; i++) {
    int id = s->clips[i].block, j = 0;
    while (j < nassets && asset_ids[j] != id) j++;
    if (j == nassets) asset_ids[nassets++] = id;
  }
  char tmp_path[sizeof(temp)];
  snprintf(tmp_path, sizeof(tmp_path), "%s", temp);
  FILE *f = fopen(tmp_path, "wb");
  if (!f) return false;
  uint32_t mute = track_flags(s->mute), solo = track_flags(s->solo);
  bool ok = fprintf(f, "ORION_GROOVE 4\n%d %d %" PRIu32 " %" PRIu32 " %d %d\n",
                    s->bpm, s->loop ? 1 : 0, mute, solo, nassets, s->nclips) > 0;
  for (int i = 0; ok && i < nassets; i++)
    ok = fprintf(f, "B %d\t%s\n", asset_ids[i], block_get(asset_ids[i])->name) > 0;
  for (int i = 0; ok && i < s->nclips; i++) {
    const clip_t *c = &s->clips[i];
    int asset = 0;
    while (asset < nassets && asset_ids[asset] != c->block) asset++;
    int span = song_clip_ticks(s, i), natural = block_get(c->block)->bars * GR_TICKS_BAR;
    ok = asset < nassets && fprintf(f, "%d %d %d %" PRIu64 " %d %d\n", asset, c->track, c->position, c->order, span == natural ? 0 : span, c->gain_db) > 0;
  }
  if (fclose(f) != 0) ok = false;
#ifdef _WIN32
  if (ok && !MoveFileExA(tmp_path, target, MOVEFILE_REPLACE_EXISTING)) ok = false;
#else
  if (ok && rename(tmp_path, target) != 0) ok = false;
#endif
  if (!ok) { remove(tmp_path); return false; }
  snprintf(g_app->filename, sizeof(g_app->filename), "%s", target);
  return true;
}

bool song_read(const char *path, song_t *out) {
  if (!path || !path[0] || !out) return false;
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  char magic[32];
  int version = 0, bpm = 0, loop = 0, nassets = 0, nclips = 0;
  uint32_t mute = 0, solo = 0;
  song_t next;
  song_init(&next);
  bool ok = fscanf(f, "%31s %d", magic, &version) == 2 && !strcmp(magic, "ORION_GROOVE") && version >= 2 && version <= 4;
  if (ok) ok = fscanf(f, "%d %d %" SCNu32 " %" SCNu32 " %d %d", &bpm, &loop, &mute, &solo, &nassets, &nclips) == 6;
  ok = ok && bpm >= GR_BPM_MIN && bpm <= GR_BPM_MAX && (loop == 0 || loop == 1) &&
       !(mute & ~((1u << GR_TRACKS) - 1)) && !(solo & ~((1u << GR_TRACKS) - 1)) &&
       nassets >= 0 && nassets <= GR_MAX_CLIPS && nclips >= 0 && nclips <= GR_MAX_CLIPS;
  next.bpm = bpm;
  next.loop = loop != 0;
  if (ok) { flags_from_bits(next.mute, mute); flags_from_bits(next.solo, solo); }
  int asset_ids[GR_MAX_CLIPS];
  for (int i = 0; ok && i < nassets; i++) {
    char kind = 0, name[256];
    int block = -1;
    if (fscanf(f, " %c %d", &kind, &block) != 2 || kind != 'B' || fgetc(f) != '\t' || !fgets(name, sizeof(name), f)) { ok = false; break; }
    name[strcspn(name, "\r\n")] = 0;
    const block_t *b = block_get(block);
    if (!b || strcmp(name, b->name)) {
      block = -1;
      for (int id = 0; id < blocks_count(); id++) if (!strcmp(name, block_get(id)->name)) {
        if (block >= 0) { ok = false; break; }
        block = id;
      }
      if (!ok || block < 0) { ok = false; break; }
    }
    for (int j = 0; j < i; j++) if (asset_ids[j] == block) ok = false;
    if (ok) asset_ids[i] = block;
  }
  for (int i = 0; ok && i < nclips; i++) {
    int source, track, position, length = 0, gain_db = 0;
    uint64_t order;
    int fields = version == 4
      ? fscanf(f, "%d %d %d %" SCNu64 " %d %d", &source, &track, &position, &order, &length, &gain_db)
      : version == 3 ? fscanf(f, "%d %d %d %" SCNu64 " %d", &source, &track, &position, &order, &length)
      : fscanf(f, "%d %d %d %" SCNu64, &source, &track, &position, &order);
    if (fields != (version == 4 ? 6 : version == 3 ? 5 : 4) || gain_db < -60 || gain_db > 6) { ok = false; break; }
    int block = source >= 0 && source < nassets ? asset_ids[source] : -1;
    const block_t *b = block_get(block);
    int natural = b ? b->bars * GR_TICKS_BAR : 0;
    int ticks = length > 0 ? length : natural;
    if (length < 0 || (length > 0 && (length < GR_FRACTION_TICKS || length % GR_FRACTION_TICKS))) { ok = false; break; }
    if (!b || !song_can_place(&next, track, position, ticks, -1) || !order) { ok = false; break; }
    for (int j = 0; j < next.nclips; j++) if (next.clips[j].order == order) ok = false;
    if (!ok) break;
    next.clips[next.nclips++] = (clip_t){ block, track, position, order, length, gain_db };
    next.clip_order = MAX(next.clip_order, order);
  }
  if (ok) { int c; do c = fgetc(f); while (c != EOF && isspace((unsigned char)c)); ok = c == EOF && !ferror(f); }
  if (fclose(f) != 0) ok = false;
  if (ok) *out = next;
  return ok;
}

bool app_open_song(const char *path) {
  if (!g_app || !path || !path[0] || strlen(path) >= sizeof(g_app->filename)) return false;
  song_t next;
  if (!song_read(path, &next)) return false;
  int bpm = next.bpm;
  bool ok = true;

  block_pcm_t prepared[GR_MAX_CLIPS] = {{0}};
  bool needs_render[GR_MAX_BLOCKS] = {0};
  for (int i = 0; i < next.nclips; i++) {
    int id = next.clips[i].block;
    const block_t *b = block_get(id);
    if ((!b->audio.pcm || b->audio_bpm != bpm) && !needs_render[id]) {
      needs_render[id] = true;
      if (!block_render(id, bpm, &prepared[i])) { ok = false; break; }
    }
  }
  if (!ok) { for (int i = 0; i < next.nclips; i++) free(prepared[i].pcm); return false; }

  float *stale[GR_MAX_BLOCKS] = {0};
  app_lock();
  for (int i = 0; i < blocks_count(); i++) {
    const block_t *b = block_get(i);
    if (b->audio.pcm && b->audio_bpm != bpm) stale[i] = block_release(i);
  }
  g_app->song = next;
  g_app->song.playing = false;
  g_app->song.pos = 0;
  g_app->song.preview_block = -1;
  for (int i = 0; i < next.nclips; i++) if (prepared[i].pcm) block_install(next.clips[i].block, bpm, &prepared[i]);
  app_unlock();
  for (int i = 0; i < blocks_count(); i++) free(stale[i]);
  for (int i = 0; i < next.nclips; i++) free(prepared[i].pcm);
  g_app->selected_clip = -1;
  g_app->auditioned = -1;
  snprintf(g_app->filename, sizeof(g_app->filename), "%s", path);
  if (g_app->sheet) invalidate_window(g_app->sheet);
  if (g_app->library) invalidate_window(g_app->library);
  transport_refresh();
  return true;
}

// ── Groove's own song storage ────────────────────────────────────────────
// Songs live in the app's private settings area (Application Support on
// Apple platforms), like a reader app's library, never in Files or iCloud.

bool song_path_for_name(const char *name, char *path, size_t cap) {
  if (!g_app || !name || !path || !cap || !g_app->songs_dir[0]) return false;
  while (isspace((unsigned char)*name)) name++;
  size_t n = strlen(name);
  while (n && isspace((unsigned char)name[n - 1])) n--;
  if (!n || name[0] == '.' || n >= 128) return false;
  for (size_t i = 0; i < n; i++) if (strchr("/\\:", name[i]) || (unsigned char)name[i] < 0x20) return false;
  int written = snprintf(path, cap, "%s/%.*s", g_app->songs_dir, (int)n, name);
  return written > 0 && (size_t)written < cap && append_extension(path, cap);
}

void song_name_from_path(const char *path, char *name, size_t cap) {
  if (!name || !cap) return;
  const char *slash = path ? strrchr(path, '/') : NULL, *backslash = path ? strrchr(path, '\\') : NULL;
  const char *base = path ? MAX(slash ? slash + 1 : path, backslash ? backslash + 1 : path) : "";
  const char *dot = strrchr(base, '.');
  size_t n = dot && !strcasecmp(dot, ".groove") ? (size_t)(dot - base) : strlen(base);
  snprintf(name, cap, "%.*s", (int)n, base);
}
