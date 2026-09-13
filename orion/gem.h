#ifndef GEM_MAGIC_H
#define GEM_MAGIC_H

#include <stdbool.h>
#include <stdlib.h>
#include <orion/commctl/menubar.h>

// gem_interface_t — the ABI every .gem must export via gem_get_interface().
//
// Shell discovery sequence:
//   1. axDynlibOpen("foo.gem")
//   2. sym = axDynlibSym(handle, "gem_get_interface")
//   3. gem_interface_t *iface = sym();
//   4. iface->init(argc, argv, hinstance)   — creates windows, returns true on success
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
    bool (*init)(int argc, char *argv[], hinstance_t hinstance); // Create windows; true = success
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
// Add #include "gem.h" + GEM_MAIN() to a simple standalone program
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
//   #include "../../gem.h"
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

// GEM_STANDALONE_MAIN is a no-op in gem mode; the shell owns the event loop.
#define GEM_STANDALONE_MAIN(title_, flags_, w_, h_, menubar_, accel_) \
    /* no-op: standalone main() not needed when loaded as a .gem */

// Rename main() → gem_main() so it can be invoked as the gem's init fn.
// (No declaration headers use the identifier 'main', so this is safe.)
#define main  gem_main
int gem_main(int argc, char *argv[]);

// GEM_MAIN — register the (renamed) main() as the gem's init function.
// The shell passes hinstance to init(); GEM_MAIN-style programs receive it
// as a global for use with create_window() root-window calls.
extern hinstance_t g_gem_hinstance;
#define GEM_MAIN(gem_name_, gem_ver_, gem_types_)                         \
    hinstance_t g_gem_hinstance = 0;                                      \
    static bool __gem_main_init_(int argc, char *argv[],                  \
                                 hinstance_t hinstance) {                 \
        g_gem_hinstance = hinstance;                                      \
        return gem_main(argc, argv) == 0;                                 \
    }                                                                     \
    GEM_DEFINE(gem_name_, gem_ver_, __gem_main_init_, NULL, gem_types_)

#else   /* !BUILD_AS_GEM — standalone mode, macros are empty */

#define GEM_DEFINE(n_, v_, i_, s_, t_)  /* no-op */
// In standalone mode g_gem_hinstance is always 0 (system). Provide it as
// a macro constant so GEM_MAIN-style code can still reference it.
#define g_gem_hinstance ((hinstance_t)0)
#define GEM_MAIN(n_, v_, t_)            /* no-op */

