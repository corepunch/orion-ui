#include <orion/ui.h>
#include <orion/gem.h>

gem_interface_t *gem_get_interface(void);

static gem_interface_t *g_ios_gem;

static bool_t ios_gem_start(int argc, char **argv) {
  g_ios_gem = gem_get_interface();
  if (!g_ios_gem || !g_ios_gem->name || !g_ios_gem->init) return FALSE;
  if (!ui_init_graphics(UI_INIT_DESKTOP, g_ios_gem->name, 0, 0)) return FALSE;
  if (!g_ios_gem->init(argc, argv, 0)) {
    ui_shutdown_graphics();
    return FALSE;
  }
  return TRUE;
}

static void ios_gem_frame(void) {
  if (!ui_is_running()) return;
  ui_event_t e;
  while (axPeekMessage(&e)) dispatch_message(&e);
  repost_messages();
}

static void ios_gem_stop(void) {
  if (g_ios_gem && g_ios_gem->shutdown) g_ios_gem->shutdown();
  ui_shutdown_graphics();
}

int main(int argc, char **argv) {
  return axRunApplication(argc, argv, ios_gem_start, ios_gem_frame, ios_gem_stop);
}
