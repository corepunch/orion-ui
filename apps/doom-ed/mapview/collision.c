#include <mapview/map.h>
#include <math.h>

// Constants
#define MAX_STEP 24.0f        // Maximum step height
#define EPSILON 0.1f          // Floating point error prevention
#define WALL_DIST 2.0f        // Minimum wall distance
#define MAX_CORNERS 8         // Maximum corners to handle

// Collision result structure
typedef struct {
  bool collided;              // Collision detected
  float nx, ny;               // Wall normal
  float pen;                  // Penetration depth
  float cx, cy;               // Closest point
  int linedef;                // Linedef index
  bool corner;                // Corner collision flag
} collision_t;

void update_player_pos(map_data_t const *map, player_t *player, float mx, float my, int depth);

/**
 * Calculate squared distance between two points
 */
float dist_sq(float x1, float y1, float x2, float y2) {
  float dx = x2 - x1;
  float dy = y2 - y1;
  return dx*dx + dy*dy;
}

/**
 * Check if a vertex forms a corner
 */
bool is_corner(map_data_t const *map, uint16_t v_idx) {
  mapvertex_t const *vertex = &map->vertices[v_idx];
  
  // Find connected linedefs
  int conn_count = 0;
  int conn_idx[MAX_CORNERS];
  
  for (int i = 0; i < map->num_linedefs && conn_count < MAX_CORNERS; i++) {
    maplinedef_t const *line = &map->linedefs[i];
    if (line->start == v_idx || line->end == v_idx) {
      conn_idx[conn_count++] = i;
    }
  }
  
  // Not a corner if only one linedef is connected
  if (conn_count <= 1) return false;
  
  // Check angles between connected walls
  for (int i = 0; i < conn_count; i++) {
    maplinedef_t const *line1 = &map->linedefs[conn_idx[i]];
    
    // Get direction vector of first line
    float dx1, dy1;
    if (line1->start == v_idx) {
      dx1 = map->vertices[line1->end].x - vertex->x;
      dy1 = map->vertices[line1->end].y - vertex->y;
    } else {
      dx1 = vertex->x - map->vertices[line1->start].x;
      dy1 = vertex->y - map->vertices[line1->start].y;
    }
    
    float len1 = sqrt(dx1*dx1 + dy1*dy1);
    if (len1 <= EPSILON) continue;
    dx1 /= len1;
    dy1 /= len1;
    
    for (int j = i+1; j < conn_count; j++) {
      maplinedef_t const *line2 = &map->linedefs[conn_idx[j]];
      
      // Get direction vector of second line
      float dx2, dy2;
      if (line2->start == v_idx) {
        dx2 = map->vertices[line2->end].x - vertex->x;
        dy2 = map->vertices[line2->end].y - vertex->y;
      } else {
        dx2 = vertex->x - map->vertices[line2->start].x;
        dy2 = vertex->y - map->vertices[line2->start].y;
      }
      
      float len2 = sqrt(dx2*dx2 + dy2*dy2);
      if (len2 <= EPSILON) continue;
      dx2 /= len2;
      dy2 /= len2;
      
      // If angle is significant (dot product < 0.7), it's a corner
      if (dx1*dx2 + dy1*dy2 < 0.7f) return true;
    }
  }
  
  return false;
}

/**
 * Check if wall can be passed through based on sector heights
 */
bool can_pass_wall(map_data_t const *map, maplinedef_t const *line, float player_z) {
  // Wall not solid if both sides are the same sector
  if (line->sidenum[1] == 0xFFFF) return false;
  
  uint16_t side1 = line->sidenum[0];
  uint16_t side2 = line->sidenum[1];
  
  if (side1 == 0xFFFF || side2 == 0xFFFF) return false;
  
  mapsector_t *s1 = &map->sectors[map->sidedefs[side1].sector];
  mapsector_t *s2 = &map->sectors[map->sidedefs[side2].sector];
  
  // Pass if sectors have identical heights
  if (s1->floorheight == s2->floorheight && s1->ceilingheight == s2->ceilingheight)
    return true;
  
  // Check step heights against player position
  float player_feet = player_z - EYE_HEIGHT;
  float diff1 = s1->floorheight - player_feet;
  float diff2 = s2->floorheight - player_feet;
  
  // Pass if both steps are acceptable
  return (diff1 <= MAX_STEP && diff2 <= MAX_STEP);
}

/**
 * Check point-vertex collision
 */
