#include "gitclient.h"
#include <sys/stat.h>

#define GC_SETTINGS_FILE "gitclient-repositories.txt"

void gc_recent_load(void) {
  gc_state_t *gc = g_gc; if (!gc) return;
  char buf[GC_MAX_RECENT_REPOS * 512]; size_t n = 0;
  if (!axSettingsLoad(GC_SETTINGS_FILE, buf, sizeof(buf) - 1, &n)) return;
  buf[n] = 0; char *line = buf;
  while (*line && gc->recent_repo_count < GC_MAX_RECENT_REPOS) {
    char *nl = strchr(line, '\n'); if (nl) *nl = 0;
    size_t len = strlen(line); while (len && line[len - 1] == '\r') line[--len] = 0;
    if (line[0]) strncpy(gc->recent_repos[gc->recent_repo_count++], line, 511);
    if (!nl) break; line = nl + 1;
  }
}

void gc_recent_save(void) {
  gc_state_t *gc = g_gc; if (!gc || gc->ephemeral) return;
  char buf[GC_MAX_RECENT_REPOS * 513]; size_t used = 0;
  for (int i = 0; i < gc->recent_repo_count; i++) {
    int n = snprintf(buf + used, sizeof(buf) - used, "%s\n", gc->recent_repos[i]);
    if (n <= 0 || (size_t)n >= sizeof(buf) - used) break; used += (size_t)n;
  }
  axSettingsSave(GC_SETTINGS_FILE, buf, used);
}

void gc_recent_add(const char *path) {
  gc_state_t *gc = g_gc; if (!gc || !path || !path[0]) return;
  int found = -1; for (int i = 0; i < gc->recent_repo_count; i++)
    if (!strcmp(gc->recent_repos[i], path)) { found = i; break; }
  if (found < 0) found = gc->recent_repo_count < GC_MAX_RECENT_REPOS ? gc->recent_repo_count++ : GC_MAX_RECENT_REPOS - 1;
  for (int i = found; i > 0; i--) memcpy(gc->recent_repos[i], gc->recent_repos[i - 1], 512);
  strncpy(gc->recent_repos[0], path, 511); gc->recent_repos[0][511] = 0; gc_recent_save();
}

