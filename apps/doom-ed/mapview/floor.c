#include <mapview/gl_compat.h>
#include <cglm/cglm.h>
#include <cglm/cglm.h>
#include <mapview/map.h>

// Global variables to add
extern GLuint world_prog;

#define MAX_EDGES 0x10000
#define MAX_VERTICES 1024
#define TEX_SIZE 64.0f

GLuint compile(GLenum type, const char* src);
mapside_texture_t const *get_flat_texture(const char* name);

bool point_in_frustum(vec3 point, vec4 const planes[6]) {
  for (int i = 0; i < 6; i++) {
    float distance = glm_vec3_dot(point, (float*)planes[i]) + planes[i][3];
    if (distance < 0.0f) {
      return false; // Point is outside this plane
    }
  }
  return true; // Inside all planes
}

//
// linedef_quad_in_frustum_3d
// Proper 3D frustum culling for a linedef quad (4-pointed polygon in 3D space).
// The quad is defined by two endpoints (a, b) and floor/ceiling heights.
// Tests if the quad formed by the linedef is visible in the view frustum.
//
// Quad vertices:
//   v0: (a.x, a.y, floor_height)  - bottom left
//   v1: (a.x, a.y, ceiling_height) - top left
//   v2: (b.x, b.y, floor_height)  - bottom right
//   v3: (b.x, b.y, ceiling_height) - top right
//
bool linedef_in_frustum(vec4 const frustum[6],
                        mapvertex_t a,
                        mapvertex_t b,
                        float floor_height,
                        float ceiling_height)
{
  // Build the 4 corners of the quad in 3D space
  vec3 corners[4] = {
    {a.x, a.y, floor_height},     // bottom left
    {a.x, a.y, ceiling_height},   // top left
    {b.x, b.y, floor_height},     // bottom right
    {b.x, b.y, ceiling_height}    // top right
  };
  
  // For each frustum plane, check if all corners are outside
  // If all corners are outside any single plane, the quad is not visible
  for (int plane = 0; plane < 6; plane++) {
    int outside_count = 0;
    
    for (int corner = 0; corner < 4; corner++) {
      float distance = glm_vec3_dot(corners[corner], (float*)frustum[plane]) + frustum[plane][3];
      if (distance < 0.0f) {
        outside_count++;
      }
    }
    
    // All 4 corners are outside this plane - quad is definitely not visible
    if (outside_count == 4) {
      return false;
    }
  }
  
  // At least part of the quad is potentially visible
  return true;
}

// Improved linedef-in-MVP check that handles edge cases better
bool linedef_in_mvp_2d(mat4 mvp, vec2 a, vec2 b) {
  vec4 pa = { a[0], a[1], 0.0f, 1.0f };
  vec4 pb = { b[0], b[1], 0.0f, 1.0f };
  
  // Transform points to clip space
  glm_mat4_mulv(mvp, pa, pa);
  glm_mat4_mulv(mvp, pb, pb);
  
  // Handle points behind the camera (negative w)
  if (pa[3] <= 0.0f && pb[3] <= 0.0f)
    return false;
  
  // Perspective divide for points with positive w
  if (pa[3] > 0.0f) {
    pa[0] /= pa[3]; pa[1] /= pa[3]; pa[2] /= pa[3];
  }
  
  if (pb[3] > 0.0f) {
    pb[0] /= pb[3]; pb[1] /= pb[3]; pb[2] /= pb[3];
  }
  
  // Special case: handle one point behind camera
  if (pa[3] <= 0.0f || pb[3] <= 0.0f) {
    // Line crosses near plane, so part is visible
    return true;
  }
  
  // Clipping check in normalized device coordinates
  const float EPSILON = 0.01f; // Small buffer to avoid edge cases
  bool completely_left = (pa[0] < -1.0f-EPSILON && pb[0] < -1.0f-EPSILON);
  bool completely_right = (pa[0] > 1.0f+EPSILON && pb[0] > 1.0f+EPSILON);
  bool completely_top = (pa[1] > 1.0f+EPSILON && pb[1] > 1.0f+EPSILON);
  bool completely_bottom = (pa[1] < -1.0f-EPSILON && pb[1] < -1.0f-EPSILON);
  
  // If completely outside any plane, reject
  if (completely_left || completely_right || completely_top || completely_bottom)
    return false;
  
  return true; // At least partially visible
}
// Add this to your init_resources function
void init_floor_shader(void) {
}

