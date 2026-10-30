#include <mapview/gl_compat.h>
#include <cglm/cglm.h>
#include <math.h>

#include <editor/editor.h>
#include <mapview/sprites.h>

// Globals
extern GLuint ui_prog;
extern GLuint white_tex;
extern unsigned frame;

#define COLOR_HOVER 0xff00ffff
#define COLOR_SELECTED 0xffffff00
#define COLOR_SELECTED_HOVER 0xffffffff

void set_editor_color(int prog, uint32_t value) {
  uint8_t const *c = (uint8_t*)&value;
  R_GpuUniform4f(ui_prog_color, c[0]/255.f, c[1]/255.f, c[2]/255.f, c[3]/255.f);
}

// Initialize editor state
void init_editor(editor_state_t *editor) {
  editor->grid_size = 32;
  editor->drawing = false;
  editor->num_draw_points = 0;
  editor->scale = 1;
  
  // Create VAO and VBO for editor rendering
  R_GpuGenVertexArrays(1, &editor->vao);
  R_GpuGenBuffers(1, &editor->vbo);
  
  // Set up VAO
  R_GpuBindVertexArray(editor->vao);
  R_GpuBindBuffer(GL_ARRAY_BUFFER, editor->vbo);
  
  // Enable vertex attributes
  R_GpuEnableVertexAttribArray(0); // Position
  R_GpuEnableVertexAttribArray(1); // Texture coords
//  R_GpuEnableVertexAttribArray(3); // Color

  // Reset bindings
  R_GpuBindVertexArray(0);
  R_GpuBindBuffer(GL_ARRAY_BUFFER, 0);
}

void set_editor_camera(editor_state_t *editor, int16_t x, int16_t y) {
  editor->camera[0] = x;
  editor->camera[1] = y;
}

// Draw the grid
static void draw_grid(int grid_size, player_t const *player, int view_size) {
  int start_x = ((int)player->x / grid_size - view_size) * grid_size;
  int end_x = ((int)player->x / grid_size + view_size) * grid_size;
  int start_y = ((int)player->y / grid_size - view_size) * grid_size;
  int end_y = ((int)player->y / grid_size + view_size) * grid_size;
  
  // Set grid color (dark gray)
  R_GpuUniform4f(ui_prog_color, .15f, .15f, 0.15f, 1.0f);
  R_GpuEnable(GL_BLEND);
  R_GpuBlendFunc(GL_ONE, GL_ONE);
  
    // Draw vertical grid lines
    for (int x = start_x; x <= end_x; x += grid_size) {
      // Create two vertices for each grid line
      wall_vertex_t verts[2] = {
        { x, start_y, 0, 0, 0, 0, 0, 0 },
        { x, end_y, 0, 0, 0, 0, 0, 0 }
      };
  
      // Draw the line
      R_GpuVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, sizeof(wall_vertex_t), &verts[0].x);
      R_GpuVertexAttribPointer(1, 2, GL_SHORT, GL_FALSE, sizeof(wall_vertex_t), &verts[0].u);
      R_GpuDrawArrays(GL_LINES, 0, 2);
    }
  
    // Draw horizontal grid lines
    for (int y = start_y; y <= end_y; y += grid_size) {
      // Create two vertices for each grid line
      wall_vertex_t verts[2] = {
        { start_x, y, 0, 0, 0, 0, 0, 0 },
        { end_x, y, 0, 0, 0, 0, 0, 0 }
      };
  
      // Draw the line
      R_GpuVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, sizeof(wall_vertex_t), &verts[0].x);
      R_GpuVertexAttribPointer(1, 2, GL_SHORT, GL_FALSE, sizeof(wall_vertex_t), &verts[0].u);
      R_GpuDrawArrays(GL_LINES, 0, 2);
    }
  
  // Draw dots at grid intersections