static void gc_ws_trim(char *line) {
  size_t n = strlen(line);
  while (n && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t')) line[--n] = 0;
}

static bool gc_line_is_magic(const char *line) {
  return line && !strcmp(line, GC_WORKSPACE_MAGIC);
}

static void gc_parent_dir(const char *file, char *out, size_t n) {
  snprintf(out, n, "%s", file ? file : ".");
  size_t len = strlen(out);
  while (len > 1 && (out[len - 1] == '/' || out[len - 1] == '\\')) out[--len] = 0;
  char *slash = NULL;
  for (char *p = out; *p; p++) if (*p == '/' || *p == '\\') slash = p;
  if (!slash) { snprintf(out, n, "."); return; }
  if (slash == out) { out[1] = 0; return; }
  *slash = 0;
}

static bool gc_is_abs_path(const char *p) {
  return p && (p[0] == '/' || p[0] == '\\' || (p[0] && p[1] == ':'));
}

static bool gc_workspace_contains(const char *root) {
  gc_state_t *gc = g_gc; if (!gc || !root) return false;
  for (int i = 0; i < gc->workspace_count; i++) if (!strcmp(gc->workspace[i], root)) return true;
  return false;
}

static bool gc_workspace_insert(const char *root, bool remember, bool mark) {
  gc_state_t *gc = g_gc;
  if (!gc || !root || !root[0] || gc_workspace_contains(root)) return false;
  if (gc->workspace_count >= GC_MAX_RECENT_REPOS) {
    fprintf(stderr, "[gc] workspace is full (%d)\n", GC_MAX_RECENT_REPOS); fflush(stderr);
    return false;
  }
  strncpy(gc->workspace[gc->workspace_count], root, 511);
  gc->workspace[gc->workspace_count][511] = 0;
  gc->workspace_count++;
  if (mark) gc->workspace_dirty = true;
  if (remember) gc_recent_add(root);
  gc_update_title();
  return true;
}

bool gc_workspace_unsaved(void) {
  gc_state_t *gc = g_gc; if (!gc) return false;
  if (gc->workspace_file[0]) return gc->workspace_dirty;
  return gc->workspace_count > 1;
}

bool gc_workspace_file_is(const char *path) {
  struct stat st;
  if (!path || !path[0] || stat(path, &st) != 0) return false;
#ifdef S_ISREG
  if (!S_ISREG(st.st_mode)) return false;
#else
  if ((st.st_mode & _S_IFMT) != _S_IFREG) return false;
#endif
  FILE *f = fopen(path, "r"); if (!f) return false;
  char line[256]; bool ok = false;
  while (fgets(line, sizeof(line), f)) {
    gc_ws_trim(line);
    if (!line[0] || line[0] == '#') continue;
    ok = gc_line_is_magic(line);
    break;
  }
  fclose(f);
  return ok;
}

int gc_workspace_read(const char *file, char (*out)[512], int max) {
  if (!file || !out || max <= 0) return -1;
  FILE *f = fopen(file, "r");
  if (!f) return -1;
  char dir[512]; gc_parent_dir(file, dir, sizeof(dir));
  char line[1024]; int count = 0; bool magic = false;
  while (fgets(line, sizeof(line), f)) {
    gc_ws_trim(line);
    if (!line[0] || line[0] == '#') continue;
    if (!magic) {
      if (!gc_line_is_magic(line)) { fclose(f); return -1; }
      magic = true; continue;
    }
    if (count >= max) break;
    char joined[1024];
    if (gc_is_abs_path(line)) snprintf(joined, sizeof(joined), "%s", line);
    else snprintf(joined, sizeof(joined), "%s/%s", dir, line);
    if (!git_path_absolute(joined, out[count], 512)) {
      strncpy(out[count], joined, 511); out[count][511] = 0;
    }
    count++;
  }
  fclose(f);
  return magic ? count : -1;
}

bool gc_workspace_write(const char *file, char (*paths)[512], int count) {
  if (!file || !file[0] || count < 0) return false;
  FILE *f = fopen(file, "w");
  if (!f) {
    fprintf(stderr, "[gc] could not write workspace %s\n", file); fflush(stderr);
    return false;
  }
  bool ok = fprintf(f, "%s\n", GC_WORKSPACE_MAGIC) > 0;
  for (int i = 0; ok && i < count; i++) ok = fprintf(f, "%s\n", paths[i]) > 0;
  if (fclose(f) != 0) ok = false;
  if (!ok) fprintf(stderr, "[gc] workspace write failed: %s\n", file), fflush(stderr);
  return ok;
}

bool gc_workspace_add(const char *path) {
  char checkout[512], main_root[512];
  if (!git_locate(path, checkout, sizeof(checkout), main_root, sizeof(main_root))) return false;
  (void)checkout;
  if (gc_workspace_contains(main_root)) return true;
  return gc_workspace_insert(main_root, true, true);
}

bool gc_workspace_save(const char *file) {
  gc_state_t *gc = g_gc; if (!gc || !file || !file[0]) return false;
  if (!gc_workspace_write(file, gc->workspace, gc->workspace_count)) return false;
  strncpy(gc->workspace_file, file, sizeof(gc->workspace_file) - 1);
  gc->workspace_file[sizeof(gc->workspace_file) - 1] = 0;
  gc->workspace_dirty = false;
  gc_update_title();
  return true;
}

static bool gc_open_first_checkout(void) {
  gc_state_t *gc = g_gc; if (!gc) return false;
  for (int i = 0; i < gc->workspace_count; i++) {
    char checkout[512], main_root[512];
    if (!git_locate(gc->workspace[i], checkout, sizeof(checkout), main_root, sizeof(main_root))) continue;
    gc_open_repo(checkout);
    return gc->repo != NULL;
  }
  return false;
}

bool gc_workspace_open(const char *file, bool confirm) {
  gc_state_t *gc = g_gc; if (!gc || !file || !file[0]) return false;
  char roots[GC_MAX_RECENT_REPOS][512];
  int n = gc_workspace_read(file, roots, GC_MAX_RECENT_REPOS);
  if (n < 0) {
    fprintf(stderr, "[gc] not a workspace file: %s\n", file); fflush(stderr);
    if (gc->main_win) message_box(gc->main_win, "That file is not a gitclient workspace.", "Open Workspace", MB_OK);
    return false;
  }
  if (confirm && gc_workspace_unsaved() && gc->main_win &&
      message_box(gc->main_win, "The current workspace has unsaved repositories. Replace it?",
                  "Open Workspace", MB_YESNO) != IDYES) return false;
  gc->workspace_count = 0;
  gc->workspace_dirty = false;
  for (int i = 0; i < n; i++) {
    char checkout[512], main_root[512];
    if (git_locate(roots[i], checkout, sizeof(checkout), main_root, sizeof(main_root)))
      gc_workspace_insert(main_root, true, false);
    else gc_workspace_insert(roots[i], false, false);
  }
  strncpy(gc->workspace_file, file, sizeof(gc->workspace_file) - 1);
  gc->workspace_file[sizeof(gc->workspace_file) - 1] = 0;
  gc->workspace_dirty = false;
  bool opened = gc_open_first_checkout();
  if (gc->workspace_count != 1 || !opened) gc_set_view_mode(GC_TAB_OVERVIEW);
  gc_update_title();
  return true;
}
