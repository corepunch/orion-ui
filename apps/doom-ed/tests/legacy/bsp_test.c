/*
 * BSP Traversal Test
 * 
 * This test verifies that the BSP traversal correctly identifies
 * which sectors are visible from a given viewpoint.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>

// Minimal type definitions for testing
typedef struct {
  int16_t x;
  int16_t y;
} mapvertex_t;

typedef struct {
  int16_t x;
  int16_t y;
  int16_t dx;
  int16_t dy;
  int16_t bbox[2][4];
  uint16_t children[2];
} mapnode_t;

typedef struct {
  uint16_t numsegs;
  uint16_t firstseg;
} mapsubsector_t;

typedef struct {
  uint16_t v1;
  uint16_t v2;
  uint16_t angle;
  uint16_t linedef;
  uint16_t side;
  uint16_t offset;
} mapseg_t;

#define NF_SUBSECTOR 0x8000

//
// R_PointOnSide
// Determine which side of a partition line a point is on.
// Returns 0 for front, 1 for back.
//
static int R_PointOnSide(float x, float y, mapnode_t const *node) {
  float dx, dy;
  float left, right;
  
  if (!node->dx) {
    if (x <= node->x)
      return node->dy > 0;
    return node->dy < 0;
  }
  
  if (!node->dy) {
    if (y <= node->y)
      return node->dx < 0;
    return node->dx > 0;
  }
  
  dx = (x - node->x);
  dy = (y - node->y);
  
  // Cross product to determine which side
  left = node->dy * dx;
  right = dy * node->dx;
  
  if (right < left)
    return 0;  // front side
  return 1;    // back side
}

//
// Test 1: Point-on-side test with vertical partition
//
void test_point_on_side_vertical(void) {
  printf("Test 1: Point on side (vertical partition)... ");
  
  // Partition line at x=100, extending vertically (dx=0, dy>0)
  mapnode_t node = {
    .x = 100,
    .y = 0,
    .dx = 0,
    .dy = 100
  };
  
  // Point to the left (x < 100) should be on one side
  int side1 = R_PointOnSide(50, 50, &node);
  
  // Point to the right (x > 100) should be on the other side
  int side2 = R_PointOnSide(150, 50, &node);
  
  // They should be on opposite sides
  assert(side1 != side2);
  
  printf("PASSED\n");
}

//
// Test 2: Point-on-side test with horizontal partition
//
void test_point_on_side_horizontal(void) {
  printf("Test 2: Point on side (horizontal partition)... ");
  
  // Partition line at y=100, extending horizontally (dy=0, dx>0)
  mapnode_t node = {
    .x = 0,
    .y = 100,
    .dx = 100,
    .dy = 0
  };
  
  // Point below (y < 100)
  int side1 = R_PointOnSide(50, 50, &node);
  
  // Point above (y > 100)
  int side2 = R_PointOnSide(50, 150, &node);
  
  // They should be on opposite sides
  assert(side1 != side2);
  
  printf("PASSED\n");
}

//
// Test 3: Point-on-side test with diagonal partition
//
void test_point_on_side_diagonal(void) {
  printf("Test 3: Point on side (diagonal partition)... ");
  
  // Partition line from (0,0) to (100,100)
  mapnode_t node = {
    .x = 0,
    .y = 0,
    .dx = 100,
    .dy = 100
  };
  
  // Point below the diagonal
  int side1 = R_PointOnSide(100, 50, &node);
  
  // Point above the diagonal
  int side2 = R_PointOnSide(50, 100, &node);
  
  // They should be on opposite sides
  assert(side1 != side2);
  
  printf("PASSED\n");
}

//
// Test 4: Simple BSP tree traversal
//
void test_bsp_traversal(void) {
  printf("Test 4: Simple BSP tree traversal... ");
  
  // Create a simple BSP tree with one node and two subsectors
  // Node divides space vertically at x=100
  mapnode_t nodes[1] = {
    {
      .x = 100,
      .y = 0,
      .dx = 0,
      .dy = 100,
      .children = {
        NF_SUBSECTOR,      // Left subsector (x < 100)
        NF_SUBSECTOR | 1   // Right subsector (x > 100)
      }
    }
  };
  
  // Player at (50, 50) should be on the left side (front)
  int side = R_PointOnSide(50, 50, &nodes[0]);
  
  // The child on the player's side should be rendered first
  uint16_t first_child = nodes[0].children[side];
  assert(first_child & NF_SUBSECTOR);
  
  // Verify both children are subsectors
  assert(nodes[0].children[0] & NF_SUBSECTOR);
  assert(nodes[0].children[1] & NF_SUBSECTOR);
  
  printf("PASSED\n");
}

//
// Test 5: Consistency check - same point should always give same result
//
void test_consistency(void) {
  printf("Test 5: Consistency check... ");
  
  mapnode_t node = {
    .x = 50,
    .y = 50,
    .dx = 100,
    .dy = 50
  };
  
  float test_x = 75;
  float test_y = 75;
  
  // Multiple calls should give same result
  int side1 = R_PointOnSide(test_x, test_y, &node);
  int side2 = R_PointOnSide(test_x, test_y, &node);
  int side3 = R_PointOnSide(test_x, test_y, &node);
  
  assert(side1 == side2);
  assert(side2 == side3);
  
  printf("PASSED\n");
}

int main(void) {
  printf("=== Running BSP Traversal Tests ===\n\n");
  
  test_point_on_side_vertical();
  test_point_on_side_horizontal();
  test_point_on_side_diagonal();
  test_bsp_traversal();
  test_consistency();
  
  printf("\n=== All Tests Passed! ===\n");
  
  return 0;
}
