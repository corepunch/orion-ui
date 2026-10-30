/*
 * Collision Detection Tests
 *
 * Tests for the pure geometric helper functions in collision.c:
 *   dist_sq, closest_point_on_line, calc_slide, can_enter_sector
 *
 * These functions have no external dependencies (no OpenGL, no SDL, no cglm),
 * so they can be compiled and tested in isolation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>

// ── Minimal type stubs ──────────────────────────────────────────────────────

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

typedef struct {
  int16_t floorheight;
  int16_t ceilingheight;
  texname_t floorpic;
  texname_t ceilingpic;
  int16_t lightlevel;
  int16_t special;
  int16_t tag;
} mapsector_t;

typedef struct {
  mapvertex_t *vertices;
  int num_vertices;
  maplinedef_t *linedefs;
  int num_linedefs;
  mapsidedef_t *sidedefs;
  int num_sidedefs;
  mapsector_t *sectors;
  int num_sectors;
} map_data_t;

typedef struct {
  float x, y, z;
  float angle;
  float pitch;
  float height;
  float vel_x, vel_y;
  int sector;
  int mouse_x_rel;
  int mouse_y_rel;
  float forward_move;
  float strafe_move;
} player_t;

#define EYE_HEIGHT 48
#define MAX_STEP   24.0f
#define P_RADIUS   12.0f
#define WALL_DIST  2.0f
#define EPSILON    0.1f

// ── Functions under test (copied from collision.c to isolate from map.h) ───

float dist_sq(float x1, float y1, float x2, float y2) {
  float dx = x2 - x1;
  float dy = y2 - y1;
  return dx*dx + dy*dy;
}

float closest_point_on_line(float point_x, float point_y,
                            float line_x1, float line_y1,
                            float line_x2, float line_y2,
                            float *closest_x, float *closest_y,
                            float *t_param) {
  float dx = line_x2 - line_x1;
  float dy = line_y2 - line_y1;
  float len_sq = dx*dx + dy*dy;

  if (len_sq < EPSILON) {
    *closest_x = line_x1;
    *closest_y = line_y1;
    *t_param = 0.0f;
    return dist_sq(point_x, point_y, line_x1, line_y1);
  }

  float t = ((point_x - line_x1) * dx + (point_y - line_y1) * dy) / len_sq;
  t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);

  *closest_x = line_x1 + t * dx;
  *closest_y = line_y1 + t * dy;
  *t_param = t;

  return dist_sq(point_x, point_y, *closest_x, *closest_y);
}

void calc_slide(float mx, float my, float nx, float ny, float *sx, float *sy) {
  float dot = mx * nx + my * ny;

  if (dot > -EPSILON) {
    *sx = mx;
    *sy = my;
    return;
  }

  *sx = mx - (nx * dot);
  *sy = my - (ny * dot);

  float s_len = sqrt((*sx) * (*sx) + (*sy) * (*sy));
  if (s_len > EPSILON) {
    float m_len = sqrt(mx * mx + my * my);
    *sx = (*sx) * m_len / s_len;
    *sy = (*sy) * m_len / s_len;
  }
}

bool can_enter_sector(mapsector_t const *current, mapsector_t const *new_sector, float player_z) {
  if (!new_sector) return false;

  float player_feet = player_z - EYE_HEIGHT;
  float floor_diff = new_sector->floorheight - player_feet;

  if (floor_diff > MAX_STEP) return false;
  if ((new_sector->ceilingheight - new_sector->floorheight) < EYE_HEIGHT) return false;

  return true;
}

// ── Test helpers ─────────────────────────────────────────────────────────────

static int tests_passed = 0;
static int tests_total  = 0;

static int float_eq(float a, float b, float eps) {
  return fabsf(a - b) < eps;
}

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

// ── dist_sq tests ────────────────────────────────────────────────────────────

static void test_dist_sq_same_point(void) {
  TEST("dist_sq: same point returns 0");
  ASSERT(float_eq(dist_sq(5.0f, 5.0f, 5.0f, 5.0f), 0.0f, 1e-6f),
         "dist_sq of identical points should be 0");
  PASS();
}

static void test_dist_sq_horizontal(void) {
  TEST("dist_sq: horizontal distance");
  ASSERT(float_eq(dist_sq(0.0f, 0.0f, 3.0f, 0.0f), 9.0f, 1e-6f),
         "dist_sq(0,0,3,0) should be 9");
  PASS();
}

static void test_dist_sq_diagonal(void) {
  TEST("dist_sq: 3-4-5 triangle");
  ASSERT(float_eq(dist_sq(0.0f, 0.0f, 3.0f, 4.0f), 25.0f, 1e-6f),
         "dist_sq(0,0,3,4) should be 25");
  PASS();
}

static void test_dist_sq_negative_coords(void) {
  TEST("dist_sq: negative coordinates");
  /* (-1,-1) to (2,3): dx=3, dy=4, sq=25 */
  ASSERT(float_eq(dist_sq(-1.0f, -1.0f, 2.0f, 3.0f), 25.0f, 1e-6f),
         "dist_sq with negative coords should work");
  PASS();
}

