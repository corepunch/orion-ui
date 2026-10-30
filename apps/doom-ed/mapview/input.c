#include <limits.h>

#include <platform/platform.h>
#include <mapview/map.h>
#include <editor/editor.h>

extern bool running;
extern bool mode;

// Check if a point is inside a sector's bounding box
static inline bool point_in_bbox(mapsector2_t const* sector, int x, int y) {
  return !(x < sector->bbox[BOXLEFT] || x > sector->bbox[BOXRIGHT] ||
           y < sector->bbox[BOXBOTTOM] || y > sector->bbox[BOXTOP]);
}

// Compute bounding box for a single sector
// Note: Primarily used for debugging/testing. Production code uses build_floor_vertex_buffer()
void compute_sector_bbox(map_data_t *map, int sector_index) {
  if (sector_index < 0 || sector_index >= map->num_sectors) {
    return;
  }
  
  mapsector_t *sector = &map->sectors[sector_index];
  mapsector2_t *sector2 = &map->floors.sectors[sector_index];
  
  // Initialize bbox to extreme values
  sector2->bbox[BOXTOP] = INT16_MIN;
  sector2->bbox[BOXBOTTOM] = INT16_MAX;
  sector2->bbox[BOXLEFT] = INT16_MAX;
  sector2->bbox[BOXRIGHT] = INT16_MIN;
  
  // Iterate through all linedefs to find those belonging to this sector
  bool found = false;
  for (int i = 0; i < map->num_linedefs; i++) {
    maplinedef_t* line = &map->linedefs[i];
    
    // Check both sides
    for (int s = 0; s < 2; s++) {
      int sidenum = line->sidenum[s];
      if (sidenum == 0xFFFF) continue;
      if (map->sidedefs[sidenum].sector != sector_index) continue;
      
      // This linedef belongs to our sector, update bbox with its vertices
      mapvertex_t* v1 = &map->vertices[line->start];
      mapvertex_t* v2 = &map->vertices[line->end];
      
      // Update bounding box
      if (v1->y > sector2->bbox[BOXTOP]) sector2->bbox[BOXTOP] = v1->y;
      if (v1->y < sector2->bbox[BOXBOTTOM]) sector2->bbox[BOXBOTTOM] = v1->y;
      if (v1->x < sector2->bbox[BOXLEFT]) sector2->bbox[BOXLEFT] = v1->x;
      if (v1->x > sector2->bbox[BOXRIGHT]) sector2->bbox[BOXRIGHT] = v1->x;
      if (v2->y > sector2->bbox[BOXTOP]) sector2->bbox[BOXTOP] = v2->y;
      if (v2->y < sector2->bbox[BOXBOTTOM]) sector2->bbox[BOXBOTTOM] = v2->y;
      if (v2->x < sector2->bbox[BOXLEFT]) sector2->bbox[BOXLEFT] = v2->x;
      if (v2->x > sector2->bbox[BOXRIGHT]) sector2->bbox[BOXRIGHT] = v2->x;
      
      found = true;
      break; // Both vertices processed, no need to check other side
    }
  }
  
  // If no linedefs found for this sector, set bbox to zero
  if (!found) {
    sector2->bbox[BOXTOP] = 0;
    sector2->bbox[BOXBOTTOM] = 0;
    sector2->bbox[BOXLEFT] = 0;
    sector2->bbox[BOXRIGHT] = 0;
  }
}

// Compute bounding boxes for all sectors
void compute_all_sector_bboxes(map_data_t *map) {
  for (int i = 0; i < map->num_sectors; i++) {
    compute_sector_bbox(map, i);
  }
}

