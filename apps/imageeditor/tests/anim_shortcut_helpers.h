#ifndef __ANIM_SHORTCUT_HELPERS_H__
#define __ANIM_SHORTCUT_HELPERS_H__

static bool test_anim_shortcut(uint16_t key, uint16_t modifiers) {
  if (!g_app->accel)
    g_app->accel = load_accelerators(imageeditor_default_accels, imageeditor_default_accel_count);
  if (!g_app->menubar_win) {
    window_t *focused = g_ui_runtime.focused;
    g_app->menubar_win = create_window("", WINDOW_NOTITLE, MAKERECT(0, 0, 1, 1), NULL,
                                      editor_menubar_proc, 0, NULL);
    if (focused) set_focus(focused);
  }
  ui_event_t event = {.message = kEventKeyDown, .keyCode = key, .modflags = modifiers};
  return translate_accelerator(g_app->menubar_win, &event, g_app->accel);
}

#endif
