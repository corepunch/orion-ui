/*
 * Wall Normal Tests
 *
 * Tests for compute_normal_packed() from walls.c:
 *   - Packs a 2D perpendicular normal into signed 8-bit components.
 *   - Input (dx, dy) is the wall direction vector.
 *   - Normal = (-dy, dx) normalised to unit length, packed into int8_t * 127.
 *   - Returns the length of the input vector (useful as a wall length).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>

// ── Function under test ──────────────────────────────────────────────────────
//
// Copied verbatim from walls.c.  walls.c cannot be #included in a standalone
// test because it pulls in OpenGL, cglm and map.h (which in turn requires SDL2
// and the ui submodule).  All other test files in this repo use the same
// copy-and-compile pattern for the same reason.  If the production
// implementation changes, update this copy to match.

static float compute_normal_packed(float dx, float dy, int8_t out[3]) {
  float nx = -dy;
  float ny = dx;

  float length = sqrtf(nx * nx + ny * ny);
  if (length == 0.0f) {
    out[0] = out[1] = out[2] = 0;
    return 0;
  }

  nx /= length;
  ny /= length;

  out[0] = (int8_t)(nx * 127.0f);
  out[1] = (int8_t)(ny * 127.0f);
  out[2] = 0;

  return length;
}

// ── Test helpers ─────────────────────────────────────────────────────────────

static int tests_passed = 0;
static int tests_total  = 0;

#define TEST(name) \
  printf("\nTest %d: %s... ", ++tests_total, name); \
  fflush(stdout)

#define PASS() \
  printf("PASSED\n"); \
  tests_passed++

#define ASSERT(cond, msg) \
  if (!(cond)) { \
    printf("FAILED: %s\n", msg); \
    return; \
  }

static int float_eq(float a, float b, float eps) {
  return fabsf(a - b) < eps;
}

/* Packed normals are rounded toward zero; allow ±2 for platform rounding. */
static int i8_near(int8_t a, int expected) {
  return abs((int)a - expected) <= 2;
}

// ── Tests ────────────────────────────────────────────────────────────────────

/* Rightward wall: dx=1, dy=0  →  normal = (-0, 1) = (0,+1)
 * out[0]=0, out[1]=+127, return=1 */
static void test_rightward_wall(void) {
  TEST("rightward wall (dx=1, dy=0): normal is (0, +127)");
  int8_t n[3];
  float len = compute_normal_packed(1.0f, 0.0f, n);
  ASSERT(i8_near(n[0], 0),    "out[0] should be 0");
  ASSERT(i8_near(n[1], 127),  "out[1] should be +127");
  ASSERT(n[2] == 0,           "out[2] should always be 0");
  ASSERT(float_eq(len, 1.0f, 1e-5f), "return value should be 1.0");
  PASS();
}

/* Leftward wall: dx=-1, dy=0  →  normal = (0, -1)
 * out[0]=0, out[1]=-127 */
static void test_leftward_wall(void) {
  TEST("leftward wall (dx=-1, dy=0): normal is (0, -127)");
  int8_t n[3];
  float len = compute_normal_packed(-1.0f, 0.0f, n);
  ASSERT(i8_near(n[0], 0),    "out[0] should be 0");
  ASSERT(i8_near(n[1], -127), "out[1] should be -127");
  ASSERT(n[2] == 0,           "out[2] should always be 0");
  ASSERT(float_eq(len, 1.0f, 1e-5f), "return value should be 1.0");
  PASS();
}

/* Upward wall: dx=0, dy=1  →  normal = (-1, 0)
 * out[0]=-127, out[1]=0 */
static void test_upward_wall(void) {
  TEST("upward wall (dx=0, dy=1): normal is (-127, 0)");
  int8_t n[3];
  float len = compute_normal_packed(0.0f, 1.0f, n);
  ASSERT(i8_near(n[0], -127), "out[0] should be -127");
  ASSERT(i8_near(n[1], 0),    "out[1] should be 0");
  ASSERT(n[2] == 0,           "out[2] should always be 0");
  ASSERT(float_eq(len, 1.0f, 1e-5f), "return value should be 1.0");
  PASS();
}

/* Downward wall: dx=0, dy=-1  →  normal = (+1, 0)
 * out[0]=+127, out[1]=0 */
static void test_downward_wall(void) {
  TEST("downward wall (dx=0, dy=-1): normal is (+127, 0)");
  int8_t n[3];
  float len = compute_normal_packed(0.0f, -1.0f, n);
  ASSERT(i8_near(n[0], 127),  "out[0] should be +127");
  ASSERT(i8_near(n[1], 0),    "out[1] should be 0");
  ASSERT(n[2] == 0,           "out[2] should always be 0");
  ASSERT(float_eq(len, 1.0f, 1e-5f), "return value should be 1.0");
  PASS();
}

