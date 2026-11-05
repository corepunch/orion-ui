// Versioned project files for Groove arrangements.

#include "groove.h"
#include <inttypes.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__) && TARGET_OS_OSX
#include <unistd.h>
#endif

bool song_seed_demos(const char *songs_dir, const char *demos_dir) {
  static const struct { const char *source, *name; } demos[] = {
    { "neon-skyline.groove", "Neon Skyline.groove" },
    { "afterglow-avenue.groove", "Afterglow Avenue.groove" },
    { "victory-lights.groove", "Victory Lights.groove" },
  };
  char marker[1024], source[1024], target[1024];
  if (!songs_dir || !demos_dir || snprintf(marker, sizeof(marker), "%s/.demo-songs-v1", songs_dir) >= (int)sizeof(marker)) return false;
  if (axPathExists(marker)) return true;
  for (int i = 0; i < ARRAY_LEN(demos); i++) {
    if (snprintf(source, sizeof(source), "%s/%s", demos_dir, demos[i].source) >= (int)sizeof(source) ||
        snprintf(target, sizeof(target), "%s/%s", songs_dir, demos[i].name) >= (int)sizeof(target)) return false;
    FILE *in = fopen(source, "rb");
    if (!in) return false;
    FILE *out = fopen(target, "wbx");
    if (!out) { int error = errno; fclose(in); if (error == EEXIST) continue; return false; }
    char buffer[8192];
    size_t n;
    bool ok = true;
    while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0)
      if (fwrite(buffer, 1, n, out) != n) { ok = false; break; }
    if (ferror(in)) ok = false;
    if (fclose(out) != 0) ok = false;
    fclose(in);
    if (!ok) { remove(target); return false; }
  }
  FILE *done = fopen(marker, "wb");
  return done && fclose(done) == 0;
}

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

static bool song_write(const char *path, bool make_current) {
  if (!g_app || !path || !path[0]) return false;
  char target[sizeof(g_app->filename)];
  if (snprintf(target, sizeof(target), "%s", path) >= (int)sizeof(target) || !append_extension(target, sizeof(target))) return false;
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
  char tmp_path[sizeof(target) + 32];
  FILE *f;
#if defined(__APPLE__) && TARGET_OS_OSX
  bool native = axFileDialogsRequireNative();
  if (native) {
    const char *dir = axSettingsDirectory();
    if (!dir || !*dir || !axMkDir(dir) ||
        snprintf(tmp_path, sizeof(tmp_path), "%s/groove-save-XXXXXX", dir) >= (int)sizeof(tmp_path)) return false;
    int fd = mkstemp(tmp_path);
    if (fd < 0) return false;
    f = fdopen(fd, "wb");
    if (!f) { close(fd); remove(tmp_path); return false; }
  } else
#endif
  {
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", target);
    f = fopen(tmp_path, "wb");
    if (!f) return false;
  }
  uint32_t mute = track_flags(s->mute), solo = track_flags(s->solo);
  int version = 6;
  bool ok = fprintf(f, "ORION_GROOVE %d\n%d %d %" PRIu32 " %" PRIu32 " %d %d\n",
                    version, s->bpm, s->loop ? 1 : 0, mute, solo, nassets, s->nclips) > 0;
  for (int i = 0; ok && i < GR_TRACKS; i++) ok = fprintf(f, "%d%c", (int)s->track_family[i], i == GR_TRACKS - 1 ? '\n' : ' ') > 0;
  for (int i = 0; ok && i < nassets; i++) {
    const sample_t *sample = sample_get(asset_ids[i]);
    if (sample) ok = fputs("S\n", f) != EOF && sample_write(f, sample) && fputc('\n', f) != EOF;
    else ok = fprintf(f, "B %d\t%s\n", asset_ids[i], block_get(asset_ids[i])->name) > 0;
  }
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
#if defined(__APPLE__) && TARGET_OS_OSX
  if (ok && native) { ok = axMacPublishFile(tmp_path, target); remove(tmp_path); }
  else
#endif
  if (ok) ok = rename(tmp_path, target) == 0;
#endif
  if (!ok) { remove(tmp_path); return false; }
  if (make_current) {
    snprintf(g_app->filename, sizeof(g_app->filename), "%s", target);
    g_app->song_dirty = false;
  }
  return true;
}

