#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

// Forward declarations and minimal types needed for testing
// (We can't include map.h directly due to external dependencies like cglm)
typedef char texname_t[8];

typedef struct {
  int16_t x;
  int16_t y;
} mapvertex_t;

typedef struct {
  uint16_t start;
  uint16_t end;
  uint16_t flags;
  uint16_t special;
  uint16_t tag;
  uint16_t sidenum[2];
} maplinedef_t;

typedef struct {
  int16_t textureoffset;
  int16_t rowoffset;
  texname_t toptexture;
  texname_t bottomtexture;
  texname_t midtexture;
  uint16_t sector;
} mapsidedef_t;

// Bounding box coordinates (from map.h)
enum {
  BOXTOP,
  BOXBOTTOM,
  BOXLEFT,
  BOXRIGHT
};

typedef struct {
  int16_t floorheight;
  int16_t ceilingheight;
  texname_t floorpic;
  texname_t ceilingpic;
  int16_t lightlevel;
  int16_t special;
  int16_t tag;
  int16_t bbox[4];  // Bounding box for optimization
} mapsector_t;

typedef struct {
  mapvertex_t* vertices;
  int num_vertices;
  maplinedef_t* linedefs;
  int num_linedefs;
  mapsidedef_t* sidedefs;
  int num_sidedefs;
  mapsector_t* sectors;
  int num_sectors;
} map_data_t;

// Bbox computation function (copied from input.c)
void compute_sector_bbox(map_data_t *map, int sector_index) {
  if (sector_index < 0 || sector_index >= map->num_sectors) {
    return;
  }
  
  mapsector_t *sector = &map->sectors[sector_index];
  
  // Initialize bbox to extreme values
  sector->bbox[BOXTOP] = INT16_MIN;
  sector->bbox[BOXBOTTOM] = INT16_MAX;
  sector->bbox[BOXLEFT] = INT16_MAX;
  sector->bbox[BOXRIGHT] = INT16_MIN;
  
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
      if (v1->y > sector->bbox[BOXTOP]) sector->bbox[BOXTOP] = v1->y;
      if (v1->y < sector->bbox[BOXBOTTOM]) sector->bbox[BOXBOTTOM] = v1->y;
      if (v1->x < sector->bbox[BOXLEFT]) sector->bbox[BOXLEFT] = v1->x;
      if (v1->x > sector->bbox[BOXRIGHT]) sector->bbox[BOXRIGHT] = v1->x;
      
      if (v2->y > sector->bbox[BOXTOP]) sector->bbox[BOXTOP] = v2->y;
      if (v2->y < sector->bbox[BOXBOTTOM]) sector->bbox[BOXBOTTOM] = v2->y;
      if (v2->x < sector->bbox[BOXLEFT]) sector->bbox[BOXLEFT] = v2->x;
      if (v2->x > sector->bbox[BOXRIGHT]) sector->bbox[BOXRIGHT] = v2->x;
      
      found = true;
      break;
    }
  }
  
  // If no linedefs found for this sector, set bbox to zero
  if (!found) {
    sector->bbox[BOXTOP] = 0;
    sector->bbox[BOXBOTTOM] = 0;
    sector->bbox[BOXLEFT] = 0;
    sector->bbox[BOXRIGHT] = 0;
  }
}

void compute_all_sector_bboxes(map_data_t *map) {
  for (int i = 0; i < map->num_sectors; i++) {
    compute_sector_bbox(map, i);
  }
}

bool point_in_sector(map_data_t const* map, int x, int y, int sector_index) {
  if (sector_index < 0 || sector_index >= map->num_sectors) {
    return false;
  }
  
  // Quick rejection test using bounding box
  mapsector_t const* sector = &map->sectors[sector_index];
  if (x < sector->bbox[BOXLEFT] || x > sector->bbox[BOXRIGHT] ||
      y < sector->bbox[BOXBOTTOM] || y > sector->bbox[BOXTOP]) {
    return false;
  }
  
  // Perform detailed ray-casting test
  int inside = 0;
  for (int i = 0; i < map->num_linedefs; i++) {
    maplinedef_t* line = &map->linedefs[i];
    for (int s = 0; s < 2; s++) {
      int sidenum = line->sidenum[s];
      if (sidenum == 0xFFFF) continue;
      if (map->sidedefs[sidenum].sector != sector_index) continue;
      
      mapvertex_t* v1 = &map->vertices[line->start];
      mapvertex_t* v2 = &map->vertices[line->end];
      
      if (((v1->y > y) != (v2->y > y)) &&
          (x < (v2->x - v1->x) * (y - v1->y) / (v2->y - v1->y) + v1->x))
        inside = !inside;
    }
  }
  return inside;
}