// ── closest_point_on_line tests ──────────────────────────────────────────────

static void test_closest_midpoint(void) {
  TEST("closest_point_on_line: point projects to midpoint");
  float cx, cy, t;
  /* Horizontal line (0,0)→(10,0); point (5,3) projects to (5,0) */
  float d = closest_point_on_line(5.0f, 3.0f, 0.0f, 0.0f, 10.0f, 0.0f, &cx, &cy, &t);
  ASSERT(float_eq(cx, 5.0f, 1e-4f), "Closest x should be 5");
  ASSERT(float_eq(cy, 0.0f, 1e-4f), "Closest y should be 0");
  ASSERT(float_eq(t,  0.5f, 1e-4f), "t should be 0.5");
  ASSERT(float_eq(d,  9.0f, 1e-4f), "Squared distance should be 9");
  PASS();
}

static void test_closest_clamped_to_start(void) {
  TEST("closest_point_on_line: point behind start clamped to start");
  float cx, cy, t;
  /* Line (10,0)→(20,0); point (-5,0) is behind start */
  closest_point_on_line(-5.0f, 0.0f, 10.0f, 0.0f, 20.0f, 0.0f, &cx, &cy, &t);
  ASSERT(float_eq(cx, 10.0f, 1e-4f), "Closest x should be clamped to line start");
  ASSERT(float_eq(t,  0.0f,  1e-4f), "t should be 0 (clamped)");
  PASS();
}

static void test_closest_clamped_to_end(void) {
  TEST("closest_point_on_line: point beyond end clamped to end");
  float cx, cy, t;
  /* Line (0,0)→(10,0); point (20,0) is beyond end */
  closest_point_on_line(20.0f, 0.0f, 0.0f, 0.0f, 10.0f, 0.0f, &cx, &cy, &t);
  ASSERT(float_eq(cx, 10.0f, 1e-4f), "Closest x should be clamped to line end");
  ASSERT(float_eq(t,  1.0f,  1e-4f), "t should be 1 (clamped)");
  PASS();
}

static void test_closest_degenerate_line(void) {
  TEST("closest_point_on_line: degenerate (zero-length) line");
  float cx, cy, t;
  /* Degenerate line: both endpoints identical */
  float d = closest_point_on_line(3.0f, 4.0f, 0.0f, 0.0f, 0.0f, 0.0f, &cx, &cy, &t);
  ASSERT(float_eq(cx, 0.0f, 1e-4f), "Closest x should equal line point");
  ASSERT(float_eq(cy, 0.0f, 1e-4f), "Closest y should equal line point");
  ASSERT(float_eq(t,  0.0f, 1e-4f), "t should be 0");
  ASSERT(float_eq(d, 25.0f, 1e-4f), "Squared distance should be 25 (3-4-5)");
  PASS();
}