void check_vertex_collision(map_data_t const *map, float x, float y, float max_dist_sq, collision_t *result) {
  for (int i = 0; i < map->num_vertices; i++) {
    mapvertex_t const *v = &map->vertices[i];
    float dx = x - v->x;
    float dy = y - v->y;
    float d_sq = dx*dx + dy*dy;
    
    if (d_sq < max_dist_sq && is_corner(map, i) && d_sq < max_dist_sq) {
      float dist = sqrt(d_sq);
      if (dist > EPSILON) {
        result->collided = true;
        result->nx = dx / dist;
        result->ny = dy / dist;
        result->pen = P_RADIUS + WALL_DIST - dist;
        result->cx = v->x;
        result->cy = v->y;
        result->corner = true;
        max_dist_sq = d_sq; // Update for closer collisions
      }
    }
  }
}

/**
 * Calculate the closest point on a line segment to a given point
 *
 * @param point_x X coordinate of point
 * @param point_y Y coordinate of point
 * @param line_x1 X coordinate of line start
 * @param line_y1 Y coordinate of line start
 * @param line_x2 X coordinate of line end
 * @param line_y2 Y coordinate of line end
 * @param closest_x Pointer to store closest point X coordinate
 * @param closest_y Pointer to store closest point Y coordinate
 * @param t_param Pointer to store parametric value (0-1) along line
 * @return Distance squared from point to line
 */
float closest_point_on_line(float point_x, float point_y,
                            float line_x1, float line_y1,
                            float line_x2, float line_y2,
                            float *closest_x, float *closest_y,
                            float *t_param) {
  // Line segment properties
  float dx = line_x2 - line_x1;
  float dy = line_y2 - line_y1;
  float len_sq = dx*dx + dy*dy;
  
  // If line segment is effectively a point
  if (len_sq < EPSILON) {
    *closest_x = line_x1;
    *closest_y = line_y1;
    *t_param = 0.0f;
    return dist_sq(point_x, point_y, line_x1, line_y1);
  }
  
  // Find closest point on line
  float t = ((point_x - line_x1) * dx + (point_y - line_y1) * dy) / len_sq;
  t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
  
  *closest_x = line_x1 + t * dx;
  *closest_y = line_y1 + t * dy;
  *t_param = t;
  
  return dist_sq(point_x, point_y, *closest_x, *closest_y);
}

/**
 * Check for collisions between a point and map line segments
 */
void check_line_collision(map_data_t const *map, float x, float y, float player_z,
                          float max_dist_sq, collision_t *result) {
  for (int i = 0; i < map->num_linedefs; i++) {
    maplinedef_t const *line = &map->linedefs[i];
    
    // Skip passable walls
    if (can_pass_wall(map, line, player_z)) continue;
    
    // Get wall vertices
    mapvertex_t const *v1 = &map->vertices[line->start];
    mapvertex_t const *v2 = &map->vertices[line->end];
    
    // Line segment properties
    float wx1 = v1->x, wy1 = v1->y;
    float wx2 = v2->x, wy2 = v2->y;
    
    // Find closest point on line
    float cx, cy, t;
    float d_sq = closest_point_on_line(x, y, wx1, wy1, wx2, wy2, &cx, &cy, &t);
    
    // Check for collision
    if (d_sq < max_dist_sq && t > 0.01f && t < 0.99f) {
      max_dist_sq = d_sq;
      
      // Calculate wall normal
      float dx = wx2 - wx1;
      float dy = wy2 - wy1;
      float len = sqrt(dx*dx + dy*dy);
      
      if (len > EPSILON) {
        float nx = -dy / len;
        float ny = dx / len;
        
        // Ensure normal points away from player
        if ((x - cx) * nx + (y - cy) * ny < 0) {
          nx = -nx;
          ny = -ny;
        }
        
        result->collided = true;
        result->nx = nx;
        result->ny = ny;
        result->pen = P_RADIUS + WALL_DIST - sqrt(d_sq);
        result->cx = cx;
        result->cy = cy;
        result->linedef = i;
        result->corner = false;
      }
    }
  }
}
/**
 * Check all wall collisions
 */
void check_collision(map_data_t const *map, float x, float y, float player_z, collision_t *result) {
  result->collided = false;
  result->pen = 0.0f;
  result->corner = false;
  
  float max_dist_sq = (P_RADIUS + WALL_DIST) * (P_RADIUS + WALL_DIST) * 4.0f;
  
  // Check vertex collisions first
  check_vertex_collision(map, x, y, max_dist_sq, result);
  
  // Then check line collisions
  check_line_collision(map, x, y, player_z, max_dist_sq, result);
}

/**
 * Calculate slide vector along a wall
 */