bool point_in_sector(map_data_t const* map, int x, int y, int secidx) {
  if (secidx < 0 || secidx >= map->num_sectors) {
    return false;
  }
  
  // Quick rejection test using bounding box
  if (map->floors.sectors &&
      !point_in_bbox(map->floors.sectors+secidx, x, y))
  {
    return false;
  }
  
  // Perform detailed ray-casting test
  int inside = 0;
  for (int i = 0; i < map->num_linedefs; i++) {
    maplinedef_t const* line = &map->linedefs[i];
    for (int s = 0; s < 2; s++) {
      int sidenum = line->sidenum[s];
      if (sidenum == 0xFFFF) continue;
      if (map->sidedefs[sidenum].sector != secidx) continue;
      mapvertex_t const* a = &map->vertices[line->start];
      mapvertex_t const* b = &map->vertices[line->end];
      if (((a->y>y)!=(b->y>y))&&(x<(b->x-a->x)*(y-a->y)/(b->y-a->y)+a->x))
        inside = !inside;
    }
  }
  return inside;
}

//mapsector_t const *find_player_sector(map_data_t const *map, int x, int y) {
//  for (int i = 0; i < map->num_sectors; i++)
//    if (point_in_sector(map, x, y, i))
//      return &map->sectors[i];
//  return NULL; // not found
//}

mapsector_t const *find_player_sector(map_data_t const *map, int x, int y) {
  mapsector_t const *highest_sector = NULL;
  int highest_floor = INT_MIN;
  
//  // Check center point and four points at player radius distance
//  int check_points[5][2] = {
//    {x, y},               // Center
//    {x - P_RADIUS, y},    // Left
//    {x + P_RADIUS, y},    // Right
//    {x, y - P_RADIUS},    // Up
//    {x, y + P_RADIUS}     // Down
//  };
//  
//  // Check each sample point
//  for (int p = 0; p < 5; p++) {
//    int px = check_points[p][0];
//    int py = check_points[p][1];
    int px = x;
    int py = y;
  
    for (int i = 0; i < map->num_sectors; i++) {
      if (point_in_sector(map, px, py, i)) {
        // If this is a higher sector than what we've found so far, remember it
        if (!highest_sector || map->sectors[i].floorheight > highest_floor) {
          highest_sector = &map->sectors[i];
          highest_floor = map->sectors[i].floorheight;
        }
      }
    }
//  }
  
  return highest_sector; // Returns NULL if no sector was found
}

void handle_scroll(int wheel[], map_data_t *map) {
  extern int pixel;
  int move_x = wheel[0], move_y = wheel[1];
//  static int buffer_x = 0;
//  static int buffer_y = 0;
//  buffer_x += wheel[0];
//  buffer_y += wheel[1];
//  
//  // Reset buffer_y if scroll direction changed
//  if ((buffer_y ^ wheel[1]) < 0) {
//    buffer_y = wheel[1];
//  }
//  if ((buffer_x ^ wheel[0]) < 0) {
//    buffer_x = wheel[0];
//  }
//  
//  int move_x, move_y;
//  
//  // Correct rounding for both positive and negative
//  if (buffer_y >= 0) {
//    move_y = (buffer_y & ~7); // positive side: floor down
//  } else {
//    move_y = -((-buffer_y) & ~7); // negative side: floor up
//  }
//  
//  // Correct rounding for both positive and negative
//  if (buffer_x >= 0) {
//    move_x = (buffer_x & ~7); // positive side: floor down
//  } else {
//    move_x = -((-buffer_x) & ~7); // negative side: floor up
//  }
  
  uint16_t index = pixel&~PIXEL_MASK;
//  if (move_y != 0) {
//    buffer_y -= move_y;
    switch (pixel&PIXEL_MASK) {
      case PIXEL_FLOOR:
        if (index < map->num_sectors) {
          map->sectors[index].floorheight -= move_y;
        }
        break;
      case PIXEL_CEILING:
        if (index < map->num_sectors) {
          map->sectors[index].ceilingheight -= move_y;
        }
        break;
      case PIXEL_MID:
      case PIXEL_TOP:
      case PIXEL_BOTTOM:
        if (index < map->num_sidedefs) {
          map->sidedefs[index].rowoffset -= move_y;
        }
        break;
      default:
        break;
    }
    
//    build_wall_vertex_buffer(map);
//    build_floor_vertex_buffer(map);
//  }
//  
//  if (move_x != 0) {
//    buffer_x -= move_x;
    switch (pixel&PIXEL_MASK) {
      case PIXEL_MID:
      case PIXEL_TOP:
      case PIXEL_BOTTOM:
        if (index < map->num_sidedefs) {
          map->sidedefs[pixel&~PIXEL_MASK].textureoffset += move_x;
        }
        break;
      default:
        break;
    }
    
    build_wall_vertex_buffer(map);
    build_floor_vertex_buffer(map);
//  }
}

