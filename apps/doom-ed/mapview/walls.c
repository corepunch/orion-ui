#include <mapview/gl_compat.h>
#include <cglm/cglm.h>
#include <math.h>

#include <mapview/map.h>

#define init_wall_vertex(X, Y, Z, U, V) \
write_line=true;\
map->walls.vertices[map->walls.num_vertices++] = \
(wall_vertex_t) {X,Y,Z,U*dist+u_offset,V*height+v_offset,n[0],n[1],n[2],color}

extern GLuint world_prog, ui_prog;
extern GLuint no_tex, white_tex;

float compute_normal_packed(float dx, float dy, int8_t out[3]) {
  // 2. Normal vector (perpendicular, e.g., CCW)
  float nx = -dy;
  float ny = dx;
  
  // 3. Normalize
  float length = sqrtf(nx * nx + ny * ny);
  if (length == 0.0f) {
    out[0] = out[1] = out[2] = 0; // degenerate case
    return 0;
  }
  
  nx /= length;
  ny /= length;
  
  // 4. Pack into signed 8-bit normalized (–127 to +127)
  out[0] = (int8_t)(nx * 127.0f);
  out[1] = (int8_t)(ny * 127.0f);
  out[2] = 0; // unused z channel (or could be a flag)
  
  // Optional: Clamp to [-127, 127] if paranoid
  return length;
}

