#include "animation_timing.h"
#include <errno.h>

static bool animation_integer(window_t *win, uint32_t id, int maximum, int *value) {
  window_t *edit = get_window_item(win, id);
  if (!edit || !*edit->title) return false;
  char *end; errno = 0;
  long parsed = strtol(edit->title, &end, 10);
  if (errno || *end || parsed < 1 || parsed > maximum) return false;
  *value = parsed;
  return true;
}

static result_t animation_timing_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  animation_timing_state_t *st = win->userdata;
  switch (msg) {
    case evCreate:
      st = lparam; win->userdata = st;
      set_window_item_text(win, st->duration ? ID_FRAME_DURATION_VALUE : ID_ANIMATION_FPS_VALUE, "%d",
        st->duration ? st->doc->anim->frames[st->doc->anim->active_frame].delay_ms : st->doc->anim->fps);
      if (!st->duration) send_message(get_window_item(win, ID_ANIMATION_FPS_SCOPE), btnSetCheck, btnStateChecked, NULL);
      return true;
    case evPaint: return false;
    case evDestroy: return true;
    case evCommand: {
      uint32_t id = LOWORD(wparam);
      if (HIWORD(wparam) != btnClicked) return false;
      if (id == ID_ANIMATION_FPS_CANCEL || id == ID_FRAME_DURATION_CANCEL) { end_dialog(win, 0); return true; }
      if (id != ID_ANIMATION_FPS_OK && id != ID_FRAME_DURATION_OK) return false;
      int value;
      bool valid = animation_integer(win, st->duration ? ID_FRAME_DURATION_VALUE : ID_ANIMATION_FPS_VALUE,
                                     st->duration ? 65535 : 1000, &value);
      bool scope = send_message(get_window_item(win, st->duration ? ID_FRAME_DURATION_SCOPE : ID_ANIMATION_FPS_SCOPE),
                                btnGetCheck, 0, NULL) == btnStateChecked;
      if (valid) valid = st->duration ? cmd_frame_duration(st->doc, value, scope) : cmd_anim_fps(st->doc, value, scope);
      if (!valid) {
        set_window_item_text(win, st->duration ? ID_FRAME_DURATION_ERROR : ID_ANIMATION_FPS_ERROR, "%s",
          st->duration ? "Use a whole number from 1 to 65535 ms." : "Use 1–1000 FPS and durations up to 65535 ms.");
        return true;
      }
      end_dialog(win, 1);
      return true;
    }
    default: return false;
  }
}

void show_animation_timing(window_t *parent, canvas_doc_t *doc, bool duration) {
  if (!doc || !doc->anim || doc->command.before) return;
  anim_stop_playback(doc);
  animation_timing_state_t st = {.doc = doc, .duration = duration};
  show_dialog_from_form_ex(duration ? &imageeditor_frame_duration_form : &imageeditor_animation_fps_form,
    duration ? "Frame Duration" : "Animation Speed", parent, WINDOW_DIALOG | WINDOW_NOTRAYBUTTON,
    animation_timing_proc, &st);
}
