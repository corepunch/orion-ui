#ifndef GEM_MAGIC_H
#define GEM_MAGIC_H

#include <stdbool.h>
#include "commctl/menubar.h"

// gem_interface_t — the ABI every .gem must export via gem_get_interface().
//
// Shell discovery sequence:
//   1. axDynlibOpen("foo.gem")
//   2. sym = axDynlibSym(handle, "gem_get_interface")
//   3. gem_interface_t *iface = sym();
//   4. iface->init(argc, argv)   — creates windows, returns true on success
//   5. … shell runs shared event loop …
//   6. iface->shutdown()         — called when the gem is unloaded
//
// Menu contribution (optional):
//   After init() returns the gem may populate:
//     iface->menus         — pointer to the gem's menu_def_t array
//     iface->menu_count    — number of entries in that array
//     iface->handle_command — called by the shell for every menu command ID
//   The shell merges these menus into its own menu bar and routes commands
//   via handle_command.  Set all three to NULL/0 for no menu contribution.
typedef struct {
    const char  *name;          // Display name, e.g. "Image Editor"
    const char  *version;       // Version string, e.g. "1.0"
    const char **file_types;    // NULL-terminated list of handled file
                                // extensions (e.g. {".png",".bmp",NULL}),
                                // or NULL for no file associations.
    bool (*init)(int argc, char *argv[]); // Create windows; true = success
    void (*shutdown)(void);               // Cleanup on unload (may be NULL)

    // Menu contribution — filled by init(), read by shell after init() returns.
    const menu_def_t *menus;          // gem's top-level menu definitions
    int               menu_count;     // number of entries in menus[]
    void (*handle_command)(uint16_t id); // dispatch menu commands to gem
} gem_interface_t;

// -----------------------------------------------------------------------
// BUILD_AS_GEM — active when compiling a .gem shared library
// -----------------------------------------------------------------------
#ifdef BUILD_AS_GEM

// In gem mode, 'ui_is_running()' must always be false so that any
// GEM_MAIN-style event loop body is never executed at runtime.
// The compiler will typically dead-strip the loop entirely, but the
// key guarantee is that it is never *entered*, not that it is absent.
// 'ui_request_quit()' is silenced: a gem must not shut down the shell.
#define ui_is_running()   (false)
#define ui_request_quit() ((void)0)

// Forward declaration — allows gem_init() to retrieve the static interface
// struct (emitted by GEM_DEFINE below) so it can populate menu fields.
gem_interface_t *gem_get_interface(void);
//
// Macro parameters are suffixed with underscores to avoid accidental
// expansion inside struct member accesses such as __iface.name.
//
//   gem_name_  - display name (string literal or const char *)
//   gem_ver_   - version string
//   gem_init_  - bool (*)(int argc, char *argv[])  — create windows
//   gem_shdn_  - void (*)(void)  — cleanup on unload, or NULL
//   gem_types_ - NULL-terminated const char *[] of extensions, or NULL
//
// Example (with file associations):
//   static const char *img_types[] = { ".png", ".bmp", NULL };
//   GEM_DEFINE("Image Editor", "1.0", gem_init, gem_shutdown, img_types)
//
// Example (no file associations):
//   GEM_DEFINE("Hello World", "1.0", gem_init, NULL, NULL)
#define GEM_DEFINE(gem_name_, gem_ver_, gem_init_, gem_shdn_, gem_types_) \
    __attribute__((visibility("default")))                                  \
    gem_interface_t *gem_get_interface(void) {                              \
        static gem_interface_t __iface;                                     \
        if (!__iface.name) {                                                \
            __iface.name       = (gem_name_);                               \
            __iface.version    = (gem_ver_);                                \
            __iface.file_types = (gem_types_);                              \
            __iface.init       = (gem_init_);                               \
            __iface.shutdown   = (gem_shdn_);                               \
        }                                                                   \
        return &__iface;                                                     \
    }

// -----------------------------------------------------------------------
// GEM_MAIN — magic standalone-to-gem bridge (-Dmain=gem_main)
//
// Add #include "gem_magic.h" + GEM_MAIN() to a simple standalone program
// and it compiles both as an executable and as a .gem loaded by the shell.
//
// In .gem mode the following are automatically handled:
//   - main() is renamed to gem_main() and called as the gem's init fn.
//   - while(ui_is_running()){…} is not entered (returns false in gem mode).
//
// IMPORTANT caveats for GEM_MAIN programs:
//   - ui_init_graphics() is safe to call multiple times; the framework
//     handles the "already initialized" case when used with liborion.so.
//   - ui_shutdown_graphics() must NOT be called when running inside the
//     shell.  Guard it: #ifndef BUILD_AS_GEM … #endif
//   - destroy_window() for the main window after the event loop must also
//     be guarded: #ifndef BUILD_AS_GEM … #endif
//
// For programs with non-trivial cleanup, use the explicit GEM_DEFINE
// approach with separate gem_init / gem_shutdown functions, and guard
// the standalone main() with #ifndef BUILD_AS_GEM … #endif.
//
// Usage:
//   #include "../../gem_magic.h"
//   GEM_MAIN("My App", "1.0", NULL)   // NULL = no file associations
//
//   int main(int argc, char *argv[]) {
//       ui_init_graphics(…);   // safe — no-op if already initialized
//       window_t *w = create_window(…);
//       show_window(w, true);
//       while (ui_is_running()) { … } // not entered in gem mode
//   #ifndef BUILD_AS_GEM
//       destroy_window(w);
//       ui_shutdown_graphics();
//   #endif
//   }
// -----------------------------------------------------------------------

// Rename main() → gem_main() so it can be invoked as the gem's init fn.
// (No declaration headers use the identifier 'main', so this is safe.)
#define main  gem_main
int gem_main(int argc, char *argv[]);

// GEM_MAIN — register the (renamed) main() as the gem's init function.
#define GEM_MAIN(gem_name_, gem_ver_, gem_types_)                       \
    static bool __gem_main_init_(int argc, char *argv[]) {              \
        return gem_main(argc, argv) == 0;                               \
    }                                                                    \
    GEM_DEFINE(gem_name_, gem_ver_, __gem_main_init_, NULL, gem_types_)

#else   /* !BUILD_AS_GEM — standalone mode, macros are empty */

#define GEM_DEFINE(n_, v_, i_, s_, t_)  /* no-op */
#define GEM_MAIN(n_, v_, t_)            /* no-op */

#endif  /* BUILD_AS_GEM */

#endif  /* GEM_MAGIC_H */
