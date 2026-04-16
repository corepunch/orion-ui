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

#include "filepicker.h"
#include "filelist.h"
#include "commctl.h"
#include "../user/user.h"
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

// Vertical positions of each row (relative to client-area origin)
#define FP_LIST_Y     FP_PAD
#define FP_FILE_Y     (FP_LIST_Y + FP_LIST_H + FP_ROW_GAP)

// ---------------------------------------------------------------------------
// Maximum number of filter entries parsed from lpstrFilter
// ---------------------------------------------------------------------------
#define FP_MAX_FILTERS 16

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
  strncpy(ps->edit_win->title, base, sizeof(ps->edit_win->title) - 1);
  ps->edit_win->title[sizeof(ps->edit_win->title) - 1] = '\0';
  invalidate_window(ps->edit_win);
}

// Build the full path from the selected filelist item or the edit box + cwd.
// Returns false when the edit box is empty.
static bool fp_build_path(fp_state_t *ps, char *out, size_t out_sz) {
  const char *fname = ps->edit_win->title;
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

      // File browser list
      ps->list_win = create_window("", WINDOW_NOTITLE | WINDOW_VSCROLL,
          MAKERECT(FP_PAD, FP_LIST_Y, FP_LIST_W, FP_LIST_H),
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

      // "File:" label
      create_window("File:", WINDOW_NOTITLE,
          MAKERECT(FP_PAD, FP_FILE_Y, FP_LABEL_W, FP_EDIT_H),
          win, win_label, 0, NULL);

      // Filename text edit
      int edit_x = FP_PAD + FP_LABEL_W + 2;
      int edit_w = FP_WIN_W - edit_x - FP_PAD;
      ps->edit_win = create_window("", WINDOW_NOTITLE,
          MAKERECT(edit_x, FP_FILE_Y, edit_w, FP_EDIT_H),
          win, win_textedit, 0, NULL);

      // Copy pre-fill basename now that edit_win exists
      if (ps->ofn->lpstrFile && ps->ofn->lpstrFile[0]) {
        const char *base = strrchr(ps->ofn->lpstrFile, '/');
        base = base ? base + 1 : ps->ofn->lpstrFile;
        strncpy(ps->edit_win->title, base, sizeof(ps->edit_win->title) - 1);
        ps->edit_win->title[sizeof(ps->edit_win->title) - 1] = '\0';
      }

      // Compute where the button row starts (depends on filter row presence)
      int btn_y = FP_FILE_Y + FP_EDIT_H + FP_ROW_GAP;

      // Filter combobox row (shown when at least one filter is defined)
      if (ps->num_filters > 0) {
        int filter_label_y = btn_y + (FP_COMBO_H - CONTROL_HEIGHT) / 2;
        create_window("Filter:", WINDOW_NOTITLE,
            MAKERECT(FP_PAD, filter_label_y, FP_LABEL_W, CONTROL_HEIGHT),
            win, win_label, 0, NULL);

        int combo_x = FP_PAD + FP_LABEL_W + 2;
        int combo_w = FP_WIN_W - combo_x - FP_PAD;
        ps->filter_combo = create_window("", WINDOW_NOTITLE,
            MAKERECT(combo_x, btn_y, combo_w, FP_COMBO_H),
            win, win_combobox, 0, NULL);

        for (int i = 0; i < ps->num_filters; i++) {
          send_message(ps->filter_combo, kComboBoxMessageAddString,
                       0, (void *)ps->filters[i].description);
        }
        if (ps->active_filter >= 0)
          send_message(ps->filter_combo, kComboBoxMessageSetCurrentSelection,
                       (uint32_t)ps->active_filter, NULL);

        btn_y += FP_COMBO_H + FP_ROW_GAP;
      }

      // OK (Open/Save) and Cancel buttons
      const char *ok_label = ps->save_mode ? "Save" : "Open";
      int ok_x   = FP_WIN_W - (FP_BTN_W + FP_PAD) * 2;
      int cncl_x = FP_WIN_W - (FP_BTN_W + FP_PAD);
      create_window(ok_label, BUTTON_DEFAULT,
          MAKERECT(ok_x,   btn_y, FP_BTN_W, FP_BTN_H), win, win_button, 0, NULL);
      create_window("Cancel", 0,
          MAKERECT(cncl_x, btn_y, FP_BTN_W, FP_BTN_H), win, win_button, 0, NULL);

      return true;
    }

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

        if (strcmp(btn->title, "Cancel") == 0) {
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

// Compute the dialog height depending on whether a filter row is needed.
static int fp_dialog_height(int num_filters) {
  int h = FP_FILE_Y + FP_EDIT_H + FP_ROW_GAP;  // up to and including file row
  if (num_filters > 0)
    h += FP_COMBO_H + FP_ROW_GAP;               // filter row
  h += FP_BTN_H + FP_PAD;                       // button row + bottom padding
  return h;
}

static bool fp_run(openfilename_t *ofn, bool save_mode,
                   const char *title) {
  if (!ofn || !ofn->lpstrFile || ofn->nMaxFile == 0) return false;

  fp_state_t ps = {0};
  ps.save_mode     = save_mode;
  ps.ofn           = ofn;
  ps.num_filters   = fp_parse_filters(ofn->lpstrFilter,
                                      ps.filters, FP_MAX_FILTERS);
  ps.active_filter = (ofn->nFilterIndex >= 1 &&
                      ofn->nFilterIndex <= ps.num_filters)
                     ? ofn->nFilterIndex - 1 : 0;

  int h = fp_dialog_height(ps.num_filters) + TITLEBAR_HEIGHT;
  uint32_t result = show_dialog(title,
      MAKERECT(50, 30, FP_WIN_W, h),
      ofn->hwndOwner, fp_proc, &ps);

  return result != 0 && ps.accepted;
}

bool get_open_filename(openfilename_t *ofn) {
  return fp_run(ofn, false, "Open File");
}

bool get_save_filename(openfilename_t *ofn) {
  return fp_run(ofn, true, "Save File");
}