bool app_save_song(const char *path) { return song_write(path, true); }
bool song_write_copy(const char *path) { return song_write(path, false); }

static bool song_read_file(const char *path, song_t *out, bool preview) {
  if (!path || !path[0] || !out) return false;
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  char magic[32];
  int version = 0, bpm = 0, loop = 0, nassets = 0, nclips = 0;
  uint32_t mute = 0, solo = 0;
  song_t next;
  song_init(&next);
  bool ok = fscanf(f, "%31s %d", magic, &version) == 2 && !strcmp(magic, "ORION_GROOVE") && version >= 2 && version <= 6;
  if (ok) ok = fscanf(f, "%d %d %" SCNu32 " %" SCNu32 " %d %d", &bpm, &loop, &mute, &solo, &nassets, &nclips) == 6;
  ok = ok && bpm >= GR_BPM_MIN && bpm <= GR_BPM_MAX && (loop == 0 || loop == 1) &&
       !(mute & ~((1u << GR_TRACKS) - 1)) && !(solo & ~((1u << GR_TRACKS) - 1)) &&
       nassets >= 0 && nassets <= GR_MAX_CLIPS && nclips >= 0 && nclips <= GR_MAX_CLIPS;
  next.bpm = bpm;
  next.loop = loop != 0;
  if (ok) { flags_from_bits(next.mute, mute); flags_from_bits(next.solo, solo); }
  for (int i = 0; ok && version >= 6 && i < GR_TRACKS; i++) {
    int family = -1;
    ok = fscanf(f, "%d", &family) == 1 && family >= 0 && family < FAMILY_COUNT;
    if (ok) next.track_family[i] = (family_t)family;
  }
  int asset_ids[GR_MAX_CLIPS], asset_bars[GR_MAX_CLIPS];
  sample_t *embedded = ok ? calloc((size_t)MAX(nassets, 1), sizeof(*embedded)) : NULL;
  if (!embedded) ok = false;
  size_t embedded_bytes = 0;
  for (int i = 0; ok && i < nassets; i++) {
    char kind = 0, name[256];
    int block = -1;
    if (fscanf(f, " %c", &kind) != 1) { ok = false; break; }
    if (kind == 'S' && version >= 5) {
      if (fgetc(f) != '\n' || !(preview ? sample_read_info(f, &embedded[i]) : sample_read(f, &embedded[i])) ||
          fgetc(f) != '\n') { ok = false; break; }
      embedded_bytes += (size_t)embedded[i].frames * sizeof(int16_t);
      if (embedded_bytes > 64u * 1024u * 1024u) { ok = false; break; }
      asset_ids[i] = -1;
      asset_bars[i] = embedded[i].bars;
    } else if (kind == 'B') {
      if (fscanf(f, "%d", &block) != 1 || fgetc(f) != '\t' || !fgets(name, sizeof(name), f)) { ok = false; break; }
      name[strcspn(name, "\r\n")] = 0;
      const block_t *b = block >= 0 && block < blocks_builtin_count() ? block_get(block) : NULL;
      if (!b || strcmp(name, b->name)) {
        block = -1;
        for (int id = 0; id < blocks_builtin_count(); id++) if (!strcmp(name, block_get(id)->name)) {
          if (block >= 0) { ok = false; break; }
          block = id;
        }
        if (!ok || block < 0) { ok = false; break; }
      }
      for (int j = 0; j < i; j++) if (asset_ids[j] == block) ok = false;
      asset_ids[i] = block;
      asset_bars[i] = block_get(block)->bars;
    } else ok = false;
  }
  for (int i = 0; ok && i < nclips; i++) {
    int source, track, position, length = 0, gain_db = 0;
    uint64_t order;
    int fields = version >= 4
      ? fscanf(f, "%d %d %d %" SCNu64 " %d %d", &source, &track, &position, &order, &length, &gain_db)
      : version == 3 ? fscanf(f, "%d %d %d %" SCNu64 " %d", &source, &track, &position, &order, &length)
      : fscanf(f, "%d %d %d %" SCNu64, &source, &track, &position, &order);
    if (fields != (version >= 4 ? 6 : version == 3 ? 5 : 4) || gain_db < -60 || gain_db > 6) { ok = false; break; }
    bool valid_asset = source >= 0 && source < nassets;
    int natural = valid_asset ? asset_bars[source] * GR_TICKS_BAR : 0;
    int ticks = length > 0 ? length : natural;
    if (length < 0 || (length > 0 && (length < GR_FRACTION_TICKS || length % GR_FRACTION_TICKS))) { ok = false; break; }
    if (!valid_asset || !song_can_place(&next, track, position, ticks, -1) || !order) { ok = false; break; }
    for (int j = 0; j < next.nclips; j++) if (next.clips[j].order == order) ok = false;
    if (!ok) break;
    next.clips[next.nclips++] = (clip_t){ source, track, position, order,
      preview && asset_ids[source] < 0 ? ticks : length, gain_db };
    next.clip_order = MAX(next.clip_order, order);
  }
  if (ok) { int c; do c = fgetc(f); while (c != EOF && isspace((unsigned char)c)); ok = c == EOF && !ferror(f); }
  if (fclose(f) != 0) ok = false;
  int old_samples = samples_count();
  for (int i = 0; ok && i < nassets; i++) if (embedded[i].pcm) {
    asset_ids[i] = sample_add(&embedded[i]);
    if (asset_ids[i] < 0) ok = false;
  }
  if (ok) {
    for (int i = 0; i < next.nclips; i++) next.clips[i].block = asset_ids[next.clips[i].block];
    *out = next;
  } else samples_truncate(old_samples);
  for (int i = 0; embedded && i < nassets; i++) free(embedded[i].pcm);
  free(embedded);
  return ok;
}

