#include <tests/map_test.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>

// External function to test
int triangulate_sector(mapvertex_t *vertices, int vertex_count, wall_vertex_t *out_vertices);

// Helper to check if two floats are approximately equal
static int float_equals(float a, float b, float epsilon) {
  return fabs(a - b) < epsilon;
}

// Helper to calculate area of a triangle
static float triangle_area(float ax, float ay, float bx, float by, float cx, float cy) {
  return fabs((bx - ax) * (cy - ay) - (by - ay) * (cx - ax)) / 2.0f;
}

// Helper to calculate polygon area (absolute value)
static float polygon_area(mapvertex_t const *vertices, int count) {
  float area2 = 0.0f;
  for (int i = 0; i < count; i++) {
    int j = (i + 1) % count;
    area2 += (float)vertices[i].x * (float)vertices[j].y -
             (float)vertices[j].x * (float)vertices[i].y;
  }
  return fabsf(area2) / 2.0f;
}

// Helper to check that an output vertex comes from the original polygon
static int is_vertex_in_input_set(mapvertex_t const *vertices, int count, int16_t x, int16_t y) {
  for (int i = 0; i < count; i++) {
    if (vertices[i].x == x && vertices[i].y == y) {
      return 1;
    }
  }
  return 0;
}

// Test 1: Simple triangle (should return the same triangle)
static void test_simple_triangle(void) {
  printf("Test 1: Simple triangle... ");
  
  mapvertex_t vertices[3] = {
    {0, 0},
    {100, 0},
    {50, 100}
  };
  
  wall_vertex_t out_vertices[10];
  int result = triangulate_sector(vertices, 3, out_vertices);
  
  assert(result == 3);  // Should produce exactly 1 triangle (3 vertices)
  
  printf("PASSED\n");
}

// Test 2: Simple square (should produce 2 triangles)
static void test_simple_square(void) {
  printf("Test 2: Simple square... ");
  
  mapvertex_t vertices[4] = {
    {0, 0},
    {100, 0},
    {100, 100},
    {0, 100}
  };
  
  wall_vertex_t out_vertices[20];
  int result = triangulate_sector(vertices, 4, out_vertices);
  
  assert(result == 6);  // Should produce 2 triangles (6 vertices)
  
  printf("PASSED\n");
}

// Test 3: Pentagon (should produce 3 triangles)
static void test_pentagon(void) {
  printf("Test 3: Pentagon... ");
  
  mapvertex_t vertices[5] = {
    {50, 0},
    {100, 38},
    {81, 100},
    {19, 100},
    {0, 38}
  };
  
  wall_vertex_t out_vertices[30];
  int result = triangulate_sector(vertices, 5, out_vertices);
  
  assert(result == 9);  // Should produce 3 triangles (9 vertices)
  
  printf("PASSED\n");
}

// Test 4: Concave polygon (L-shape)
static void test_concave_l_shape(void) {
  printf("Test 4: Concave L-shape... ");
  
  mapvertex_t vertices[6] = {
    {0, 0},
    {100, 0},
    {100, 50},
    {50, 50},
    {50, 100},
    {0, 100}
  };
  
  wall_vertex_t out_vertices[30];
  int result = triangulate_sector(vertices, 6, out_vertices);
  
  assert(result == 12);  // Should produce 4 triangles (12 vertices)
  
  printf("PASSED\n");
}

// Test 5: Complex concave polygon
static void test_complex_concave(void) {
  printf("Test 5: Complex concave polygon... ");
  
  mapvertex_t vertices[8] = {
    {0, 0},
    {200, 0},
    {200, 100},
    {150, 100},
    {150, 50},
    {50, 50},
    {50, 100},
    {0, 100}
  };
  
  wall_vertex_t out_vertices[50];
  int result = triangulate_sector(vertices, 8, out_vertices);
  
  assert(result == 18);  // Should produce 6 triangles (18 vertices)
  
  printf("PASSED\n");
}

// Test 6: Clockwise winding (should still work)
static void test_clockwise_winding(void) {
  printf("Test 6: Clockwise winding... ");
  
  mapvertex_t vertices[4] = {
    {0, 0},
    {0, 100},
    {100, 100},
    {100, 0}
  };
  
  wall_vertex_t out_vertices[20];
  int result = triangulate_sector(vertices, 4, out_vertices);
  
  assert(result == 6);  // Should produce 2 triangles (6 vertices)
  
  printf("PASSED\n");
}