static void test_closest_diagonal_line(void) {
  TEST("closest_point_on_line: diagonal line");
  float cx, cy, t;
  /* Line (0,0)→(10,10); point (0,10) projects to (5,5) */
  float d = closest_point_on_line(0.0f, 10.0f, 0.0f, 0.0f, 10.0f, 10.0f, &cx, &cy, &t);
  ASSERT(float_eq(cx, 5.0f, 1e-4f), "Closest x should be 5");
  ASSERT(float_eq(cy, 5.0f, 1e-4f), "Closest y should be 5");
  ASSERT(float_eq(t,  0.5f, 1e-4f), "t should be 0.5");
  /* Distance from (0,10) to (5,5) is sqrt(50), squared = 50 */
  ASSERT(float_eq(d, 50.0f, 1e-3f), "Squared distance should be 50");
  PASS();
}

// ── calc_slide tests ─────────────────────────────────────────────────────────

static void test_slide_perpendicular_wall(void) {
  TEST("calc_slide: movement into perpendicular wall gives zero slide");
  float sx, sy;
  /* Moving straight into a wall with normal (1,0) */
  calc_slide(-5.0f, 0.0f, 1.0f, 0.0f, &sx, &sy);
  /* Projection: dot = -5; slide = (-5 - 1*(-5), 0) = (0, 0) */
  ASSERT(float_eq(sx, 0.0f, 1e-3f), "Slide x should be 0 for head-on impact");
  ASSERT(float_eq(sy, 0.0f, 1e-3f), "Slide y should be 0 for head-on impact");
  PASS();
}

static void test_slide_along_wall(void) {
  TEST("calc_slide: movement parallel to wall is unchanged");
  float sx, sy;
  /* Moving along a wall: move=(0,5), normal=(1,0) — dot=0 (not into wall) */
  calc_slide(0.0f, 5.0f, 1.0f, 0.0f, &sx, &sy);
  ASSERT(float_eq(sx, 0.0f, 1e-3f), "Slide x should be 0");
  ASSERT(float_eq(sy, 5.0f, 1e-3f), "Slide y should be unchanged (5)");
  PASS();
}

static void test_slide_diagonal_into_wall(void) {
  TEST("calc_slide: diagonal movement into wall slides along wall");
  float sx, sy;
  /* Move (-3,-3), normal (1,0): dot=-3 (into wall) */
  /* slide = (-3 - 1*(-3), -3 - 0*(-3)) = (0, -3) */
  calc_slide(-3.0f, -3.0f, 1.0f, 0.0f, &sx, &sy);
  /* Result should be (0, -3) normalized to same speed as original */
  float speed_orig = sqrtf((-3.0f)*(-3.0f) + (-3.0f)*(-3.0f));
  float speed_slide = sqrtf(sx*sx + sy*sy);
  ASSERT(float_eq(speed_slide, speed_orig, 1e-3f), "Slide speed should equal move speed");
  ASSERT(sx > -1e-3f, "Slide x should be ~0 (no movement into wall)");
  PASS();
}

static void test_slide_away_from_wall(void) {
  TEST("calc_slide: movement away from wall is unchanged");
  float sx, sy;
  /* Moving away: move=(5,0), normal=(1,0): dot=5 > 0 → pass-through */
  calc_slide(5.0f, 0.0f, 1.0f, 0.0f, &sx, &sy);
  ASSERT(float_eq(sx, 5.0f, 1e-3f), "Movement away from wall should be unchanged");
  ASSERT(float_eq(sy, 0.0f, 1e-3f), "Slide y should be 0");
  PASS();
}

// ── can_enter_sector tests ───────────────────────────────────────────────────

static void test_can_enter_null_sector(void) {
  TEST("can_enter_sector: NULL new_sector returns false");
  mapsector_t cur = { .floorheight = 0, .ceilingheight = 128 };
  ASSERT(!can_enter_sector(&cur, NULL, EYE_HEIGHT), "NULL sector should return false");
  PASS();
}

static void test_can_enter_flat_floor(void) {
  TEST("can_enter_sector: flat floor transition is passable");
  mapsector_t cur  = { .floorheight = 0, .ceilingheight = 128 };
  mapsector_t next = { .floorheight = 0, .ceilingheight = 128 };
  ASSERT(can_enter_sector(&cur, &next, (float)EYE_HEIGHT),
         "Same-height floors should be passable");
  PASS();
}

