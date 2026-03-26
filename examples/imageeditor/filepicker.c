// File picker dialog (modal, PNG-filtered) — uses win_shellview internally.

#include "imageeditor.h"

#define PICKER_LIST_W  360
#define PICKER_LIST_H  220
#define PICKER_WIN_W   (PICKER_LIST_W + 8)
#define PICKER_WIN_H   (PICKER_LIST_H + 60)

typedef enum { PICKER_OPEN, PICKER_SAVE } picker_mode_t;

typedef struct {
  picker_mode_t  mode;
  char           result[512];
  bool           accepted;
  window_t      *list_win;   // win_shellview child
  window_t      *edit_win;   // filename text edit
} picker_state_t;

// Filter: show all directories and .png files only.
static bool png_only_filter(const char *name, bool is_dir) {
  return is_dir || is_png(name);
}

static result_t picker_proc(window_t *win, uint32_t msg,
                             uint32_t wparam, void *lparam) {
  picker_state_t *ps = (picker_state_t *)win->userdata;
  switch (msg) {
    case kWindowMessageCreate: {
      ps = (picker_state_t *)lparam;
      win->userdata = ps;

      // Shell-view handles all directory listing and navigation.
      ps->list_win = create_window("", WINDOW_NOTITLE | WINDOW_VSCROLL,
          MAKERECT(2, 2, PICKER_LIST_W, PICKER_LIST_H),
          win, win_shellview, NULL);

      // Restrict to directories and .png files.
      send_message(ps->list_win, SVM_SETFILTER, 0, png_only_filter);

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

      return true;
    }

    case kWindowMessageCommand: {
      uint16_t code = HIWORD(wparam);
      (void)LOWORD(wparam); // index — unused at this level

      if (code == SVN_SELCHANGE) {
        // Single-click on a file: populate the filename edit box.
        const char *name = (const char *)lparam;
        if (name && name[0]) {
          strncpy(ps->edit_win->title, name,
                  sizeof(ps->edit_win->title) - 1);
          ps->edit_win->title[sizeof(ps->edit_win->title) - 1] = '\0';
          invalidate_window(ps->edit_win);
        }
        return true;
      }

      if (code == SVN_ITEMACTIVATE) {
        // Double-click on a file: populate edit box (directory navigation is
        // already handled inside win_shellview before this notification).
        const char *name = (const char *)lparam;
        if (name && name[0]) {
          strncpy(ps->edit_win->title, name,
                  sizeof(ps->edit_win->title) - 1);
          ps->edit_win->title[sizeof(ps->edit_win->title) - 1] = '\0';
          invalidate_window(ps->edit_win);
        }
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
          // Ask shellview for the current directory.
          char curpath[512] = {0};
          send_message(ps->list_win, SVM_GETPATH, sizeof(curpath), curpath);

          char full[600];
          snprintf(full, sizeof(full), "%s/%s", curpath, fname);
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

