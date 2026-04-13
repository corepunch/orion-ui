#ifndef SHELL_GEM_LOADER_H
#define SHELL_GEM_LOADER_H

#include "../ui.h"
#include "../gem_magic.h"
#include <stdbool.h>

// Load a .gem file, call its init function with the supplied arguments,
// and register it in the internal list.  Returns true on success.
bool shell_load_gem(const char *gem_path, int argc, char *argv[]);

// Unload the gem whose init created the given window.  Calls shutdown()
// and dlclose().  Silently ignored if no such gem is found.
void shell_unload_gem(window_t *window);

// Return the path of the first loaded gem that handles 'extension'
// (e.g. ".png"), or NULL if none.
const char *shell_get_gem_for_extension(const char *extension);

// Shut down and dlclose every loaded gem.  Called on shell exit.
// NOTE: call shell_notify_gem_shutdown() first (while the GL context is still
// active), then ui_shutdown_graphics() (destroys windows while gem procs are
// still in memory), then this function to release the library handles.
void shell_cleanup_all_gems(void);

// Call every loaded gem's shutdown() function while the GL context is still
// active.  Does NOT dlclose() — gem procs remain valid so that window
// kWindowMessageDestroy handlers work correctly when ui_shutdown_graphics()
// later destroys the gem windows.  Call this before ui_shutdown_graphics().
void shell_notify_gem_shutdown(void);

// Scan all loaded gems and unload any whose main window has been destroyed.
// Call this from the shell's main loop.  Returns the number unloaded.
int shell_check_closed_gems(void);

// Build a combined menu array: prefix menus first, then all loaded gems'
// menus appended in load order.  The caller must free() the returned array.
// Returns the total number of menu_def_t entries written.
int shell_collect_menus(const menu_def_t *prefix, int prefix_count,
                        menu_def_t **out_menus);

// Dispatch a menu command id to every loaded gem's handle_command callback.
void shell_dispatch_gem_command(uint16_t id);

#endif /* SHELL_GEM_LOADER_H */