void build_wall_vertex_buffer(map_data_t *map) {
  map->walls.sections = realloc(map->walls.sections, sizeof(mapsidedef2_t) * map->num_sidedefs);
  memset(map->walls.sections, 0, sizeof(mapsidedef2_t) * map->num_sidedefs);
  for (uint32_t i = 0; i < map->num_sidedefs; i++) {
    map->walls.sections[i].def = &map->sidedefs[i];
    map->walls.sections[i].sector = &map->sectors[map->sidedefs[i].sector];
  }
  
  map->walls.num_vertices = 0;
  // Create VAO for walls if not already created
  if (!map->walls.vao) {
    R_GpuGenVertexArrays(1, &map->walls.vao);
    R_GpuGenBuffers(1, &map->walls.vbo);
  }

  // Loop through all linedefs
  for (int i = 0; i < map->num_linedefs; i++) {
    maplinedef_t const *linedef = &map->linedefs[i];
    mapvertex_t const *v1 = &map->vertices[linedef->start];
    mapvertex_t const *v2 = &map->vertices[linedef->end];
    int8_t n[3];
    bool write_line = false;
    float dx = v2->x - v1->x;
    float dy = v2->y - v1->y;
    float dist = compute_normal_packed(-dx, -dy, n);

    // Skip if invalid front sidedef
//    if (linedef->sidenum[0] >= map->num_sidedefs) {
    
    mapsidedef2_t *front = NULL;
    mapsidedef2_t *back = NULL;
    bool two_sided = linedef->sidenum[1] != 0xFFFF;
    int32_t color = two_sided ? 0x00e0b000 : 0x00408040;

    // Check if there's a back side to this linedef
    if (linedef->sidenum[0] != 0xFFFF && linedef->sidenum[0] < map->num_sidedefs) {
      front = &map->walls.sections[linedef->sidenum[0]];
    }
    
    // Check if there's a back side to this linedef
    if (linedef->sidenum[1] != 0xFFFF && linedef->sidenum[1] < map->num_sidedefs) {
      back = &map->walls.sections[linedef->sidenum[1]];
    }
    
    // Process front side
    if (front) {
      float u_offset = front->def->textureoffset;
      float v_offset = front->def->rowoffset;
      
      // Add vertices for upper texture (if ceiling heights differ)
      if (back && front->sector->ceilingheight > back->sector->ceilingheight) {
        // Store vertex_start in the appropriate structure (this would need to be added to sidedef)
        front->upper_section.vertex_start = map->walls.num_vertices;
        front->upper_section.vertex_count = 4;
        front->upper_section.texture = get_texture(front->def->toptexture);
        
        float height = fabs((float)(back->sector->ceilingheight - front->sector->ceilingheight));
        
        init_wall_vertex(v1->x, v1->y, back->sector->ceilingheight, 0.0f, 1.0f);
        init_wall_vertex(v2->x, v2->y, back->sector->ceilingheight, 1.0f, 1.0f);
        init_wall_vertex(v2->x, v2->y, front->sector->ceilingheight, 1.0f, 0.0f);
        init_wall_vertex(v1->x, v1->y, front->sector->ceilingheight, 0.0f, 0.0f);
      }
      
      // Add vertices for lower texture (if floor heights differ)
      if (back && front->sector->floorheight < back->sector->floorheight) {
        front->lower_section.vertex_start = map->walls.num_vertices;
        front->lower_section.vertex_count = 4;
        front->lower_section.texture = get_texture(front->def->bottomtexture);
        
        float height = fabs((float)(back->sector->floorheight - front->sector->floorheight));
        
        init_wall_vertex(v1->x, v1->y, front->sector->floorheight, 0.0f, 1.0f);
        init_wall_vertex(v2->x, v2->y, front->sector->floorheight, 1.0f, 1.0f);
        init_wall_vertex(v2->x, v2->y, back->sector->floorheight, 1.0f, 0.0f);
        init_wall_vertex(v1->x, v1->y, back->sector->floorheight, 0.0f, 0.0f);
      }
      
      // Add vertices for middle texture
      if (get_texture(front->def->midtexture)) {
        front->mid_section.vertex_start = map->walls.num_vertices;
        front->mid_section.vertex_count = 4;
        front->mid_section.texture = get_texture(front->def->midtexture);
        
        float bottom = back ? MAX(front->sector->floorheight, back->sector->floorheight) : front->sector->floorheight;
        float top = back ? MIN(front->sector->ceilingheight, back->sector->ceilingheight) : front->sector->ceilingheight;
        float height = fabs(top - bottom);
        
        init_wall_vertex(v1->x, v1->y, bottom, 0.0f, 1.0f);
        init_wall_vertex(v2->x, v2->y, bottom, 1.0f, 1.0f);
        init_wall_vertex(v2->x, v2->y, top, 1.0f, 0.0f);
        init_wall_vertex(v1->x, v1->y, top, 0.0f, 0.0f);
      }
    }
    
    // Process back side if it exists
    if (back) {
      float u_offset = back->def->textureoffset;
      float v_offset = back->def->rowoffset;
      // Add vertices for upper texture (if ceiling heights differ)
      if (back->sector->ceilingheight > front->sector->ceilingheight) {
        back->upper_section.vertex_start = map->walls.num_vertices;
        back->upper_section.vertex_count = 4;
        back->upper_section.texture = get_texture(back->def->toptexture);
        
        float height = fabs((float)(back->sector->ceilingheight - front->sector->ceilingheight));
        
        init_wall_vertex(v2->x, v2->y, front->sector->ceilingheight, 0.0f, 1.0f);
        init_wall_vertex(v1->x, v1->y, front->sector->ceilingheight, 1.0f, 1.0f);
        init_wall_vertex(v1->x, v1->y, back->sector->ceilingheight, 1.0f, 0.0f);
        init_wall_vertex(v2->x, v2->y, back->sector->ceilingheight, 0.0f, 0.0f);
      }
      
      // Add vertices for lower texture (if floor heights differ)
      if (back->sector->floorheight < front->sector->floorheight) {
        back->lower_section.vertex_start = map->walls.num_vertices;
        back->lower_section.vertex_count = 4;
        back->lower_section.texture = get_texture(back->def->bottomtexture);
        
        float height = fabs((float)(back->sector->floorheight - front->sector->floorheight));
        
        init_wall_vertex(v2->x, v2->y, back->sector->floorheight, 0.0f, 1.0f);
        init_wall_vertex(v1->x, v1->y, back->sector->floorheight, 1.0f, 1.0f);
        init_wall_vertex(v1->x, v1->y, front->sector->floorheight, 1.0f, 0.0f);
        init_wall_vertex(v2->x, v2->y, front->sector->floorheight, 0.0f, 0.0f);
      }
      
      // Add vertices for middle texture
      if (get_texture(back->def->midtexture)) {
        back->mid_section.vertex_start = map->walls.num_vertices;
        back->mid_section.vertex_count = 4;
        back->mid_section.texture = get_texture(back->def->midtexture);
        
        float bottom = MAX(front->sector->floorheight, back->sector->floorheight);
        float top = MIN(front->sector->ceilingheight, back->sector->ceilingheight);
        float height = fabs(top - bottom);
        
        init_wall_vertex(v2->x, v2->y, bottom, 0.0f, 1.0f);
        init_wall_vertex(v1->x, v1->y, bottom, 1.0f, 1.0f);
        init_wall_vertex(v1->x, v1->y, top, 1.0f, 0.0f);
        init_wall_vertex(v2->x, v2->y, top, 0.0f, 0.0f);
      }
    }
    
    if (!write_line) {
      int32_t color = 0x00ffff00;
      float height = 0;
      float u_offset = 0;
      float v_offset = 0;
      init_wall_vertex(v1->x, v1->y, 0, 0.0f, 1.0f);
      init_wall_vertex(v2->x, v2->y, 0, 1.0f, 1.0f);
      init_wall_vertex(v2->x, v2->y, 0, 1.0f, 0.0f);
      init_wall_vertex(v1->x, v1->y, 0, 0.0f, 0.0f);
    }
  }
  
  // Upload vertex data to GPU
  R_GpuBindVertexArray(map->walls.vao);
  R_GpuBindBuffer(GL_ARRAY_BUFFER, map->walls.vbo);
  R_GpuBufferData(GL_ARRAY_BUFFER, map->walls.num_vertices * sizeof(wall_vertex_t), map->walls.vertices, GL_STATIC_DRAW);
  
  // Set up vertex attributes
  R_GpuVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, sizeof(wall_vertex_t), OFFSET_OF(wall_vertex_t, x)); // Position
  R_GpuEnableVertexAttribArray(0);
  R_GpuVertexAttribPointer(1, 2, GL_SHORT, GL_FALSE, sizeof(wall_vertex_t), OFFSET_OF(wall_vertex_t, u)); // UV
  R_GpuEnableVertexAttribArray(1);
  R_GpuVertexAttribPointer(2, 3, GL_BYTE, GL_TRUE, sizeof(wall_vertex_t), OFFSET_OF(wall_vertex_t, nx)); // Normal
  R_GpuEnableVertexAttribArray(2);
  R_GpuVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(wall_vertex_t), OFFSET_OF(wall_vertex_t, color)); // Color
  R_GpuEnableVertexAttribArray(3);