// Test counter
static int tests_passed = 0;
static int tests_total = 0;

// Simple test macros
#define TEST(name) \
  printf("\nTest %d: %s... ", ++tests_total, name); \
  fflush(stdout);

#define PASS() \
  printf("PASSED\n"); \
  tests_passed++;

#define FAIL(msg) \
  printf("FAILED: %s\n", msg); \
  return false;

#define ASSERT(condition, msg) \
  if (!(condition)) { \
    FAIL(msg); \
  }

// Test helper: Create a simple square sector
bool test_simple_square_bbox() {
  TEST("Simple square sector bbox");
  
  // Create a simple map with a square sector
  map_data_t map = {0};
  
  // 4 vertices forming a square (0,0) to (100,100)
  map.num_vertices = 4;
  map.vertices = malloc(4 * sizeof(mapvertex_t));
  map.vertices[0] = (mapvertex_t){.x = 0, .y = 0};
  map.vertices[1] = (mapvertex_t){.x = 100, .y = 0};
  map.vertices[2] = (mapvertex_t){.x = 100, .y = 100};
  map.vertices[3] = (mapvertex_t){.x = 0, .y = 100};
  
  // Create sidedefs
  map.num_sidedefs = 4;
  map.sidedefs = malloc(4 * sizeof(mapsidedef_t));
  for (int i = 0; i < 4; i++) {
    map.sidedefs[i] = (mapsidedef_t){.sector = 0};
  }
  
  // Create linedefs forming the square
  map.num_linedefs = 4;
  map.linedefs = malloc(4 * sizeof(maplinedef_t));
  map.linedefs[0] = (maplinedef_t){.start = 0, .end = 1, .sidenum = {0, 0xFFFF}};
  map.linedefs[1] = (maplinedef_t){.start = 1, .end = 2, .sidenum = {1, 0xFFFF}};
  map.linedefs[2] = (maplinedef_t){.start = 2, .end = 3, .sidenum = {2, 0xFFFF}};
  map.linedefs[3] = (maplinedef_t){.start = 3, .end = 0, .sidenum = {3, 0xFFFF}};
  
  // Create one sector
  map.num_sectors = 1;
  map.sectors = malloc(sizeof(mapsector_t));
  memset(map.sectors, 0, sizeof(mapsector_t));
  
  // Compute bbox
  compute_sector_bbox(&map, 0);
  
  // Check bbox values
  ASSERT(map.sectors[0].bbox[BOXTOP] == 100, "BOXTOP should be 100");
  ASSERT(map.sectors[0].bbox[BOXBOTTOM] == 0, "BOXBOTTOM should be 0");
  ASSERT(map.sectors[0].bbox[BOXLEFT] == 0, "BOXLEFT should be 0");
  ASSERT(map.sectors[0].bbox[BOXRIGHT] == 100, "BOXRIGHT should be 100");
  
  // Test point_in_sector with bbox optimization
  // Point clearly outside bbox should return false quickly
  ASSERT(!point_in_sector(&map, -50, 50, 0), "Point outside left should be rejected");
  ASSERT(!point_in_sector(&map, 150, 50, 0), "Point outside right should be rejected");
  ASSERT(!point_in_sector(&map, 50, -50, 0), "Point outside bottom should be rejected");
  ASSERT(!point_in_sector(&map, 50, 150, 0), "Point outside top should be rejected");
  
  // Point inside bbox and inside sector
  ASSERT(point_in_sector(&map, 50, 50, 0), "Point inside should be accepted");
  
  // Cleanup
  free(map.vertices);
  free(map.sidedefs);
  free(map.linedefs);
  free(map.sectors);
  
  PASS();
  return true;
}

// Test: Empty sector (no linedefs)
bool test_empty_sector_bbox() {
  TEST("Empty sector bbox");
  
  map_data_t map = {0};
  
  // Create sector with no linedefs
  map.num_sectors = 1;
  map.sectors = malloc(sizeof(mapsector_t));
  memset(map.sectors, 0, sizeof(mapsector_t));
  
  // Initialize to non-zero to verify it gets set
  map.sectors[0].bbox[BOXTOP] = 999;
  map.sectors[0].bbox[BOXBOTTOM] = 999;
  map.sectors[0].bbox[BOXLEFT] = 999;
  map.sectors[0].bbox[BOXRIGHT] = 999;
  
  compute_sector_bbox(&map, 0);
  
  // Empty sector should have zero bbox
  ASSERT(map.sectors[0].bbox[BOXTOP] == 0, "Empty sector BOXTOP should be 0");
  ASSERT(map.sectors[0].bbox[BOXBOTTOM] == 0, "Empty sector BOXBOTTOM should be 0");
  ASSERT(map.sectors[0].bbox[BOXLEFT] == 0, "Empty sector BOXLEFT should be 0");
  ASSERT(map.sectors[0].bbox[BOXRIGHT] == 0, "Empty sector BOXRIGHT should be 0");
  
  free(map.sectors);
  
  PASS();
  return true;
}

