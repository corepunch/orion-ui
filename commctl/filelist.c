// commctl/filelist.c — file-browser control
//
// win_filelist extends win_columnview: it calls win_columnview for rendering
// (kWindowMessagePaint), scroll (kWindowMessageWheel), item management
// (CVM_*), and cleanup (kWindowMessageDestroy), while implementing its own
// directory loading, click/double-click handling, navigation, and extension
// filtering.
//
// Click events are handled here rather than delegated to win_columnview so
// that FLN_* notifications are emitted directly, avoiding the CVN_* routing
// that win_columnview uses (which sends to get_root_window and would bypass
// win_filelist when it is used as a child control).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "filelist.h"
#include "columnview.h"
#include "../user/user.h"
#include "../user/messages.h"
#include "../user/draw.h"

// ---------------------------------------------------------------------------
// Layout constants — mirror columnview.c (exported via columnview.h)
// ---------------------------------------------------------------------------
#define FL_ENTRY_HEIGHT  COLUMNVIEW_ENTRY_HEIGHT
#define FL_WIN_PADDING   COLUMNVIEW_WIN_PADDING

// ---------------------------------------------------------------------------
// Icons and colours — match the values used by the original filemanager so
// that the appearance is identical.  draw_icon8 renders icon N as character
// (N + 128 + 6*16) in the bitmap font, so these are just font glyph indices.
// ---------------------------------------------------------------------------
#define FL_ICON_UP      7   // ".." parent-directory entry
#define FL_ICON_FOLDER  5   // directory
#define FL_ICON_FILE    6   // regular file
#define FL_COLOR_FOLDER 0xffa0d000u
#define FL_COLOR_GEM    0xff50d050u  // bright green — executable .gem plugin

// ---------------------------------------------------------------------------
// Private state
// ---------------------------------------------------------------------------
typedef struct {
  char       curpath[512];
  char       filter[64];        // normalised extension, e.g. ".png", or ""
  fileitem_t *items;            // heap-allocated; each items[i].path is owned
  int        count;
  int        cap;
  int        selected;          // index into items[], -1 when nothing selected
  fileitem_t notify_item;       // shallow copy used as lparam during send_message
} filelist_data_t;

// ---------------------------------------------------------------------------
// Sort buffer entry
// ---------------------------------------------------------------------------
typedef struct {
  char   path[768];
  bool   is_dir;
  bool   is_hidden;
  size_t size;
  time_t modified;
} fl_sort_entry_t;

static int fl_sort_compare(const void *a, const void *b) {
  const fl_sort_entry_t *ea = (const fl_sort_entry_t *)a;
  const fl_sort_entry_t *eb = (const fl_sort_entry_t *)b;
  if (ea->is_dir != eb->is_dir)
    return (int)eb->is_dir - (int)ea->is_dir; // directories first
  const char *na = strrchr(ea->path, '/'); na = na ? na + 1 : ea->path;
  const char *nb = strrchr(eb->path, '/'); nb = nb ? nb + 1 : eb->path;
  return strcasecmp(na, nb);
}

// ---------------------------------------------------------------------------
// Extension filter helpers
// ---------------------------------------------------------------------------

// Store a normalised extension (leading '.', lower-case) in data->filter.
// Accepts ".png", "png", "*.png", or NULL/"" to clear.
static void fl_set_filter(filelist_data_t *data, const char *ext) {
  if (!ext || !ext[0]) { data->filter[0] = '\0'; return; }
  if (ext[0] == '*') ext++;          // strip leading '*'
  if (ext[0] == '.') {
    strncpy(data->filter, ext, sizeof(data->filter) - 1);
  } else {
    data->filter[0] = '.';
    strncpy(data->filter + 1, ext, sizeof(data->filter) - 2);
  }
  data->filter[sizeof(data->filter) - 1] = '\0';
}

// Return true if the filename matches the stored filter (case-insensitive).
// Always returns true when no filter is set.
static bool fl_matches_filter(const filelist_data_t *data, const char *name) {
  if (!data->filter[0]) return true;
  size_t nlen = strlen(name);
  size_t flen = strlen(data->filter);
  if (nlen < flen) return false;
  return strcasecmp(name + nlen - flen, data->filter) == 0;
}

