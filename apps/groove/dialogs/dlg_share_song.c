#include "groove.h"
#include <stdatomic.h>

#if defined(GROOVE_SHARE_LINKS) && GROOVE_SHARE_LINKS && defined(GROOVE_USE_SHINE)
#include <unistd.h>
bool groove_share_upload(const char *title, const char *audio, const char *project, char *url, size_t url_cap);
bool groove_share_configured(void);
bool groove_share_temp_dir(char *path, size_t cap);
size_t groove_share_count(void);
bool groove_share_at(size_t index, char *title, size_t title_cap, char *url, size_t url_cap);
bool groove_share_delete(size_t index);

typedef struct {
  song_t song;
  block_pcm_t *audio;
  int audio_count;
  char dir[900], wav[1024], mp3[2048], project[1024], title[1024], url[2048];
  axthread_t worker;
  uint32_t timer;
  atomic_bool done;
  bool ok, attachment;
} share_song_t;

static void share_audio_clear(share_song_t *st) {
  if (st->audio) for (int i = 0; i < st->audio_count; i++) free(st->audio[i].pcm);
  free(st->audio); st->audio = NULL;
}

static void share_song_cleanup(share_song_t *st, bool keep_mp3) {
  if (st->wav[0]) remove(st->wav);
  if (st->project[0]) remove(st->project);
  if (!keep_mp3) {
    if (st->mp3[0]) remove(st->mp3);
    if (st->dir[0]) rmdir(st->dir);
  }
}

static void share_song_worker(void *userdata) {
  share_song_t *st = userdata;
  st->ok = song_export_wav_with_audio(&st->song, st->audio, st->wav, NULL, NULL) &&
    groove_encode_mp3(st->wav, st->mp3, NULL, NULL) &&
    (st->attachment || groove_share_upload(st->title, st->mp3, st->project[0] ? st->project : NULL, st->url, sizeof(st->url)));
  share_song_cleanup(st, st->attachment && st->ok);
  atomic_store_explicit(&st->done, true, memory_order_release);
}

static void share_song_start(window_t *win, share_song_t *st, bool attachment) {
  if (st->worker) return;
  app_lock(); st->song = g_app->song; app_unlock();
  if (song_length_ticks(&st->song) <= 0) {
    set_window_item_text(win, ID_SHARE_SONG_STATUS, "%s", ui_tr("Add blocks to the song before sharing.")); return;
  }
  st->attachment = attachment;
  st->dir[0] = st->wav[0] = st->mp3[0] = st->project[0] = 0;
  song_name_from_path(g_app->filename, st->title, sizeof(st->title));
  if (!st->title[0]) snprintf(st->title, sizeof(st->title), "%s", ui_tr("Untitled"));
  if (!groove_share_temp_dir(st->dir, sizeof(st->dir))) {
    st->dir[0] = 0;
    goto fail;
  }
  snprintf(st->wav, sizeof(st->wav), "%s/song.wav", st->dir);
  snprintf(st->mp3, sizeof(st->mp3), "%s/%s.mp3", st->dir, strlen(st->title) <= 240 ? st->title : "Groove Song");
  if (!attachment && send_message(get_window_item(win, ID_SHARE_SONG_PROJECT), btnGetCheck, 0, NULL)) {
    snprintf(st->project, sizeof(st->project), "%s/song.groove", st->dir);
    if (!song_write_copy(st->project)) goto fail;
  }
  for (int i = 0; i < st->song.nclips; i++) if (!app_block_audio(st->song.clips[i].block)) goto fail;
  st->audio_count = blocks_count();
  st->audio = calloc((size_t)st->audio_count, sizeof(*st->audio));
  if (!st->audio) goto fail;
  for (int i = 0; i < st->song.nclips; i++) {
    int id = st->song.clips[i].block;
    if (st->audio[id].pcm) continue;
    const block_t *b = block_get(id);
    st->audio[id] = b->audio;
    st->audio[id].pcm = malloc((size_t)b->audio.frames * sizeof(float));
    if (!st->audio[id].pcm) goto fail;
    memcpy(st->audio[id].pcm, b->audio.pcm, (size_t)b->audio.frames * sizeof(float));
  }
  atomic_store(&st->done, false);
  st->worker = axThreadCreate(share_song_worker, st);
  if (!st->worker) goto fail;
  enable_window(get_window_item(win, ID_SHARE_SONG_SHARE), false);
  enable_window(get_window_item(win, ID_SHARE_SONG_ATTACHMENT), false);
  enable_window(get_window_item(win, ID_SHARE_SONG_PROJECT), false);
  enable_window(get_window_item(win, ID_SHARE_SONG_MANAGE), false);
  enable_window(get_window_item(win, ID_SHARE_SONG_CLOSE), false);
  set_window_item_text(win, ID_SHARE_SONG_STATUS, "%s", ui_tr(attachment ? "Preparing your MP3..." : "Preparing and uploading your song..."));
  return;
fail:
  share_audio_clear(st);
  share_song_cleanup(st, false);
  set_window_item_text(win, ID_SHARE_SONG_STATUS, "%s", ui_tr("Could not prepare the song for sharing."));
}
typedef struct {
  char title[1024], url[2048];
  size_t index;
  axthread_t worker;
  uint32_t timer;
  atomic_bool done;
  bool ok;
} manage_link_t;

