#include <cglm/cglm.h>
#include <cglm/cglm.h>

#include <mapview/gl_compat.h>
#include <mapview/map.h>
#include <mapview/sprites.h>
#include <mapview/console.h>
#include <editor/editor.h>
#include <orion/user/user.h>
#include <orion/user/draw.h>

game_t *g_game = NULL;

bool init_sky(map_data_t const*);
const char *get_map_name(const char *name);

extern GLuint world_prog, ui_prog;
extern unsigned frame;

// Track FPS (relative mouse capture) mode for the game window
static bool g_relative_mouse_mode = false;

// Initialize player position based on map data
void init_player(map_data_t const *map, player_t *player) {
  memset(player, 0, sizeof(player_t));
  player->height = 41.0;  // Default DOOM player eye height
  
  // Find player start position (thing type 1)
  for (int i = 0; i < map->num_things; i++) {
    if (map->things[i].type == 1) {
      player->x = map->things[i].x;
      player->y = map->things[i].y;
      player->angle = map->things[i].angle;
      break;
    }
  }
}

result_t win_editor(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);


static irect16_t *new_frame(void) {
  static uint8_t counter = 0;
  static irect16_t rect = { 0, 0, 400, 400 };
  rect.x = counter+32;
  rect.y = counter+32;
  counter += 16;
  return &rect;
}

void new_map(void) {
  game_t *gm = calloc(1, sizeof(game_t));
  if (!gm) return;
  memset(gm, 0, sizeof(game_t));
  gm->last_time = (uint32_t)axGetMilliseconds();
  show_window(create_window("New map", 0, new_frame(), NULL, win_editor, 0, gm), true);
  init_editor(&gm->state);
  g_game = gm;
}

void open_map(const char *mapname) {
  game_t *gm = calloc(1, sizeof(game_t));
  if (!gm) return;
  gm->map = load_map(mapname);
  gm->last_time = (uint32_t)axGetMilliseconds();

  if (gm->map.num_vertices > 0) {
    print_map_info(&gm->map);
//    allocate_mapside_textures(&game->map);
//    allocate_flat_textures(&game->map);
    init_sky(&gm->map);
    init_player(&gm->map, &gm->player);
    build_wall_vertex_buffer(&gm->map);
    build_floor_vertex_buffer(&gm->map);

    set_editor_camera(&gm->state, gm->player.x, gm->player.y);

    conprintf("Successfully loaded map %s", get_map_name(mapname));
  } else {
    conprintf("Failed to load map %s", mapname);
  }

  init_editor(&gm->state);

  show_window(create_window(mapname, 0, new_frame(), NULL, win_editor, 0, gm), true);
  
  g_game = gm;
}

//#define ISOMETRIC

void get_view_matrix(map_data_t const *map, player_t const *player, float aspect, mat4 out) {
  // Convert angle to radians for direction calculation
#ifdef ISOMETRIC
  float angle_rad = (player->angle + 45*3) * M_PI / 180.0f + 0.001f;
  float pitch_rad = 60/*player->pitch*/ * M_PI / 180.0f;
#else
  float angle_rad = player->angle * M_PI / 180.0f + 0.001f;
  float pitch_rad = player->pitch * M_PI / 180.0f;
#endif
  
  // Calculate looking direction vector
  float look_dir_x = -cos(angle_rad);
  float look_dir_y = sin(angle_rad);
  float look_dir_z = sin(pitch_rad);
  
  float camera_dist = 500;
  
  // Scale the horizontal component of the look direction by the cosine of the pitch
  // This prevents the player from moving faster when looking up or down
  float cos_pitch = cos(pitch_rad);
  look_dir_x *= cos_pitch;
  look_dir_y *= cos_pitch;
  
  // Calculate look-at point
  float look_x = player->x + look_dir_x * camera_dist;
  float look_y = player->y + look_dir_y * camera_dist;
  float look_z = player->z + look_dir_z * camera_dist;
  
  // Create projection matrix
  mat4 proj;
  glm_perspective(glm_rad(PLAYER_FOV), aspect, 1.0f, 2000.0f, proj);
  
  // Create view matrix
  mat4 view;
  vec3 eye = {player->x, player->y, player->z};
  vec3 center = {look_x, look_y, look_z}; // Look vector now includes pitch
  vec3 up = {0.0f, 0.0f, 1.0f};           // Z is up in Doom
  
  // Add a small offset to prevent precision issues with axis-aligned views
  if (fabs(player->pitch) > 89.5f) {
    // Adjust up vector when looking almost straight up or down
    up[0] = -sin(angle_rad);
    up[1] = -cos(angle_rad);
    up[2] = 0.0f;
  }
  
#ifdef ISOMETRIC
  glm_lookat(center, eye, up, view);
#else
  glm_lookat(eye, center, up, view);
#endif
  
  // Combine view and projection into MVP
  glm_mat4_mul(proj, view, out);
}