//  for (int y = start_y; y <= end_y; y += grid_size) {
//    for (int x = start_x; x <= end_x; x += grid_size) {
//      wall_vertex_t vert = { x, y, 0, 0, 0, 0, 0, 0 };
//      
//      // Set up vertex attributes
//      R_GpuVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, sizeof(wall_vertex_t), &vert.x);
//      R_GpuVertexAttribPointer(1, 2, GL_SHORT, GL_FALSE, sizeof(wall_vertex_t), &vert.u);
//
//      // Draw a single point
//      R_GpuPointSize(!((x/grid_size) % 4)&&!((y/grid_size) % 4) ? 2.0f : 1.f);
//      R_GpuDrawArrays(GL_POINTS, 0, 1);
//    }
//  }
  R_GpuDisable(GL_BLEND);
}

extern GLuint no_tex;
//
//static void draw_floors_ids_editor(map_data_t const *map) {
//  R_GpuBindTexture(GL_TEXTURE_2D, white_tex);
//  R_GpuBindTexture(GL_TEXTURE_2D, white_tex);
//  R_GpuUniform1i(ui_prog_tex0, 0);
//  R_GpuUniform2f(ui_prog_tex0_size, 1, 1);
//  for (int i = 0; i < map->num_sectors; i++) {
//    int id = i;// | PIXEL_FLOOR;
//    uint8_t *c = (uint8_t *)&id;
//    R_GpuUniform4f(ui_prog_color, c[0]/255.f, c[1]/255.f, c[2]/255.f, c[3]/255.f);
//    R_GpuDrawArrays(GL_TRIANGLES,
//                 map->floors.sectors[i].floor.vertex_start,
//                 map->floors.sectors[i].floor.vertex_count);
//  }
//}

static void draw_floors_editor(map_data_t const *map) {
  GLuint tex0_size = R_GpuGetUniformLocation(ui_prog, "tex0_size");
  
  R_GpuUniform4f(ui_prog_color, 1.0f, 1.0f, 1.0f, 1.0f);
  R_GpuBindVertexArray(map->floors.vao);
  
  for (int i = 0; i < map->num_sectors; i++) {
    if (map->floors.sectors[i].floor.texture) {
      mapside_texture_t const *tex = map->floors.sectors[i].floor.texture;
      R_GpuBindTexture(GL_TEXTURE_2D, tex->texture);
      R_GpuUniform2f(tex0_size, tex->width, tex->height);
    } else {
      R_GpuBindTexture(GL_TEXTURE_2D, no_tex);
    }
    R_GpuDrawArrays(GL_TRIANGLES,
                 map->floors.sectors[i].floor.vertex_start,
                 map->floors.sectors[i].floor.vertex_count);
  }
}

