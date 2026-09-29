#include "gitclient.h"

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