// Improved triangulation function for DOOM sectors
int triangulate_sector(mapvertex_t *vertices, int num_vertices, wall_vertex_t *out_vertices);

// Function to calculate texture coordinates based on sector dimensions
void calculate_texture_coords(wall_vertex_t *vertices, int vertex_count) {
  // Find bounds of the sector
  float min_x = FLT_MAX, min_y = FLT_MAX;
  float max_x = -FLT_MAX, max_y = -FLT_MAX;
  
  for (int i = 0; i < vertex_count; i++) {
    float x = vertices[i].x;
    float y = vertices[i].y;
    
    min_x = MIN(min_x, x);
    min_y = MIN(min_y, y);
    max_x = MAX(max_x, x);
    max_y = MAX(max_y, y);
  }
  
  // Update texture coordinates
  for (int i = 0; i < vertex_count; i++) {
    // Texture coordinates (normalized 0-1)
    vertices[i].u = (vertices[i].x - min_x);
    vertices[i].v = (vertices[i].y - min_y);
    vertices[i].nz = -127;
  }
}

static bool
belongs_to_sector(uint32_t i,
                  const maplinedef_t *linedef,
                  const map_data_t *map)
{
  bool value = false;
  if (linedef->sidenum[0] != 0xFFFF && map->sidedefs[linedef->sidenum[0]].sector == i) {
    value = !value;
  }
  if (linedef->sidenum[1] != 0xFFFF && map->sidedefs[linedef->sidenum[1]].sector == i) {
    value = !value;
  }
  return value;
}

uint32_t find_first_linedef(map_data_t const *map, uint32_t i) {
  for (int j = 0; j < map->num_linedefs; j++) {
    if (belongs_to_sector(i, &map->linedefs[j], map)) {
      return j;
    }
  }
  return -1;
}

uint32_t get_sector_vertices(map_data_t const *map,
                             uint32_t i,
                             mapvertex_t *sector_vertices)
{
  static bool used[MAX_EDGES];
  
  memset(used, 0, sizeof(used));
  
  uint32_t num_vertices = 0;
  uint32_t first = find_first_linedef(map, i);
  
  if (first > map->num_linedefs)
    return 0;
  
  used[first]=true;

  sector_vertices[num_vertices++] = map->vertices[map->linedefs[first].start];
  sector_vertices[num_vertices++] = map->vertices[map->linedefs[first].end];
  
  int current = map->linedefs[first].end;
  
  while (current != map->linedefs[first].start) {
    bool found = false;
    for (int j = first + 1; j < map->num_linedefs; j++) {
      maplinedef_t const *linedef = &map->linedefs[j];
      if (!belongs_to_sector(i, linedef, map) || used[j])
        continue;
      if (linedef->start == current) {
        sector_vertices[num_vertices++] = map->vertices[linedef->end];
        current = linedef->end;
        used[j] = true;
        found = true;
        break;
      }
      if (linedef->end == current) {
        sector_vertices[num_vertices++] = map->vertices[linedef->start];
        current = linedef->start;
        used[j] = true;
        found = true;
        break;
      }
    }
    if (!found) {
      return 0;
    }
  }
  return num_vertices;
}

static void compute_bbox(mapsector2_t *s, int numverts, mapvertex_t *verts) {
  if (numverts > 0) {
    s->bbox[BOXTOP] = INT16_MIN;
    s->bbox[BOXBOTTOM] = INT16_MAX;
    s->bbox[BOXLEFT] = INT16_MAX;
    s->bbox[BOXRIGHT] = INT16_MIN;
    
    for (int j = 0; j < numverts; j++) {
      if (verts[j].y > s->bbox[BOXTOP]) s->bbox[BOXTOP] = verts[j].y;
      if (verts[j].y < s->bbox[BOXBOTTOM]) s->bbox[BOXBOTTOM] = verts[j].y;
      if (verts[j].x < s->bbox[BOXLEFT]) s->bbox[BOXLEFT] = verts[j].x;
      if (verts[j].x > s->bbox[BOXRIGHT]) s->bbox[BOXRIGHT] = verts[j].x;
    }
  } else {
    // Empty sector, set bbox to zero
    s->bbox[BOXTOP] = 0;
    s->bbox[BOXBOTTOM] = 0;
    s->bbox[BOXLEFT] = 0;
    s->bbox[BOXRIGHT] = 0;
  }
}

