#include "gem_loader.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Portable strdup — avoids the unreliable _GNU_SOURCE feature-test approach.
static char *shell_strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (!copy) return NULL;
    memcpy(copy, s, len);
    return copy;
}

// Internal record for a single loaded .gem.
typedef struct loaded_gem {
    char            *path;
    void            *handle;
    gem_interface_t *iface;
    window_t        *main_window;   // window created first by init(), used
                                    // to detect when the gem should unload.
    struct loaded_gem *next;
} loaded_gem_t;

static loaded_gem_t *gems_head = NULL;
static unsigned g_gem_generation = 0;  // incremented on every load/unload

// Returns the current generation counter so callers can detect changes.
unsigned shell_gem_generation(void) { return g_gem_generation; }

// ---------------------------------------------------------------------------
bool shell_load_gem(const char *gem_path, int argc, char *argv[]) {
    void *handle = dlopen(gem_path, RTLD_LAZY | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "shell: failed to load '%s': %s\n",
                gem_path, dlerror());
        return false;
    }

    typedef gem_interface_t *(*get_iface_fn)(void);
    get_iface_fn get_iface = (get_iface_fn)dlsym(handle, "gem_get_interface");
    if (!get_iface) {
        fprintf(stderr, "shell: '%s' is not a valid .gem (missing gem_get_interface)\n",
                gem_path);
        dlclose(handle);
        return false;
    }

    gem_interface_t *iface = get_iface();
    if (!iface || !iface->name || !iface->version || !iface->init) {
        fprintf(stderr, "shell: '%s' returned an incomplete gem_interface_t\n",
                gem_path);
        dlclose(handle);
        return false;
    }

    printf("shell: loading %s v%s\n", iface->name, iface->version);

    // Snapshot the tail of the window list so we can identify the first window
    // the gem creates.  create_window() appends to the tail, so we need to
    // find the node just after the current tail after init() returns.
    extern window_t *windows;
    window_t *tail_before = NULL;
    for (window_t *w = windows; w; w = w->next)
        if (!w->next) { tail_before = w; break; }

    if (!iface->init(argc, argv)) {
        fprintf(stderr, "shell: %s init() failed\n", iface->name);
        dlclose(handle);
        return false;
    }

    // The first window appended by init() is now tail_before->next (or
    // windows itself if the list was empty before).
    window_t *main_win = tail_before ? tail_before->next : windows;

    loaded_gem_t *lg = calloc(1, sizeof(loaded_gem_t));
    if (!lg) {
        if (iface->shutdown) iface->shutdown();
        dlclose(handle);
        return false;
    }

    lg->path        = shell_strdup(gem_path);
    lg->handle      = handle;
    lg->iface       = iface;
    lg->main_window = main_win;
    // Append to tail so gems are tracked in load order.
    if (!gems_head) {
        gems_head = lg;
    } else {
        loaded_gem_t *tail = gems_head;
        while (tail->next) tail = tail->next;
        tail->next = lg;
    }
    g_gem_generation++;

    return true;
}

// ---------------------------------------------------------------------------
void shell_unload_gem(window_t *window) {
    loaded_gem_t **pp = &gems_head;
    while (*pp) {
        loaded_gem_t *lg = *pp;
        if (lg->main_window == window) {
            if (lg->iface->shutdown)
                lg->iface->shutdown();
            dlclose(lg->handle);
            *pp = lg->next;
            free(lg->path);
            free(lg);
            g_gem_generation++;
            return;
        }
        pp = &lg->next;
    }
}

// ---------------------------------------------------------------------------
const char *shell_get_gem_for_extension(const char *extension) {
    for (loaded_gem_t *lg = gems_head; lg; lg = lg->next) {
        if (!lg->iface->file_types) continue;
        for (const char **ft = lg->iface->file_types; *ft; ft++) {
            if (strcmp(extension, *ft) == 0)
                return lg->path;
        }
    }
    return NULL;
}

// Automatic unload-on-main-window-close is unsafe: a gem may create multiple
// top-level windows.  Unloading when only the tracked window is closed would
// dlclose() code that remaining windows may still call into.  Gem cleanup is
// deferred to shell_notify_gem_shutdown() + shell_cleanup_all_gems() at shell
// exit, which guarantees all windows are gone before dlclose().
// Returns the number of gems that were unloaded.
int shell_check_closed_gems(void) {
    return 0;
}

