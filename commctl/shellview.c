// Shell-view control — a directory-browsing window that wraps win_columnview.
//
// win_shellview extends win_columnview: it delegates rendering and item
// management to win_columnview (via userdata2) and manages its own state in
// userdata (path, filter, selection, double-click tracking).
//
// Click handling is implemented here rather than delegated to win_columnview
// so that directory navigation can be performed before any CVN_* notification
// reaches the root window.  win_shellview emits SVN_* notifications instead.

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "shellview.h"
#include "columnview.h"
#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"

// ---------------------------------------------------------------------------
// Layout constants (mirror columnview.c; defined in columnview.h)
// ---------------------------------------------------------------------------
#define SV_ENTRY_HEIGHT  COLUMNVIEW_ENTRY_HEIGHT
#define SV_WIN_PADDING   COLUMNVIEW_WIN_PADDING

// ---------------------------------------------------------------------------
// Icons and colours (match filepicker / filemanager conventions)
// ---------------------------------------------------------------------------
#define SV_ICON_FOLDER  icon8_collapse   // 1
#define SV_ICON_FILE    icon8_checkbox   // 4
#define SV_COLOR_FOLDER 0xffa0d000u

// ---------------------------------------------------------------------------
// Private state
// ---------------------------------------------------------------------------
typedef struct {
  char               path[512];
  char               selected[256];   // base-name of selected entry, or ""
  shellview_filter_t filter;          // NULL → show all non-hidden entries
  uint32_t           last_click_ms;   // SDL timestamp of last click
  uint32_t           last_click_idx;  // item index of last click
} shellview_data_t;

// ---------------------------------------------------------------------------
// Temporary sort buffer entry
// ---------------------------------------------------------------------------
typedef struct {
  char name[256];
  bool is_dir;
} sv_entry_t;

static int sv_compare(const void *a, const void *b) {
  const sv_entry_t *ea = (const sv_entry_t *)a;
  const sv_entry_t *eb = (const sv_entry_t *)b;
  // Directories come before files; within a group sort case-insensitively.
  if (ea->is_dir != eb->is_dir)
    return eb->is_dir - ea->is_dir; // true(1) – true(1) = 0; dirs first
  return strcasecmp(ea->name, eb->name);
}

// ---------------------------------------------------------------------------
// Directory loader
// ---------------------------------------------------------------------------
static void sv_load_directory(window_t *win, shellview_data_t *data) {
  send_message(win, CVM_CLEAR, 0, NULL);
  win->scroll[0] = 0;
  win->scroll[1] = 0;

  // The ".." entry is always shown first, regardless of the filter.
  send_message(win, CVM_ADDITEM, 0,
    &(columnview_item_t){"..", SV_ICON_FOLDER, SV_COLOR_FOLDER, 1});

  DIR *dir = opendir(data->path);
  if (!dir) return;

  // Collect, optionally filter, then sort.
  sv_entry_t *entries = NULL;
  int count = 0, cap = 0;

  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL) {
    // Skip hidden entries (including "." and "..").
    if (ent->d_name[0] == '.') continue;

    char full[768];
    snprintf(full, sizeof(full), "%s/%s", data->path, ent->d_name);
    struct stat st;
    if (stat(full, &st) != 0) continue;

    bool is_dir = S_ISDIR(st.st_mode);

    // Apply caller-supplied filter (if any).
    if (data->filter && !data->filter(ent->d_name, is_dir)) continue;

    // Grow buffer if needed.
    if (count >= cap) {
      int new_cap = cap ? cap * 2 : 32;
      sv_entry_t *tmp = realloc(entries, (size_t)new_cap * sizeof(sv_entry_t));
      if (!tmp) { free(entries); closedir(dir); return; }
      entries = tmp;
      cap = new_cap;
    }

    strncpy(entries[count].name, ent->d_name, sizeof(entries[count].name) - 1);
    entries[count].name[sizeof(entries[count].name) - 1] = '\0';
    entries[count].is_dir = is_dir;
    count++;
  }
  closedir(dir);

  if (count > 0) {
    qsort(entries, (size_t)count, sizeof(sv_entry_t), sv_compare);
    for (int i = 0; i < count; i++) {
      send_message(win, CVM_ADDITEM, 0,
        &(columnview_item_t){
          entries[i].name,
          entries[i].is_dir ? SV_ICON_FOLDER : SV_ICON_FILE,
          entries[i].is_dir ? SV_COLOR_FOLDER : (uint32_t)COLOR_TEXT_NORMAL,
          (uint32_t)entries[i].is_dir,
        });
    }
    free(entries);
  }
}

// ---------------------------------------------------------------------------
// Navigation helpers
// ---------------------------------------------------------------------------