// Test 7: Very small triangle (edge case)
static void test_tiny_triangle(void) {
  printf("Test 7: Tiny triangle... ");
  
  mapvertex_t vertices[3] = {
    {0, 0},
    {1, 0},
    {0, 1}
  };
  
  wall_vertex_t out_vertices[10];
  int result = triangulate_sector(vertices, 3, out_vertices);
  
  assert(result == 3);  // Should still produce 1 triangle (3 vertices)
  
  printf("PASSED\n");
}

// Test 8: Large polygon (stress test)
static void test_large_polygon(void) {
  printf("Test 8: Large polygon (16 vertices)... ");
  
  mapvertex_t vertices[16];
  // Create a circular-ish polygon with 16 vertices
  for (int i = 0; i < 16; i++) {
    float angle = (float)i * 2.0f * M_PI / 16.0f;
    vertices[i].x = (int)(100.0f + 80.0f * cos(angle));
    vertices[i].y = (int)(100.0f + 80.0f * sin(angle));
  }
  
  wall_vertex_t out_vertices[100];
  int result = triangulate_sector(vertices, 16, out_vertices);
  
  assert(result == 42);  // Should produce 14 triangles (42 vertices)
  
  printf("PASSED\n");
}

// Test 9: Invalid input (less than 3 vertices)
static void test_invalid_input(void) {
  printf("Test 9: Invalid input (< 3 vertices)... ");
  
  mapvertex_t vertices[2] = {
    {0, 0},
    {100, 0}
  };
  
  wall_vertex_t out_vertices[10];
  int result = triangulate_sector(vertices, 2, out_vertices);
  
  assert(result == 0);  // Should return 0 for invalid input
  
  printf("PASSED\n");
}

// Test 10: Star-shaped polygon
static void test_star_shape(void) {
  printf("Test 10: Star-shaped polygon... ");
  
  // 5-pointed star (concave)
  mapvertex_t vertices[10] = {
    {50, 0},      // Top point
    {61, 35},     // Inner
    {95, 35},     // Outer
    {68, 57},     // Inner
    {79, 91},     // Outer
    {50, 70},     // Inner
    {21, 91},     // Outer
    {32, 57},     // Inner
    {5, 35},      // Outer
    {39, 35}      // Inner
  };
  
  wall_vertex_t out_vertices[100];
  int result = triangulate_sector(vertices, 10, out_vertices);
  
  assert(result == 24);  // Should produce 8 triangles (24 vertices)
  
  printf("PASSED\n");
}

// Test 11: Verify total area is preserved
static void test_area_preservation(void) {
  printf("Test 11: Area preservation... ");
  
  mapvertex_t vertices[4] = {
    {0, 0},
    {100, 0},
    {100, 100},
    {0, 100}
  };
  
  wall_vertex_t out_vertices[20];
  int result = triangulate_sector(vertices, 4, out_vertices);
  
  // Calculate total area of triangulated output
  float total_area = 0.0f;
  for (int i = 0; i < result; i += 3) {
    float ax = out_vertices[i].x;
    float ay = out_vertices[i].y;
    float bx = out_vertices[i + 1].x;
    float by = out_vertices[i + 1].y;
    float cx = out_vertices[i + 2].x;
    float cy = out_vertices[i + 2].y;
    total_area += triangle_area(ax, ay, bx, by, cx, cy);
  }
  
  // Original square has area of 10000
  assert(float_equals(total_area, 10000.0f, 1.0f));
  
  printf("PASSED\n");
}

// Test 12: All-collinear input should not emit triangles
static void test_all_collinear(void) {
  printf("Test 12: All-collinear input... ");

  mapvertex_t vertices[5] = {
    {0, 0},
    {10, 0},
    {20, 0},
    {30, 0},
    {40, 0}
  };

  wall_vertex_t out_vertices[30];
  int result = triangulate_sector(vertices, 5, out_vertices);
  assert(result == 0);

  printf("PASSED\n");
}

