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
void shell_cleanup_all_gems(void);

// Scan all loaded gems and unload any whose main window has been destroyed.
// Call this from the shell's main loop.  Returns the number unloaded.
int shell_check_closed_gems(void);

#endif /* SHELL_GEM_LOADER_H */