// Draw walls with different colors based on whether they're one-sided or two-sided
static void
draw_walls_editor(editor_state_t const *editor,
                  map_data_t const *map)
{
#if 1
  R_GpuBindTexture(GL_TEXTURE_2D, white_tex); // Use default 1x1 white texture
  R_GpuUniform4f(ui_prog_color, 1.0f, 1.0f, 1.0f, 1.0f);
  R_GpuBindVertexArray(map->walls.vao);
  R_GpuDrawArrays(GL_LINES, 0, map->walls.num_vertices);

  R_GpuBindVertexArray(editor->vao);
  R_GpuBindBuffer(GL_ARRAY_BUFFER, editor->vbo);
  R_GpuVertexAttribPointer(0, 2, GL_SHORT, GL_FALSE, 4, map->vertices);
  R_GpuDisableVertexAttribArray(1);
  R_GpuDisableVertexAttribArray(2);
  R_GpuDisableVertexAttribArray(3);
  R_GpuPointSize(3.0f);
  R_GpuDrawArrays(GL_POINTS, 0, map->num_vertices);
  R_GpuPointSize(1.0f);
#else
  // Draw each linedef
  for (int i = 0; i < map->num_linedefs; i++) {
    maplinedef_t const *linedef = &map->linedefs[i];
    mapvertex_t const *v1 = &map->vertices[linedef->start];
    mapvertex_t const *v2 = &map->vertices[linedef->end];
    
    // Determine if this is a one-sided or two-sided wall
    bool two_sided = linedef->sidenum[1] != 0xFFFF;
    // Set color - white for one-sided (outer) walls, red for two-sided (inner) walls
    if (editor->hover.line == i) {
      R_GpuUniform4f(ui_prog_color, 1.0f, 1.0f, 0.0f, 1.0f);
    } else if (!two_sided) {
      R_GpuUniform4f(ui_prog_color, 1.0f, 1.0f, 1.0f, 1.0f);
    } else {
      R_GpuUniform4f(ui_prog_color, 1.0f, 0.0f, 0.0f, 1.0f);
    }
    
    int16_t z = 0;
    
    if (linedef->sidenum[0] != 0xFFFF) {
      z = map->sectors[map->sidedefs[linedef->sidenum[0]].sector].floorheight;
    }
    
    // Create and draw vertices
    wall_vertex_t verts[2] = {
      { v1->x, v1->y, z, 0, 0, 0, 0, 0 },
      { v2->x, v2->y, z, 0, 0, 0, 0, 0 }
    };
    
    R_GpuVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, sizeof(wall_vertex_t), &verts[0].x);
    R_GpuVertexAttribPointer(1, 2, GL_SHORT, GL_FALSE, sizeof(wall_vertex_t), &verts[0].u);
    R_GpuDrawArrays(GL_LINES, 0, 2);
    
    // Draw vertices as points
    R_GpuPointSize(3.0f);
    R_GpuVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, sizeof(wall_vertex_t), &verts[0].x);
    R_GpuDrawArrays(GL_POINTS, 0, 1);
    R_GpuVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, sizeof(wall_vertex_t), &verts[1].x);
    R_GpuDrawArrays(GL_POINTS, 0, 1);
    R_GpuPointSize(1.0f);
  }
#endif
}

// Draw cursor position
static void draw_cursor(int x, int y) {
  // Set cursor color (green)
  R_GpuUniform4f(ui_prog_color, 0.0f, 1.0f, 0.0f, 1.0f);

//  wall_vertex_t verts = { x, y, 0, 0, 0, 0, 0, 0 };
//  R_GpuPointSize(3.0f);
//  R_GpuVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, sizeof(wall_vertex_t), &verts.x);
//  R_GpuVertexAttribPointer(1, 2, GL_SHORT, GL_FALSE, sizeof(wall_vertex_t), &verts.u);
//  R_GpuDrawArrays(GL_POINTS, 0, 1);

  // Draw crosshair
  int size = 16;
  wall_vertex_t verts[4] = {
    { x - size, y, 0, 0, 0, 0, 0, 0 },
    { x + size, y, 0, 0, 0, 0, 0, 0 },
    { x, y - size, 0, 0, 0, 0, 0, 0 },
    { x, y + size, 0, 0, 0, 0, 0, 0 }
  };
  
  R_GpuBindTexture(GL_TEXTURE_2D, white_tex);
  
  R_GpuVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, sizeof(wall_vertex_t), &verts[0].x);
  R_GpuVertexAttribPointer(1, 2, GL_SHORT, GL_FALSE, sizeof(wall_vertex_t), &verts[0].u);
  R_GpuDrawArrays(GL_LINES, 0, 4);
}

void get_mouse_position(window_t *, editor_state_t const *, int16_t const *screen, mat4 const, vec3);
void snap_mouse_position(editor_state_t const *, vec2 const, mapvertex_t *);
void get_editor_mvp(editor_state_t const *editor, mat4 mvp) {
  // Set up orthographic projection for 2D view
  float w = editor->window->frame.w * editor->scale;
  float h = editor->window->frame.h * editor->scale;
  
  // Create projection and view matrices
  mat4 proj, view;
  glm_ortho(-w, w, -h, h, -1000, 1000, proj);
  
  // Center view on player
  glm_translate_make(view, (vec3) {
    -editor->camera[0],
    -editor->camera[1],
    0.0f
  });
  
  // Combine matrices
  glm_mat4_mul(proj, view, mvp);
}