// Test 13: Polygon with collinear edge points should still triangulate
static void test_collinear_edge_points(void) {
  printf("Test 13: Collinear edge points... ");

  mapvertex_t vertices[6] = {
    {0, 0},
    {50, 0},
    {100, 0},
    {100, 100},
    {50, 100},
    {0, 100}
  };

  wall_vertex_t out_vertices[40];
  int result = triangulate_sector(vertices, 6, out_vertices);
  assert(result == 12); // (6 - 2) triangles * 3 vertices

  float total_area = 0.0f;
  for (int i = 0; i < result; i += 3) {
    total_area += triangle_area(
      out_vertices[i].x, out_vertices[i].y,
      out_vertices[i + 1].x, out_vertices[i + 1].y,
      out_vertices[i + 2].x, out_vertices[i + 2].y);
  }
  assert(float_equals(total_area, polygon_area(vertices, 6), 1.0f));
  for (int i = 0; i < result; i++) {
    assert(is_vertex_in_input_set(vertices, 6, out_vertices[i].x, out_vertices[i].y));
  }

  printf("PASSED\n");
}

// Test 14: Output vertices should always come from input polygon vertices
static void test_output_vertices_from_input_set(void) {
  printf("Test 14: Output vertices are from input set... ");

  mapvertex_t vertices[7] = {
    {0, 0},
    {90, 0},
    {120, 40},
    {80, 80},
    {120, 120},
    {40, 130},
    {0, 70}
  };

  wall_vertex_t out_vertices[50];
  int result = triangulate_sector(vertices, 7, out_vertices);
  assert(result == 15); // (7 - 2) triangles * 3 vertices

  for (int i = 0; i < result; i++) {
    assert(is_vertex_in_input_set(vertices, 7, out_vertices[i].x, out_vertices[i].y));
  }

  printf("PASSED\n");
}

// Test 15: Triangle count scales for larger convex polygons
static void test_convex_triangle_count_scaling(void) {
  printf("Test 15: Convex triangle count scaling... ");

  enum { NUM_VERTICES = 24 };
  mapvertex_t vertices[NUM_VERTICES];
  for (int i = 0; i < NUM_VERTICES; i++) {
    float angle = (float)i * 2.0f * M_PI / (float)NUM_VERTICES;
    vertices[i].x = (int16_t)(200.0f + 150.0f * cosf(angle));
    vertices[i].y = (int16_t)(200.0f + 150.0f * sinf(angle));
  }

  wall_vertex_t out_vertices[120];
  int result = triangulate_sector(vertices, NUM_VERTICES, out_vertices);
  assert(result == (NUM_VERTICES - 2) * 3);

  float total_area = 0.0f;
  for (int i = 0; i < result; i += 3) {
    total_area += triangle_area(
      out_vertices[i].x, out_vertices[i].y,
      out_vertices[i + 1].x, out_vertices[i + 1].y,
      out_vertices[i + 2].x, out_vertices[i + 2].y);
  }
  assert(float_equals(total_area, polygon_area(vertices, NUM_VERTICES), 2.0f));

  printf("PASSED\n");
}

// Test 16: Degenerate duplicated interior points do not crash triangulation
static void test_repeated_points(void) {
  printf("Test 16: Repeated points... ");

  mapvertex_t vertices[8] = {
    {0, 0},
    {100, 0},
    {100, 0},   // repeated point
    {100, 100},
    {50, 100},
    {50, 100},  // repeated point
    {0, 100},
    {0, 50}
  };

  wall_vertex_t out_vertices[80];
  int result = triangulate_sector(vertices, 8, out_vertices);
  assert(result >= 0);
  assert(result <= (8 - 2) * 3);

  printf("PASSED\n");
}

// Main test runner
int main(void) {
  printf("\n=== Running Triangulation Tests ===\n\n");
  
  test_simple_triangle();
  test_simple_square();
  test_pentagon();
  test_concave_l_shape();
  test_complex_concave();
  test_clockwise_winding();
  test_tiny_triangle();
  test_large_polygon();
  test_invalid_input();
  test_star_shape();
  test_area_preservation();
  test_all_collinear();
  test_collinear_edge_points();
  test_output_vertices_from_input_set();
  test_convex_triangle_count_scaling();
  test_repeated_points();
  
  printf("\n=== All Tests Passed! ===\n\n");
  
  return 0;
}
