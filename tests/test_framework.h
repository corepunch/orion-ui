// Simple test framework for Orion
// Inspired by Windows 1.0 testing approach - simple, direct, minimal

#ifndef __TEST_FRAMEWORK_H__
#define __TEST_FRAMEWORK_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

// Test statistics
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// Color codes for output
#define COLOR_RESET   "\033[0m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_RED     "\033[31m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"

// Test macros
#define TEST_START(name) \
    printf(COLOR_BLUE "Starting test suite: %s\n" COLOR_RESET, name);

#define TEST_END() \
    printf("\n" COLOR_BLUE "Test Results:\n" COLOR_RESET); \
    printf("  Total:  %d\n", tests_run); \
    printf("  " COLOR_GREEN "Passed: %d" COLOR_RESET "\n", tests_passed); \
    if (tests_failed > 0) { \
        printf("  " COLOR_RED "Failed: %d" COLOR_RESET "\n", tests_failed); \
        return 1; \
    } \
    printf("\n" COLOR_GREEN "All tests passed!\n" COLOR_RESET); \
    return 0;

#define TEST(name) \
    tests_run++; \
    printf("  Testing: %s ... ", name); \
    fflush(stdout);

#define PASS() \
    tests_passed++; \
    printf(COLOR_GREEN "PASS" COLOR_RESET "\n");

#define FAIL(msg) \
    tests_failed++; \
    printf(COLOR_RED "FAIL" COLOR_RESET ": %s\n", msg);

#define ASSERT(condition, msg) \
    if (!(condition)) { \
        FAIL(msg); \
        return; \
    }

#define ASSERT_TRUE(condition) \
    ASSERT(condition, "Expected true but got false")

#define ASSERT_FALSE(condition) \
    ASSERT(!(condition), "Expected false but got true")

#define ASSERT_NULL(ptr) \
    ASSERT((ptr) == NULL, "Expected NULL pointer")

#define ASSERT_NOT_NULL(ptr) \
    ASSERT((ptr) != NULL, "Expected non-NULL pointer")

#define ASSERT_EQUAL(a, b) \
    ASSERT((a) == (b), "Values not equal")

#define ASSERT_NOT_EQUAL(a, b) \
    ASSERT((a) != (b), "Values should not be equal")

#define ASSERT_STR_EQUAL(a, b) \
    ASSERT(strcmp(a, b) == 0, "Strings not equal")

/* SKIP: gracefully skip a test (counts as a pass with a "SKIP" note).
 * Call after TEST() which has already incremented tests_run.
 * Use when the test depends on a feature that is not compiled in. */
#define SKIP(reason) \
    tests_passed++; \
    printf(COLOR_YELLOW "SKIP" COLOR_RESET ": %s\n", reason); \
    return;

/* SKIP_IF_NO_LUA: skip the test when Lua was not compiled in (-DHAVE_LUA). */
#ifndef HAVE_LUA
#  define SKIP_IF_NO_LUA() SKIP("Lua not compiled in")
#else
#  define SKIP_IF_NO_LUA() /* Lua available – continue */
#endif

#endif // __TEST_FRAMEWORK_H__