void draw_line_ex(map_data_t const *map, int x1, int y1, int x2, int y2) {
  wall_vertex_t verts[2] = { {x1,y1}, {x2,y2} };
  R_GpuVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, sizeof(wall_vertex_t), &verts[0].x);
  R_GpuVertexAttribPointer(1, 2, GL_SHORT, GL_FALSE, sizeof(wall_vertex_t), &verts[0].u);
  R_GpuVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(wall_vertex_t), &verts[0].color);
  R_GpuDrawArrays(GL_LINES, 0, 2);
}

void draw_line(map_data_t const *map, uint16_t i) {
  if (i >= map->num_linedefs) return;
  draw_line_ex(map,
    map->vertices[map->linedefs[i].start].x,
    map->vertices[map->linedefs[i].start].y,
    map->vertices[map->linedefs[i].end].x,
    map->vertices[map->linedefs[i].end].y);
}

void draw_square(map_data_t const *map, int x, int y, int w, int h) {
  draw_line_ex(map, x - w, y - h, x + w, y - h);
  draw_line_ex(map, x + w, y - h, x + w, y + h);
  draw_line_ex(map, x - w, y + h, x + w, y + h);
  draw_line_ex(map, x - w, y - h, x - w, y + h);
}

void draw_thing_outline(map_data_t const *map, uint16_t index) {
  if (index >= map->num_things) return;
  mapthing_t const *thing = &map->things[index];
  sprite_t *spr = get_thing_sprite_name(thing->type, 0);
  if (!spr) return;
  draw_square(map, thing->x, thing->y, spr->width/2, spr->height/2);
}

void draw_vertex_outline(map_data_t const *map, uint16_t index) {
  if (index >= map->num_vertices) return;
  mapvertex_t const *vertex = &map->vertices[index];
  draw_square(map, vertex->x, vertex->y, 8, 8);
}

void draw_sector_outline(map_data_t const *map, uint16_t index) {
  for (int i = 0; i < map->num_linedefs; i++) {
    maplinedef_t const *ld = &map->linedefs[i];
    if ((ld->sidenum[0] != 0xFFFF && map->sidedefs[ld->sidenum[0]].sector == index) ||
        (ld->sidenum[1] != 0xFFFF && map->sidedefs[ld->sidenum[1]].sector == index))
    {
      draw_line(map, i);
    }
  }
}

void draw_player_icon(editor_state_t const *editor, const player_t *player) {
  R_GpuUseProgram(ui_prog);
  R_GpuBindVertexArray(editor->vao);
  R_GpuBindBuffer(GL_ARRAY_BUFFER, editor->vbo);
  R_GpuDisable(GL_DEPTH_TEST);
  
  R_GpuUniform4f(ui_prog_color, 1.0f, 1.0f, 0.0f, 0.5f);
  
  float angle_rad = glm_rad(player->angle);
  float shaft_length = 20.0f;
  float head_length = 20.0f;
  float head_angle = glm_rad(30.0f); // 30 degrees for arrowhead lines
  
  float dx = -shaft_length * cosf(angle_rad);
  float dy =  shaft_length * sinf(angle_rad);
  
  float tip_x = player->x + dx;
  float tip_y = player->y + dy;
  
  // Arrowhead lines
  float left_dx = head_length * cosf(angle_rad + head_angle);
  float left_dy = -head_length * sinf(angle_rad + head_angle);
  float right_dx = head_length * cosf(angle_rad - head_angle);
  float right_dy = -head_length * sinf(angle_rad - head_angle);
  
  wall_vertex_t verts[6] = {
    // Shaft
    { player->x - dx, player->y - dy },
    { tip_x, tip_y },
    // Left head
    { tip_x, tip_y },
    { tip_x + left_dx, tip_y + left_dy },
    // Right head
    { tip_x, tip_y },
    { tip_x + right_dx, tip_y + right_dy },
  };
  
  R_GpuBindTexture(GL_TEXTURE_2D, white_tex);
  R_GpuVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, sizeof(wall_vertex_t), &verts[0].x);
  R_GpuVertexAttribPointer(1, 2, GL_SHORT, GL_FALSE, sizeof(wall_vertex_t), &verts[0].u);
  R_GpuVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(wall_vertex_t), &verts[0].color);
  R_GpuDrawArrays(GL_LINES, 0, 6);
  