static void test_can_enter_small_step_up(void) {
  TEST("can_enter_sector: step up within MAX_STEP is passable");
  mapsector_t cur  = { .floorheight = 0,  .ceilingheight = 128 };
  /* floor_diff = 16 - (EYE_HEIGHT - EYE_HEIGHT) = 16 ≤ MAX_STEP(24) */
  mapsector_t next = { .floorheight = 16, .ceilingheight = 144 };
  ASSERT(can_enter_sector(&cur, &next, (float)EYE_HEIGHT),
         "Step up of 16 should be passable");
  PASS();
}

static void test_can_enter_step_too_high(void) {
  TEST("can_enter_sector: step exceeding MAX_STEP is blocked");
  mapsector_t cur  = { .floorheight = 0,  .ceilingheight = 128 };
  /* floor_diff = 32 > MAX_STEP(24) */
  mapsector_t next = { .floorheight = 32, .ceilingheight = 160 };
  ASSERT(!can_enter_sector(&cur, &next, (float)EYE_HEIGHT),
         "Step up of 32 should be blocked");
  PASS();
}

static void test_can_enter_low_ceiling(void) {
  TEST("can_enter_sector: ceiling too low is blocked");
  mapsector_t cur  = { .floorheight = 0,  .ceilingheight = 128 };
  /* ceiling - floor = 40 < EYE_HEIGHT(48) */
  mapsector_t next = { .floorheight = 0,  .ceilingheight = 40 };
  ASSERT(!can_enter_sector(&cur, &next, (float)EYE_HEIGHT),
         "Sector with ceiling too low should be blocked");
  PASS();
}

static void test_can_enter_step_down(void) {
  TEST("can_enter_sector: step down is always passable");
  mapsector_t cur  = { .floorheight = 32, .ceilingheight = 160 };
  /* floor_diff = 0 - 32 = -32 ≤ MAX_STEP */
  mapsector_t next = { .floorheight = 0,  .ceilingheight = 128 };
  ASSERT(can_enter_sector(&cur, &next, 32.0f + EYE_HEIGHT),
         "Stepping down should always be passable");
  PASS();
}

static void test_can_enter_exact_max_step(void) {
  TEST("can_enter_sector: step equal to MAX_STEP is passable");
  mapsector_t cur  = { .floorheight = 0,              .ceilingheight = 128 };
  /* floor_diff = 24 == MAX_STEP → should pass (≤ MAX_STEP) */
  mapsector_t next = { .floorheight = (int16_t)MAX_STEP, .ceilingheight = 128 + (int16_t)MAX_STEP };
  ASSERT(can_enter_sector(&cur, &next, (float)EYE_HEIGHT),
         "Step equal to MAX_STEP should be passable");
  PASS();
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(void) {
  printf("\n=== Running Collision Detection Tests ===\n");

  /* dist_sq */
  test_dist_sq_same_point();
  test_dist_sq_horizontal();
  test_dist_sq_diagonal();
  test_dist_sq_negative_coords();

  /* closest_point_on_line */
  test_closest_midpoint();
  test_closest_clamped_to_start();
  test_closest_clamped_to_end();
  test_closest_degenerate_line();
  test_closest_diagonal_line();

  /* calc_slide */
  test_slide_perpendicular_wall();
  test_slide_along_wall();
  test_slide_diagonal_into_wall();
  test_slide_away_from_wall();

  /* can_enter_sector */
  test_can_enter_null_sector();
  test_can_enter_flat_floor();
  test_can_enter_small_step_up();
  test_can_enter_step_too_high();
  test_can_enter_low_ceiling();
  test_can_enter_step_down();
  test_can_enter_exact_max_step();

  printf("\n=== Test Results ===\n");
  printf("Passed: %d/%d\n", tests_passed, tests_total);

  if (tests_passed == tests_total) {
    printf("\n=== All Tests Passed! ===\n");
    return 0;
  }
  printf("\n=== Some Tests Failed ===\n");
  return 1;
}
