#include "exporter_registry.h"
#include <platform/platform.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exporter discovery: every shared library in the search directories that exports
   scener_get_exporter() and matches the ABI is registered under its id. A library that
   fails to load is reported on stderr and skipped; it never stops the others. */

#define EXPORTER_MAX 32
#ifdef _WIN32
#  define EXPORTER_PATH_SEPARATOR ';'
#else
#  define EXPORTER_PATH_SEPARATOR ':'
#endif

typedef struct { void *lib; const scener_exporter_t *exporter; char path[1024]; } exporter_entry_t;

static exporter_entry_t g_exporters[EXPORTER_MAX];
static int g_nexporters;
static bool g_exporters_loaded;

static bool exporter_has_extension(const char *name, const char *ext) {
  size_t n = strlen(name), e = strlen(ext);
  if (n < e) return false;
  for (size_t i = 0; i < e; i++) if (tolower((unsigned char)name[n - e + i]) != ext[i]) return false;
  return true;
}

static bool exporter_valid(const char *path, const scener_exporter_t *x) {
  if (!x->id || !x->id[0] || !x->short_desc || !x->extensions || !x->extensions[0] || !x->export_scene) {
    fprintf(stderr, "exporter: %s describes no id, extension or export function\n", path);
    return false;
  }
  if (x->abi != SCENER_EXPORTER_ABI) {
    fprintf(stderr, "exporter: %s (%s) was built for ABI %u, this scener uses %u\n", path, x->id, (unsigned)x->abi, SCENER_EXPORTER_ABI);
    return false;
  }
  for (int i = 0; i < g_nexporters; i++) if (!strcmp(g_exporters[i].exporter->id, x->id)) {
    fprintf(stderr, "exporter: %s repeats id \"%s\" already provided by %s\n", path, x->id, g_exporters[i].path);
    return false;
  }
  return true;
}

static void exporter_load_file(const char *path) {
  if (g_nexporters >= EXPORTER_MAX) { fprintf(stderr, "exporter: limit of %d reached, ignoring %s\n", EXPORTER_MAX, path); return; }
  void *lib = axDynlibOpen(path);
  if (!lib) { const char *why = axDynlibError(); fprintf(stderr, "exporter: cannot load %s: %s\n", path, why ? why : "unknown error"); return; }
  scener_get_exporter_fn get = (scener_get_exporter_fn)axDynlibSym(lib, "scener_get_exporter");
  const scener_exporter_t *x = get ? get() : NULL;
  if (!x) { fprintf(stderr, "exporter: %s exports no scener_get_exporter\n", path); axDynlibClose(lib); return; }
  if (!exporter_valid(path, x)) { axDynlibClose(lib); return; }
  exporter_entry_t *e = &g_exporters[g_nexporters++];
  e->lib = lib; e->exporter = x; snprintf(e->path, sizeof(e->path), "%s", path);
}

typedef struct { const char *dir; } exporter_scan_t;

static bool_t exporter_scan_entry(const AXdirent *entry, void *userdata) {
  const exporter_scan_t *scan = userdata;
  if (entry->is_directory || entry->is_hidden || !exporter_has_extension(entry->name, AX_DYNLIB_EXT)) return true;
  char path[1024];
  snprintf(path, sizeof(path), "%s/%s", scan->dir, entry->name);
  exporter_load_file(path);
  return true;
}

static void exporter_scan_dir(const char *dir) {
  if (!dir || !dir[0] || !axPathExists(dir)) return;
  exporter_scan_t scan = { dir };
  axListDir(dir, exporter_scan_entry, &scan);
}

/* Search order: SCENER_EXPORTER_PATH, the installed library directory, then the user's settings. */
void exporter_registry_load(void) {
  if (g_exporters_loaded) return;
  g_exporters_loaded = true;
  const char *env = getenv("SCENER_EXPORTER_PATH");
  while (env && *env) {
    const char *end = strchr(env, EXPORTER_PATH_SEPARATOR);
    size_t n = end ? (size_t)(end - env) : strlen(env);
    char dir[1024];
    if (n && n < sizeof(dir)) { memcpy(dir, env, n); dir[n] = 0; exporter_scan_dir(dir); }
    env = end ? end + 1 : NULL;
  }
  char dir[1024];
  const char *lib = axLibDirectory(), *settings = axSettingsDirectory();
  if (lib && snprintf(dir, sizeof(dir), "%s/exporters", lib) < (int)sizeof(dir)) exporter_scan_dir(dir);
  if (settings && snprintf(dir, sizeof(dir), "%s/exporters", settings) < (int)sizeof(dir)) exporter_scan_dir(dir);
}

int exporter_registry_count(void) { return g_nexporters; }
const scener_exporter_t *exporter_registry_at(int i, const char **path) {
  if (i < 0 || i >= g_nexporters) return NULL;
  if (path) *path = g_exporters[i].path;
  return g_exporters[i].exporter;
}

const scener_exporter_t *exporter_registry_find(const char *id) {
  for (int i = 0; i < g_nexporters; i++) if (!strcmp(g_exporters[i].exporter->id, id)) return g_exporters[i].exporter;
  return NULL;
}

const scener_exporter_t *exporter_registry_for_file(const char *file) {
  for (int i = 0; i < g_nexporters; i++)
    for (const char *const *ext = g_exporters[i].exporter->extensions; *ext; ext++)
      if (exporter_has_extension(file, *ext)) return g_exporters[i].exporter;
  return NULL;
}