// Navigate into a subdirectory or up via "..".
static void sv_navigate(window_t *win, shellview_data_t *data, const char *name) {
  if (strcmp(name, "..") == 0) {
    char *slash = strrchr(data->path, '/');
    if (slash && slash != data->path) {
      *slash = '\0';
    } else {
      data->path[0] = '/';
      data->path[1] = '\0';
    }
  } else {
    // Append subdirectory name.
    size_t plen = strlen(data->path);
    if (plen + 1 + strlen(name) + 1 < sizeof(data->path)) {
      data->path[plen] = '/';
      strncpy(data->path + plen + 1, name, sizeof(data->path) - plen - 2);
      data->path[sizeof(data->path) - 1] = '\0';
    }
  }

  data->selected[0] = '\0';
  sv_load_directory(win, data);

  // Notify root of the path change.
  window_t *root = get_root_window(win);
  send_message(root, kWindowMessageCommand, MAKEDWORD(0, SVN_PATHCHANGE), data->path);
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------
result_t win_shellview(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  shellview_data_t *data = (shellview_data_t *)win->userdata;

  switch (msg) {

    // -----------------------------------------------------------------------
    case kWindowMessageCreate: {
      // Initialise columnview rendering infrastructure first.
      win_columnview(win, msg, wparam, NULL);

      // Allocate and initialise shellview state.
      data = malloc(sizeof(shellview_data_t));
      if (!data) return false;
      memset(data, 0, sizeof(*data));
      win->userdata = data;

      // Initial path: use lparam if provided, otherwise cwd.
      const char *init_path = (const char *)lparam;
      if (init_path && init_path[0])
        strncpy(data->path, init_path, sizeof(data->path) - 1);
      else if (!getcwd(data->path, sizeof(data->path)))
        strncpy(data->path, "/", sizeof(data->path) - 1);

      data->last_click_idx = (uint32_t)-1;

      sv_load_directory(win, data);
      return true;
    }

    // -----------------------------------------------------------------------
    // Delegate rendering to win_columnview.
    case kWindowMessagePaint:
      return win_columnview(win, msg, wparam, lparam);

    // -----------------------------------------------------------------------
    // Delegate scroll-wheel to win_columnview.
    case kWindowMessageWheel:
      return win_columnview(win, msg, wparam, lparam);

    // -----------------------------------------------------------------------
    // Own click handling — not delegated to win_columnview so that directory
    // navigation can happen before any CVN_* notification reaches the root.
    case kWindowMessageLeftButtonDown: {
      int mx = (int)(int16_t)LOWORD(wparam);
      int my = (int)(int16_t)HIWORD(wparam);

      // Determine which item was clicked using the same geometry as columnview.
      int col_w  = (int)(uint32_t)send_message(win, CVM_GETCOLUMNWIDTH, 0, NULL);
      int ncol   = col_w > 0 ? (win->frame.w / col_w) : 1;
      if (ncol < 1) ncol = 1;
      int col    = mx / col_w;
      int row    = (my - SV_WIN_PADDING + (int)win->scroll[1]) / SV_ENTRY_HEIGHT;
      uint32_t index = (uint32_t)(row * ncol + col);

      uint32_t item_count = (uint32_t)send_message(win, CVM_GETITEMCOUNT, 0, NULL);
      if (index >= item_count) return true;

      uint32_t now = SDL_GetTicks();
      bool is_dbl  = (data->last_click_idx == index &&
                      (now - data->last_click_ms) < 500u);

      if (is_dbl) {
        // Double-click: reset tracking first.
        data->last_click_ms  = 0;
        data->last_click_idx = (uint32_t)-1;

        columnview_item_t item;
        if (!send_message(win, CVM_GETITEMDATA, index, &item)) return true;

        if (item.userdata) {
          // Directory: navigate, send SVN_PATHCHANGE.
          sv_navigate(win, data, item.text);
        } else {
          // File: send SVN_ITEMACTIVATE with the base filename.
          strncpy(data->selected, item.text, sizeof(data->selected) - 1);
          data->selected[sizeof(data->selected) - 1] = '\0';
          send_message(get_root_window(win), kWindowMessageCommand,
                       MAKEDWORD(index, SVN_ITEMACTIVATE), data->selected);
        }
      } else {
        // Single click: update selection.
        uint32_t old_sel = (uint32_t)send_message(win, CVM_GETSELECTION, 0, NULL);
        send_message(win, CVM_SETSELECTION, index, NULL);
        data->last_click_ms  = now;
        data->last_click_idx = index;

        if (old_sel != index) {
          columnview_item_t item;
          if (send_message(win, CVM_GETITEMDATA, index, &item)) {
            if (!item.userdata) {
              // File selected: copy filename and notify.
              strncpy(data->selected, item.text, sizeof(data->selected) - 1);
              data->selected[sizeof(data->selected) - 1] = '\0';
              send_message(get_root_window(win), kWindowMessageCommand,
                           MAKEDWORD(index, SVN_SELCHANGE), data->selected);
            } else {
              // Directory selected: clear filename, notify with NULL.
              data->selected[0] = '\0';
              send_message(get_root_window(win), kWindowMessageCommand,
                           MAKEDWORD(index, SVN_SELCHANGE), NULL);
            }
          }
        }
      }
      return true;
    }

    // -----------------------------------------------------------------------
    case SVM_NAVIGATE: {
      const char *path = (const char *)lparam;
      if (path && path[0]) {
        strncpy(data->path, path, sizeof(data->path) - 1);
        data->path[sizeof(data->path) - 1] = '\0';
        data->selected[0] = '\0';
        sv_load_directory(win, data);
      }
      return true;
    }

    case SVM_GETPATH: {
      char *buf = (char *)lparam;
      size_t sz = (size_t)wparam;
      if (buf && sz > 0) {
        strncpy(buf, data->path, sz - 1);
        buf[sz - 1] = '\0';
      }
      return true;
    }

    case SVM_SETFILTER: {
      data->filter = (shellview_filter_t)lparam;
      data->selected[0] = '\0';
      sv_load_directory(win, data);
      return true;
    }

    case SVM_REFRESH:
      data->selected[0] = '\0';
      sv_load_directory(win, data);
      return true;

    case SVM_GETSELECTION: {
      char *buf = (char *)lparam;
      size_t sz = (size_t)wparam;
      if (buf && sz > 0) {
        strncpy(buf, data->selected, sz - 1);
        buf[sz - 1] = '\0';
      }
      return true;
    }

    // -----------------------------------------------------------------------
    case kWindowMessageDestroy:
      free(data);
      win->userdata = NULL;
      return win_columnview(win, msg, wparam, lparam);

    // -----------------------------------------------------------------------
    default:
      return win_columnview(win, msg, wparam, lparam);
  }
}
