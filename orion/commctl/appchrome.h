#ifndef __UI_APPCHROME_H__
#define __UI_APPCHROME_H__

#include "menubar.h"
#include <orion/user/toolbar.h>

// Application chrome owns menu and docked toolbar bands. Empty space passes input through.
// A NULL menubar_proc leaves room for the shell menu.
window_t *create_app_chrome(const char *title, winproc_t menubar_proc,
                            const menu_def_t *menus, int menu_count,
                            winproc_t toolbar_proc, hinstance_t hinstance);
window_t *app_chrome_menubar(window_t *chrome);
window_t *create_application_chrome(const char *title, winproc_t menubar_proc,
                                    const menu_def_t *menus, int menu_count,
                                    winproc_t toolbar_proc, const application_toolbar_t *toolbar,
                                    hinstance_t hinstance);
window_t *app_chrome_toolbar(window_t *chrome);
window_t *app_chrome_add_toolbar(window_t *chrome, toolbar_dock_t dock, winproc_t proc);

#endif