void game_tick(game_t *game) {
  uint32_t current_time = (uint32_t)axGetMilliseconds();
  float delta_time = (current_time - game->last_time) / 1000.0f;
  game->last_time = current_time;

  player_t *player = &game->player;
  // Apply mouse rotation if relative mode is enabled
  if (true) {
    // Horizontal mouse movement controls yaw (left/right rotation)
    player->angle += player->mouse_x_rel * sensitivity_x;
    
    // Keep angle within 0-360 range
    if (player->angle < 0) player->angle += 360;
    if (player->angle >= 360) player->angle -= 360;
    
    // Vertical mouse movement controls pitch (up/down looking)
    player->pitch -= player->mouse_y_rel * sensitivity_y;
    
    // Clamp pitch to prevent flipping over
    if (player->pitch > 89.0f) player->pitch = 89.0f;
    if (player->pitch < -89.0f) player->pitch = -89.0f;
  }
  
  // Convert player angle to radians for movement calculations
  float angle_rad = player->angle * M_PI / 180.0;
  
//  // Calculate forward/backward direction vector
//  float forward_x = -forward_move * cos(angle_rad) * MOVEMENT_SPEED; // Negative because DOOM's coordinate system
//  float forward_y = forward_move * sin(angle_rad) * MOVEMENT_SPEED;
//  float strafe_x = strafe_move * sin(angle_rad) * MOVEMENT_SPEED;
//  float strafe_y = strafe_move * cos(angle_rad) * MOVEMENT_SPEED;
//  
//  // Combine movement vectors
//  float move_x = forward_x + strafe_x;
//  float move_y = forward_y + strafe_y;
//  
//  // If there's any movement, apply it with collision detection and sliding
//  if (move_x != 0.0f || move_y != 0.0f) {
////    update_player_position_with_sliding(map, player, move_x * delta_time, move_y * delta_time);
//    player->x += move_x * delta_time;
//    player->y += move_y * delta_time;
//  }

  float input_x = -player->forward_move * cos(angle_rad) + player->strafe_move * sin(angle_rad);
  float input_y =  player->forward_move * sin(angle_rad) + player->strafe_move * cos(angle_rad);
  
  // Normalize input direction
  float input_len = sqrtf(input_x * input_x + input_y * input_y);
  if (input_len > 0.0f) {
    input_x /= input_len;
    input_y /= input_len;
    
    // Accelerate
    player->vel_x += input_x * ACCELERATION * delta_time;
    player->vel_y += input_y * ACCELERATION * delta_time;
  } else {
    // Apply friction
    float speed = sqrtf(player->vel_x * player->vel_x + player->vel_y * player->vel_y);
    if (speed > 0.0f) {
      float decel = FRICTION * delta_time;
      float new_speed = fmaxf(0.0f, speed - decel);
      float scale = new_speed / speed;
      player->vel_x *= scale;
      player->vel_y *= scale;
    }
  }
  
  // Clamp velocity
  float speed = sqrtf(player->vel_x * player->vel_x + player->vel_y * player->vel_y);
  if (speed > MAX_SPEED) {
    float scale = MAX_SPEED / speed;
    player->vel_x *= scale;
    player->vel_y *= scale;
  }
  
  // Update position
  
  if (player->vel_x != 0 && player->vel_y != 0) {
      player->x += player->vel_x * delta_time;
      player->y += player->vel_y * delta_time;
//    update_player_position_with_sliding(&game->map, player, player->vel_x * delta_time, player->vel_y * delta_time);
  }
}