//  R_GpuPointSize(4);
//  R_GpuDrawArrays(GL_POINTS, 0, 1);
}

static viewdef_t setup_matrix(vec4 *mvp, const player_t *player) {
  extern GLuint world_prog;
  R_GpuUseProgram(world_prog);
  R_GpuUniformMatrix4fv(world_prog_mvp, 1, GL_FALSE, (const float*)mvp);
  R_GpuUniform3f(world_prog_viewPos, 0,0,-10001);//player->x, player->y, player->z);
  
  viewdef_t viewdef = {.frame=frame++};
  memcpy(viewdef.mvp, mvp, sizeof(mat4));
  memcpy(viewdef.viewpos, &player->x, sizeof(vec3));
  viewdef.viewpos[2] = -100000;
  glm_frustum_planes(mvp, viewdef.frustum);
  
  // Set up rendering
  R_GpuUseProgram(ui_prog);
  R_GpuUniformMatrix4fv(ui_prog_mvp, 1, GL_FALSE, (const float*)mvp);
  
  R_GpuDisable(GL_DEPTH_TEST);
  
  return viewdef;
}

static void
draw_selection(const editor_selection_t *selected,
               const map_data_t *map,
               uint32_t color)
{
  set_editor_color(ui_prog_color, color);
  switch (selected->type) {
    case ObjTypeLine:
      draw_line(map, selected->index);
      break;
    case ObjTypeSector:
      draw_sector_outline(map, selected->index);
      break;
    case ObjTypeThing:
      draw_thing_outline(map, selected->index);
      break;
    case ObjTypePoint:
      draw_vertex_outline(map, selected->index);
      break;
    default:
      break;
  }
}