static void manage_link_refresh(window_t *win, manage_link_t *st) {
  size_t count = groove_share_count();
  if (count && st->index >= count) st->index = count - 1;
  bool found = groove_share_at(st->index, st->title, sizeof(st->title), st->url, sizeof(st->url));
  set_window_item_text(win, ID_MANAGE_SONG_LINK_TITLE, "%s", found ? st->title : ui_tr("No shared songs."));
  enable_window(get_window_item(win, ID_MANAGE_SONG_LINK_SHARE), found);
  enable_window(get_window_item(win, ID_MANAGE_SONG_LINK_DELETE), found);
  enable_window(get_window_item(win, ID_MANAGE_SONG_LINK_OLDER), found && st->index > 0);
  enable_window(get_window_item(win, ID_MANAGE_SONG_LINK_NEWER), found && st->index + 1 < count);
  enable_window(get_window_item(win, ID_MANAGE_SONG_LINK_CLOSE), true);
}
static void manage_link_worker(void *data) {
  manage_link_t *st = data;
  st->ok = groove_share_delete(st->index);
  atomic_store_explicit(&st->done, true, memory_order_release);
}
static result_t manage_link_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)lparam; manage_link_t *st = win->userdata;
  switch (msg) {
    case evCreate:
      st = allocate_window_data(win, sizeof(*st)); if (!st) return false;
      atomic_init(&st->done, false); st->timer = axSetTimer(win, 100, NULL, true);
      st->index = SIZE_MAX;
      manage_link_refresh(win, st); return true;
    case evPaint: return false;
    case evTimer:
      if (st->worker && atomic_load_explicit(&st->done, memory_order_acquire)) {
        axThreadJoin(st->worker); st->worker = NULL; manage_link_refresh(win, st);
        set_window_item_text(win, ID_MANAGE_SONG_LINK_STATUS, "%s", ui_tr(st->ok ? "Sharing stopped." : "Could not stop sharing. Check your connection and try again."));
      }
      return true;
    case evCommand:
      if (HIWORD(wparam) != btnClicked || st->worker) return false;
      if (LOWORD(wparam) == ID_MANAGE_SONG_LINK_CLOSE) { end_dialog(win, 0); return true; }
      if (LOWORD(wparam) == ID_MANAGE_SONG_LINK_OLDER || LOWORD(wparam) == ID_MANAGE_SONG_LINK_NEWER) {
        if (LOWORD(wparam) == ID_MANAGE_SONG_LINK_OLDER && st->index) st->index--;
        if (LOWORD(wparam) == ID_MANAGE_SONG_LINK_NEWER && st->index < groove_share_count()) st->index++;
        set_window_item_text(win, ID_MANAGE_SONG_LINK_STATUS, "%s", "");
        manage_link_refresh(win, st); return true;
      }
      if (LOWORD(wparam) == ID_MANAGE_SONG_LINK_SHARE) {
        if (!ui_media_share_url(st->url)) message_box(win, st->url, ui_tr("Song link"), MB_OK);
        return true;
      }
      if (LOWORD(wparam) == ID_MANAGE_SONG_LINK_DELETE) {
        if (message_box(win, ui_tr("Stop sharing this song? Its listening link will no longer work."), ui_tr("Stop Sharing"), MB_YESNO) != IDYES) return true;
        atomic_store(&st->done, false); st->worker = axThreadCreate(manage_link_worker, st);
        if (!st->worker) { set_window_item_text(win, ID_MANAGE_SONG_LINK_STATUS, "%s", ui_tr("Could not stop sharing. Check your connection and try again.")); return true; }
        enable_window(get_window_item(win, ID_MANAGE_SONG_LINK_SHARE), false);
        enable_window(get_window_item(win, ID_MANAGE_SONG_LINK_DELETE), false);
        enable_window(get_window_item(win, ID_MANAGE_SONG_LINK_OLDER), false);
        enable_window(get_window_item(win, ID_MANAGE_SONG_LINK_NEWER), false);
        enable_window(get_window_item(win, ID_MANAGE_SONG_LINK_CLOSE), false);
        set_window_item_text(win, ID_MANAGE_SONG_LINK_STATUS, "%s", ui_tr("Stopping sharing...")); return true;
      }
      return false;
    case evClose: if (!st->worker) end_dialog(win, 0); return true;
    case evDestroy:
      if (st) { if (st->timer) axCancelTimer(st->timer); axThreadJoin(st->worker); free(st); win->userdata = NULL; }
      return true;
    default: return false;
  }
}

