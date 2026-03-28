// File picker dialog (modal, PNG-filtered) — uses win_filelist internally.

#include "imageeditor.h"

#define PICKER_LIST_W  320
#define PICKER_LIST_H  140
#define WIN_PADDING 4
#define PICKER_WIN_W   (PICKER_LIST_W + WIN_PADDING * 2)
#define PICKER_WIN_H   (PICKER_LIST_H + 22 + BUTTON_HEIGHT + WIN_PADDING)
#define BUTTON_WIDTH 50

typedef enum { PICKER_OPEN, PICKER_SAVE } picker_mode_t;

typedef struct {
  picker_mode_t  mode;
  char           result[512];
  bool           accepted;
  window_t      *list_win;   // win_filelist child
  window_t      *edit_win;   // filename text edit
} picker_state_t;

static bool is_png(const char *path) {
  if (!path) return false;
  size_t n = strlen(path);
  if (n < 5) return false;
  const char *ext = path + n - 4;
  return (ext[0]=='.' &&
          (ext[1]=='p'||ext[1]=='P') &&
          (ext[2]=='n'||ext[2]=='N') &&
          (ext[3]=='g'||ext[3]=='G'));
}

static result_t picker_proc(window_t *win, uint32_t msg,
                             uint32_t wparam, void *lparam) {
  picker_state_t *ps = (picker_state_t *)win->userdata;
  switch (msg) {
    case kWindowMessageCreate: {
      ps = (picker_state_t *)lparam;
      win->userdata = ps;

      // win_filelist handles all directory listing, navigation, and sorting.
      ps->list_win = create_window("", WINDOW_NOTITLE | WINDOW_VSCROLL,
          MAKERECT(WIN_PADDING, WIN_PADDING, PICKER_LIST_W, PICKER_LIST_H),
          win, win_filelist, NULL);

      // Restrict file listing to .png files (directories are always shown).
      send_message(ps->list_win, FLM_SETFILTER, 0, (void *)".png");

      create_window("File:", WINDOW_NOTITLE,
          MAKERECT(WIN_PADDING, PICKER_LIST_H + 4, 28, CONTROL_HEIGHT),
          win, win_label, NULL);

      ps->edit_win = create_window("", WINDOW_NOTITLE,
          MAKERECT(32, PICKER_LIST_H + 4, PICKER_WIN_W - 32 - WIN_PADDING, CONTROL_HEIGHT),
          win, win_textedit, NULL);

      create_window(ps->mode == PICKER_OPEN ? "Open" : "Save", 0,
          MAKERECT(PICKER_WIN_W - (BUTTON_WIDTH + WIN_PADDING) * 2, PICKER_LIST_H + 22, BUTTON_WIDTH, BUTTON_HEIGHT),
          win, win_button, NULL);
      create_window("Cancel", 0,
          MAKERECT(PICKER_WIN_W - (BUTTON_WIDTH + WIN_PADDING), PICKER_LIST_H + 22, BUTTON_WIDTH, BUTTON_HEIGHT),
          win, win_button, NULL);

      return true;
    }

    case kWindowMessageCommand: {
      uint16_t code = HIWORD(wparam);

      if (code == FLN_SELCHANGE) {
        // Single-click on a file: populate the filename edit box.
        const fileitem_t *item = (const fileitem_t *)lparam;
        if (item && !item->is_directory && item->path) {
          const char *base = strrchr(item->path, '/');
          base = base ? base + 1 : item->path;
          strncpy(ps->edit_win->title, base,
                  sizeof(ps->edit_win->title) - 1);
          ps->edit_win->title[sizeof(ps->edit_win->title) - 1] = '\0';
          invalidate_window(ps->edit_win);
        }
        return true;
      }

      if (code == FLN_FILEOPEN) {
        // Double-click on a file: populate edit box.
        // Directory navigation is already handled inside win_filelist.
        const fileitem_t *item = (const fileitem_t *)lparam;
        if (item && item->path) {
          const char *base = strrchr(item->path, '/');
          base = base ? base + 1 : item->path;
          strncpy(ps->edit_win->title, base,
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
          // Try the selected item's full path first.  If no file item is
          // selected (e.g. the user typed a name manually in the edit box),
          // construct the path from the current directory and the edit-box content.
          char full[600] = {0};
          send_message(ps->list_win, FLM_GETSELECTEDPATH, sizeof(full), full);
          if (!full[0]) {
            char curpath[512] = {0};
            send_message(ps->list_win, FLM_GETPATH, sizeof(curpath), curpath);
            snprintf(full, sizeof(full), "%s/%s", curpath, fname);
          }
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