void build_floor_vertex_buffer(map_data_t *map) {
  map->floors.sectors = realloc(map->floors.sectors, sizeof(mapsector2_t) * map->num_sectors);
  memset(map->floors.sectors, 0, sizeof(mapsector2_t) * map->num_sectors);
  for (uint32_t i = 0; i < map->num_sectors; i++) {
    map->floors.sectors[i].sector = &map->sectors[i];
  }
  
  map->floors.num_vertices = 0;
  // Create VAO for walls if not already created
  if (!map->floors.vao) {
    R_GpuGenVertexArrays(1, &map->floors.vao);
    R_GpuGenBuffers(1, &map->floors.vbo);
  }
    
  for (int i = 0; i < map->num_sectors; i++) {
    // Collect all vertices for this sector
    mapvertex_t sector_vertices[MAX_VERTICES]; // Assuming max MAX_VERTICES vertices per sector
    int num_vertices = get_sector_vertices(map, i, sector_vertices);
    
    // Compute bounding box for this sector from collected vertices
    compute_bbox(map->floors.sectors+i, num_vertices, sector_vertices);
    
    // Skip sectors with too few vertices
    if (num_vertices < 3) continue;
    
    // Triangulate the sector
    wall_vertex_t vertices[MAX_VERTICES]={0};
    int vertex_count = triangulate_sector(sector_vertices, num_vertices, vertices);
    
    // Set appropriate texture coordinates
    calculate_texture_coords(vertices, vertex_count);
    
    // Draw floor
    // Set z height to floor height
    for (int j = 0; j < vertex_count; j++) {
      vertices[j].z = map->sectors[i].floorheight;
    }
    
    map->floors.sectors[i].floor.vertex_start = map->floors.num_vertices;
    map->floors.sectors[i].floor.vertex_count = vertex_count;
    map->floors.sectors[i].floor.texture = get_flat_texture(map->sectors[i].floorpic);

    memcpy(&map->floors.vertices[map->floors.num_vertices], vertices, vertex_count * sizeof(wall_vertex_t));
    map->floors.num_vertices += vertex_count;

    if (!strncmp(map->sectors[i].ceilingpic, "F_SKY", 5)) {
      continue;
    }
    
    for (int j = 0; j < vertex_count; j++) {
      vertices[j].z = map->sectors[i].ceilingheight;
    }

    map->floors.sectors[i].ceiling.vertex_start = map->floors.num_vertices;
    map->floors.sectors[i].ceiling.vertex_count = vertex_count;
    map->floors.sectors[i].ceiling.texture = get_flat_texture(map->sectors[i].ceilingpic);
    
    memcpy(&map->floors.vertices[map->floors.num_vertices], vertices, vertex_count * sizeof(wall_vertex_t));
    map->floors.num_vertices += vertex_count;
  }
  
  // Upload vertex data to GPU
  R_GpuBindVertexArray(map->floors.vao);
  R_GpuBindBuffer(GL_ARRAY_BUFFER, map->floors.vbo);
  R_GpuBufferData(GL_ARRAY_BUFFER, map->floors.num_vertices * sizeof(wall_vertex_t), map->floors.vertices, GL_STATIC_DRAW);
  
  // Set up vertex attributes
  R_GpuVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, sizeof(wall_vertex_t), OFFSET_OF(wall_vertex_t, x)); // Position
  R_GpuEnableVertexAttribArray(0);
  R_GpuVertexAttribPointer(1, 2, GL_SHORT, GL_FALSE, sizeof(wall_vertex_t), OFFSET_OF(wall_vertex_t, u)); // UV
  R_GpuEnableVertexAttribArray(1);
  R_GpuVertexAttribPointer(2, 3, GL_BYTE, GL_TRUE, sizeof(wall_vertex_t), OFFSET_OF(wall_vertex_t, nx)); // Normal
  R_GpuEnableVertexAttribArray(2);
  R_GpuVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(wall_vertex_t), OFFSET_OF(wall_vertex_t, color)); // Normal
  R_GpuEnableVertexAttribArray(3);
}

int sectors_drawn = 0;

void draw_walls(map_data_t const *map,
                mapsector_t const *sector,
                viewdef_t const *viewdef);

void draw_wall_ids(map_data_t const *map,
                   mapsector_t const *sector,
                   viewdef_t const *viewdef);