// ---------------------------------------------------------------------------
// gem_rc_query — main-thread handler for RC read queries.
//
// Registered via axRCSetQueryHandler so the RC server can answer
// list_windows, get_rect, get_ctrl_rect, get_text, get_value, and
// click_ctrl commands by reading the live window tree.
// ---------------------------------------------------------------------------
static void
gem_rc_query(const char *req, char *resp, int resplen)
{
  /* list_windows */
  if (strcmp(req, "list_windows") == 0) {
    char *p = resp;
    int   left = resplen - 1;
    for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
      int n = snprintf(p, (size_t)left, "window %d %d %d %d %s\n",
                       (int)w->frame.x, (int)w->frame.y,
                       (int)w->frame.w, (int)w->frame.h, w->title);
      if (n <= 0 || n >= left) break;
      p += n; left -= n;
    }
    snprintf(p, (size_t)(left + 1), "ok\n");
    return;
  }

  /* get_focus */
  if (strcmp(req, "get_focus") == 0) {
    window_t *f = g_ui_runtime.focused;
    snprintf(resp, (size_t)resplen, "focused %s\nok\n", f ? f->title : "");
    return;
  }

  /* get_rect <title> */
  if (strncmp(req, "get_rect ", 9) == 0) {
    const char *title = req + 9;
    for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
      if (strcmp(w->title, title) == 0) {
        snprintf(resp, (size_t)resplen, "rect %d %d %d %d\nok\n",
                 (int)w->frame.x, (int)w->frame.y,
                 (int)w->frame.w, (int)w->frame.h);
        return;
      }
    }
    snprintf(resp, (size_t)resplen, "err no window\n");
    return;
  }

  /* get_ctrl_rect <ctrl_id> <title> */
  if (strncmp(req, "get_ctrl_rect ", 14) == 0) {
    int ctrl_id; char title[512];
    if (sscanf(req + 14, "%d %511[^\t\n]", &ctrl_id, title) == 2) {
      for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
        if (strcmp(w->title, title) == 0) {
          window_t *c = get_window_item(w, (uint32_t)ctrl_id);
          if (!c) { snprintf(resp, (size_t)resplen, "err no ctrl\n"); return; }
          snprintf(resp, (size_t)resplen, "rect %d %d %d %d\nok\n",
                   window_screen_x(c), window_screen_y(c),
                   (int)c->frame.w, (int)c->frame.h);
          return;
        }
      }
    }
    snprintf(resp, (size_t)resplen, "err no window\n");
    return;
  }

  /* get_text <ctrl_id> <title> */
  if (strncmp(req, "get_text ", 9) == 0) {
    int ctrl_id; char title[512];
    if (sscanf(req + 9, "%d %511[^\t\n]", &ctrl_id, title) == 2) {
      for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
        if (strcmp(w->title, title) == 0) {
          window_t *c = get_window_item(w, (uint32_t)ctrl_id);
          if (!c) { snprintf(resp, (size_t)resplen, "err no ctrl\n"); return; }
          snprintf(resp, (size_t)resplen, "text %s\nok\n", c->title);
          return;
        }
      }
    }
    snprintf(resp, (size_t)resplen, "err no window\n");
    return;
  }

  /* get_value <ctrl_id> <title> */
  if (strncmp(req, "get_value ", 10) == 0) {
    int ctrl_id; char title[512];
    if (sscanf(req + 10, "%d %511[^\t\n]", &ctrl_id, title) == 2) {
      for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
        if (strcmp(w->title, title) == 0) {
          window_t *c = get_window_item(w, (uint32_t)ctrl_id);
          if (!c) { snprintf(resp, (size_t)resplen, "err no ctrl\n"); return; }
          snprintf(resp, (size_t)resplen, "value %u\nok\n", (unsigned)c->value);
          return;
        }
      }
    }
    snprintf(resp, (size_t)resplen, "err no window\n");
    return;
  }

  /* click_ctrl <ctrl_id> <title> */
  if (strncmp(req, "click_ctrl ", 11) == 0) {
    int ctrl_id; char title[512];
    if (sscanf(req + 11, "%d %511[^\t\n]", &ctrl_id, title) == 2) {
      for (window_t *w = g_ui_runtime.windows; w; w = w->next) {
        if (strcmp(w->title, title) == 0) {
          window_t *c = get_window_item(w, (uint32_t)ctrl_id);
          if (!c) { snprintf(resp, (size_t)resplen, "err no ctrl\n"); return; }
          int cx = window_screen_x(c) + (int)c->frame.w / 2;
          int cy = window_screen_y(c) + (int)c->frame.h / 2;
          axPostMessageW(NULL, kEventLeftButtonDown, MAKEDWORD(cx, cy), NULL);
          axPostMessageW(NULL, kEventLeftButtonUp,   MAKEDWORD(cx, cy), NULL);
          snprintf(resp, (size_t)resplen, "ok\n");
          return;
        }
      }
    }
    snprintf(resp, (size_t)resplen, "err no window\n");
    return;
  }

  snprintf(resp, (size_t)resplen, "err unknown query\n");
}

// ---------------------------------------------------------------------------
// GEM_STANDALONE_MAIN — standard standalone entry point for MDI applications.
//
// Generates the canonical int main() for an MDI app that uses gem_init /
// gem_shutdown and an accelerator-aware event loop.  The macro expands to a
// no-op in BUILD_AS_GEM mode so it can be placed outside any #ifndef guard.
//
// Parameters:
//   title_   - window title string passed to ui_init_graphics().
//   flags_   - init flags (e.g. UI_INIT_DESKTOP).
//   w_, h_   - logical screen dimensions passed to ui_init_graphics().
//   menubar_ - expression that yields the menubar window_t * (e.g. g_app->menubar_win).
//   accel_   - expression that yields the accel_table_t * (e.g. g_app->accel).
//
// Example usage (at file scope, after GEM_DEFINE):
//   GEM_STANDALONE_MAIN("Orion My App", UI_INIT_DESKTOP, SCREEN_W, SCREEN_H,
//                        g_app->menubar_win, g_app->accel)
//
// Standard options consumed by the launcher and not passed to gem_init():
//   --screenshot PATH  Capture the first fully painted frame as JPEG and exit.
//   -rc [PORT]         Start the TCP remote-control server (default 17777).
// ---------------------------------------------------------------------------
#ifdef AX_PLATFORM_IOS
#define GEM_STANDALONE_MAIN(title_, flags_, w_, h_, menubar_, accel_) \
  static bool_t gem_ios_start(int argc, char **argv) { \
    if (!ui_init_graphics((flags_), (title_), (w_), (h_))) return FALSE; \
    char *args[] = {argv[0], NULL}; \
    if (!gem_init(1, args, 0)) { ui_shutdown_graphics(); return FALSE; } \
    return TRUE; \
  } \
  static void gem_ios_frame(void) { \
    if (!ui_is_running()) return; \
    ui_event_t e; \
    while (get_message(&e)) \
      if (!translate_accelerator((menubar_), &e, (accel_))) dispatch_message(&e); \
    repost_messages(); \
  } \
  static void gem_ios_stop(void) { gem_shutdown(); ui_shutdown_graphics(); } \
  int main(int argc, char **argv) { \
    return axRunApplication(argc, argv, gem_ios_start, gem_ios_frame, gem_ios_stop); \
  }