void draw_floors(map_data_t const *, mapsector_t const *, viewdef_t const *);
void draw_sky(map_data_t const *map, player_t const *player, mat4 mvp);

int pixel = 0;

mapsector_t const *update_player_height(map_data_t const *map, player_t *player) {
  mapsector_t const *sector;
  if (player->sector != -1 && point_in_sector(map, player->x, player->y, player->sector)) {
    sector = &map->sectors[player->sector];
  } else {
    sector = find_player_sector(map, player->x, player->y);
  }
  if (sector) {
    player->z = sector->floorheight + EYE_HEIGHT;
    player->sector = (int)(sector-map->sectors);
  }
  return sector;
}

void draw_floor_ids(map_data_t const *, mapsector_t const *, viewdef_t const *);

static void
read_center_pixel(window_t const *win,
                  map_data_t const *map,
                  mapsector_t const *sector,
                  viewdef_t const *viewdef)
{
  R_GpuClear(GL_DEPTH_BUFFER_BIT);
  R_GpuUseProgram(ui_prog);
  R_GpuUniformMatrix4fv(ui_prog_mvp, 1, GL_FALSE, viewdef->mvp[0]);
  
  draw_floor_ids(map, sector, viewdef);
  
  struct AXsize axsz;
  axGetSize(&axsz);
  int fb_width = (int)axsz.width;
  int fb_height = (int)axsz.height;

  int x = (win->frame.x + win->frame.w / 2) * fb_width / ui_get_system_metrics(kSystemMetricScreenWidth);
  int y = fb_height - (win->frame.y + win->frame.h / 2) * fb_height / ui_get_system_metrics(kSystemMetricScreenHeight);

  R_GpuReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &pixel);
  
//  static int _pixel = 0;
//  if (pixel != _pixel) {
//    printf("%08x\n", pixel);
//    _pixel = pixel;
//  }
}

//float verticalToHorizontalFOV(float verticalFOV_deg, float aspectRatio) {
//  float verticalFOV_rad = verticalFOV_deg * (M_PI / 180.0f); // degrees to radians
//  float horizontalFOV_rad = 2.0f * atanf(tanf(verticalFOV_rad / 2.0f) * aspectRatio);
//  return horizontalFOV_rad * (180.0f / M_PI); // radians to degrees
//}

void draw_dungeon(window_t const *win, bool draw_pixel) {
  void draw_minimap(map_data_t const *, editor_state_t const *, player_t const *);
  void draw_things(map_data_t const *, viewdef_t const *, bool);
  
  game_t *game = win->userdata;
  
  if (game->map.num_vertices == 0) {
    fill_rect(get_sys_color(brControlBg), R(0, 0, win->frame.w, win->frame.h));
    return;
  }

  map_data_t const *map = &game->map;
  mapsector_t const *sector = update_player_height(map, &game->player);
  player_t *player = &game->player;
  mat4 mvp;
  
  get_view_matrix(map, player, (float)win->frame.w/(float)win->frame.h, mvp);
  
  viewdef_t viewdef={0};
  memcpy(viewdef.mvp, mvp, sizeof(mat4));
  memcpy(viewdef.viewpos, &player->x, sizeof(vec3));
  viewdef.frame = frame++;
  viewdef.time = game->last_time;
  glm_frustum_planes(mvp, viewdef.frustum);
  
  R_GpuDepthMask(GL_TRUE);
  R_GpuEnable(GL_DEPTH_TEST);

  if (draw_pixel) {
    read_center_pixel(win, map, sector, &viewdef);
  }
  
  R_GpuClear(/*GL_COLOR_BUFFER_BIT|*/GL_DEPTH_BUFFER_BIT);
#if 1
  draw_sky(map, player, mvp);
  
  R_GpuUseProgram(world_prog);
  R_GpuUniformMatrix4fv(world_prog_mvp, 1, GL_FALSE, (const float*)mvp);
  
  extern int sectors_drawn;
  sectors_drawn = 0;
//  
//  float angle_rad1 = glm_rad(player->angle - verticalToHorizontalFOV(PLAYER_FOV,(float)win->frame.w/(float)win->frame.h)/2);
//  float angle_rad2 = glm_rad(player->angle + verticalToHorizontalFOV(PLAYER_FOV,(float)win->frame.w/(float)win->frame.h)/2);
//
//  player->points[0][0] = -100 * cos(angle_rad1);
//  player->points[0][1] =  100 * sin(angle_rad1);
//  player->points[1][0] = -100 * cos(angle_rad2);
//  player->points[1][1] =  100 * sin(angle_rad2);
//  
//  memcpy(player->points2, player->points, sizeof(player->points));

  viewdef.player = *player;
  viewdef.frame = frame++;
  
  // Use portal-based rendering
  draw_bsp(map, &viewdef);
#endif
  draw_things(map, &viewdef, true);
  
//  draw_weapon(&game->player, (float)win->frame.w/(float)win->frame.h);
  
  draw_crosshair((float)win->frame.w/(float)win->frame.h);

  void set_projection(int x, int y, int w, int h);
  set_projection(0, 0, win->frame.w, win->frame.h);
  
//  result_t win_perf(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
//  win_perf((window_t*)win, evPaint, 0, NULL);
  
//  mapside_texture_t const *tex1 = get_texture(get_selected_texture());
//  mapside_texture_t const *tex2 = get_flat_texture(get_selected_flat_texture());
//  if (tex1) {
//    draw_rect(tex1->texture, 8, 8, tex1->width/2, tex1->height/2);
//  }
//  if (tex2) {
//    float w = tex2->width, h = tex2->height;
//    draw_rect(tex2->texture, win->frame.w-w-8, 8, w, h);
//  }

  extern bool mode;
  if (mode) draw_minimap(map, get_editor(), player);
}

