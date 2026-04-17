// commctl/filepicker.c — generic modal file-picker dialog.
//
// Implements get_open_filename() and get_save_filename(), both analogous to
// the WinAPI GetOpenFileName / GetSaveFileName functions.  Internally uses
// win_filelist for directory browsing, win_textedit for the filename input,
// and (when multiple filters are provided) win_combobox for filter selection.
//
// Double-clicking a file in open mode immediately accepts without requiring
// a click on the Open button (WinAPI / Explorer behaviour).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/stat.h>

#include "filepicker.h"
#include "filelist.h"
#include "commctl.h"
#include "../user/user.h"
#include "../user/icons.h"
#include "../user/messages.h"

// ---------------------------------------------------------------------------
// Layout constants
// ---------------------------------------------------------------------------

#define FP_LIST_W    320
#define FP_LIST_H    160
#define FP_PAD         4
#define FP_LABEL_W    38   // "File:" / "Filter:" label column width
#define FP_BTN_W      50
#define FP_BTN_H      BUTTON_HEIGHT
#define FP_EDIT_H     CONTROL_HEIGHT
#define FP_COMBO_H    BUTTON_HEIGHT
#define FP_ROW_GAP     4   // vertical gap between rows
#define FP_WIN_W      (FP_LIST_W + FP_PAD * 2)
#define FP_CTRL_X     (FP_PAD + FP_LABEL_W + 2)
#define FP_CTRL_W     (FP_WIN_W - FP_CTRL_X - FP_PAD)
#define FP_FILTER_Y   (FP_FILE_Y + FP_EDIT_H + FP_ROW_GAP)
#define FP_BTN_Y      (FP_FILTER_Y + FP_COMBO_H + FP_ROW_GAP)

// Vertical positions of each row (relative to client-area origin)
#define FP_LIST_Y     FP_PAD
#define FP_FILE_Y     (FP_LIST_Y + FP_LIST_H + FP_ROW_GAP)

// ---------------------------------------------------------------------------
// Maximum number of filter entries parsed from lpstrFilter
// ---------------------------------------------------------------------------
#define FP_MAX_FILTERS 16

enum {
  FP_ID_TOOL_UP = 1,
  FP_ID_TOOL_NEW_FOLDER,
  FP_ID_FILE_EDIT,
  FP_ID_FILTER_COMBO,
  FP_ID_OK,
  FP_ID_CANCEL,
  FP_ID_NEWFOLDER_EDIT = 100,
  FP_ID_NEWFOLDER_OK,
  FP_ID_NEWFOLDER_CANCEL,
};

static const toolbar_button_t kFilePickerToolbar[] = {
  { sysicon_folder_up, FP_ID_TOOL_UP, 0 },
  { sysicon_folder, FP_ID_TOOL_NEW_FOLDER, 0 },
};

typedef struct {
  char name[256];
} fp_newfolder_state_t;

static const form_ctrl_def_t kNewFolderChildren[] = {
  { FORM_CTRL_LABEL, FP_ID_NEWFOLDER_EDIT + 1000, {8, 10, 72, CONTROL_HEIGHT}, 0, "Folder name:", "label_name" },
  { FORM_CTRL_TEXTEDIT, FP_ID_NEWFOLDER_EDIT, {82, 8, 150, CONTROL_HEIGHT}, 0, "", "edit_name" },
  { FORM_CTRL_BUTTON, FP_ID_NEWFOLDER_OK, {116, 32, 54, BUTTON_HEIGHT}, BUTTON_DEFAULT, "OK", "ok" },
  { FORM_CTRL_BUTTON, FP_ID_NEWFOLDER_CANCEL, {176, 32, 60, BUTTON_HEIGHT}, 0, "Cancel", "cancel" },
};

static const form_def_t kNewFolderForm = {
  .name = "Create Folder",
  .w = 244,
  .h = 58,
  .children = kNewFolderChildren,
  .child_count = sizeof(kNewFolderChildren) / sizeof(kNewFolderChildren[0]),
};

