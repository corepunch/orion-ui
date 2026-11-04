#include "groove.h"
#include <orion/kernel/audio_input.h>
#include <stdatomic.h>
#include <orion/commctl/columnview.h>

typedef struct {
  sample_t sample;
  atomic_int received;
  ui_audio_input_t *input;
  uint32_t timer;
} record_sample_t;

static const ctrl_binding_t kRecordBindings[] = { DDX_TEXT(ID_RECORD_SAMPLE_NAME, record_sample_t, sample.name) };

static void record_receive(void *userdata, const int16_t *pcm, int frames) {
  record_sample_t *st = userdata;
  int used = atomic_load_explicit(&st->received, memory_order_relaxed);
  int n = MIN(frames, st->sample.frames - used);
  if (n > 0) {
    memcpy(st->sample.pcm + used, pcm, (size_t)n * sizeof(*pcm));
    atomic_store_explicit(&st->received, used + n, memory_order_release);
  }
}

static void record_preview_clear(void) {
  app_lock();
  g_app->sample_preview = NULL; g_app->sample_preview_frames = g_app->sample_preview_pos = 0;
  app_unlock();
}

static void record_stop(record_sample_t *st) {
  app_lock();
  g_app->recording_beat_frames = 0;
  app_unlock();
  ui_audio_input_close(st->input);
  st->input = NULL;
}

static void record_refresh(window_t *win, record_sample_t *st) {
  bool active = st->input != NULL, ready = !active && atomic_load(&st->received) > 0;
  enable_window(get_window_item(win, ID_RECORD_SAMPLE_RECORD), !active);
  enable_window(get_window_item(win, ID_RECORD_SAMPLE_BARS), !active);
  enable_window(get_window_item(win, ID_RECORD_SAMPLE_METRONOME), !active && g_app->audio_dev);
  enable_window(get_window_item(win, ID_RECORD_SAMPLE_STOP), active);
  enable_window(get_window_item(win, ID_RECORD_SAMPLE_PREVIEW), ready && g_app->audio_dev);
  enable_window(get_window_item(win, ID_RECORD_SAMPLE_SAVE), ready);
}

static void record_start(window_t *win, record_sample_t *st) {
  if (st->input) return;
  ui_microphone_permission_t permission = ui_microphone_permission(true);
  if (permission != UI_MIC_ALLOWED) {
    const char *text = permission == UI_MIC_DENIED ? "Enable Microphone for Groove in system settings." :
      permission == UI_MIC_UNAVAILABLE ? "Microphone recording is unavailable on this platform." : "Allow microphone access, then press Record again.";
    set_window_item_text(win, ID_RECORD_SAMPLE_STATUS, "%s", ui_tr(text));
    return;
  }
  record_preview_clear();
  int choice = (int)send_message(get_window_item(win, ID_RECORD_SAMPLE_BARS), cbGetCurrentSelection, 0, NULL);
  int bars = choice == 0 ? 1 : choice == 2 ? 4 : 2;
  free(st->sample.pcm);
  st->sample = (sample_t){ .bpm = g_app->song.bpm, .bars = bars, .frames = bars * bar_frames_for_bpm(g_app->song.bpm) };
  st->sample.pcm = calloc((size_t)st->sample.frames, sizeof(int16_t));
  atomic_store(&st->received, 0);
  if (st->sample.pcm) st->input = ui_audio_input_open(GR_SAMPLE_RATE, record_receive, st);
  if (!st->input) {
    set_window_item_text(win, ID_RECORD_SAMPLE_STATUS, "%s", ui_tr("Microphone unavailable. Check access and the input device."));
    record_refresh(win, st);
    return;
  }
  if (g_app->audio_dev && send_message(get_window_item(win, ID_RECORD_SAMPLE_METRONOME), btnGetCheck, 0, NULL)) {
    app_lock();
    g_app->recording_beat_frames = bar_frames_for_bpm(st->sample.bpm) / GR_BEATS_BAR;
    g_app->recording_click_pos = 0;
    app_unlock();
  }
  set_window_item_text(win, ID_RECORD_SAMPLE_STATUS, "%s", ui_tr("Recording..."));
  record_refresh(win, st);
}

void app_samples_refresh(void) {
  if (g_app && g_app->bin) send_message(g_app->bin, binReload, 0, NULL);
  if (g_app && g_app->sheet) invalidate_window(g_app->sheet);
}