// Test: L-shaped sector
bool test_l_shaped_sector_bbox() {
  TEST("L-shaped sector bbox");
  
  map_data_t map = {0};
  
  // 6 vertices forming an L-shape
  map.num_vertices = 6;
  map.vertices = malloc(6 * sizeof(mapvertex_t));
  map.vertices[0] = (mapvertex_t){.x = 0, .y = 0};
  map.vertices[1] = (mapvertex_t){.x = 50, .y = 0};
  map.vertices[2] = (mapvertex_t){.x = 50, .y = 50};
  map.vertices[3] = (mapvertex_t){.x = 100, .y = 50};
  map.vertices[4] = (mapvertex_t){.x = 100, .y = 100};
  map.vertices[5] = (mapvertex_t){.x = 0, .y = 100};
  
  // Create sidedefs
  map.num_sidedefs = 6;
  map.sidedefs = malloc(6 * sizeof(mapsidedef_t));
  for (int i = 0; i < 6; i++) {
    map.sidedefs[i] = (mapsidedef_t){.sector = 0};
  }
  
  // Create linedefs
  map.num_linedefs = 6;
  map.linedefs = malloc(6 * sizeof(maplinedef_t));
  for (int i = 0; i < 6; i++) {
    map.linedefs[i] = (maplinedef_t){
      .start = i, 
      .end = (i + 1) % 6, 
      .sidenum = {i, 0xFFFF}
    };
  }
  
  // Create sector
  map.num_sectors = 1;
  map.sectors = malloc(sizeof(mapsector_t));
  memset(map.sectors, 0, sizeof(mapsector_t));
  
  compute_sector_bbox(&map, 0);
  
  // Check bbox encompasses all vertices
  ASSERT(map.sectors[0].bbox[BOXTOP] == 100, "BOXTOP should be 100");
  ASSERT(map.sectors[0].bbox[BOXBOTTOM] == 0, "BOXBOTTOM should be 0");
  ASSERT(map.sectors[0].bbox[BOXLEFT] == 0, "BOXLEFT should be 0");
  ASSERT(map.sectors[0].bbox[BOXRIGHT] == 100, "BOXRIGHT should be 100");
  
  // Point outside bbox should be rejected quickly
  ASSERT(!point_in_sector(&map, -10, 50, 0), "Point outside bbox should be rejected");
  
  // Point in upper-right corner (inside bbox, outside L-shape)
  // This tests that bbox is just a quick rejection, not the final answer
  bool result = point_in_sector(&map, 75, 25, 0);
  ASSERT(!result, "Point in bbox but outside L-shape should be rejected");
  
  free(map.vertices);
  free(map.sidedefs);
  free(map.linedefs);
  free(map.sectors);
  
  PASS();
  return true;
}

