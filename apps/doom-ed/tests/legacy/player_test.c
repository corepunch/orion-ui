/*
 * Player Physics Tests
 *
 * Tests for the mathematical operations governing player movement in input.c:
 *   - Angle wrapping (kept in 0–360 range)
 *   - Pitch clamping (kept in –89 to +89)
 *   - Velocity clamping (speed capped at MAX_SPEED)
 *   - Friction / deceleration (speed reduced toward 0 without going negative)
 *
 * These are pure floating-point calculations with no external dependencies,
 * so they can be exercised entirely in isolation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>

// ── Constants (from map.h / input.c) ─────────────────────────────────────────

#define MAX_SPEED    300.0f
#define ACCELERATION 1000.0f
#define FRICTION     1200.0f

// ── Pure-math helpers extracted from game_tick() in input.c ──────────────────

/* Wrap angle into [0, 360). */
static float wrap_angle(float angle) {
  if (angle < 0.0f)   angle += 360.0f;
  if (angle >= 360.0f) angle -= 360.0f;
  return angle;
}

/* Clamp pitch into [-89, 89]. */
static float clamp_pitch(float pitch) {
  if (pitch >  89.0f) pitch =  89.0f;
  if (pitch < -89.0f) pitch = -89.0f;
  return pitch;
}

/* Clamp a velocity vector (vx, vy) so its magnitude never exceeds max_speed.
 * Direction is preserved; if speed <= max_speed the vector is unchanged. */
static void clamp_velocity(float *vx, float *vy, float max_speed) {
  float speed = sqrtf(*vx * *vx + *vy * *vy);
  if (speed > max_speed) {
    float scale = max_speed / speed;
    *vx *= scale;
    *vy *= scale;
  }
}

/* Apply one step of friction to a velocity vector.
 * Reduces speed by (friction * dt) but never below zero. */