extern bool mode;

void paint_face(map_data_t *map, bool eyedropper) {
  extern int pixel;
  if ((pixel&~PIXEL_MASK) < map->num_sidedefs) {
    switch (pixel&PIXEL_MASK) {
      case PIXEL_MID:
        if (eyedropper) {
          set_selected_texture(map->sidedefs[pixel&~PIXEL_MASK].midtexture);
        } else {
          memcpy(map->sidedefs[pixel&~PIXEL_MASK].midtexture, get_selected_texture(), sizeof(texname_t));
        }
        break;
      case PIXEL_BOTTOM:
        if (eyedropper) {
          set_selected_texture(map->sidedefs[pixel&~PIXEL_MASK].bottomtexture);
        } else {
          memcpy(map->sidedefs[pixel&~PIXEL_MASK].bottomtexture, get_selected_texture(), sizeof(texname_t));
        }
        break;
      case PIXEL_TOP:
        if (eyedropper) {
          set_selected_texture(map->sidedefs[pixel&~PIXEL_MASK].toptexture);
        } else {
          memcpy(map->sidedefs[pixel&~PIXEL_MASK].toptexture, get_selected_texture(), sizeof(texname_t));
        }
        break;
      case PIXEL_FLOOR:
        if (eyedropper) {
          set_selected_flat_texture(map->sectors[pixel&~PIXEL_MASK].floorpic);
        } else {
          memcpy(map->sectors[pixel&~PIXEL_MASK].floorpic, get_selected_flat_texture(), sizeof(texname_t));
        }
        break;
      case PIXEL_CEILING:
        if (eyedropper) {
          set_selected_flat_texture(map->sectors[pixel&~PIXEL_MASK].ceilingpic);
        } else {
          memcpy(map->sectors[pixel&~PIXEL_MASK].ceilingpic, get_selected_flat_texture(), sizeof(texname_t));
        }
        break;
    }
    build_wall_vertex_buffer(map);
    build_floor_vertex_buffer(map);
  }

}

void handle_scroll(int wheel[], map_data_t *map);
void game_tick(game_t *game);

result_t win_perf(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);
result_t win_editor(window_t *win, uint32_t msg, uint32_t wparam, void *lparam);

