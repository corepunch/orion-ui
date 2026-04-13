#include "gem_loader.h"
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// POSIX strdup
#ifndef _GNU_SOURCE
char *strdup(const char *s);
#endif

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

// ---------------------------------------------------------------------------
// Internal: check whether a window pointer is still alive (in the list).
// ---------------------------------------------------------------------------
static bool is_window_alive(window_t *win) {
    extern window_t *windows;
    for (window_t *w = windows; w; w = w->next) {
        if (w == win) return true;
    }
    return false;
}

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

    // Snapshot window list so we can detect the gem's first window below.
    extern window_t *windows;
    window_t *before = windows;

    if (!iface->init(argc, argv)) {
        fprintf(stderr, "shell: %s init() failed\n", iface->name);
        dlclose(handle);
        return false;
    }

    // The gem's init() pushed at least one new window to the head of the list.
    window_t *main_win = (windows != before) ? windows : NULL;

    loaded_gem_t *lg = calloc(1, sizeof(loaded_gem_t));
    if (!lg) {
        if (iface->shutdown) iface->shutdown();
        dlclose(handle);
        return false;
    }

    lg->path        = strdup(gem_path);
    lg->handle      = handle;
    lg->iface       = iface;
    lg->main_window = main_win;
    lg->next        = gems_head;
    gems_head       = lg;

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

// ---------------------------------------------------------------------------
// Scan all loaded gems and unload any whose tracked main window has been
// destroyed.  Call this from the shell's main loop after dispatch_message().
// Returns the number of gems that were unloaded.
int shell_check_closed_gems(void) {
    int count = 0;
    loaded_gem_t *lg = gems_head;
    while (lg) {
        loaded_gem_t *next = lg->next;
        if (lg->main_window && !is_window_alive(lg->main_window)) {
            shell_unload_gem(lg->main_window);
            count++;
        }
        lg = next;
    }
    return count;
}

// ---------------------------------------------------------------------------
// Call every loaded gem's shutdown() while the GL context is still active.
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