static void apply_friction(float *vx, float *vy, float friction, float dt) {
  float speed = sqrtf(*vx * *vx + *vy * *vy);
  if (speed <= 0.0f) return;
  float decel = friction * dt;
  float new_speed = fmaxf(0.0f, speed - decel);
  float scale = new_speed / speed;
  *vx *= scale;
  *vy *= scale;
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

// ── Angle wrapping tests ─────────────────────────────────────────────────────

static void test_angle_wrap_below_zero(void) {
  TEST("angle wrap: -10 wraps to 350");
  float a = wrap_angle(-10.0f);
  ASSERT(float_eq(a, 350.0f, 1e-4f), "angle -10 should wrap to 350");
  PASS();
}

static void test_angle_wrap_exactly_zero(void) {
  TEST("angle wrap: 0 stays 0");
  float a = wrap_angle(0.0f);
  ASSERT(float_eq(a, 0.0f, 1e-4f), "angle 0 should stay 0");
  PASS();
}

static void test_angle_wrap_below_360(void) {
  TEST("angle wrap: 359 stays 359");
  float a = wrap_angle(359.0f);
  ASSERT(float_eq(a, 359.0f, 1e-4f), "angle 359 should be unchanged");
  PASS();
}

static void test_angle_wrap_exactly_360(void) {
  TEST("angle wrap: 360 wraps to 0");
  float a = wrap_angle(360.0f);
  ASSERT(float_eq(a, 0.0f, 1e-4f), "angle 360 should wrap to 0");
  PASS();
}

static void test_angle_wrap_above_360(void) {
  TEST("angle wrap: 370 wraps to 10");
  float a = wrap_angle(370.0f);
  ASSERT(float_eq(a, 10.0f, 1e-4f), "angle 370 should wrap to 10");
  PASS();
}

// ── Pitch clamping tests ──────────────────────────────────────────────────────

static void test_pitch_clamp_above_max(void) {
  TEST("pitch clamp: 100 clamped to 89");
  float p = clamp_pitch(100.0f);
  ASSERT(float_eq(p, 89.0f, 1e-4f), "pitch above 89 should be clamped to 89");
  PASS();
}

static void test_pitch_clamp_below_min(void) {
  TEST("pitch clamp: -100 clamped to -89");
  float p = clamp_pitch(-100.0f);
  ASSERT(float_eq(p, -89.0f, 1e-4f), "pitch below -89 should be clamped to -89");
  PASS();
}

static void test_pitch_clamp_at_max(void) {
  TEST("pitch clamp: exactly 89 stays 89");
  float p = clamp_pitch(89.0f);
  ASSERT(float_eq(p, 89.0f, 1e-4f), "pitch at exactly 89 should not be clamped");
  PASS();
}

static void test_pitch_clamp_at_min(void) {
  TEST("pitch clamp: exactly -89 stays -89");
  float p = clamp_pitch(-89.0f);
  ASSERT(float_eq(p, -89.0f, 1e-4f), "pitch at exactly -89 should not be clamped");
  PASS();
}

static void test_pitch_clamp_in_range(void) {
  TEST("pitch clamp: 45 unchanged");
  float p = clamp_pitch(45.0f);
  ASSERT(float_eq(p, 45.0f, 1e-4f), "pitch in valid range should be unchanged");
  PASS();
}

// ── Velocity clamping tests ───────────────────────────────────────────────────

static void test_velocity_clamp_above_max(void) {
  TEST("velocity clamp: speed above MAX_SPEED gets clamped");
  float vx = MAX_SPEED * 2.0f;
  float vy = 0.0f;
  clamp_velocity(&vx, &vy, MAX_SPEED);
  float speed = sqrtf(vx*vx + vy*vy);
  ASSERT(float_eq(speed, MAX_SPEED, 1e-3f), "speed should be clamped to MAX_SPEED");
  PASS();
}

static void test_velocity_clamp_below_max(void) {
  TEST("velocity clamp: speed below MAX_SPEED unchanged");
  float vx = MAX_SPEED * 0.5f;
  float vy = 0.0f;
  float orig_vx = vx;
  clamp_velocity(&vx, &vy, MAX_SPEED);
  ASSERT(float_eq(vx, orig_vx, 1e-4f), "vx should not change when speed < MAX_SPEED");
  ASSERT(float_eq(vy, 0.0f,    1e-4f), "vy should remain 0");
  PASS();
}

static void test_velocity_clamp_preserves_direction(void) {
  TEST("velocity clamp: direction preserved after clamping");
  float vx = MAX_SPEED * 3.0f;
  float vy = MAX_SPEED * 4.0f;            /* 3-4-5 scaled by MAX_SPEED */
  float orig_angle = atan2f(vy, vx);
  clamp_velocity(&vx, &vy, MAX_SPEED);
  float new_angle  = atan2f(vy, vx);
  ASSERT(float_eq(orig_angle, new_angle, 1e-4f), "direction should be preserved");
  float speed = sqrtf(vx*vx + vy*vy);
  ASSERT(float_eq(speed, MAX_SPEED, 1e-3f), "speed should be clamped to MAX_SPEED");
  PASS();
}

static void test_velocity_clamp_zero(void) {
  TEST("velocity clamp: zero vector stays zero");
  float vx = 0.0f, vy = 0.0f;
  clamp_velocity(&vx, &vy, MAX_SPEED);
  ASSERT(float_eq(vx, 0.0f, 1e-6f), "zero vx stays zero");
  ASSERT(float_eq(vy, 0.0f, 1e-6f), "zero vy stays zero");
  PASS();
}

// ── Friction tests ────────────────────────────────────────────────────────────

static void test_friction_reduces_speed(void) {
  TEST("friction: speed decreases after one tick");
  float vx = MAX_SPEED;
  float vy = 0.0f;
  float dt = 1.0f / 60.0f;   /* one frame at 60 fps */
  apply_friction(&vx, &vy, FRICTION, dt);
  float speed = sqrtf(vx*vx + vy*vy);
  ASSERT(speed < MAX_SPEED, "speed should decrease after friction");
  ASSERT(speed >= 0.0f,     "speed should not go negative");
  PASS();
}

static void test_friction_stops_at_zero(void) {
  TEST("friction: large dt brings speed to zero, not below");
  float vx = 1.0f;
  float vy = 0.0f;
  apply_friction(&vx, &vy, FRICTION, 100.0f);   /* huge dt */
  float speed = sqrtf(vx*vx + vy*vy);
  ASSERT(float_eq(speed, 0.0f, 1e-6f), "speed should reach 0, not go negative");
  PASS();
}

static void test_friction_stationary_object(void) {
  TEST("friction: stationary object stays stationary");
  float vx = 0.0f, vy = 0.0f;
  apply_friction(&vx, &vy, FRICTION, 1.0f / 60.0f);
  ASSERT(float_eq(vx, 0.0f, 1e-6f), "stationary vx should stay 0");
  ASSERT(float_eq(vy, 0.0f, 1e-6f), "stationary vy should stay 0");
  PASS();
}

static void test_friction_preserves_direction(void) {
  TEST("friction: direction preserved while decelerating");
  float vx = MAX_SPEED * 0.7f;
  float vy = MAX_SPEED * 0.7f;
  float orig_angle = atan2f(vy, vx);
  float dt = 1.0f / 60.0f;
  apply_friction(&vx, &vy, FRICTION, dt);
  float speed = sqrtf(vx*vx + vy*vy);
  if (speed > 0.0f) {
    float new_angle = atan2f(vy, vx);
    ASSERT(float_eq(orig_angle, new_angle, 1e-4f), "direction should be preserved");
  }
  PASS();
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(void) {
  printf("\n=== Running Player Physics Tests ===\n");

  /* Angle wrapping */
  test_angle_wrap_below_zero();
  test_angle_wrap_exactly_zero();
  test_angle_wrap_below_360();
  test_angle_wrap_exactly_360();
  test_angle_wrap_above_360();

  /* Pitch clamping */
  test_pitch_clamp_above_max();
  test_pitch_clamp_below_min();
  test_pitch_clamp_at_max();
  test_pitch_clamp_at_min();
  test_pitch_clamp_in_range();

  /* Velocity clamping */
  test_velocity_clamp_above_max();
  test_velocity_clamp_below_max();
  test_velocity_clamp_preserves_direction();
  test_velocity_clamp_zero();

  /* Friction */
  test_friction_reduces_speed();
  test_friction_stops_at_zero();
  test_friction_stationary_object();
  test_friction_preserves_direction();

  printf("\n=== Test Results ===\n");
  printf("Passed: %d/%d\n", tests_passed, tests_total);

  if (tests_passed == tests_total) {
    printf("\n=== All Tests Passed! ===\n");
    return 0;
  }
  printf("\n=== Some Tests Failed ===\n");
  return 1;
}