result_t win_game(window_t *win, uint32_t msg, uint32_t wparam, void *lparam) {
  static bool alt = false;
  static bool moved = false;
  game_t *game = win->userdata;
  switch (msg) {
    case evCreate:
      win->userdata = lparam;
      create_window("FPS", 0, MAKERECT(0, 0, 128, 64), win, win_perf, 0, NULL);
      return true;
    case evDestroy:
      free_map_data(&game->map);
      if (game->state.vao) R_GpuDeleteVertexArrays(1, &game->state.vao);
      if (game->state.vbo) R_GpuDeleteBuffers(1, &game->state.vbo);
      if (g_game == game) g_game = NULL;
      free(game);
      win->userdata = NULL;
      return true;
    case evPaint:
      game_tick(game);
      draw_dungeon(win, moved);
      if (g_ui_runtime.focused == win) {
        post_message(win, evPaint, wparam, lparam);
      }
      moved = false;
      return false;
  }

  if (g_ui_runtime.captured == win) {
    switch (msg) {
      case evKillFocus:
        g_relative_mouse_mode = false;
        return true;
      case evKeyDown:
        switch (wparam) {
          case AX_KEY_ESCAPE:
            g_relative_mouse_mode = false;
            set_capture(NULL);
            break;
          case AX_KEY_W:
          case AX_KEY_UPARROW:
            if (alt) {
              handle_scroll((int[]){0, -8}, &game->map);
            } else {
              game->player.forward_move = 1;
            }
            break;
          case AX_KEY_S:
          case AX_KEY_DOWNARROW:
            if (alt) {
              handle_scroll((int[]){0, 8}, &game->map);
            } else {
              game->player.forward_move = -1;
            }
            break;
            // Calculate strafe direction vector (perpendicular to forward)
          case AX_KEY_D:
          case AX_KEY_RIGHTARROW:
            if (alt) {
              handle_scroll((int[]){-8, 0}, &game->map);
            } else {
              game->player.strafe_move = 1;
            }
            break;
          case AX_KEY_A:
          case AX_KEY_LEFTARROW:
            if (alt) {
              handle_scroll((int[]){8, 0}, &game->map);
            } else {
              game->player.strafe_move = -1;
            }
            break;
          case AX_KEY_SHIFT:
            mode = true;
            break;
          case AX_KEY_ALT:
            alt = true;
            break;
          case AX_KEY_TAB:
            set_capture(NULL);
            g_relative_mouse_mode = false;
            win->proc = win_editor;
            invalidate_window(win);
            return true;
        }
        return true;
      case evKeyUp:
        switch (wparam) {
          case AX_KEY_W:
          case AX_KEY_UPARROW:
          case AX_KEY_S:
          case AX_KEY_DOWNARROW:
            game->player.forward_move = 0;
            break;
            // Calculate strafe direction vector (perpendicular to forward)
          case AX_KEY_D:
          case AX_KEY_RIGHTARROW:
          case AX_KEY_A:
          case AX_KEY_LEFTARROW:
            game->player.strafe_move = 0;
            break;
          case AX_KEY_SHIFT:
            mode = false;
            break;
          case AX_KEY_ALT:
            alt = false;
            break;
          default:
            break;
        }
        return true;
//      case evWheel:
//        handle_scroll((int[]){(int16_t)LOWORD(wparam), (int16_t)HIWORD(wparam)}, &game->map);
//        return true;
      case evMouseMove:
        moved = true;
        game->player.angle += ((int16_t)LOWORD((intptr_t)lparam)) * sensitivity_x;
        game->player.pitch -= ((int16_t)HIWORD((intptr_t)lparam)) * sensitivity_y;
        // Keep angle within 0-360 range
        if (game->player.angle < 0) game->player.angle += 360;
        if (game->player.angle >= 360) game->player.angle -= 360;
        // Clamp pitch to prevent flipping over
        if (game->player.pitch > 89.0f) game->player.pitch = 89.0f;
        if (game->player.pitch < -89.0f) game->player.pitch = -89.0f;
        return true;
      case evLeftButtonUp:
        paint_face(&game->map, (ui_get_mod_state() & AX_MOD_ALT) != 0);
        return true;
      case evJoyButtonDown:
        if (wparam == 0) {
          paint_face(&game->map, false);
        } else if (wparam == 1) {
          paint_face(&game->map, true);
        }
        return true;
      case evJoyAxisMotion:
        switch (LOWORD(wparam)) {
          case 0: game->player.strafe_move = ((int16_t)HIWORD(wparam))/(float)0x8000; break;
          case 1: game->player.forward_move = -((int16_t)HIWORD(wparam))/(float)0x8000; break;
          case 3: game->player.mouse_x_rel = ((int16_t)HIWORD(wparam))/1200.f; break;
          case 4: game->player.mouse_y_rel = ((int16_t)HIWORD(wparam))/1200.f; break;
        }
        return true;
    }
  } else if (g_ui_runtime.focused == win) {
    switch (msg) {
      case evLeftButtonUp:
        if (!g_relative_mouse_mode) {
          set_capture(win);
          g_relative_mouse_mode = true;
        }
        return true;
      case evKeyDown:
        switch (wparam) {
          case AX_KEY_TAB:
            set_capture(NULL);
            g_relative_mouse_mode = false;
            win->proc = win_editor;
            invalidate_window(win);
            return true;
        }
    }
  }
  return false;
}