void calc_slide(float mx, float my, float nx, float ny, float *sx, float *sy) {
  float dot = mx * nx + my * ny;
  
  // Not moving into wall
  if (dot > -EPSILON) {
    *sx = mx;
    *sy = my;
    return;
  }
  
  // Calculate slide vector
  *sx = mx - (nx * dot);
  *sy = my - (ny * dot);
  
  // Normalize to maintain speed
  float s_len = sqrt((*sx) * (*sx) + (*sy) * (*sy));
  if (s_len > EPSILON) {
    float m_len = sqrt(mx * mx + my * my);
    *sx = (*sx) * m_len / s_len;
    *sy = (*sy) * m_len / s_len;
  }
}

/**
 * Handle corner collision with rotated approach
 */
void handle_corner(map_data_t const *map, player_t *player, collision_t *result,
                   float mx, float my, int depth) {
  float dx = player->x - result->cx;
  float dy = player->y - result->cy;
  float dist = sqrt(dx*dx + dy*dy);
  
  if (dist > EPSILON) {
    dx /= dist;
    dy /= dist;
    
    // Create vector that goes around the corner
    float a1 = atan2(my, mx);
    float a2 = atan2(dy, dx);
    float diff = a1 - a2;
    float new_angle = a2 + (diff > 0 ? 0.5f : -0.5f);
    float len = sqrt(mx * mx + my * my);
    
    float new_mx = cos(new_angle) * len;
    float new_my = sin(new_angle) * len;
    
    // Try modified movement
    update_player_pos(map, player, new_mx, new_my, depth);
  }
}

/**
 * Check for ledge transition
 */
bool check_ledge(map_data_t const *map, player_t *player, float nx, float ny,
                 float *mx, float *my) {
  mapsector_t const *current = find_player_sector(map, (int)player->x, (int)player->y);
  mapsector_t const *new_sector = find_player_sector(map, (int)(player->x + *mx),
                                                     (int)(player->y + *my));
  
  if (!current || !new_sector) return false;
  
  // Check if stepping off ledge
  if (current->floorheight > new_sector->floorheight) {
    collision_t edge;
    check_collision(map, player->x, player->y, player->z, &edge);
    
    if (edge.collided) {
      float edge_dist = sqrt(dist_sq(player->x, player->y, edge.cx, edge.cy));
      
      // If near edge, push away slightly
      if (edge_dist < P_RADIUS * 1.5f) {
        *mx += edge.nx * (P_RADIUS * 0.7f);
        *my += edge.ny * (P_RADIUS * 0.7f);
        return true;
      }
    }
  }
  return false;
}

/**
 * Check if player can move to new sector
 */
bool can_enter_sector(mapsector_t const *current, mapsector_t const *new_sector, float player_z) {
  if (!new_sector) return false;
  
  float player_feet = player_z - EYE_HEIGHT;
  float floor_diff = new_sector->floorheight - player_feet;
  
  // Too high to step up
  if (floor_diff > MAX_STEP) return false;
  
  // Ceiling too low
  if ((new_sector->ceilingheight - new_sector->floorheight) < EYE_HEIGHT) return false;
  
  return true;
}

/**
 * Update player position with collision handling
 */
void update_player_pos(map_data_t const *map, player_t *player, float mx, float my, int depth) {
  // Prevent infinite recursion
  if (depth > 3) return;
  
  // Calculate new position
  float new_x = player->x + mx;
  float new_y = player->y + my;
  
  // Check sectors
  mapsector_t const *current = find_player_sector(map, (int)player->x, (int)player->y);
  
  // Check for ledge transitions
  bool modified = check_ledge(map, player, mx, my, &mx, &my);
  if (modified) {
    new_x = player->x + mx;
    new_y = player->y + my;
  }
  
  // Check for collisions
  collision_t result;
  check_collision(map, new_x, new_y, player->z, &result);
  
  // No collision - direct move
  if (!result.collided) {
    mapsector_t const *new_sector = find_player_sector(map, (int)new_x, (int)new_y);
    
    if (can_enter_sector(current, new_sector, player->z)) {
      player->x = new_x;
      player->y = new_y;
      player->z = new_sector->floorheight + EYE_HEIGHT;
    }
    return;
  }
  
  // Handle corner collisions specially
  if (result.corner) {
    handle_corner(map, player, &result, mx, my, depth + 1);
    return;
  }
  
  // Handle collision with sliding
  float slide_x, slide_y;
  calc_slide(mx, my, result.nx, result.ny, &slide_x, &slide_y);
  
  // Skip if slide vector is very small
  float slide_len_sq = slide_x * slide_x + slide_y * slide_y;
  if (slide_len_sq < EPSILON) return;
  
  // Try the slide movement recursively
  update_player_pos(map, player, slide_x, slide_y, depth + 1);
}

/**
 * Public function to update player position with collision handling
 */
void update_player_position_with_sliding(map_data_t const *map, player_t *player,
                                         float move_x, float move_y) {
  update_player_pos(map, player, move_x, move_y, 0);
}