// Draw the editor UI
void
draw_editor(window_t *win,
            map_data_t const *map,
            editor_state_t const *editor,
            player_t const *player)
{
  mat4 mvp;
  get_editor_mvp(editor, mvp);

//  void get_view_matrix(map_data_t const *map, player_t const *player, mat4 out);
//  get_view_matrix(map, player, mvp);
  
  // Clear the screen to black
//  R_GpuClearColor(0.0f, 0.0f, 0.0f, 1.0f);
//  R_GpuClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
//  void draw_minimap(map_data_t const *map, player_t const *player);
//  draw_minimap(map, player);
//  R_GpuBindVertexArray(map->walls.vao);
//  R_GpuDrawArrays(GL_LINES, 0, map->walls.num_vertices);
  
  
  viewdef_t viewdef = setup_matrix(mvp, player);
  
  // Draw grid
//  draw_grid(editor->grid_size, player, 64);

  draw_floors_editor(map);

  // Draw existing walls
  draw_walls_editor(editor, map);

  R_GpuBindVertexArray(editor->vao);
  R_GpuBindBuffer(GL_ARRAY_BUFFER, editor->vbo);

  void draw_things(map_data_t const *map, viewdef_t const *viewdef, bool rotate);
  draw_things(map, &viewdef, false);
  
  R_GpuUseProgram(ui_prog);
  R_GpuBindVertexArray(editor->vao);
  R_GpuBindBuffer(GL_ARRAY_BUFFER, editor->vbo);
  R_GpuDisable(GL_DEPTH_TEST);
  
//  if (editor->window != active) {
//    goto draw_player;
//  }
  
//  if (editor->sel_mode == EditModeVertices || editor->sel_mode == EditModeLines) {
  if (editor->sel_mode == EditModeVertices) {
    draw_cursor(editor->sn.x, editor->sn.y);
  }
//    } else {
//      ((editor_state_t*)editor)->hover.index = -1;
//      if (editor->sel_mode == EditModeVertices) {
//        // Snap to grid
//        snap_mouse_position(editor, world, &editor->sn);
//        // Draw cursor at the snapped position
//        draw_cursor(editor->sn.x, editor->sn.y);
//      }
//    }
//  }

  R_GpuBindTexture(GL_TEXTURE_2D, white_tex);
  bool sel = !memcmp(&editor->hover, &editor->selected, sizeof(editor->hover));

  draw_selection(&editor->selected, map, COLOR_SELECTED);
  draw_selection(&editor->hover, map, sel ? COLOR_SELECTED_HOVER : COLOR_HOVER);
  
  switch (editor->sel_mode) {
    case EditModeVertices:
    case EditModeLines:
      // If currently drawing, show line from last point to cursor
      if ((editor->dragging || editor->drawing) &&
          has_selection(editor->selected, ObjTypePoint))
      {
        float x = map->vertices[editor->selected.index].x;
        float y = map->vertices[editor->selected.index].y;
        draw_line_ex(map, x, y, editor->sn.x, editor->sn.y);
      }
      break;
  }
  
draw_player:
  draw_player_icon(editor, player);
}

#define MINIMAP_SCALE 2
#define ROTATE_MAP

void minimap_matrix(player_t const *player, mat4 mvp) {
  mat4 proj, view;
  
  float w = ui_get_system_metrics(kSystemMetricScreenWidth)*MINIMAP_SCALE;
  float h = ui_get_system_metrics(kSystemMetricScreenHeight)*MINIMAP_SCALE;
  
  glm_ortho(w, -w, h, -h, -1000, 1000, proj);
  
  // Set up view matrix to center on player
#ifndef ROTATE_MAP
  glm_translate_make(view, (vec3){ -player->x, -player->y, 0.0f });
#else
  mat4 trans, rot;
  // Set up the translation matrix to center on the player
  glm_translate_make(trans, (vec3){ -player->x, -player->y, 0.0f });
  
  // Step 2: Rotate the world so player's angle is at the top
  float angle_rad = glm_rad(player->angle+90); // rotate counter-clockwise to make player face up
  glm_rotate_make(rot, angle_rad, (vec3){0.0f, 0.0f, 1.0f});
  
  // Step 3: Combine rotation and translation
  glm_mat4_mul(rot, trans, view); // view = rot * trans
#endif
  // Step 4: Combine with projection
  glm_mat4_mul(proj, view, mvp);  // mvp = proj * view
}

void draw_minimap(map_data_t const *map,
                  editor_state_t const *editor,
                  player_t const *player) {
  
  mat4 mvp;
//  minimap_matrix(player, mvp);
  get_editor_mvp(editor, mvp);
  
  R_GpuUseProgram(ui_prog);
  R_GpuBindTexture(GL_TEXTURE_2D, no_tex);
  R_GpuUniformMatrix4fv(ui_prog_mvp, 1, GL_FALSE, (const float*)mvp);
  R_GpuUniform4f(ui_prog_color, 1.0f, 1.0f, 1.0f, 0.25f);
  R_GpuDisable(GL_DEPTH_TEST);
  R_GpuEnable(GL_BLEND);
  R_GpuBlendFunc(GL_SRC_ALPHA, GL_ONE);
  R_GpuBindVertexArray(map->walls.vao);
  R_GpuDrawArrays(GL_LINES, 0, map->walls.num_vertices);
  R_GpuDisable(GL_BLEND);
  
  draw_player_icon(editor, player);
}