/* Degenerate wall (zero vector): length==0 → all zeros, return 0 */
static void test_degenerate_wall(void) {
  TEST("degenerate wall (dx=0, dy=0): returns 0 and all-zero normal");
  int8_t n[3] = {99, 99, 99};
  float len = compute_normal_packed(0.0f, 0.0f, n);
  ASSERT(float_eq(len, 0.0f, 1e-10f), "return value should be 0");
  ASSERT(n[0] == 0, "out[0] should be 0 for degenerate input");
  ASSERT(n[1] == 0, "out[1] should be 0 for degenerate input");
  ASSERT(n[2] == 0, "out[2] should be 0 for degenerate input");
  PASS();
}

/* 45° diagonal wall (dx=1, dy=1):
 * nx = -1/√2 ≈ -0.707  → out[0] ≈ -89 or -90
 * ny =  1/√2 ≈ +0.707  → out[1] ≈ +89 or +90
 * length = √2 */
static void test_diagonal_wall(void) {
  TEST("45° diagonal wall (dx=1, dy=1)");
  int8_t n[3];
  float len = compute_normal_packed(1.0f, 1.0f, n);
  ASSERT(float_eq(len, sqrtf(2.0f), 1e-5f), "return should be sqrt(2)");
  /* nx = -1/√2 → packed ≈ -90; ny = 1/√2 → packed ≈ 90 */
  ASSERT(n[0] < 0,   "out[0] should be negative for 45° diagonal");
  ASSERT(n[1] > 0,   "out[1] should be positive for 45° diagonal");
  ASSERT(n[2] == 0,  "out[2] should always be 0");
  /* The two components should be roughly equal in magnitude */
  ASSERT(abs((int)n[0]) == abs((int)n[1]) || abs(abs((int)n[0]) - abs((int)n[1])) <= 1,
         "components should have equal magnitude for 45°");
  PASS();
}

/* 3-4-5 right triangle: dx=3, dy=4
 * nx = -4/5 = -0.8  → out[0] = (int8_t)(-101.6) = -101
 * ny =  3/5 = +0.6  → out[1] = (int8_t)( 76.2)  =  76
 * length = 5 */
static void test_3_4_5_wall(void) {
  TEST("3-4-5 wall (dx=3, dy=4): return=5, normals correct");
  int8_t n[3];
  float len = compute_normal_packed(3.0f, 4.0f, n);
  ASSERT(float_eq(len, 5.0f, 1e-4f), "return value should be 5 for 3-4-5");
  ASSERT(i8_near(n[0], -101), "out[0] ≈ -101 for 3-4-5");
  ASSERT(i8_near(n[1],   76), "out[1] ≈  76 for 3-4-5");
  ASSERT(n[2] == 0,           "out[2] should always be 0");
  PASS();
}

/* Scale independence: doubling the input length doubles the return value
 * but should produce the same packed normal components. */
static void test_scale_independence(void) {
  TEST("scale independence: double length → same normal, double return");
  int8_t n1[3], n2[3];
  float len1 = compute_normal_packed(1.0f,  0.0f, n1);
  float len2 = compute_normal_packed(2.0f,  0.0f, n2);
  ASSERT(float_eq(len2, 2.0f * len1, 1e-5f), "return should scale with input length");
  ASSERT(n1[0] == n2[0], "packed normal x should be same regardless of scale");
  ASSERT(n1[1] == n2[1], "packed normal y should be same regardless of scale");
  ASSERT(n2[2] == 0,     "out[2] should always be 0");
  PASS();
}

/* The z component should always be zero regardless of input. */
static void test_z_component_always_zero(void) {
  TEST("z component is always 0 for any valid input");
  int8_t n[3];
  compute_normal_packed(1.0f, 1.0f, n);
  ASSERT(n[2] == 0, "out[2] should be 0 for diagonal");
  compute_normal_packed(0.0f, 5.0f, n);
  ASSERT(n[2] == 0, "out[2] should be 0 for vertical");
  compute_normal_packed(-3.0f, -7.0f, n);
  ASSERT(n[2] == 0, "out[2] should be 0 for arbitrary direction");
  PASS();
}

/* Opposite directions produce opposite normals (anti-parallel walls). */
static void test_opposite_directions_flip_normal(void) {
  TEST("opposite wall directions produce negated normals");
  int8_t n_fwd[3], n_rev[3];
  compute_normal_packed( 1.0f, 0.0f, n_fwd);
  compute_normal_packed(-1.0f, 0.0f, n_rev);
  /* n_fwd and n_rev should be negations of each other (within rounding) */
  ASSERT(i8_near(n_fwd[0], -n_rev[0]), "x components should be negated");
  ASSERT(i8_near(n_fwd[1], -n_rev[1]), "y components should be negated");
  PASS();
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(void) {
  printf("\n=== Running Wall Normal Tests ===\n");

  test_rightward_wall();
  test_leftward_wall();
  test_upward_wall();
  test_downward_wall();
  test_degenerate_wall();
  test_diagonal_wall();
  test_3_4_5_wall();
  test_scale_independence();
  test_z_component_always_zero();
  test_opposite_directions_flip_normal();

  printf("\n=== Test Results ===\n");
  printf("Passed: %d/%d\n", tests_passed, tests_total);

  if (tests_passed == tests_total) {
    printf("\n=== All Tests Passed! ===\n");
    return 0;
  }
  printf("\n=== Some Tests Failed ===\n");
  return 1;
}