static const form_ctrl_def_t kFilePickerChildren[] = {
  { FORM_CTRL_LABEL, -1, { FP_PAD, FP_FILE_Y, FP_LABEL_W, FP_EDIT_H }, 0, "File:", "lbl_file" },
  { FORM_CTRL_TEXTEDIT, FP_ID_FILE_EDIT, { FP_CTRL_X, FP_FILE_Y, FP_CTRL_W, FP_EDIT_H }, 0, "", "edit_file" },
  { FORM_CTRL_LABEL, -1, { FP_PAD, FP_FILTER_Y + (FP_COMBO_H - CONTROL_HEIGHT) / 2, FP_LABEL_W, CONTROL_HEIGHT }, 0, "Filter:", "lbl_filter" },
  { FORM_CTRL_COMBOBOX, FP_ID_FILTER_COMBO, { FP_CTRL_X, FP_FILTER_Y, FP_CTRL_W, FP_COMBO_H }, 0, "", "combo_filter" },
  { FORM_CTRL_BUTTON, FP_ID_OK, { FP_WIN_W - (FP_BTN_W + FP_PAD) * 2, FP_BTN_Y, FP_BTN_W, FP_BTN_H }, BUTTON_DEFAULT, "Open", "btn_ok" },
  { FORM_CTRL_BUTTON, FP_ID_CANCEL, { FP_WIN_W - (FP_BTN_W + FP_PAD), FP_BTN_Y, FP_BTN_W, FP_BTN_H }, 0, "Cancel", "btn_cancel" },
};

static const form_def_t kFilePickerForm = {
  .name = "File Picker",
  .w = FP_WIN_W,
  .h = FP_BTN_Y + FP_BTN_H + FP_PAD,
  .flags = 0,
  .children = kFilePickerChildren,
  .child_count = sizeof(kFilePickerChildren) / sizeof(kFilePickerChildren[0]),
};

// ---------------------------------------------------------------------------
// Internal types
// ---------------------------------------------------------------------------

typedef struct {
  char description[128];
  char extension[32];   // e.g. ".png"; empty string means "all files"
} fp_filter_t;

typedef struct {
  bool            save_mode;
  bool            accepted;
  window_t       *list_win;
  window_t       *edit_win;
  window_t       *filter_combo;   // NULL when only 0–1 filters
  openfilename_t *ofn;
  fp_filter_t     filters[FP_MAX_FILTERS];
  int             num_filters;
  int             active_filter;  // 0-based
} fp_state_t;

static result_t fp_newfolder_proc(window_t *win, uint32_t msg,
                                  uint32_t wparam, void *lparam) {
  fp_newfolder_state_t *st = (fp_newfolder_state_t *)win->userdata;

  switch (msg) {
    case kWindowMessageCreate: {
      st = (fp_newfolder_state_t *)lparam;
      win->userdata = st;
      if (st) {
        set_window_item_text(win, FP_ID_NEWFOLDER_EDIT, "%s", st->name);
      }
      window_t *edit = get_window_item(win, FP_ID_NEWFOLDER_EDIT);
      if (edit) set_focus(edit);
      return true;
    }

    case kWindowMessageCommand:
      if (HIWORD(wparam) == kButtonNotificationClicked) {
        window_t *src = (window_t *)lparam;
        if (!src) return true;
        if (src->id == FP_ID_NEWFOLDER_OK) {
          dialog_pull(win, st,
                      &(ctrl_binding_t){ FP_ID_NEWFOLDER_EDIT, offsetof(fp_newfolder_state_t, name), sizeof(st->name), BIND_STRING },
                      1);
          end_dialog(win, 1);
          return true;
        }
        if (src->id == FP_ID_NEWFOLDER_CANCEL) {
          end_dialog(win, 0);
          return true;
        }
      }
      return false;

    default:
      return false;
  }
}

// ---------------------------------------------------------------------------
// Filter parsing
// ---------------------------------------------------------------------------

