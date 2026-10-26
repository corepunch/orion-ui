#include "groove.h"
#include <stdatomic.h>
#include <time.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#endif

typedef struct {
  int kind, format;
  song_t song;
  block_pcm_t *audio;
  int audio_count;
  ui_media_video_t video;
  char path[1024], stage[1200], wav[1200];
  axthread_t worker;
  uint32_t timer;
  atomic_int progress;
  atomic_bool cancel, done;
  bool ok, share;
} export_media_t;

static bool export_render_progress(void *userdata, float fraction) {
  export_media_t *st = userdata;
  atomic_store(&st->progress, (int)(fraction * 40));
  return !atomic_load(&st->cancel);
}

static bool export_encode_progress(void *userdata, float fraction) {
  export_media_t *st = userdata;
  atomic_store(&st->progress, 40 + (int)(fraction * 60));
  return !atomic_load(&st->cancel);
}

static void export_media_worker(void *userdata) {
  export_media_t *st = userdata;
  bool wav_only = st->kind != 2 && st->format == 0;
  st->ok = song_export_wav_with_audio(&st->song, st->audio, wav_only ? st->stage : st->wav, export_render_progress, st);
  if (st->ok && !wav_only) {
    if (st->format == 1 && st->kind != 2) st->ok = groove_encode_mp3(st->wav, st->stage, export_encode_progress, st);
    else st->ok = ui_media_encode(st->wav, st->stage, st->kind == 2 ? UI_MEDIA_MP4 : UI_MEDIA_M4A,
      st->kind == 2 ? &st->video : NULL, export_encode_progress, st);
  }
  remove(st->wav);
  if (atomic_load(&st->cancel)) st->ok = false;
  if (st->ok) {
#ifdef _WIN32
    st->ok = MoveFileExA(st->stage, st->path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    st->ok = rename(st->stage, st->path) == 0;
#endif
  }
  if (!st->ok) { fprintf(stderr, "[groove-export] export failed output=%s stage=%s: %s\n", st->path, st->stage, strerror(errno)); fflush(stderr); remove(st->stage); }
  atomic_store_explicit(&st->done, true, memory_order_release);
}

static void export_controls(window_t *win, export_media_t *st, bool working) {
  enable_window(get_window_item(win, ID_EXPORT_MEDIA_FORMAT), !working && st->kind != 2);
  enable_window(get_window_item(win, ID_EXPORT_MEDIA_ASPECT), !working && st->kind == 2);
  enable_window(get_window_item(win, ID_EXPORT_MEDIA_SAVE), !working);
  enable_window(get_window_item(win, ID_EXPORT_MEDIA_SHARE), !working);
  set_window_item_text(win, ID_EXPORT_MEDIA_CANCEL, "%s", ui_tr(working ? "Cancel Export" : "Close"));
}

static void export_audio_clear(export_media_t *st) {
  if (st->audio) for (int i = 0; i < st->audio_count; i++) free(st->audio[i].pcm);
  free(st->audio); st->audio = NULL; st->audio_count = 0;
}

static void export_media_start(window_t *win, export_media_t *st, bool share) {
  if (st->worker) return;
  st->format = (int)send_message(get_window_item(win, ID_EXPORT_MEDIA_FORMAT), cbGetCurrentSelection, 0, NULL);
  const char *ext = st->kind == 2 ? "mp4" : st->format == 1 ? "mp3" : st->format == 2 ? "m4a" : "wav";
  st->share = share;
  if (share) {
    const char *settings = axSettingsDirectory();
    char dir[900];
    if (!settings || snprintf(dir, sizeof(dir), "%s/Exports", settings) >= (int)sizeof(dir) || !axMkDir(settings) || !axMkDir(dir)) {
      set_window_item_text(win, ID_EXPORT_MEDIA_STATUS, "%s", ui_tr("Could not create the export folder. Try Save File.")); return;
    }
    snprintf(st->path, sizeof(st->path), "%s/Groove-%lld-%u.%s", dir, (long long)time(NULL), (unsigned)rand(), ext);
  } else {
    char filter[60]; snprintf(filter, sizeof(filter), "Audio / Video");
    size_t n = strlen(filter) + 1;
    snprintf(filter + n, sizeof(filter) - n, "*.%s", ext); filter[n + strlen(filter + n) + 1] = 0;
    snprintf(st->path, sizeof(st->path), "Groove.%s", ext);
    openfilename_t ofn = {.lStructSize = sizeof(ofn), .hwndOwner = win, .lpstrFile = st->path,
      .nMaxFile = sizeof(st->path), .lpstrFilter = filter, .Flags = OFN_OVERWRITEPROMPT};
    if (!get_save_filename(&ofn)) return;
    // The codec is selected explicitly; do not quietly write WAV into an .mp3.
    const char *dot = strrchr(st->path, '.');
    if (!dot || strcmp(dot + 1, ext)) {
      set_window_item_text(win, ID_EXPORT_MEDIA_STATUS, "%s", ui_tr("Use the filename extension matching the selected format.")); return;
    }
  }
  app_lock(); st->song = g_app->song; app_unlock();
  if (song_length_ticks(&st->song) <= 0) {
    set_window_item_text(win, ID_EXPORT_MEDIA_STATUS, "%s", ui_tr("Add blocks to the song before exporting.")); return;
  }
  for (int i = 0; i < st->song.nclips; i++) if (!app_block_audio(st->song.clips[i].block)) {
    set_window_item_text(win, ID_EXPORT_MEDIA_STATUS, "%s", ui_tr("Could not prepare the song audio.")); return;
  }
  export_audio_clear(st);
  st->audio_count = blocks_count();
  st->audio = calloc((size_t)st->audio_count, sizeof(*st->audio));
  bool ready = st->audio != NULL;
  for (int i = 0; ready && i < st->song.nclips; i++) {
    int id = st->song.clips[i].block;
    if (st->audio[id].pcm) continue;
    const block_t *b = block_get(id);
    st->audio[id] = b->audio;
    st->audio[id].pcm = malloc((size_t)b->audio.frames * sizeof(float));
    ready = st->audio[id].pcm != NULL;
    if (ready) memcpy(st->audio[id].pcm, b->audio.pcm, (size_t)b->audio.frames * sizeof(float));
  }
  if (!ready) {
    export_audio_clear(st);
    set_window_item_text(win, ID_EXPORT_MEDIA_STATUS, "%s", ui_tr("Not enough memory to prepare the export.")); return;
  }
  free((void *)st->video.rgba); st->video = (ui_media_video_t){0};
  if (st->kind == 2) {
    int aspect = (int)send_message(get_window_item(win, ID_EXPORT_MEDIA_ASPECT), cbGetCurrentSelection, 0, NULL);
    if (!song_export_canvas(&st->song, aspect == 1 ? 1680 : 1280, 720, &st->video)) {
      set_window_item_text(win, ID_EXPORT_MEDIA_STATUS, "%s", ui_tr("Could not render the timeline.")); return;
    }
  }
  unsigned token = (unsigned)rand();
  snprintf(st->stage, sizeof(st->stage), "%s.part-%u.%s", st->path, token, ext);
  snprintf(st->wav, sizeof(st->wav), "%s.part-%u.wav", st->path, token + 1);
  atomic_store(&st->cancel, false); atomic_store(&st->done, false); atomic_store(&st->progress, 0);
  st->worker = axThreadCreate(export_media_worker, st);
  if (!st->worker) { set_window_item_text(win, ID_EXPORT_MEDIA_STATUS, "%s", ui_tr("Could not start the export.")); return; }
  export_controls(win, st, true);
  set_window_item_text(win, ID_EXPORT_MEDIA_STATUS, "%s", ui_tr("Exporting..."));
}

static result_t export_media_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  export_media_t *st = win->userdata;
  switch (msg) {
    case evCreate: {
      int kind = *(int *)lparam;
      st = allocate_window_data(win, sizeof(*st));
      if (!st) return false;
      st->kind = kind;
      atomic_init(&st->progress, 0); atomic_init(&st->cancel, false); atomic_init(&st->done, false);
      window_t *format = get_window_item(win, ID_EXPORT_MEDIA_FORMAT), *aspect = get_window_item(win, ID_EXPORT_MEDIA_ASPECT);
      if (kind == 2) send_message(format, cbAddString, 0, "MP4 (H.264 + AAC)");
      else {
        send_message(format, cbAddString, 0, "WAV (44.1 kHz, 16-bit)");
        send_message(format, cbAddString, 0, "MP3 (192 kbps)");
        send_message(format, cbAddString, 0, "M4A (AAC)");
      }
      send_message(format, cbSetCurrentSelection, kind == 1 ? 2 : 0, NULL);
      send_message(aspect, cbAddString, 0, "16:9 (1280 x 720)");
      send_message(aspect, cbAddString, 0, "21:9 (1680 x 720)");
      send_message(aspect, cbSetCurrentSelection, 0, NULL);
      export_controls(win, st, false);
      st->timer = axSetTimer(win, 100, NULL, true);
      return true;
    }
    case evPaint: return false;
    case evTimer:
      if (st->worker) {
        if (atomic_load_explicit(&st->done, memory_order_acquire)) {
          axThreadJoin(st->worker); st->worker = NULL;
          export_audio_clear(st);
          export_controls(win, st, false);
          bool cancelled = atomic_load(&st->cancel);
          set_window_item_text(win, ID_EXPORT_MEDIA_STATUS, "%s", ui_tr(st->ok ? "Export saved." : cancelled ? "Export cancelled." : "Export failed. Check available space and encoder availability."));
          if (st->ok && st->share && !ui_media_share(st->path))
            message_box(win, ui_tr("The export is saved in the Exports folder. Native sharing is unavailable here; use Save File to choose another location."), ui_tr("Export saved"), MB_OK);
        } else {
          char number[32]; ui_format_number(atomic_load(&st->progress), 0, number, sizeof(number));
          set_window_item_text(win, ID_EXPORT_MEDIA_STATUS, ui_tr("Exporting: %s%%"), number);
        }
      }
      return true;
    case evCommand:
      if (HIWORD(wparam) != btnClicked) return false;
      switch (LOWORD(wparam)) {
        case ID_EXPORT_MEDIA_SAVE: export_media_start(win, st, false); return true;
        case ID_EXPORT_MEDIA_SHARE: export_media_start(win, st, true); return true;
        case ID_EXPORT_MEDIA_CANCEL:
          if (st->worker) { atomic_store(&st->cancel, true); set_window_item_text(win, ID_EXPORT_MEDIA_STATUS, "%s", ui_tr("Cancelling...")); }
          else end_dialog(win, 0);
          return true;
      }
      return false;
    case evClose:
      if (st->worker) atomic_store(&st->cancel, true);
      else end_dialog(win, 0);
      return true;
    case evDestroy:
      if (st) {
        if (st->timer) axCancelTimer(st->timer);
        atomic_store(&st->cancel, true); axThreadJoin(st->worker);
        export_audio_clear(st); free((void *)st->video.rgba); free(st); win->userdata = NULL;
      }
      return true;
    default: return false;
  }
}

void app_export_media(window_t *owner, int kind) {
  if (g_app) show_dialog_from_form(&groove_export_media_form, NULL, owner, export_media_proc, &kind);
}