// Test: Multiple sectors
bool test_multiple_sectors_bbox() {
  TEST("Multiple sectors bbox computation");
  
  map_data_t map = {0};
  
  // Create two separate square sectors
  map.num_vertices = 8;
  map.vertices = malloc(8 * sizeof(mapvertex_t));
  
  // Sector 0: (0,0) to (50,50)
  map.vertices[0] = (mapvertex_t){.x = 0, .y = 0};
  map.vertices[1] = (mapvertex_t){.x = 50, .y = 0};
  map.vertices[2] = (mapvertex_t){.x = 50, .y = 50};
  map.vertices[3] = (mapvertex_t){.x = 0, .y = 50};
  
  // Sector 1: (100,100) to (200,200)
  map.vertices[4] = (mapvertex_t){.x = 100, .y = 100};
  map.vertices[5] = (mapvertex_t){.x = 200, .y = 100};
  map.vertices[6] = (mapvertex_t){.x = 200, .y = 200};
  map.vertices[7] = (mapvertex_t){.x = 100, .y = 200};
  
  // Create sidedefs for both sectors
  map.num_sidedefs = 8;
  map.sidedefs = malloc(8 * sizeof(mapsidedef_t));
  for (int i = 0; i < 4; i++) {
    map.sidedefs[i] = (mapsidedef_t){.sector = 0};
    map.sidedefs[i + 4] = (mapsidedef_t){.sector = 1};
  }
  
  // Create linedefs
  map.num_linedefs = 8;
  map.linedefs = malloc(8 * sizeof(maplinedef_t));
  for (int i = 0; i < 4; i++) {
    map.linedefs[i] = (maplinedef_t){
      .start = i, 
      .end = (i + 1) % 4, 
      .sidenum = {i, 0xFFFF}
    };
    map.linedefs[i + 4] = (maplinedef_t){
      .start = i + 4, 
      .end = ((i + 1) % 4) + 4, 
      .sidenum = {i + 4, 0xFFFF}
    };
  }
  
  // Create sectors
  map.num_sectors = 2;
  map.sectors = malloc(2 * sizeof(mapsector_t));
  memset(map.sectors, 0, 2 * sizeof(mapsector_t));
  
  // Compute all bboxes
  compute_all_sector_bboxes(&map);
  
  // Check sector 0 bbox
  ASSERT(map.sectors[0].bbox[BOXTOP] == 50, "Sector 0 BOXTOP should be 50");
  ASSERT(map.sectors[0].bbox[BOXBOTTOM] == 0, "Sector 0 BOXBOTTOM should be 0");
  ASSERT(map.sectors[0].bbox[BOXLEFT] == 0, "Sector 0 BOXLEFT should be 0");
  ASSERT(map.sectors[0].bbox[BOXRIGHT] == 50, "Sector 0 BOXRIGHT should be 50");
  
  // Check sector 1 bbox
  ASSERT(map.sectors[1].bbox[BOXTOP] == 200, "Sector 1 BOXTOP should be 200");
  ASSERT(map.sectors[1].bbox[BOXBOTTOM] == 100, "Sector 1 BOXBOTTOM should be 100");
  ASSERT(map.sectors[1].bbox[BOXLEFT] == 100, "Sector 1 BOXLEFT should be 100");
  ASSERT(map.sectors[1].bbox[BOXRIGHT] == 200, "Sector 1 BOXRIGHT should be 200");
  
  free(map.vertices);
  free(map.sidedefs);
  free(map.linedefs);
  free(map.sectors);
  
  PASS();
  return true;
}

// Test: Negative coordinates
bool test_negative_coordinates_bbox() {
  TEST("Negative coordinates bbox");
  
  map_data_t map = {0};
  
  // Square with negative coordinates
  map.num_vertices = 4;
  map.vertices = malloc(4 * sizeof(mapvertex_t));
  map.vertices[0] = (mapvertex_t){.x = -100, .y = -100};
  map.vertices[1] = (mapvertex_t){.x = -50, .y = -100};
  map.vertices[2] = (mapvertex_t){.x = -50, .y = -50};
  map.vertices[3] = (mapvertex_t){.x = -100, .y = -50};
  
  map.num_sidedefs = 4;
  map.sidedefs = malloc(4 * sizeof(mapsidedef_t));
  for (int i = 0; i < 4; i++) {
    map.sidedefs[i] = (mapsidedef_t){.sector = 0};
  }
  
  map.num_linedefs = 4;
  map.linedefs = malloc(4 * sizeof(maplinedef_t));
  for (int i = 0; i < 4; i++) {
    map.linedefs[i] = (maplinedef_t){
      .start = i, 
      .end = (i + 1) % 4, 
      .sidenum = {i, 0xFFFF}
    };
  }
  
  map.num_sectors = 1;
  map.sectors = malloc(sizeof(mapsector_t));
  memset(map.sectors, 0, sizeof(mapsector_t));
  
  compute_sector_bbox(&map, 0);
  
  ASSERT(map.sectors[0].bbox[BOXTOP] == -50, "BOXTOP should be -50");
  ASSERT(map.sectors[0].bbox[BOXBOTTOM] == -100, "BOXBOTTOM should be -100");
  ASSERT(map.sectors[0].bbox[BOXLEFT] == -100, "BOXLEFT should be -100");
  ASSERT(map.sectors[0].bbox[BOXRIGHT] == -50, "BOXRIGHT should be -50");
  
  // Test point_in_sector works with negative coords
  ASSERT(point_in_sector(&map, -75, -75, 0), "Point inside negative coords sector");
  ASSERT(!point_in_sector(&map, 0, 0, 0), "Point outside negative coords sector");
  
  free(map.vertices);
  free(map.sidedefs);
  free(map.linedefs);
  free(map.sectors);
  
  PASS();
  return true;
}

int main() {
  printf("\n=== Running Bounding Box Tests ===\n");
  
  test_simple_square_bbox();
  test_empty_sector_bbox();
  test_l_shaped_sector_bbox();
  test_multiple_sectors_bbox();
  test_negative_coordinates_bbox();
  
  printf("\n=== Test Results ===\n");
  printf("Passed: %d/%d\n", tests_passed, tests_total);
  
  if (tests_passed == tests_total) {
    printf("\n=== All Tests Passed! ===\n");
    return 0;
  } else {
    printf("\n=== Some Tests Failed ===\n");
    return 1;
  }
}