// Parse a WinAPI-style double-NUL-terminated filter string into entries.
// Each entry is a (description, pattern) pair, e.g.:
//   "PNG Files\0*.png\0All Files\0*.*\0"
// The extension field is filled with ".ext" for "*.ext", "" for "*.*".
static int fp_parse_filters(const char *raw, fp_filter_t *out, int max) {
  if (!raw || !raw[0]) return 0;
  int count = 0;
  const char *p = raw;
  while (*p && count < max) {
    // Description string
    strncpy(out[count].description, p, sizeof(out[count].description) - 1);
    out[count].description[sizeof(out[count].description) - 1] = '\0';
    p += strlen(p) + 1;
    if (!*p) break;  // malformed — no pattern

    // Pattern string, e.g. "*.png" or "*.*" or "*.png;*.jpg"
    const char *pattern = p;
    p += strlen(p) + 1;

    // Extract first extension from patterns like "*.png" or "*.png;*.jpg"
    // "*.ext" → ".ext", "*.*" → "" (all files)
    const char *star = strchr(pattern, '*');
    if (star && star[1] == '.') {
      const char *dot = star + 1;
      // Find end of this extension (semicolon separates multiple patterns)
      const char *end = strpbrk(dot, ";");
      if (!end) end = dot + strlen(dot);
      if (dot[1] == '*') {
        // ".*" means all files
        out[count].extension[0] = '\0';
      } else {
        size_t len = (size_t)(end - dot);
        if (len >= sizeof(out[count].extension))
          len = sizeof(out[count].extension) - 1;
        strncpy(out[count].extension, dot, len);
        out[count].extension[len] = '\0';
      }
    } else {
      out[count].extension[0] = '\0';  // unknown pattern → show all
    }

    count++;
  }
  return count;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Apply the currently selected extension filter to the filelist.
static void fp_apply_filter(fp_state_t *ps) {
  if (!ps->list_win) return;
  const char *ext = (ps->active_filter >= 0 && ps->active_filter < ps->num_filters)
                    ? ps->filters[ps->active_filter].extension
                    : "";
  send_message(ps->list_win, FLM_SETFILTER, 0, (void *)(ext[0] ? ext : NULL));
}

// Populate the filename edit box with the basename of a path.
static void fp_set_edit_from_path(fp_state_t *ps, const char *path) {
  const char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  set_window_item_text(get_root_window(ps->edit_win), FP_ID_FILE_EDIT, "%s", base);
}

static void fp_get_current_dir(fp_state_t *ps, char *out, size_t out_sz) {
  if (!out || out_sz == 0) return;
  out[0] = '\0';
  send_message(ps->list_win, FLM_GETPATH, (uint32_t)out_sz, out);
}

static void fp_navigate_to_parent(fp_state_t *ps) {
  char curpath[512] = {0};
  fp_get_current_dir(ps, curpath, sizeof(curpath));
  if (!curpath[0] || strcmp(curpath, "/") == 0) return;

  char *slash = strrchr(curpath, '/');
  if (slash && slash != curpath) {
    *slash = '\0';
  } else {
    curpath[0] = '/';
    curpath[1] = '\0';
  }

  send_message(ps->list_win, FLM_SETPATH, 0, curpath);
}

static bool fp_prompt_new_folder(window_t *parent, char *out, size_t out_sz) {
  fp_newfolder_state_t st = {{0}};
  uint32_t result;

  if (!out || out_sz == 0) return false;

  result = show_dialog_from_form(&kNewFolderForm, "Create Folder", parent,
                                 fp_newfolder_proc, &st);
  if (result == 0 || !st.name[0]) return false;

  strncpy(out, st.name, out_sz - 1);
  out[out_sz - 1] = '\0';
  return true;
}

static void fp_create_folder(window_t *win, fp_state_t *ps) {
  char curpath[512] = {0};
  char name[256] = {0};
  char full[768] = {0};

  if (!ps || !ps->list_win) return;
  fp_get_current_dir(ps, curpath, sizeof(curpath));
  if (!curpath[0]) return;

  if (!fp_prompt_new_folder(win, name, sizeof(name))) return;

  if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0 || strchr(name, '/')) {
    message_box(win, "Enter a valid folder name.", "Create Folder", MB_OK);
    return;
  }

  if (strcmp(curpath, "/") == 0)
    snprintf(full, sizeof(full), "/%s", name);
  else
    snprintf(full, sizeof(full), "%s/%s", curpath, name);

  if (mkdir(full, 0777) != 0) {
    char text[256];
    snprintf(text, sizeof(text), "Could not create folder:\n%s", strerror(errno));
    message_box(win, text, "Create Folder", MB_OK);
    return;
  }

  send_message(ps->list_win, FLM_SETPATH, 0, full);
}