//  printf("Built wall vertex buffer with %d vertices\n", map->walls.num_vertices);
}

// Helper function to draw a textured quad from the wall vertex buffer
void draw_textured_surface(wall_section_t const *surface, float light, int mode) {
  if (surface->texture) {
    R_GpuBindTexture(GL_TEXTURE_2D, surface->texture->texture);
    R_GpuUniform2f(world_prog_tex0_size, surface->texture->width, surface->texture->height);
  } else {
    R_GpuBindTexture(GL_TEXTURE_2D, no_tex);
    R_GpuUniform2f(world_prog_tex0_size, 1, 1);
  }
  R_GpuUniform1i(world_prog_tex0, 0);
  R_GpuUniform1f(world_prog_light, light);
  
  // Draw 4 vertices starting at vertex_start
  R_GpuDrawArrays(mode, surface->vertex_start, surface->vertex_count);
}

// Draw the map from player perspective
void draw_walls(map_data_t const *map,
                mapsector_t const *sector,
                viewdef_t const *viewdef)
{
  // Bind the wall VAO
  R_GpuBindVertexArray(map->walls.vao);
  R_GpuDisable(GL_BLEND);

  R_GpuUniformMatrix4fv(world_prog_mvp, 1, GL_FALSE, viewdef->mvp[0]);
  R_GpuUniform3fv(world_prog_viewPos, 1, viewdef->viewpos);
  
  // Draw linedefs
  for (int i = 0; i < map->num_linedefs; i++) {
    maplinedef_t const *linedef = &map->linedefs[i];
    
    // Check if front sidedef exists
    if (linedef->sidenum[0] >= map->num_sidedefs) {
      continue; // Skip if invalid front sidedef
    }
    
    mapsidedef2_t const *front = &map->walls.sections[linedef->sidenum[0]];
    float light = front->sector->lightlevel / 255.0f;
    
    extern int pixel;

    if (front->sector == sector) {
      // Draw front side
      if (CHECK_PIXEL(pixel, TOP, linedef->sidenum[0])) {
        draw_textured_surface(&front->upper_section, HIGHLIGHT(light), GL_TRIANGLE_FAN);
      } else if (front->upper_section.texture || strncmp(front->sector->ceilingpic, "F_SKY", 5)) {
        draw_textured_surface(&front->upper_section, light, GL_TRIANGLE_FAN);
      }
      if (CHECK_PIXEL(pixel, BOTTOM, linedef->sidenum[0])) {
        draw_textured_surface(&front->lower_section, HIGHLIGHT(light), GL_TRIANGLE_FAN);
      } else {
        draw_textured_surface(&front->lower_section, light, GL_TRIANGLE_FAN);
      }
      if (CHECK_PIXEL(pixel, MID, linedef->sidenum[0])) {
        draw_textured_surface(&front->mid_section, HIGHLIGHT(light), GL_TRIANGLE_FAN);
      } else {
        draw_textured_surface(&front->mid_section, light, GL_TRIANGLE_FAN);
      }
    }

    // Draw back side if it exists
    if (linedef->sidenum[1] != 0xFFFF && linedef->sidenum[1] < map->num_sidedefs) {
      mapsidedef2_t const *back = &map->walls.sections[linedef->sidenum[1]];
      if (back->sector == sector) {
        draw_textured_surface(&back->upper_section, light, GL_TRIANGLE_FAN);
        draw_textured_surface(&back->lower_section, light, GL_TRIANGLE_FAN);
        draw_textured_surface(&back->mid_section, light, GL_TRIANGLE_FAN);
      }
    }
  }
  
  // Reset texture binding
  R_GpuBindTexture(GL_TEXTURE_2D, 0);
}