// ---------------------------------------------------------------------------
// Build a combined menu array for the shell's menu bar.
// prefix_count entries from prefix come first, then each loaded gem's menus
// are appended in load order.  The returned array is heap-allocated; caller
// must free() it.  Returns the total entry count.
int shell_collect_menus(const menu_def_t *prefix, int prefix_count,
                        menu_def_t **out_menus) {
    int total = prefix_count;
    for (loaded_gem_t *lg = gems_head; lg; lg = lg->next) {
        if (lg->iface->menus)
            total += lg->iface->menu_count;
    }
    *out_menus = malloc(sizeof(menu_def_t) * total);
    if (!*out_menus) return 0;
    memcpy(*out_menus, prefix, sizeof(menu_def_t) * prefix_count);
    int idx = prefix_count;
    for (loaded_gem_t *lg = gems_head; lg; lg = lg->next) {
        if (lg->iface->menus) {
            memcpy(*out_menus + idx, lg->iface->menus,
                   sizeof(menu_def_t) * lg->iface->menu_count);
            idx += lg->iface->menu_count;
        }
    }
    return total;
}

// ---------------------------------------------------------------------------
// Dispatch a menu command to every loaded gem that has a handle_command
// callback.  Gems should silently ignore IDs they don't own.
void shell_dispatch_gem_command(uint16_t id) {
    for (loaded_gem_t *lg = gems_head; lg; lg = lg->next) {
        if (lg->iface->handle_command)
            lg->iface->handle_command(id);
    }
}
// Does NOT dlclose(): gem procs must remain valid so that window
// kWindowMessageDestroy handlers work when ui_shutdown_graphics() later
// tears down the gem windows.  Call before ui_shutdown_graphics().
void shell_notify_gem_shutdown(void) {
    for (loaded_gem_t *lg = gems_head; lg; lg = lg->next) {
        if (lg->iface && lg->iface->shutdown)
            lg->iface->shutdown();
    }
}

// ---------------------------------------------------------------------------
// Release every loaded gem: dlclose() and free loader records.
// Call AFTER ui_shutdown_graphics() — all windows are gone and no more
// window proc calls will be made into gem code.
// NOTE: does NOT call iface->shutdown(); use shell_notify_gem_shutdown() for that.
void shell_cleanup_all_gems(void) {
    while (gems_head) {
        loaded_gem_t *lg = gems_head;
        gems_head = lg->next;
        dlclose(lg->handle);
        free(lg->path);
        free(lg);
    }
}

// ---------------------------------------------------------------------------
// ui_open_file handler — passed to ui_register_open_file_handler() at shell
// startup.  Routes files to the appropriate handler:
//   .gem  → load the gem directly via shell_load_gem()
//   other → find an already loaded gem that declares the extension in
//            file_types and call shell_load_gem() with the file path as
//            argv[1].  This does not auto-discover gems that are not yet
//            loaded; the caller must ensure the handler gem is loaded first.
// Returns true if the file was handled, false otherwise.
bool shell_handle_open_file(const char *path) {
    if (!path) return false;

    // Determine the file extension (last dot after the last slash).
    const char *ext = NULL;
    const char *p = path;
    while (*p) {
        if (*p == '.') ext = p;
        if (*p == '/' || *p == '\\') ext = NULL;
        p++;
    }

    // .gem file → load it directly.  argv[0] is the gem path itself, matching
    // the convention that argv[0] is always the program/module name.
    if (ext && strcmp(ext, ".gem") == 0) {
        char *gem_argv[] = { (char *)path, NULL };
        return shell_load_gem(path, 1, gem_argv);
    }

    // Other extension → find a loaded gem that handles it and re-invoke its
    // init() with the file as argv[1]: argv[0]=gem_path, argv[1]=file_path.
    // This mirrors the standard C convention where argv[0] is the program name.
    if (ext) {
        const char *gem_path = shell_get_gem_for_extension(ext);
        if (gem_path) {
            char *gem_argv[] = { (char *)gem_path, (char *)path, NULL };
            return shell_load_gem(gem_path, 2, gem_argv);
        }
    }

    return false;
}