// Build the full path from the selected filelist item or the edit box + cwd.
// Returns false when the edit box is empty.
static bool fp_build_path(fp_state_t *ps, char *out, size_t out_sz) {
  const char *fname = ps->edit_win ? ps->edit_win->title : NULL;
  if (!fname || !fname[0]) return false;

  // Try the selected item's full path first (set by single-click).
  char selected[512] = {0};
  send_message(ps->list_win, FLM_GETSELECTEDPATH, sizeof(selected), selected);
  if (selected[0]) {
    strncpy(out, selected, out_sz - 1);
    out[out_sz - 1] = '\0';
    return true;
  }

  // Otherwise construct from current directory + edit-box text.
  char curpath[512] = {0};
  send_message(ps->list_win, FLM_GETPATH, sizeof(curpath), curpath);
  snprintf(out, out_sz, "%s/%s", curpath, fname);
  return true;
}

// Accept the dialog with the full path of 'item' (double-click shortcut).
static void fp_accept_item(window_t *win, fp_state_t *ps,
                            const fileitem_t *item) {
  if (!item || !item->path) return;
  strncpy(ps->ofn->lpstrFile, item->path, ps->ofn->nMaxFile - 1);
  ps->ofn->lpstrFile[ps->ofn->nMaxFile - 1] = '\0';
  ps->accepted = true;
  end_dialog(win, 1);
}

// ---------------------------------------------------------------------------
// Dialog window procedure
// ---------------------------------------------------------------------------

