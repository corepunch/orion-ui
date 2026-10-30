#include <mapview/map.h>
#include <mapview/sprites.h>
#include <mapview/console.h>
#include <editor/editor.h>
#include <mapview/gamefont.h>
#include <orion/kernel/kernel.h>
#include <orion/gem.h>

#define SCREEN_WIDTH 720
#define SCREEN_HEIGHT 480

void init_floor_shader(void);
void init_sky_geometry(void);
bool init_radial_menu(void);

result_t win_statbar(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_console(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_game(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_things(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_thing(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_sector(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_project(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_dummy(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

result_t win_sprite(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

window_t *g_inspector = NULL;
static const char *startup_map = "MAP01";
static bool startup_3d;

static void init_windows(hinstance_t hinstance) {
  UI_CLASS(win_sprite);
  show_window(create_window("Things", WINDOW_VSCROLL, MAKERECT(8, 96, THING_SIZE*3, 256), NULL, win_things, hinstance, NULL), true);
  show_window(create_window("Project", WINDOW_VSCROLL, MAKERECT(4, 20, 128, 256), NULL, win_project, hinstance, NULL), true);
  g_inspector = create_window("Inspector", WINDOW_TOOLBAR, MAKERECT(ui_get_system_metrics(kSystemMetricScreenWidth)-200, 40, 150, 300), NULL, win_dummy, hinstance, NULL);
  show_window(g_inspector, true);
}

bool gem_init(int argc, char *argv[], hinstance_t hinstance) {
  if (argc < 2) {
    printf("Usage: doom-ed <wad_file>\n");
    return false;
  }

  const char *filename = argv[1];

  if (!init_wad(filename)) {
    printf("Error: Could not open file %s\n", filename);
    return false;
  }

  if (!cache_lump("PLAYPAL")) {
    printf("Error: Required lump not found (PLAYPAL)\n");
    return false;
  }
  extern palette_entry_t *palette;
  palette = cache_lump("PLAYPAL");

  ui_joystick_init();
  init_resources();
  init_floor_shader();
  init_sky_geometry();
  init_radial_menu();
  init_gamefont();
  load_console_font();
  init_sprites();
  init_things();
  init_intermission();
  init_windows(hinstance);

  allocate_mapside_textures();
  allocate_flat_textures();

  open_map(startup_map);
  if (startup_3d && g_game && g_game->state.window)
    g_game->state.window->proc = win_game;
  return true;
}

void gem_shutdown(void) {
  shutdown_wad();
  ui_joystick_shutdown();
}

static int doom_ed_graphics_flags = UI_INIT_DESKTOP | UI_INIT_TRAY;
#define main doom_ed_graphics_main
GEM_STANDALONE_MAIN("Doom Editor", doom_ed_graphics_flags,
                    SCREEN_WIDTH, SCREEN_HEIGHT, NULL, NULL)

#undef main
#ifndef BUILD_AS_GEM
static void check_map(const char *name, void *context) {
  int *failures = context;
  map_data_t map = load_map(name);
  if (!map.vertices || !map.linedefs || !map.sectors) (*failures)++;
  printf("%s: vertices=%d lines=%d sectors=%d things=%d\n", name,
         map.num_vertices, map.num_linedefs, map.num_sectors, map.num_things);
  free_map_data(&map);
}
int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--map") && i + 1 < argc) {
      startup_map = argv[i + 1];
      memmove(argv + i, argv + i + 2, (size_t)(argc - i - 1) * sizeof(*argv));
      argc -= 2;
      i--;
      continue;
    }
    if (!strcmp(argv[i], "--3d")) startup_3d = true;
    else if (strcmp(argv[i], "--offscreen")) continue;
    if (!strcmp(argv[i], "--offscreen")) doom_ed_graphics_flags |= UI_INIT_HIDDEN;
    memmove(argv + i, argv + i + 1, (size_t)(argc - i) * sizeof(*argv));
    argc--;
    i--;
  }
  if (argc == 3 && !strcmp(argv[1], "--check-wad")) {
    int failures = 0;
    if (!init_wad(argv[2])) return 1;
    find_all_maps(check_map, &failures);
    shutdown_wad();
    return failures ? 1 : 0;
  }
  return doom_ed_graphics_main(argc, argv);
}
#endif
