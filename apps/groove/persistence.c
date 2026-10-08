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
  bool ok = fprintf(f, "ORION_GROOVE 2\n%d %d %" PRIu32 " %" PRIu32 " %d %d\n",
                    s->bpm, s->loop ? 1 : 0, mute, solo, nassets, s->nclips) > 0;
  for (int i = 0; ok && i < nassets; i++)
    ok = fprintf(f, "B %d\t%s\n", asset_ids[i], block_get(asset_ids[i])->name) > 0;
  for (int i = 0; ok && i < s->nclips; i++) {
    const clip_t *c = &s->clips[i];
    int asset = 0;
    while (asset < nassets && asset_ids[asset] != c->block) asset++;
    ok = asset < nassets && fprintf(f, "%d %d %d %" PRIu64 "\n", asset, c->track, c->position, c->order) > 0;
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

bool app_open_song(const char *path) {
  if (!g_app || !path || !path[0] || strlen(path) >= sizeof(g_app->filename)) return false;
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  char magic[32];
  int version = 0, bpm = 0, loop = 0, nassets = 0, nclips = 0;
  uint32_t mute = 0, solo = 0;
  song_t next;
  song_init(&next);
  bool ok = fscanf(f, "%31s %d", magic, &version) == 2 && !strcmp(magic, "ORION_GROOVE") && version == 2;
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
    if (!b || strcmp(name, b->name)) { ok = false; break; }
    for (int j = 0; j < i; j++) if (asset_ids[j] == block) ok = false;
    if (ok) asset_ids[i] = block;
  }
  for (int i = 0; ok && i < nclips; i++) {
    int source, track, position;
    uint64_t order;
    if (fscanf(f, "%d %d %d %" SCNu64, &source, &track, &position, &order) != 4) { ok = false; break; }
    int block = source >= 0 && source < nassets ? asset_ids[source] : -1;
    const block_t *b = block_get(block);
    int ticks = b ? b->bars * GR_TICKS_BAR : 0;
    if (!b || !song_can_place(&next, track, position, ticks, -1) || !order) { ok = false; break; }
    for (int j = 0; j < next.nclips; j++) if (next.clips[j].order == order) ok = false;
    if (!ok) break;
    next.clips[next.nclips++] = (clip_t){ block, track, position, order };
    next.clip_order = MAX(next.clip_order, order);
  }
  if (ok) { int c; do c = fgetc(f); while (c != EOF && isspace((unsigned char)c)); ok = c == EOF && !ferror(f); }
  if (fclose(f) != 0) ok = false;
  if (!ok) return false;

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
