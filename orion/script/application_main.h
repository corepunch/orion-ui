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
  bool supplied = false;
  for (int i = 1; i < argc; i++) {
    const char *option = argv[i];
    if (!strcmp(option, "--help") || !strcmp(option, "-h")) {
      fprintf(stderr, "Usage: %s [APP_DIRECTORY|application.orion] [--data DIRECTORY] [--lang LANGUAGE]\n", argv[0]);
      return false;
    }
    if (!strcmp(option, "--app") || !strcmp(option, "--data") || !strcmp(option, "--lang")) {
      if (i + 1 >= argc) { fprintf(stderr, "[application] missing value for %s\n", option); return false; }
      if (!strcmp(option, "--app")) { path = argv[++i]; supplied = true; }
      else if (!strcmp(option, "--data")) data = argv[++i];
      else ui_set_language(argv[++i]);
    } else if (option[0] != '-' && !supplied) { path = option; supplied = true; }
    else { fprintf(stderr, "[application] unknown argument: %s\n", option); return false; }
  }
  if (!supplied) {
    char resource_manifest[1400];
    snprintf(resource_manifest, sizeof(resource_manifest), "%s/../Resources/application.orion", ui_get_exe_dir());
    FILE *file = fopen(resource_manifest, "rb");
    if (file) { fclose(file); snprintf(manifest, sizeof(manifest), "%s", resource_manifest); }
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