// ---------------------------------------------------------------------------
// ".." sentinel helper
// ---------------------------------------------------------------------------
// Items[0] is always the ".." parent-directory entry, stored with the literal
// path string ".." so that fl_basename() naturally returns ".." for display.
// Use this predicate wherever the sentinel must be identified by path string.
static inline bool fl_is_parent_sentinel(const char *path) {
  return path[0] == '.' && path[1] == '.' && path[2] == '\0';
}

// ---------------------------------------------------------------------------
// Item list management
// ---------------------------------------------------------------------------

static void fl_free_items(filelist_data_t *data) {
  for (int i = 0; i < data->count; i++) free(data->items[i].path);
  free(data->items);
  data->items    = NULL;
  data->count    = 0;
  data->cap      = 0;
  data->selected = -1;
}

// Append one item; takes ownership of a heap-allocated path string.
static bool fl_push_item(filelist_data_t *data,
                          char *path_heap,   // ownership transferred
                          bool is_dir, bool is_hidden,
                          size_t size, time_t modified) {
  if (data->count >= data->cap) {
    int new_cap = data->cap ? data->cap * 2 : 32;
    fileitem_t *tmp = realloc(data->items,
                              (size_t)new_cap * sizeof(fileitem_t));
    if (!tmp) { free(path_heap); return false; }
    data->items = tmp;
    data->cap   = new_cap;
  }
  fileitem_t *it = &data->items[data->count++];
  it->path         = path_heap;
  it->is_directory = is_dir;
  it->size         = size;
  it->modified     = modified;
  bool is_parent   = fl_is_parent_sentinel(path_heap);
  it->icon  = is_parent ? FL_ICON_UP : (is_dir ? FL_ICON_FOLDER : FL_ICON_FILE);
  const char *ext = strrchr(path_heap, '.');
  bool is_gem = !is_dir && ext && strcmp(ext, ".gem") == 0;
  it->color = is_hidden  ? (uint32_t)COLOR_TEXT_DISABLED
            : is_dir     ? FL_COLOR_FOLDER
            : is_gem     ? FL_COLOR_GEM
                         : (uint32_t)COLOR_TEXT_NORMAL;
  return true;
}

// Rebuild both the private item list and the columnview from disk.
static void fl_load_directory(window_t *win, filelist_data_t *data) {
  fl_free_items(data);
  send_message(win, CVM_CLEAR, 0, NULL);
  win->scroll[0] = 0;
  win->scroll[1] = 0;

  // ".." is always the first entry.  Store the sentinel string ".." as its
  // path; fl_navigate handles the actual parent-path computation when the user
  // activates it.  This means the display code (strrchr + basename) naturally
  // produces ".." since there is no '/' in the sentinel string.
  char *parent_path = strdup("..");
  if (parent_path) fl_push_item(data, parent_path, true, false, 0, 0);

  DIR *dir = opendir(data->curpath);
  if (dir) {
    // Collect entries that pass the optional extension filter.
    fl_sort_entry_t *entries = NULL;
    int  count = 0, cap = 0;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
      // Skip only the self-reference "." and parent-reference ".."; all other
      // entries (including hidden files/directories that start with '.') are
      // shown — mirroring the original filemanager behaviour.
      if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
        continue;

      char full[768];
      snprintf(full, sizeof(full), "%s/%s", data->curpath, ent->d_name);
      struct stat st;
      if (stat(full, &st) != 0) continue;

      bool is_dir    = S_ISDIR(st.st_mode);
      bool is_hidden = (ent->d_name[0] == '.');

      // Apply caller-supplied extension filter (directories always pass).
      if (!is_dir && !fl_matches_filter(data, ent->d_name)) continue;

      if (count >= cap) {
        int nc = cap ? cap * 2 : 32;
        fl_sort_entry_t *tmp = realloc(entries,
                                        (size_t)nc * sizeof(fl_sort_entry_t));
        if (!tmp) {
          // Allocation failed; keep existing entries and stop collecting more.
          break;
        }
        entries = tmp;
        cap = nc;
      }
      strncpy(entries[count].path, full, sizeof(entries[count].path) - 1);
      entries[count].path[sizeof(entries[count].path) - 1] = '\0';
      entries[count].is_dir   = is_dir;
      entries[count].is_hidden = is_hidden;
      entries[count].size     = is_dir ? 0 : (size_t)st.st_size;
      entries[count].modified = st.st_mtime;
      count++;
    }
    closedir(dir);

    if (count > 0) {
      qsort(entries, (size_t)count, sizeof(fl_sort_entry_t), fl_sort_compare);
      for (int i = 0; i < count; i++) {
        char *path = strdup(entries[i].path);
        if (!path) continue; // skip on allocation failure
        fl_push_item(data, path,
                     entries[i].is_dir,
                     entries[i].is_hidden,
                     entries[i].size,
                     entries[i].modified);
      }
      free(entries);
    }
  }

  // Populate columnview — display the basename of each item.
  // For the ".." sentinel the basename IS ".." (no '/' in the string).
  for (int i = 0; i < data->count; i++) {
    const char *base = strrchr(data->items[i].path, '/');
    base = base ? base + 1 : data->items[i].path;
    send_message(win, CVM_ADDITEM, 0,
      &(columnview_item_t){
        .text     = base,
        .icon     = data->items[i].icon,
        .color    = data->items[i].color,
        .userdata = data->items[i].is_directory ? 1u : 0u,
      });
  }
}