#else
#define GEM_STANDALONE_MAIN(title_, flags_, w_, h_, menubar_, accel_)     \
  int main(int argc, char *argv[]) {                                        \
    const char *gem_screenshot_path = NULL;                                 \
    uint16_t gem_rc_port = 0;                                                \
    char *gem_app_argv[argc + 1];                                           \
    int gem_app_argc = 1;                                                   \
    gem_app_argv[0] = argv[0];                                              \
    for (int gem_arg_index = 1; gem_arg_index < argc; gem_arg_index++) {    \
      if (strcmp(argv[gem_arg_index], "--screenshot") == 0 &&             \
          gem_arg_index + 1 < argc) {                                       \
        gem_screenshot_path = argv[++gem_arg_index];                        \
      } else if (strcmp(argv[gem_arg_index], "-rc") == 0) {                \
        gem_rc_port = 17777;                                                 \
        if (gem_arg_index + 1 < argc && argv[gem_arg_index + 1][0] != '-') \
          gem_rc_port = (uint16_t)atoi(argv[++gem_arg_index]);              \
      } else {                                                              \
        gem_app_argv[gem_app_argc++] = argv[gem_arg_index];                 \
      }                                                                     \
    }                                                                       \
    gem_app_argv[gem_app_argc] = NULL;                                      \
    if (!ui_init_graphics((flags_), (title_), (w_), (h_)))                 \
      return 1;                                                             \
    if (!gem_init(gem_app_argc, gem_app_argv, 0)) {                        \
      ui_shutdown_graphics();                                               \
      return 1;                                                             \
    }                                                                       \
    if (gem_rc_port && !axRCStart(gem_rc_port)) {                           \
      gem_shutdown();                                                       \
      ui_shutdown_graphics();                                               \
      return 1;                                                             \
    }                                                                       \
    if (gem_rc_port)                                                        \
      axRCSetQueryHandler(gem_rc_query);                                    \
    if (gem_screenshot_path &&                                              \
        !ui_request_screenshot(gem_screenshot_path, 90, true)) {           \
      gem_shutdown();                                                       \
      ui_shutdown_graphics();                                               \
      return 1;                                                             \
    }                                                                       \
    while (ui_is_running()) {                                               \
      char gem_rc_screenshot[1024];                                         \
      if (axRCPopScreenshot(gem_rc_screenshot, sizeof(gem_rc_screenshot)))  \
        ui_request_screenshot(gem_rc_screenshot, 90, false);                \
      axRCProcessQuery();                                                   \
      ui_event_t e;                                                         \
      while (get_message(&e)) {                                             \
        if (!translate_accelerator((menubar_), &e, (accel_)))              \
          dispatch_message(&e);                                             \
      }                                                                     \
      repost_messages();                                                    \
    }                                                                       \
    axRCStop();                                                              \
    gem_shutdown();                                                         \
    ui_shutdown_graphics();                                                 \
    return 0;                                                               \
  }

#endif  /* AX_PLATFORM_IOS */
#endif  /* BUILD_AS_GEM */

// ---------------------------------------------------------------------------
// set_app_menu — register the application menu and command handler.
//
// In standalone mode: creates a full-width menubar window at y=0, sends
// kMenuBarMessageSetMenus, shows it, and returns the window pointer.
// handle_command is unused in this path — the caller's proc is responsible
// for dispatching kMenuBarNotificationItemClick to the command handler.
//
// In gem mode: populates the gem interface's menu-contribution fields
// (iface->menus / iface->menu_count / iface->handle_command) so the shell
// can merge them into its own menu bar, then returns NULL.  proc and
// hinstance are unused in this path.
//
// Parameters:
//   proc           — window procedure for the menu bar window (standalone
//                    only).  Typically wraps win_menubar and routes
//                    kMenuBarNotificationItemClick to handle_command.
//   menus          — array of menu_def_t describing the top-level menus.
//   menu_count     — number of entries in menus[].
//   handle_command — dispatch function for menu item selections.  Used
//                    directly by the shell in gem mode; in standalone mode
//                    the caller's proc is expected to call it.
//   hinstance      — owning application instance (standalone only).
//
// Returns the menubar window in standalone mode, NULL in gem mode.
static inline window_t *set_app_menu(
    winproc_t proc,
    const menu_def_t *menus, int menu_count,
    void (*handle_command)(uint16_t id),
    hinstance_t hinstance)
{
#ifdef BUILD_AS_GEM
    (void)proc; (void)hinstance;
    gem_interface_t *iface = gem_get_interface();
    iface->menus          = menus;
    iface->menu_count     = menu_count;
    iface->handle_command = handle_command;
    return NULL;
#else
    (void)handle_command;
    int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
    window_t *mb = create_window(
        "menubar",
        WINDOW_NOTITLE | WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
        MAKERECT(0, 0, sw, MENUBAR_HEIGHT),
        NULL, proc, hinstance, NULL);
    send_message(mb, kMenuBarMessageSetMenus, (uint32_t)menu_count, (void *)menus);
    show_window(mb, true);
    return mb;
#endif
}

#endif  /* GEM_MAGIC_H */
