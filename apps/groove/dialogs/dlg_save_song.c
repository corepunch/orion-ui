// Save Song: asks for a name and saves into Groove's own song storage.

#include "groove.h"

typedef struct { char name[128]; } save_song_t;

static const ctrl_binding_t kSaveBindings[] = {
  DDX_TEXT(ID_SAVE_SONG_NAME, save_song_t, name),
};

static bool save_named(window_t *win, const char *name) {
  char path[sizeof(g_app->filename)];
  if (!g_app->songs_dir[0]) {
    message_box(win, "Groove's song storage is unavailable on this device.", "Save failed", MB_OK);
    return false;
  }
  if (!song_path_for_name(name, path, sizeof(path))) {
    message_box(win, "Enter a song name without slashes or colons.", "Save Song", MB_OK);
    return false;
  }
  if (strcmp(path, g_app->filename) && axPathExists(path)) {
    char prompt[256];
    snprintf(prompt, sizeof(prompt), "A song named \"%s\" already exists. Replace it?", name);
    if (message_box(win, prompt, "Save Song", MB_YESNO) != IDYES) return false;
  }
  if (!app_save_song(path)) {
    message_box(win, "Could not save the Groove song. Check the available space.", "Save failed", MB_OK);
    return false;
  }
  return true;
}

static result_t save_song_proc(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  save_song_t *st = win->userdata;
  if (msg == evCreate) {
    if (!(st = allocate_window_data(win, sizeof(*st)))) {
      fprintf(stderr, "[gr] save dialog allocation failed win=%u\n", (unsigned)win->id);
      fflush(stderr);
      return false;
    }
    snprintf(st->name, sizeof(st->name), "%s", lparam ? (const char *)lparam : "");
    dialog_push(win, st, kSaveBindings, ARRAY_LEN(kSaveBindings));
    set_focus(get_window_item(win, ID_SAVE_SONG_NAME));
    return true;
  }
  if (msg == evDestroy) { free(st); win->userdata = NULL; return true; }
  if (msg != evCommand || HIWORD(wparam) != btnClicked) return false;
  switch (LOWORD(wparam)) {
    case ID_SAVE_SONG_CANCEL: end_dialog(win, 0); return true;
    case ID_SAVE_SONG_OK:
      dialog_pull(win, st, kSaveBindings, ARRAY_LEN(kSaveBindings));
      if (save_named(win, st->name)) end_dialog(win, 1);
      return true;
    default: return false;
  }
}

bool app_save_song_as(window_t *owner) {
  if (!g_app) return false;
  char name[128];
  song_name_from_path(g_app->filename, name, sizeof(name));
  return show_dialog_from_form(&groove_save_song_form, "Save Song", owner, save_song_proc, name[0] ? name : "Untitled") == 1;
}