static result_t fp_proc(window_t *win, uint32_t msg,
                         uint32_t wparam, void *lparam) {
  fp_state_t *ps = (fp_state_t *)win->userdata;

  switch (msg) {

    // ------------------------------------------------------------------
    case kWindowMessageCreate: {
      ps = (fp_state_t *)lparam;
      win->userdata = ps;
      send_message(win, kToolBarMessageAddButtons,
                   sizeof(kFilePickerToolbar) / sizeof(kFilePickerToolbar[0]),
                   (void *)kFilePickerToolbar);

      ps->edit_win = get_window_item(win, FP_ID_FILE_EDIT);
      ps->filter_combo = get_window_item(win, FP_ID_FILTER_COMBO);

      if (ps->edit_win && ps->ofn->lpstrFile && ps->ofn->lpstrFile[0]) {
        const char *base = strrchr(ps->ofn->lpstrFile, '/');
        base = base ? base + 1 : ps->ofn->lpstrFile;
        set_window_item_text(win, FP_ID_FILE_EDIT, "%s", base);
      }

      if (ps->filter_combo) {
        for (int i = 0; i < ps->num_filters; i++) {
          send_message(ps->filter_combo, kComboBoxMessageAddString,
                       0, (void *)ps->filters[i].description);
        }
        if (ps->num_filters > 0) {
          send_message(ps->filter_combo, kComboBoxMessageSetCurrentSelection,
                       (uint32_t)ps->active_filter, NULL);
          enable_window(ps->filter_combo, true);
        } else {
          enable_window(ps->filter_combo, false);
        }
      }

      // File browser list
      ps->list_win = create_window("", WINDOW_NOTITLE | WINDOW_VSCROLL,
          MAKERECT(0, 0, FP_LIST_W + FP_PAD * 2, FP_LIST_H + FP_PAD),
          win, win_filelist, 0, NULL);

      // Apply the initial filter
      fp_apply_filter(ps);

      // Pre-fill the edit box with any path already in the buffer.
      // Navigate to the pre-filled directory; the basename is copied into the
      // edit_win below, after it's created.
      if (ps->ofn->lpstrFile && ps->ofn->lpstrFile[0]) {
        const char *slash = strrchr(ps->ofn->lpstrFile, '/');
        if (slash) {
          char dir[512];
          size_t dlen = (size_t)(slash - ps->ofn->lpstrFile);
          if (dlen >= sizeof(dir)) dlen = sizeof(dir) - 1;
          strncpy(dir, ps->ofn->lpstrFile, dlen);
          dir[dlen] = '\0';
          send_message(ps->list_win, FLM_SETPATH, 0, dir);
        }
      }

      set_window_item_text(win, FP_ID_OK, "%s", ps->save_mode ? "Save" : "Open");

      return true;
    }

    case kToolBarMessageButtonClick:
      if (wparam == FP_ID_TOOL_UP) {
        fp_navigate_to_parent(ps);
        return true;
      }
      if (wparam == FP_ID_TOOL_NEW_FOLDER) {
        fp_create_folder(win, ps);
        return true;
      }
      return false;

    // ------------------------------------------------------------------
    case kWindowMessageCommand: {
      uint16_t code = HIWORD(wparam);

      // Single-click on a file — populate the filename edit box.
      if (code == FLN_SELCHANGE) {
        const fileitem_t *item = (const fileitem_t *)lparam;
        if (item && !item->is_directory && item->path)
          fp_set_edit_from_path(ps, item->path);
        return true;
      }

      // Double-click on a file — populate edit box AND immediately accept
      // (open mode only; in save mode just populate, matching Explorer UX).
      if (code == FLN_FILEOPEN) {
        const fileitem_t *item = (const fileitem_t *)lparam;
        if (item && item->path) {
          fp_set_edit_from_path(ps, item->path);
          if (!ps->save_mode) {
            fp_accept_item(win, ps, item);
          }
        }
        return true;
      }

      // Filter combobox selection changed
      if (code == kComboBoxNotificationSelectionChange && ps->filter_combo &&
          (window_t *)lparam == ps->filter_combo) {
        int sel = (int)send_message(ps->filter_combo,
                                    kComboBoxMessageGetCurrentSelection, 0, NULL);
        if (sel >= 0 && sel < ps->num_filters) {
          ps->active_filter = sel;
          fp_apply_filter(ps);
        }
        return true;
      }

      // Button click
      if (code == kButtonNotificationClicked) {
        window_t *btn = (window_t *)lparam;
        if (!btn) return true;

        if (btn->id == FP_ID_CANCEL) {
          end_dialog(win, 0);
          return true;
        }

        // OK / Open / Save
        char full[600] = {0};
        if (!fp_build_path(ps, full, sizeof(full))) return true;

        strncpy(ps->ofn->lpstrFile, full, ps->ofn->nMaxFile - 1);
        ps->ofn->lpstrFile[ps->ofn->nMaxFile - 1] = '\0';
        ps->accepted = true;
        end_dialog(win, 1);
        return true;
      }

      return false;
    }

    default:
      return false;
  }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

static bool fp_run(openfilename_t *ofn, bool save_mode,
                   const char *title) {
  if (!ofn || !ofn->lpstrFile || ofn->nMaxFile == 0) return false;
  uint32_t flags = WINDOW_DIALOG | WINDOW_NOTRAYBUTTON | WINDOW_TOOLBAR;

  fp_state_t ps = {0};
  ps.save_mode     = save_mode;
  ps.ofn           = ofn;
  ps.num_filters   = fp_parse_filters(ofn->lpstrFilter,
                                      ps.filters, FP_MAX_FILTERS);
  ps.active_filter = (ofn->nFilterIndex >= 1 &&
                      ofn->nFilterIndex <= ps.num_filters)
                     ? ofn->nFilterIndex - 1 : 0;

  uint32_t result = show_dialog_from_form_ex(&kFilePickerForm, title,
      ofn->hwndOwner,
      flags,
      fp_proc, &ps);

  return result != 0 && ps.accepted;
}

bool get_open_filename(openfilename_t *ofn) {
  return fp_run(ofn, false, "Open File");
}

bool get_save_filename(openfilename_t *ofn) {
  return fp_run(ofn, true, "Save File");
}