//
// draw_portals
// Recursively traverse connected sectors through two-sided linedefs (portals).
// This function implements portal-based rendering for maps without BSP data.
//
// IMPORTANT FIX (2026-01):
// This function had two critical bugs that caused sectors to drop off during movement:
// 1. Missing frame tracking: Neighbor sectors were not checked for already being drawn,
//    causing redundant traversal and potential incorrect culling.
// 2. Wrong sector heights: The frustum check used the current sector's heights instead
//    of the neighbor sector's heights, causing incorrect portal rejection when sectors
//    had different floor/ceiling heights (very common in DOOM maps).
//
void draw_portals(map_data_t const *map,
                  mapsector_t const *sector,
                  viewdef_t const *viewdef,
                  void(*func)(map_data_t const *,
                              mapsector_t const *,
                              viewdef_t const *))
{
  
  for (int i = 0; i < map->num_linedefs; i++) {
    maplinedef_t const *linedef = &map->linedefs[i];
    mapvertex_t const *a = &map->vertices[linedef->start];
    mapvertex_t const *b = &map->vertices[linedef->end];
    // Skip one-sided linedefs (0xFFFF = no sidedef, standard DOOM format)
    if (linedef->sidenum[0] == 0xFFFF || linedef->sidenum[1] == 0xFFFF)
      continue;
    for (int j = 0; j < 2; j++) {
      if (map->sidedefs[linedef->sidenum[j]].sector == sector - map->sectors) {
        // Bounds check the neighboring sidedef index
        uint16_t neighbor_sidedef_idx = linedef->sidenum[!j];
        if (neighbor_sidedef_idx >= map->num_sidedefs)
          continue;
        
        // Get the neighboring sector
        uint32_t neighbor_sector_idx = map->sidedefs[neighbor_sidedef_idx].sector;
        
        // Bounds check to prevent buffer overflow
        if (neighbor_sector_idx >= map->num_sectors)
          continue;
        
        // FIX #1: Check if neighbor sector was already drawn this frame
        // This prevents redundant traversal and ensures each sector is visited once per frame
        // Note: frame fields are initialized to 0 via memset in build_floor_vertex_buffer()
        if (map->floors.sectors[neighbor_sector_idx].frame == viewdef->frame)
          continue;
        
        // FIX #2: Check frustum using proper 3D math for the linedef quad
        // The linedef forms a 4-pointed polygon in 3D space with floor and ceiling heights
        // Using the current sector's heights would incorrectly reject portals when sectors
        // have different floor/ceiling heights (e.g., stairs, lifts, different room heights)
        mapsector_t const *n = &map->sectors[neighbor_sector_idx];
        if (linedef_in_frustum(viewdef->frustum, *a, *b, n->floorheight, n->ceilingheight)) {
          func(map, n, viewdef);
        }
      }
    }
  }
}

#if 0

void draw_portals(map_data_t const *map,
                  mapsector_t const *sector,
                  viewdef_t const *viewdef,
                  void(*func)(map_data_t const *,
                              mapsector_t const *,
                              viewdef_t const *))
{
  vec2 eye = { viewdef->player.x, viewdef->player.y };
//  vec2 dir, forward;
  vec2 left = { viewdef->player.points[0][0], viewdef->player.points[0][1] };
  vec2 right = { viewdef->player.points[1][0], viewdef->player.points[1][1] };
  
  vec2 left2 = { viewdef->player.points2[0][0], viewdef->player.points2[0][1] };
  vec2 right2 = { viewdef->player.points2[1][0], viewdef->player.points2[1][1] };

//  glm_vec2_add(left, right, dir);
//  glm_vec2_scale(dir, 0.5f, dir);
//  glm_vec2_sub(dir, eye, dir);
//  if (glm_vec2_norm(dir) == 0.0f)
//    return;
//  glm_vec2_normalize_to(dir, forward);

  for (int i = 0; i < map->num_linedefs; i++) {
    maplinedef_t const *ld = &map->linedefs[i];
    if (ld->sidenum[0] == 0xFFFF || ld->sidenum[1] == 0xFFFF)
      continue;
    
    for (int j = 0; j < 2; j++) {
      if (map->sidedefs[ld->sidenum[j]].sector != (int)(sector - map->sectors))
        continue;
      if (map->walls.sections[ld->sidenum[!j]].frame == viewdef->frame)
        continue;
      vec2 a = { map->vertices[ld->start].x, map->vertices[ld->start].y };
      vec2 b = { map->vertices[ld->end].x,   map->vertices[ld->end].y };
      vec2 ab, lr, new_left, new_right;

      glm_vec2_sub(a, b, ab);
      glm_vec2_sub(left, right, lr);

      if (glm_vec2_dot(ab, lr) < 0) {
        vec2 tmp;
        glm_vec2_copy(a, tmp);
        glm_vec2_copy(b, a);
        glm_vec2_copy(tmp, b);
      }

      // Frustum clipping: calculate intersection points (if any)
      glm_vec2_sub(a, eye, new_left);
      glm_vec2_sub(b, eye, new_right);

      // Check if linedef is in front of eye using signed area (triangle winding)
      if (glm_vec2_cross(new_left, right) > 0 ||
          glm_vec2_cross(new_right, left) < 0)
        continue;
      
      if (glm_vec2_cross(new_left, left2) < 0) {
        glm_vec2_copy(left2, new_left);
      }
      
      if (glm_vec2_cross(new_right, right2) > 0) {
        glm_vec2_copy(right2, new_right);
      }
      
      if (glm_vec2_eqv_eps(new_left, new_right)) {
        continue;
      }
      
      // Prepare recursive view
      viewdef_t subview = *viewdef;
      subview.portal = i;
      glm_vec2_copy(new_left, subview.player.points[0]);
      glm_vec2_copy(new_right, subview.player.points[1]);
      
      map->walls.sections[ld->sidenum[!j]].frame = viewdef->frame;

      func(map, &map->sectors[map->sidedefs[ld->sidenum[!j]].sector], &subview);
    }
  }
}
#endif