// Helper function to draw a textured quad from the wall vertex buffer
void draw_textured_surface_id(wall_section_t const *surface, uint32_t id, int mode) {
  uint8_t *c = (uint8_t *)&id;

  R_GpuBindTexture(GL_TEXTURE_2D, white_tex);
  R_GpuUniform1i(ui_prog_tex0, 0);
  R_GpuUniform2f(ui_prog_tex0_size, 1, 1);
  R_GpuUniform4f(ui_prog_color, c[0]/255.f, c[1]/255.f, c[2]/255.f, c[3]/255.f);
  
  // Draw 4 vertices starting at vertex_start
  R_GpuDrawArrays(mode, surface->vertex_start, surface->vertex_count);
}

void
draw_wall_ids(map_data_t const *map,
              mapsector_t const *sector,
              viewdef_t const *viewdef)
{
  R_GpuDisable(GL_BLEND);
  R_GpuBindVertexArray(map->walls.vao);
  R_GpuDisableVertexAttribArray(3);
  R_GpuVertexAttrib4f(3, 0, 0, 0, 0);
  // Draw linedefs
  for (int i = 0; i < map->num_linedefs; i++) {
    maplinedef_t const *linedef = &map->linedefs[i];
    
    // Check if front sidedef exists
    if (linedef->sidenum[0] != 0xFFFF) {
      uint32_t sidenum = linedef->sidenum[0];
      mapsidedef2_t const *front = &map->walls.sections[linedef->sidenum[0]];
      if (front->sector == sector) {
        draw_textured_surface_id(&front->upper_section, sidenum|PIXEL_TOP, GL_TRIANGLE_FAN);
        draw_textured_surface_id(&front->lower_section, sidenum|PIXEL_BOTTOM, GL_TRIANGLE_FAN);
        draw_textured_surface_id(&front->mid_section, sidenum|PIXEL_MID, GL_TRIANGLE_FAN);
      }
    }

    // Draw back side if it exists
    if (linedef->sidenum[1] != 0xFFFF) {
      uint32_t sidenum = linedef->sidenum[1];
      mapsidedef2_t const *back = &map->walls.sections[linedef->sidenum[1]];
      if (back->sector == sector) {
        draw_textured_surface_id(&back->upper_section, sidenum|PIXEL_TOP, GL_TRIANGLE_FAN);
        draw_textured_surface_id(&back->lower_section, sidenum|PIXEL_BOTTOM, GL_TRIANGLE_FAN);
        draw_textured_surface_id(&back->mid_section, sidenum|PIXEL_MID, GL_TRIANGLE_FAN);
      }
    }
  }
  
  // Reset texture binding
  R_GpuEnableVertexAttribArray(3);
  R_GpuBindTexture(GL_TEXTURE_2D, 0);
}

