// Common launcher for declarative applications; also usable as orion-run.
#include <orion/ui.h>
#include <orion/gem.h>
#include <orion/script/application.h>
#include <stdio.h>
#include <string.h>
#ifndef ORION_APP_NAME
#define ORION_APP_NAME ""
#endif
static script_application_t *launched_application;
bool gem_init(int argc, char *argv[], hinstance_t instance) {
  char manifest[1400];
  snprintf(manifest, sizeof(manifest), "%s/../share/%s/application.orion", ui_get_exe_dir(), ORION_APP_NAME);
  const char *path = manifest, *data = NULL;
  for (int i = 1; i + 1 < argc; i++) {
    if (!strcmp(argv[i], "--app")) path = argv[++i];
    else if (!strcmp(argv[i], "--data")) data = argv[++i];
    else if (!strcmp(argv[i], "--lang")) ui_set_language(argv[++i]);
  }
  char error[1024];
  launched_application = script_application_open(path, data, error, sizeof(error));
#ifndef BUILD_AS_GEM
  script_application_quit_on_close(launched_application, true);
#endif
  return launched_application != NULL;
}
void gem_shutdown(void) { script_application_close(launched_application); launched_application = NULL; }
GEM_DEFINE("Orion " ORION_APP_NAME, "1.0", gem_init, gem_shutdown, NULL)
GEM_STANDALONE_MAIN("Orion " ORION_APP_NAME, UI_INIT_DESKTOP, 1240, 900, NULL, NULL)