// Main function to draw floors and ceilings
void draw_floors(map_data_t const *map,
                 mapsector_t const *sector,
                 viewdef_t const *viewdef)
{
  if (!sector) {
    if (map->sectors) {
      sector = map->sectors;
    } else {
      return;
    }
  }
  if (map->floors.sectors[sector-map->sectors].frame == viewdef->frame)
    return;

  sectors_drawn++;
    
  R_GpuDisable(GL_BLEND);
  R_GpuBindVertexArray(map->floors.vao);
  
  // Set MVP matrix uniform
  R_GpuUniformMatrix4fv(world_prog_mvp, 1, GL_FALSE, viewdef->mvp[0]);
  R_GpuUniform3fv(world_prog_viewPos, 1, viewdef->viewpos);
  
  mapsector2_t *sec = &map->floors.sectors[sector-map->sectors];
  sec->frame = viewdef->frame;
  
  // Use flat color based on sector light level
  float light = sector->lightlevel / 255.0f;
  
  extern int pixel;
  R_GpuCullFace(GL_BACK);
  if (CHECK_PIXEL(pixel, FLOOR, sector - map->sectors)) {
    draw_textured_surface(&sec->floor, HIGHLIGHT(light), GL_TRIANGLES);
  } else {
    draw_textured_surface(&sec->floor, light, GL_TRIANGLES);
  }
  R_GpuCullFace(GL_FRONT);
  if (CHECK_PIXEL(pixel, CEILING, sector - map->sectors)) {
    draw_textured_surface(&sec->ceiling, HIGHLIGHT(light), GL_TRIANGLES);
  } else {
    draw_textured_surface(&sec->ceiling, light, GL_TRIANGLES);
  }
  R_GpuCullFace(GL_BACK);
  
  draw_walls(map, sector, viewdef);

  draw_portals(map, sector, viewdef, draw_floors);
}

void
draw_floor_ids(map_data_t const *map,
               mapsector_t const *sector,
               viewdef_t const *viewdef)
{
  if (!sector) {
    if (map->sectors) {
      sector = map->sectors;
    } else {
      return;
    }
  }
  if (map->floors.sectors[sector-map->sectors].frame == viewdef->frame)
    return;

  uint32_t i = (uint32_t)(sector - map->sectors);
  mapsector2_t *sec = &map->floors.sectors[sector-map->sectors];
  sec->frame = viewdef->frame;
  
  R_GpuDisable(GL_BLEND);
  R_GpuBindVertexArray(map->floors.vao);
  
  R_GpuCullFace(GL_BACK);
  draw_textured_surface_id(&sec->floor, i | PIXEL_FLOOR, GL_TRIANGLES);
  
  R_GpuCullFace(GL_FRONT);
  draw_textured_surface_id(&sec->ceiling, i | PIXEL_CEILING, GL_TRIANGLES);
  
  // Reset texture binding
  R_GpuCullFace(GL_BACK);
  R_GpuBindTexture(GL_TEXTURE_2D, 0);
  
  draw_wall_ids(map, sector, viewdef);

  draw_portals(map, sector, viewdef, draw_floor_ids);
}

