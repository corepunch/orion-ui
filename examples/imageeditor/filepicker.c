// File picker dialog (modal, PNG-filtered)

#include "imageeditor.h"

#define PICKER_LIST_W  360
#define PICKER_LIST_H  220
#define PICKER_WIN_W   (PICKER_LIST_W + 8)
#define PICKER_WIN_H   (PICKER_LIST_H + 60)

#define ICON_FOLDER icon8_collapse
#define ICON_FILE   icon8_checkbox
#define COLOR_FOLDER 0xffa0d000u

typedef enum { PICKER_OPEN, PICKER_SAVE } picker_mode_t;

typedef struct {
  picker_mode_t  mode;
  char           path[512];
  char           result[512];
  bool           accepted;
  window_t      *list_win;
  window_t      *edit_win;
} picker_state_t;

static void picker_load_dir(window_t *list_win, picker_state_t *ps) {
  send_message(list_win, CVM_CLEAR, 0, NULL);

  send_message(list_win, CVM_ADDITEM, 0,
    &(columnview_item_t){"..", ICON_FOLDER, COLOR_FOLDER, 1});

  DIR *dir = opendir(ps->path);
  if (!dir) return;

  typedef struct { char name[256]; bool is_dir; } entry_t;
  entry_t *entries = NULL;
  int count = 0, cap = 0;

  struct dirent *ent;
  while ((ent = readdir(dir))) {
    if (ent->d_name[0] == '.') continue;
    char full[768];
    snprintf(full, sizeof(full), "%s/%s", ps->path, ent->d_name);
    struct stat st;
    if (stat(full, &st) != 0) continue;
    bool is_dir = S_ISDIR(st.st_mode);
    if (!is_dir && !is_png(ent->d_name)) continue;
    if (count >= cap) {
      int new_cap = cap ? cap * 2 : 32;
      entry_t *new_entries = realloc(entries, sizeof(entry_t) * new_cap);
      if (!new_entries) {
        free(entries);
        closedir(dir);
        return;
      }
      entries = new_entries;
      cap = new_cap;
    }
    strncpy(entries[count].name, ent->d_name, 255);
    entries[count].name[255] = '\0';
    entries[count].is_dir = is_dir;
    count++;
  }
  closedir(dir);

  for (int i = 0; i < count - 1; i++) {
    for (int j = i + 1; j < count; j++) {
      bool swap = (!entries[i].is_dir && entries[j].is_dir) ||
                  (entries[i].is_dir == entries[j].is_dir &&
                   strcasecmp(entries[i].name, entries[j].name) > 0);
      if (swap) {
        entry_t tmp = entries[i];
        entries[i] = entries[j];
        entries[j] = tmp;
      }
    }
  }

  for (int i = 0; i < count; i++) {
    send_message(list_win, CVM_ADDITEM, 0,
      &(columnview_item_t){
        entries[i].name,
        entries[i].is_dir ? ICON_FOLDER : ICON_FILE,
        entries[i].is_dir ? COLOR_FOLDER : (uint32_t)COLOR_TEXT_NORMAL,
        (uint32_t)entries[i].is_dir
      });
  }
  free(entries);
  invalidate_window(list_win);
}

static result_t picker_proc(window_t *win, uint32_t msg,
                             uint32_t wparam, void *lparam) {
  picker_state_t *ps = (picker_state_t *)win->userdata;
  switch (msg) {
    case kWindowMessageCreate: {
      ps = (picker_state_t *)lparam;
      win->userdata = ps;

      ps->list_win = create_window("", WINDOW_NOTITLE | WINDOW_VSCROLL,
          MAKERECT(2, 2, PICKER_LIST_W, PICKER_LIST_H),
          win, win_columnview, NULL);

      create_window("File:", WINDOW_NOTITLE,
          MAKERECT(2, PICKER_LIST_H + 6, 28, CONTROL_HEIGHT),
          win, win_label, NULL);

      ps->edit_win = create_window("", WINDOW_NOTITLE,
          MAKERECT(32, PICKER_LIST_H + 4, PICKER_LIST_W - 32, CONTROL_HEIGHT),
          win, win_textedit, NULL);

      create_window(ps->mode == PICKER_OPEN ? "Open" : "Save", 0,
          MAKERECT(2, PICKER_LIST_H + 22, 50, BUTTON_HEIGHT),
          win, win_button, NULL);
      create_window("Cancel", 0,
          MAKERECT(56, PICKER_LIST_H + 22, 50, BUTTON_HEIGHT),
          win, win_button, NULL);

      picker_load_dir(ps->list_win, ps);
      return true;
    }

    case kWindowMessageCommand: {
      uint16_t code = HIWORD(wparam);
      uint16_t idx  = LOWORD(wparam);

      if (code == CVN_SELCHANGE || code == CVN_DBLCLK) {
        columnview_item_t *item = (columnview_item_t *)lparam;
        if (!item || !item->text) return true;

        if (item->userdata) {
          char newpath[512];
          if (strcmp(item->text, "..") == 0) {
            strncpy(newpath, ps->path, sizeof(newpath) - 1);
            newpath[sizeof(newpath) - 1] = '\0';
            char *slash = strrchr(newpath, '/');
            if (slash && slash != newpath) *slash = '\0';
            else { newpath[0]='/'; newpath[1]='\0'; }
          } else {
            snprintf(newpath, sizeof(newpath), "%s/%s", ps->path, item->text);
          }
          strncpy(ps->path, newpath, sizeof(ps->path) - 1);
          ps->path[sizeof(ps->path) - 1] = '\0';
          picker_load_dir(ps->list_win, ps);
        } else {
          strncpy(ps->edit_win->title, item->text,
                  sizeof(ps->edit_win->title) - 1);
          ps->edit_win->title[sizeof(ps->edit_win->title) - 1] = '\0';
          invalidate_window(ps->edit_win);
        }
        (void)idx;
        return true;
      }

      if (code == kButtonNotificationClicked) {
        window_t *btn = (window_t *)lparam;
        if (!btn) return true;
        if (strcmp(btn->title, "Cancel") == 0) {
          end_dialog(win, 0);
          return true;
        }
        const char *fname = ps->edit_win->title;
        if (fname[0]) {
          char full[600];
          snprintf(full, sizeof(full), "%s/%s", ps->path, fname);
          if (!is_png(full) && strlen(full) + 5 < sizeof(full))
            strcat(full, ".png");
          strncpy(ps->result, full, sizeof(ps->result) - 1);
          ps->accepted = true;
          end_dialog(win, 1);
        }
        return true;
      }
      return false;
    }

    default:
      return false;
  }
}

static bool show_file_picker(window_t *parent, bool save_mode,
                              char *out_path, size_t out_sz) {
  picker_state_t ps = {0};
  ps.mode = save_mode ? PICKER_SAVE : PICKER_OPEN;
  getcwd(ps.path, sizeof(ps.path));

  const char *title = save_mode ? "Save PNG" : "Open PNG";
  uint32_t result = show_dialog(title,
      MAKERECT(50, 50, PICKER_WIN_W, PICKER_WIN_H),
      parent, picker_proc, &ps);

  if (result && ps.accepted) {
    strncpy(out_path, ps.result, out_sz - 1);
    out_path[out_sz - 1] = '\0';
    return true;
  }
  return false;
}