// ---------------------------------------------------------------------------
// Navigate: change curpath and reload.
// ---------------------------------------------------------------------------
static void fl_navigate(window_t *win, filelist_data_t *data, int index) {
  if (index < 0 || index >= data->count) return;

  if (fl_is_parent_sentinel(data->items[index].path)) {
    // Navigate to the parent of the current directory.
    char *slash = strrchr(data->curpath, '/');
    if (slash && slash != data->curpath) {
      *slash = '\0';                          // e.g. "/home/user" → "/home"
    } else {
      data->curpath[0] = '/';                 // e.g. "/foo" or "/" → "/"
      data->curpath[1] = '\0';
    }
  } else {
    strncpy(data->curpath, data->items[index].path,
            sizeof(data->curpath) - 1);
    data->curpath[sizeof(data->curpath) - 1] = '\0';
  }

  fl_load_directory(win, data);
  send_message(get_root_window(win), kWindowMessageCommand,
               MAKEDWORD(0, FLN_NAVDIR), data->curpath);
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

// Convert packed wparam coordinates to a filelist item index.
// Returns -1 when the position is outside the item grid.
static int fl_hit_index(window_t *win, filelist_data_t *data, uint32_t wparam) {
  int mx = (int)(int16_t)LOWORD(wparam);
  int my = (int)(int16_t)HIWORD(wparam);
  // Coordinates from handle_mouse are parent-content-relative; subtract the
  // parent's screen position to get child-local coords.
  if (win->parent) {
    mx -= (int)win->parent->frame.x;
    my -= (int)win->parent->frame.y;
  }
  int col_w = (int)(uint32_t)send_message(win, CVM_GETCOLUMNWIDTH, 0, NULL);
  int ncol  = (col_w > 0 && win->frame.w > 0)
                ? (win->frame.w / col_w) : 1;
  if (ncol < 1) ncol = 1;
  int col   = mx / col_w;
  int row   = (my - FL_WIN_PADDING) / FL_ENTRY_HEIGHT;
  int index = row * ncol + col;
  return (index >= 0 && index < data->count) ? index : -1;
}

result_t win_filelist(window_t *win, uint32_t msg,
                      uint32_t wparam, void *lparam) {
  filelist_data_t *data = (filelist_data_t *)win->userdata;

  switch (msg) {

    // -----------------------------------------------------------------------
    case kWindowMessageCreate: {
      // Initialise columnview rendering infrastructure.
      win_columnview(win, msg, wparam, NULL);

      data = malloc(sizeof(filelist_data_t));
      if (!data) return false;
      memset(data, 0, sizeof(*data));
      win->userdata  = data;
      data->selected = -1;

      // Initial path: lparam if provided, else cwd.
      const char *init = (const char *)lparam;
      if (init && init[0])
        strncpy(data->curpath, init, sizeof(data->curpath) - 1);
      else if (!getcwd(data->curpath, sizeof(data->curpath)))
        strncpy(data->curpath, "/", sizeof(data->curpath) - 1);

      fl_load_directory(win, data);
      return true;
    }

    // -----------------------------------------------------------------------
    // Delegate rendering and scrolling to win_columnview.
    case kWindowMessagePaint:
    case kWindowMessageWheel:
      return win_columnview(win, msg, wparam, lparam);

    // -----------------------------------------------------------------------
    // Own click handling — NOT delegated to win_columnview to avoid the CVN_*
    // routing (which sends to root, bypassing win_filelist when it is a child).
    case kWindowMessageLeftButtonDown: {
      int index = fl_hit_index(win, data, wparam);
      if (index < 0) return true;

      // Single click — update selection only.
      // Double-click navigation/open is handled exclusively in
      // kWindowMessageLeftButtonDoubleClick, which the platform delivers
      // as a separate event after the second button-down.  Doing the action
      // here as well would cause double navigation (1 double-click = 2 levels deep).
      int old_sel = data->selected;
      data->selected = index;
      send_message(win, CVM_SETSELECTION, (uint32_t)index, NULL);

      if (old_sel != index) {
        data->notify_item = data->items[index];
        send_message(get_root_window(win), kWindowMessageCommand,
                     MAKEDWORD((uint32_t)index, FLN_SELCHANGE),
                     &data->notify_item);
      }
      return true;
    }

    // -----------------------------------------------------------------------
    // Platform double-click (kEventLeftDoubleClick) arrives here directly on
    // macOS, X11, Wayland, Windows, and QNX.  This is the sole handler for
    // directory navigation and file activation — the kWindowMessageLeftButtonDown
    // handler intentionally does NOT duplicate this logic, preventing the
    // double navigation bug where two kWindowMessageLeftButtonDown events arrive
    // before kWindowMessageLeftButtonDoubleClick.
    case kWindowMessageLeftButtonDoubleClick: {
      int index = fl_hit_index(win, data, wparam);
      if (index < 0) return true;

      if (data->items[index].is_directory) {
        fl_navigate(win, data, index);
      } else {
        data->notify_item = data->items[index];
        send_message(get_root_window(win), kWindowMessageCommand,
                     MAKEDWORD((uint32_t)index, FLN_FILEOPEN),
                     &data->notify_item);
      }
      return true;
    }

    // -----------------------------------------------------------------------
    case FLM_SETPATH: {
      const char *path = (const char *)lparam;
      if (path && path[0]) {
        strncpy(data->curpath, path, sizeof(data->curpath) - 1);
        data->curpath[sizeof(data->curpath) - 1] = '\0';
        fl_load_directory(win, data);
      }
      return true;
    }

    case FLM_GETPATH: {
      char   *buf = (char *)lparam;
      size_t  sz  = (size_t)wparam;
      if (buf && sz > 0) {
        strncpy(buf, data->curpath, sz - 1);
        buf[sz - 1] = '\0';
      }
      return true;
    }

    case FLM_REFRESH:
      fl_load_directory(win, data);
      return true;

    case FLM_GETSELECTEDPATH: {
      char   *buf = (char *)lparam;
      size_t  sz  = (size_t)wparam;
      if (buf && sz > 0) {
        if (data->selected >= 0 && data->selected < data->count &&
            !data->items[data->selected].is_directory) {
          strncpy(buf, data->items[data->selected].path, sz - 1);
          buf[sz - 1] = '\0';
        } else {
          buf[0] = '\0';
        }
      }
      return true;
    }

    case FLM_SETFILTER: {
      fl_set_filter(data, (const char *)lparam);
      fl_load_directory(win, data);
      return true;
    }

    // -----------------------------------------------------------------------
    case kWindowMessageDestroy:
      if (data) {
        fl_free_items(data);
        free(data);
        win->userdata = NULL;
      }
      return win_columnview(win, msg, wparam, lparam);

    // -----------------------------------------------------------------------
    default:
      return win_columnview(win, msg, wparam, lparam);
  }
}