static result_t record_sample_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  record_sample_t *st = win->userdata;
  switch (msg) {
    case evCreate: {
      if (!(st = allocate_window_data(win, sizeof(*st)))) return false;
      atomic_init(&st->received, 0);
      set_window_item_text(win, ID_RECORD_SAMPLE_NAME, "%s", ui_tr("Recording"));
      window_t *bars = get_window_item(win, ID_RECORD_SAMPLE_BARS);
      send_message(bars, cbAddString, 0, (void *)ui_tr("1 bar"));
      send_message(bars, cbAddString, 0, (void *)ui_tr("2 bars"));
      send_message(bars, cbAddString, 0, (void *)ui_tr("4 bars"));
      send_message(bars, cbSetCurrentSelection, 1, NULL);
      record_refresh(win, st);
      st->timer = axSetTimer(win, 50, NULL, true);
      return true;
    }
    case evPaint: return false;
    case evTimer:
      if (st->input && atomic_load_explicit(&st->received, memory_order_acquire) >= st->sample.frames) {
        record_stop(st);
        set_window_item_text(win, ID_RECORD_SAMPLE_STATUS, "%s", ui_tr("Ready. Listen or save the sample."));
        record_refresh(win, st);
      }
      return true;
    case evCommand:
      if (HIWORD(wparam) != btnClicked) return false;
      switch (LOWORD(wparam)) {
        case ID_RECORD_SAMPLE_RECORD: record_start(win, st); return true;
        case ID_RECORD_SAMPLE_STOP:
          record_stop(st);
          set_window_item_text(win, ID_RECORD_SAMPLE_STATUS, "%s", ui_tr("Stopped. Unrecorded time is kept silent."));
          record_refresh(win, st); return true;
        case ID_RECORD_SAMPLE_PREVIEW:
          if (!st->input && atomic_load(&st->received) > 0) {
            app_set_playing(false);
            app_preview_stop();
            app_lock();
            g_app->sample_preview = st->sample.pcm;
            g_app->sample_preview_frames = st->sample.frames;
            g_app->sample_preview_pos = 0;
            app_unlock();
          }
          return true;
        case ID_RECORD_SAMPLE_SAVE: {
          if (st->input || atomic_load(&st->received) <= 0) return true;
          dialog_pull(win, st, kRecordBindings, ARRAY_LEN(kRecordBindings));
          if (!st->sample.name[0]) { set_window_item_text(win, ID_RECORD_SAMPLE_STATUS, "%s", ui_tr("Enter a sample name.")); return true; }
          int before = samples_count(), id = sample_add(&st->sample);
          if (id < 0 || !sample_store(g_app->samples_dir, id)) {
            if (id >= 0) samples_truncate(before);
            message_box(win, ui_tr("Could not save the sample. Check available space."), ui_tr("Save failed"), MB_OK);
            return true;
          }
          app_samples_refresh();
          app_set_family(FAMILY_SAMPLES);
          end_dialog(win, 1); return true;
        }
        case ID_RECORD_SAMPLE_CANCEL: end_dialog(win, 0); return true;
      }
      return false;
    case evClose: end_dialog(win, 0); return true;
    case evDestroy:
      if (st) {
        if (st->timer) axCancelTimer(st->timer);
        record_stop(st); record_preview_clear();
        free(st->sample.pcm); free(st); win->userdata = NULL;
      }
      return true;
    default: return false;
  }
}

void app_record_sample(window_t *owner) {
  if (!g_app) return;
  show_dialog_from_form(&groove_record_sample_form, NULL, owner, record_sample_proc, NULL);
}

typedef struct { char name[64]; } sample_name_t;
static const ctrl_binding_t kManageBindings[] = { DDX_TEXT(ID_MANAGE_SAMPLES_NAME, sample_name_t, name) };

static const library_block_t *manage_selection(window_t *win) {
  window_t *table = get_window_item(win, ID_MANAGE_SAMPLES_RECORDINGS);
  return table ? (const library_block_t *)(intptr_t)send_message(table, tvGetSelectedRecord, 0, NULL) : NULL;
}

static void manage_selection_changed(window_t *win) {
  const library_block_t *row = manage_selection(win);
  set_window_item_text(win, ID_MANAGE_SAMPLES_NAME, "%s", row ? row->name : "");
  enable_window(get_window_item(win, ID_MANAGE_SAMPLES_NAME), row != NULL);
  enable_window(get_window_item(win, ID_MANAGE_SAMPLES_RENAME), row != NULL);
  enable_window(get_window_item(win, ID_MANAGE_SAMPLES_DELETE), row != NULL);
  window_t *empty = get_window_item(win, ID_MANAGE_SAMPLES_EMPTY);
  if (empty) show_window(empty, !row);
}

static result_t manage_samples_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  switch (msg) {
    case evCreate: manage_selection_changed(win); return true;
    case evPaint: return false;
    case evDestroy: return true;
    case evClose: end_dialog(win, 0); return true;
    case evCommand: {
      uint16_t id = LOWORD(wparam), event = HIWORD(wparam);
      if (event == RVN_SELCHANGE && lparam == get_window_item(win, ID_MANAGE_SAMPLES_RECORDINGS)) {
        manage_selection_changed(win);
        return true;
      }
      if (event != btnClicked) return false;
      if (id == ID_MANAGE_SAMPLES_CLOSE) { end_dialog(win, 0); return true; }
      if (id != ID_MANAGE_SAMPLES_RENAME && id != ID_MANAGE_SAMPLES_DELETE) return false;
      const library_block_t *row = manage_selection(win);
      if (!row) return true;
      int block = row->id;
      if (id == ID_MANAGE_SAMPLES_RENAME) {
        sample_name_t edit = {0};
        dialog_pull(win, &edit, kManageBindings, ARRAY_LEN(kManageBindings));
        if (!edit.name[0]) {
          message_box(win, ui_tr("Enter a sample name."), ui_tr("Rename"), MB_OK);
          return true;
        }
        if (!sample_rename(block, edit.name)) {
          message_box(win, ui_tr("Could not rename the recording."), ui_tr("Manage Recordings"), MB_OK);
          return true;
        }
      } else {
        char prompt[256];
        snprintf(prompt, sizeof(prompt), ui_tr("Delete “%s” from My Samples? Saved songs keep their own copies."), row->name);
        if (message_box(win, prompt, ui_tr("Delete Recording"), MB_YESNO) != IDYES) return true;
        if (!sample_delete(block)) {
          message_box(win, ui_tr("Could not delete the recording."), ui_tr("Manage Recordings"), MB_OK);
          return true;
        }
      }
      app_samples_refresh();
      send_message(get_window_item(win, ID_MANAGE_SAMPLES_RECORDINGS), tvRefresh, 0, NULL);
      manage_selection_changed(win);
      return true;
    }
    default: return false;
  }
}

void app_manage_samples(window_t *owner) {
  if (!g_app) return;
  show_dialog_from_form(&groove_manage_samples_form, NULL, owner, manage_samples_proc, NULL);
}
