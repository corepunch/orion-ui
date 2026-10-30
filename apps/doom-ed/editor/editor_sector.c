#include <editor/editor.h>
#include <math.h>

// Macros to reduce code duplication
#define RESIZE_ARRAY(TYPE, ARRAY, SIZE) \
TYPE *new_##ARRAY = realloc(map->ARRAY, (SIZE) * sizeof(TYPE)); \
if (!new_##ARRAY) { \
fprintf(stderr, "Failed to allocate memory for new %s!\n", #ARRAY); \
return 0xFFFF; \
} \
map->ARRAY = new_##ARRAY

#define CHECK_CAPACITY(TYPE, ARRAY) \
if (map->num_##ARRAY >= 65535) { \
fprintf(stderr, "Maximum number of %s reached!\n", #ARRAY); \
return 0xFFFF; \
}

// Utility functions for vertex manipulation
static float vertex_distance_sq(mapvertex_t v1, mapvertex_t v2) {
  float dx = v1.x - v2.x;
  float dy = v1.y - v2.y;
  return dx*dx + dy*dy;
}

mapvertex_t vertex_midpoint(mapvertex_t v1, mapvertex_t v2) {
  mapvertex_t mid = {
    .x = (v1.x + v2.x) / 2,
    .y = (v1.y + v2.y) / 2
  };
  return mid;
}

//static mapvertex_t vertex_normal(mapvertex_t v1, mapvertex_t v2, float scale) {
//  float dx = v2.x - v1.x;
//  float dy = v2.y - v1.y;
//  float length = sqrtf(dx*dx + dy*dy);
//  
//  mapvertex_t normal = {
//    .x = (int)(-dy / length * scale),
//    .y = (int)(dx / length * scale)
//  };
//  return normal;
//}
//
static bool vertex_is_near(mapvertex_t v1, mapvertex_t v2, int threshold) {
  return vertex_distance_sq(v1, v2) < threshold * threshold;
}

mapvertex_t compute_centroid(map_data_t const *map, uint16_t vertices[], int count) {
  int sum_x = 0, sum_y = 0;
  for (int i = 0; i < count; i++) {
    sum_x += map->vertices[vertices[i]].x;
    sum_y += map->vertices[vertices[i]].y;
  }
  
  mapvertex_t centroid = {
    .x = sum_x / count,
    .y = sum_y / count
  };
  return centroid;
}

// Check if a point exists at the given coordinates
bool point_exists(mapvertex_t point, map_data_t *map, int *index) {
  const int threshold = 8;
  for (int i = 0; i < map->num_vertices; i++) {
    if (vertex_is_near(map->vertices[i], point, threshold)) {
      if (index) *index = i;
      return true;
    }
  }
  return false;
}

// Add a new vertex to the map
uint16_t add_vertex(map_data_t *map, mapvertex_t vertex) {
  CHECK_CAPACITY(mapvertex_t, vertices);
  RESIZE_ARRAY(mapvertex_t, vertices, map->num_vertices + 1);
  
  uint16_t index = map->num_vertices;
  map->vertices[index] = vertex;
  map->num_vertices++;
  
  return index;
}

// Add a new thing to the map
uint16_t add_thing(map_data_t *map, mapthing_t thing) {
  CHECK_CAPACITY(mapthing_t, things);
  RESIZE_ARRAY(mapthing_t, things, map->num_things + 1);
  
  uint16_t index = map->num_things;
  map->things[index] = thing;
  map->num_things++;
  
  return index;
}

// Add a new linedef to the map
uint16_t add_linedef(map_data_t *map, uint16_t start, uint16_t end,
                            uint16_t front_side, uint16_t back_side) {
  CHECK_CAPACITY(maplinedef_t, linedefs);
  RESIZE_ARRAY(maplinedef_t, linedefs, map->num_linedefs + 1);
  
  uint16_t index = map->num_linedefs;
  map->linedefs[index] = (maplinedef_t){
    .start = end,
    .end = start,
    .flags = (back_side == 0xFFFF) ? 1 : 4, // 1=impassable, 4=two-sided
    .special = 0,
//    .tag = 0,
    .sidenum = { front_side, back_side }
  };

  map->num_linedefs++;

  build_wall_vertex_buffer(map);
  
  return index;
}

// Add a new sidedef to the map
uint16_t add_sidedef(map_data_t *map, uint16_t sector_index) {
  CHECK_CAPACITY(mapsidedef_t, sidedefs);
  RESIZE_ARRAY(mapsidedef_t, sidedefs, map->num_sidedefs + 1);
  
  map->sidedefs[map->num_sidedefs] = (mapsidedef_t){
    .textureoffset = 0,
    .rowoffset = 0,
    .sector = sector_index,
  };
  
  // Set default textures
//  map->sidedefs[map->num_sidedefs].toptexture[0] = '-';
//  map->sidedefs[map->num_sidedefs].toptexture[1] = '\0';
//  map->sidedefs[map->num_sidedefs].bottomtexture[0] = '-';
//  map->sidedefs[map->num_sidedefs].bottomtexture[1] = '\0';
  
  strncpy(map->sidedefs[map->num_sidedefs].midtexture, get_selected_texture(), 8);
  strncpy(map->sidedefs[map->num_sidedefs].toptexture, get_selected_texture(), 8);
  strncpy(map->sidedefs[map->num_sidedefs].bottomtexture, get_selected_texture(), 8);

  return map->num_sidedefs++;
}

// Add a new sector to the map
uint16_t add_sector(map_data_t *map) {
  CHECK_CAPACITY(mapsector_t, sectors);
  RESIZE_ARRAY(mapsector_t, sectors, map->num_sectors + 1);
  
  uint16_t index = map->num_sectors;
  map->sectors[index] = (mapsector_t){
    .floorheight = 0,
    .ceilingheight = 128,
    .lightlevel = 160,
    .special = 0,
    .tag = 0
  };
  
  // Set default textures
  strncpy(map->sectors[index].floorpic, get_selected_flat_texture(), 8);
  strncpy(map->sectors[index].ceilingpic, get_selected_flat_texture(), 8);
  
  map->num_sectors++;
  return index;
}

// Forward declaration
bool point_in_sector(map_data_t const* map, int x, int y, int sector_index);

// Check if two line segments intersect
//static bool segments_intersect(mapvertex_t v1, mapvertex_t v2, mapvertex_t v3, mapvertex_t v4) {
//  int dx1 = v2.x - v1.x;
//  int dy1 = v2.y - v1.y;
//  int dx2 = v4.x - v3.x;
//  int dy2 = v4.y - v3.y;
//  
//  int delta = dx1 * dy2 - dy1 * dx2;
//  if (delta == 0) return false; // Parallel lines
//  
//  float t1 = ((v3.x - v1.x) * dy2 - (v3.y - v1.y) * dx2) / (float)delta;
//  float t2 = ((v1.x - v3.x) * dy1 - (v1.y - v3.y) * dx1) / (float)(-delta);
//  
//  return (t1 >= 0 && t1 <= 1 && t2 >= 0 && t2 <= 1);
//}

// Check if linedef already exists between vertices
int find_existing_linedef(map_data_t const *map, uint16_t v1, uint16_t v2) {
  for (int j = 0; j < map->num_linedefs; j++) {
    if ((map->linedefs[j].start == v1 && map->linedefs[j].end == v2) ||
        (map->linedefs[j].start == v2 && map->linedefs[j].end == v1)) {
      return j;
    }
  }
  return -1;
}

uint16_t find_point_sector(map_data_t const *map, mapvertex_t vertex) {
  mapsector_t const *sec = find_player_sector(map, vertex.x, vertex.y);
  if (sec) {
    return sec - map->sectors;
  } else {
    return -1;
  }
}

int check_closed_loop(map_data_t *map, uint16_t line, uint16_t vertices[]) {
  if (!map || line >= map->num_linedefs || !vertices)
    return 0;
  
  // Use BFS to find the shortest loop
  typedef struct {
    int vertex;              // Current vertex
    int linedef;             // Current linedef
    int prev_index;          // Previous path index
    int length;              // Path length so far
  } path_node_t;
  
  // Create a queue for BFS
  path_node_t *queue = malloc(map->num_vertices * sizeof(path_node_t));
  if (!queue) return 0;
  
  // Mark vertices as unvisited
  bool *visited = calloc(map->num_vertices, sizeof(bool));
  if (!visited) {
    free(queue);
    return 0;
  }
  
  // Start BFS from both vertices of the specified linedef
  int head = 0, tail = 0;
  int start_vertex = map->linedefs[line].start;
  int end_vertex = map->linedefs[line].end;
  
  // Add end vertex to queue
  queue[tail].vertex = end_vertex;
  queue[tail].linedef = line;
  queue[tail].prev_index = -1;
  queue[tail].length = 1;
  tail++;
  visited[end_vertex] = true;
  
  int loop_end_idx = -1;
  
  // BFS to find path back to start vertex
  while (head < tail) {
    path_node_t current = queue[head++];
    
    // For each linedef
    for (int i = 0; i < map->num_linedefs; i++) {
      if (i == line) continue; // Skip the starting linedef
      
      int next_vertex = -1;
      
      // Check if this linedef connects to current vertex
      if (map->linedefs[i].start == current.vertex)
        next_vertex = map->linedefs[i].end;
      else if (map->linedefs[i].end == current.vertex)
        next_vertex = map->linedefs[i].start;
      else
        continue; // Not connected
      
      // If we found our start vertex, we completed a loop
      if (next_vertex == start_vertex) {
        loop_end_idx = head - 1;
        goto reconstruct_path;
      }
      
      // Otherwise, if vertex not visited, add to queue
      if (!visited[next_vertex]) {
        visited[next_vertex] = true;
        queue[tail].vertex = next_vertex;
        queue[tail].linedef = i;
        queue[tail].prev_index = head - 1;
        queue[tail].length = current.length + 1;
        tail++;
      }
    }
  }
  
  // No loop found
  free(visited);
  free(queue);
  return 0;
  
reconstruct_path:
  // Reconstruct and reverse the path
  int vertex_count = queue[loop_end_idx].length + 1; // +1 for start vertex
  int *path = malloc(vertex_count * sizeof(int));
  if (!path) {
    free(visited);
    free(queue);
    return 0;
  }
  
  // Start with the start vertex
  path[0] = start_vertex;
  
  // Trace back through the queue to get the path
  int idx = loop_end_idx;
  for (int i = 1; i < vertex_count; i++) {
    path[i] = queue[idx].vertex;
    idx = queue[idx].prev_index;
  }
  
  // Copy vertices to output array
  for (int i = 0; i < vertex_count; i++) {
    vertices[i] = path[i];
  }
  
  free(path);
  free(visited);
  free(queue);
  return vertex_count;
}

// Helper function to determine if a vertex sequence is clockwise
bool is_clockwise(map_data_t *map, uint16_t *vertices, uint16_t num_vertices) {
  // Use the shoelace formula to determine winding order
  int32_t sum = 0;
  for (uint16_t i = 0; i < num_vertices; i++) {
    uint16_t j = (i + 1) % num_vertices;
    sum += (int32_t)map->vertices[vertices[i]].x * map->vertices[vertices[j]].y;
    sum -= (int32_t)map->vertices[vertices[i]].y * map->vertices[vertices[j]].x;
  }
  return sum < 0; // < 0 is clockwise in Doom's coordinate system
}

//uint16_t find_existing_linedef(map_data_t *map, uint16_t *vertices, uint16_t v1_idx, uint16_t v2_idx) {
//  uint16_t v1 = vertices[v1_idx];
//  uint16_t v2 = vertices[v2_idx];
//  
//  for (int i = 0; i < map->num_linedefs; i++) {
//    if ((map->linedefs[i].start == v1 && map->linedefs[i].end == v2) ||
//        (map->linedefs[i].start == v2 && map->linedefs[i].end == v1)) {
//      return i;
//    }
//  }
//  return -1;
//}

bool set_loop_sector(map_data_t *map, uint16_t sector, uint16_t *vertices, uint16_t num_vertices) {
  if (!map || !vertices || num_vertices < 3)
    return false;
  
  uint16_t parent = find_point_sector(map, compute_centroid(map, vertices, num_vertices));

  // Determine loop orientation (clockwise or counter-clockwise)
  bool clockwise = is_clockwise(map, vertices, num_vertices);
  
  // Process each edge in the loop
  for (uint16_t i = 0; i < num_vertices; i++) {
    uint16_t j = (i + 1) % num_vertices;
    
    // Find the linedef for this edge
    uint16_t linedef_idx = find_existing_linedef(map, vertices[i], vertices[j]);
    if (linedef_idx == 0xFFFF) {
      fprintf(stderr, "Could not find linedef\n");
      return false; // Linedef not found
    }
    
    maplinedef_t *line = &map->linedefs[linedef_idx];
    uint16_t v1 = vertices[i];
    uint16_t v2 = vertices[j];
    
    // Determine which sidedef to set based on winding direction
    bool line_matches_winding = (line->start == v1 && line->end == v2);
    
    // In Doom, sidedefs[0] is the right side (facing away from line direction)
    // sidedefs[1] is the left side (facing toward line direction)
    //
    // If clockwise loop and line matches loop direction: set sidedef[1]
    // If clockwise loop and line opposes loop direction: set sidedef[0]
    // If counter-clockwise loop and line matches loop direction: set sidedef[0]
    // If counter-clockwise loop and line opposes loop direction: set sidedef[1]
    uint16_t side;
    
    if (clockwise == line_matches_winding) {
      side = 0; // Inside/left side
    } else {
      side = 1; // Outside/right side
    }
    
    // Make sure the sidedef exists
    if (line->sidenum[side] == 0xFFFF) {
      if (line->sidenum[!side] == 0xFFFF) {
        line->sidenum[0] = add_sidedef(map, sector);
        if (clockwise) {
          uint16_t tmp = line->start;
          line->start = line->end;
          line->end = tmp;
        }
        continue;
      } else {
        line->sidenum[side] = add_sidedef(map, sector);
      }
    } else {
      // Set the sector reference in the sidedef
      map->sidedefs[line->sidenum[side]].sector = sector;
    }
    if (line->sidenum[side] != 0xFFFF && line->sidenum[!side] != 0xFFFF) {
      memset(map->sidedefs[line->sidenum[!side]].midtexture, 0, sizeof(texname_t));
      memset(map->sidedefs[line->sidenum[side]].midtexture, 0, sizeof(texname_t));
    }
    if (parent == 0xFFFF && line->sidenum[!side] != 0xFFFF) {
      parent = map->sidedefs[line->sidenum[!side]].sector;
    }
  }
  
  if (parent != 0xFFFF) {
    memcpy(map->sectors+sector, map->sectors+parent, sizeof(mapsector_t));
  }

  // Rebuild vertex buffers (which will also recompute bboxes)
  build_wall_vertex_buffer(map);
  build_floor_vertex_buffer(map);
  
  return true;
}

int split_linedef(map_data_t *map, int linedef_id, float x, float y) {
  maplinedef_t *linedef = &map->linedefs[linedef_id];
  mapvertex_t split_point = { x, y };
  
  int vertex = add_vertex(map, split_point);
  uint16_t front = 0xffff, back = 0xffff;
  
  if (linedef->sidenum[0] < map->num_sidedefs) {
    front = add_sidedef(map, 0);
    memcpy(&map->sidedefs[front], &map->sidedefs[linedef->sidenum[0]], sizeof(mapsidedef_t));
  }
  
  if (linedef->sidenum[1] < map->num_sidedefs) {
    back = add_sidedef(map, 0);
    memcpy(&map->sidedefs[back], &map->sidedefs[linedef->sidenum[1]], sizeof(mapsidedef_t));
  }
  
  uint16_t end = linedef->end;
  linedef->end = vertex;
  
  add_linedef(map, end, vertex, front, back);
  
  return vertex;
}