static result_t share_song_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  (void)lparam;
  share_song_t *st = win->userdata;
  switch (msg) {
    case evCreate:
      st = allocate_window_data(win, sizeof(*st));
      if (!st) return false;
      atomic_init(&st->done, false);
      st->timer = axSetTimer(win, 100, NULL, true);
      if (!groove_share_configured()) {
        set_window_item_text(win, ID_SHARE_SONG_DETAILS, "%s", ui_tr("Share an MP3 of this song."));
        enable_window(get_window_item(win, ID_SHARE_SONG_SHARE), false);
        enable_window(get_window_item(win, ID_SHARE_SONG_PROJECT), false);
        set_window_item_text(win, ID_SHARE_SONG_STATUS, "%s", ui_tr("Listening links are not available."));
      }
      return true;
    case evPaint: return false;
    case evTimer:
      if (st->worker && atomic_load_explicit(&st->done, memory_order_acquire)) {
        axThreadJoin(st->worker); st->worker = NULL; share_audio_clear(st);
        enable_window(get_window_item(win, ID_SHARE_SONG_SHARE), groove_share_configured());
        enable_window(get_window_item(win, ID_SHARE_SONG_ATTACHMENT), true);
        enable_window(get_window_item(win, ID_SHARE_SONG_PROJECT), groove_share_configured());
        enable_window(get_window_item(win, ID_SHARE_SONG_MANAGE), true);
        enable_window(get_window_item(win, ID_SHARE_SONG_CLOSE), true);
        set_window_item_text(win, ID_SHARE_SONG_STATUS, "%s", ui_tr(st->ok ? (st->attachment ? "MP3 ready to share." : "Link created.") : (st->attachment ? "Could not prepare the MP3. Check available space and try again." : "Could not create the link. Check your connection and try again.")));
        if (st->ok && st->attachment && !ui_media_share(st->mp3))
          message_box(win, st->mp3, ui_tr("Share MP3..."), MB_OK);
        if (st->ok && !st->attachment && !ui_media_share_url(st->url))
          message_box(win, st->url, ui_tr("Song link"), MB_OK);
      }
      return true;
    case evCommand:
      if (HIWORD(wparam) != btnClicked) return false;
      if (LOWORD(wparam) == ID_SHARE_SONG_CLOSE) { if (!st->worker) end_dialog(win, 0); return true; }
      if (LOWORD(wparam) == ID_SHARE_SONG_MANAGE) { show_dialog_from_form(&groove_manage_song_link_form, NULL, win, manage_link_proc, NULL); return true; }
      if (LOWORD(wparam) == ID_SHARE_SONG_SHARE) { share_song_start(win, st, false); return true; }
      if (LOWORD(wparam) == ID_SHARE_SONG_ATTACHMENT) { share_song_start(win, st, true); return true; }
      return false;
    case evClose: if (!st->worker) end_dialog(win, 0); return true;
    case evDestroy:
      if (st) { if (st->timer) axCancelTimer(st->timer); axThreadJoin(st->worker); share_audio_clear(st); free(st); win->userdata = NULL; }
      return true;
    default: return false;
  }
}

void app_share_song(window_t *owner) {
  if (g_app) show_dialog_from_form(&groove_share_song_form, NULL, owner, share_song_proc, NULL);
}
#endif
