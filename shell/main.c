// Orion Shell — Windows 3.x inspired desktop shell.
//
// The shell owns the graphics context and the shared event loop.
// Programs are loaded as .gem shared libraries; each .gem creates its own
// windows and the shell dispatches all messages to them.

#include "../ui.h"
#include "gem_loader.h"
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Desktop window
// ---------------------------------------------------------------------------

static result_t desktop_proc(window_t *win, uint32_t msg,
                              uint32_t wparam, void *lparam) {
    switch (msg) {
        case kWindowMessageCreate:
            return true;
        case kWindowMessagePaint:
            // Draw a simple desktop background.
            fill_rect(0xff2d5a27, 0, 0, win->frame.w, win->frame.h);
            draw_text_small("Orion Shell", 4, 4, 0x88ffffff);
            return true;
        case kWindowMessageDestroy:
            ui_request_quit();
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char *argv[]) {
    if (!ui_init_graphics(UI_INIT_DESKTOP | UI_INIT_TRAY,
                          "Orion Shell", 1024, 768)) {
        fprintf(stderr, "shell: failed to initialise graphics\n");
        return 1;
    }

    // Create a background desktop window (no title bar, always at bottom).
    int sw = ui_get_system_metrics(kSystemMetricScreenWidth);
    int sh = ui_get_system_metrics(kSystemMetricScreenHeight);
    window_t *desktop = create_window(
        "Desktop",
        WINDOW_NOTITLE | WINDOW_NOTRAYBUTTON | WINDOW_NORESIZE,
        MAKERECT(0, 0, sw, sh),
        NULL, desktop_proc, NULL);
    show_window(desktop, true);

    // Load .gem files named on the command line, or print usage.
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            char *gem_argv[] = { argv[i], NULL };
            shell_load_gem(argv[i], 1, gem_argv);
        }
    } else {
        printf("Usage: %s <gem1.gem> [gem2.gem] …\n", argv[0]);
        printf("       (loading default gems from build/lib/gems/)\n");

        // Default: try to load whatever gems were built.
        const char *defaults[] = {
            "build/lib/gems/filemanager.gem",
            NULL
        };
        for (int i = 0; defaults[i]; i++) {
            char *gem_argv[] = { (char *)defaults[i], NULL };
            shell_load_gem(defaults[i], 1, gem_argv);
        }
    }

    // Shared event loop — processes messages for all windows (shell's
    // own windows AND windows created by loaded gems, because both use
    // the same liborion.so instance and therefore the same window list).
    ui_event_t e;
    while (ui_is_running()) {
        while (get_message(&e))
            dispatch_message(&e);
        // Check whether any loaded gem's main window was closed during
        // this batch of events and unload the gem if so.
        shell_check_closed_gems();
        repost_messages();
    }

    shell_cleanup_all_gems();
    ui_shutdown_graphics();
    return 0;
}
