// Orion Shell — Windows 3.x inspired desktop shell.
//
// The shell owns the graphics context and the shared event loop.
// Programs are loaded as .gem shared libraries; each .gem creates its own
// windows and the shell dispatches all messages to them.

#include "../ui.h"
#include "gem_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Shell menu bar
// ---------------------------------------------------------------------------

// Shell-owned command IDs live in a reserved high range so they cannot
// collide with gem-defined menu IDs, which commonly start at small values.
#define ID_SHELL_CMD_BASE  0xF000
#define ID_SHELL_QUIT      (ID_SHELL_CMD_BASE + 1)

static const menu_item_t kShellFileItems[] = {
    {"Quit", ID_SHELL_QUIT},
};
static const menu_def_t kShellMenus[] = {
    {"File", kShellFileItems, 1},
};
#define SHELL_MENU_COUNT 1

static window_t *g_menubar = NULL;

// Rebuild the combined menu bar: shell's own menus first, then every loaded
// gem's contributed menus.  Call after each gem load/unload.
static void shell_rebuild_menubar(void) {
    if (!g_menubar) return;
    menu_def_t *all = NULL;
    int count = shell_collect_menus(kShellMenus, SHELL_MENU_COUNT, &all);
    if (count <= 0 || all == NULL) return;
    send_message(g_menubar, kMenuBarMessageSetMenus, (uint32_t)count, all);
    free(all);
}

static result_t shell_menubar_proc(window_t *win, uint32_t msg,
                                    uint32_t wparam, void *lparam) {
    if (msg == kWindowMessageCommand) {
        uint16_t notif = HIWORD(wparam);
        if (notif == kMenuBarNotificationItemClick) {
            uint16_t id = LOWORD(wparam);
            if (id == ID_SHELL_QUIT) {
                ui_request_quit();
            } else {
                // Route to whichever gem owns this command.
                shell_dispatch_gem_command(id);
            }
            return true;
        }
    }
    return win_menubar(win, msg, wparam, lparam);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char *argv[]) {
    if (!ui_init_graphics(UI_INIT_DESKTOP | UI_INIT_TRAY,
                          "Orion Shell", 640, 480)) {
        fprintf(stderr, "shell: failed to initialise graphics\n");
        return 1;
    }

    int sw = ui_get_system_metrics(kSystemMetricScreenWidth);

    // Register the shell's open-file handler so any gem can call ui_open_file()
    // to load a .gem or open a file in the appropriate gem.
    ui_register_open_file_handler(shell_handle_open_file);

    // Menu bar — full screen width, always on top.
    g_menubar = create_window(
        "menubar",
        WINDOW_NOTITLE | WINDOW_ALWAYSONTOP | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
        MAKERECT(0, 0, sw, MENUBAR_HEIGHT),
        NULL, shell_menubar_proc, 0, NULL);
    shell_rebuild_menubar();
    show_window(g_menubar, true);

    // Load .gem files named on the command line, or print usage.
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            char *gem_argv[] = { argv[i], NULL };
            shell_load_gem(argv[i], 1, gem_argv);
            shell_rebuild_menubar();
        }
    } else {
        printf("Usage: %s <gem1.gem> [gem2.gem] ...\n", argv[0]);
        printf("       (loading default gems from build/gem/)\n");

        // Default: try to load whatever gems were built.
        const char *defaults[] = {
            "build/gem/filemanager.gem",
            NULL
        };
        for (int i = 0; defaults[i]; i++) {
            char *gem_argv[] = { (char *)defaults[i], NULL };
            shell_load_gem(defaults[i], 1, gem_argv);
            shell_rebuild_menubar();
        }
    }

    // Shared event loop — processes messages for all windows (shell's
    // own windows AND windows created by loaded gems, because both use
    // the same liborion.so instance and therefore the same window list).
    ui_event_t e;
    unsigned last_gem_gen = shell_gem_generation();
    while (ui_is_running()) {
        while (get_message(&e))
            dispatch_message(&e);
        // Check whether any loaded gem's main window was closed during
        // this batch of events and unload the gem if so.
        shell_check_closed_gems();
        // Rebuild the menu bar if gems were loaded or unloaded (e.g. via
        // ui_open_file() triggered by the file manager).
        unsigned gen = shell_gem_generation();
        if (gen != last_gem_gen) {
            shell_rebuild_menubar();
            last_gem_gen = gen;
        }
        repost_messages();
    }

    // 1. Call gem shutdown() functions while the GL context is still active
    //    (e.g. imageeditor deletes OpenGL textures in its shutdown).
    shell_notify_gem_shutdown();
    // 2. Destroy all windows while gem code is still mapped in memory, so
    //    any kWindowMessageDestroy handlers owned by a gem remain valid.
    ui_shutdown_graphics();
    // 3. Now it is safe to axDynlibClose() — no more window proc calls will be made.
    shell_cleanup_all_gems();
    return 0;
}