bool song_read(const char *path, song_t *out) { return song_read_file(path, out, false); }
bool song_read_preview(const char *path, song_t *out) { return song_read_file(path, out, true); }

bool app_open_song(const char *path) {
  if (!g_app || !path || !path[0] || strlen(path) >= sizeof(g_app->filename)) return false;
  song_t next;
  int old_samples = samples_count();
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
  if (!ok) {
    for (int i = 0; i < next.nclips; i++) free(prepared[i].pcm);
    samples_truncate(old_samples);
    return false;
  }

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
  app_reset_tracks();
  app_select_track(0);
  g_app->auditioned = -1;
  snprintf(g_app->filename, sizeof(g_app->filename), "%s", path);
  g_app->song_dirty = false;
  if (g_app->tracks) tracks_sync();
  if (g_app->sheet) invalidate_window(g_app->sheet);
  if (g_app->library) invalidate_window(g_app->library);
  transport_refresh();
  app_samples_refresh();
  for (int i = 0; i < next.nclips; i++) {
    bool first = true;
    for (int j = 0; j < i; j++) if (next.clips[j].block == next.clips[i].block) first = false;
    const sample_t *sample = first ? sample_get(next.clips[i].block) : NULL;
    if (sample && !sample_is_deleted(next.clips[i].block) && g_app->samples_dir[0] &&
        !sample_store(g_app->samples_dir, next.clips[i].block)) {
      fprintf(stderr, "[gr] could not keep embedded recording in sample library block=%d\n", next.clips[i].block);
      fflush(stderr); // the opened song retains its embedded sample in memory and on disk
    }
  }
  return true;
}

// ── Groove's own song storage (iPad) ─────────────────────────────────────
// iPad songs live in the app's private Application Support area, like a
// reader app's library, never in Files or iCloud. Desktop saves anywhere on disk.

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
